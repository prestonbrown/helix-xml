/**
 * @file test_parser_state.c
 *
 * src/xml/lv_xml_parser.c: the parser's own state - initialisation, the section
 * machine, and the parent/item accessors every widget processor is built on.
 *
 * All four entry points are reachable from the headers the library already
 * installs (`xml/lv_xml_parser.h` exposes the full `struct
 * _lv_xml_parser_state_t`), so nothing here needs a shim or a friend hook.
 *
 * Two shapes of test:
 *
 *  1. DIRECT. `lv_xml_parser_state_init` and the section push/pop pair take a
 *     plain `lv_xml_parser_state_t *` and allocate nothing that needs freeing
 *     (state_init only lv_memzero's and lv_ll_init's), so a stack-local state
 *     is a legitimate and leak-free way to drive them.
 *
 *  2. THROUGH A REAL PARSE. `lv_xml_state_get_parent` / `lv_xml_state_get_item`
 *     are one-line accessors; asserting them on a hand-filled struct would only
 *     restate the assignment. What is worth pinning is the CONTRACT the widget
 *     processors rely on - which object those accessors return at each point of
 *     a nested document. So the tests below register a probe widget whose
 *     create/apply callbacks record exactly that, and then assert the recorded
 *     chain against the widget tree that actually came out.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <string.h>

#include "helpers/helix_log_capture.h"
#include "helpers/helix_test_env.h"
#include "helpers/helix_test_pump.h"
#include "helpers/xml_assert.h"

#include "xml/lv_xml_parser.h"
#include "xml/lv_xml_widget.h"

/*---------------------------------------------------------------------------
 * Unity fixture
 *--------------------------------------------------------------------------*/

void setUp(void)
{
    helix_test_env_setup();
}

void tearDown(void)
{
    helix_test_env_teardown();
}

/*===========================================================================
 * lv_xml_parser_state_init
 *==========================================================================*/

/**
 * Every field must come out zeroed and every list initialised, from arbitrary
 * incoming memory. The struct is poisoned first: without that, a state_init
 * that forgot the lv_memzero would still pass on a stack that happened to be
 * clean, which is precisely the bug that used to bite here (see the comment in
 * lv_xml_register_component_from_data about the globals branch skipping the
 * initialiser and reading stack garbage).
 */
static void test_parser_state_init_zeroes_a_poisoned_state_and_inits_every_list(void)
{
    lv_xml_parser_state_t st;
    memset(&st, 0xA5, sizeof(st));

    lv_xml_parser_state_init(&st);

    TEST_ASSERT_NULL_MESSAGE(st.tag_name, "tag_name must start empty");
    TEST_ASSERT_NULL_MESSAGE(st.parent, "parent must start empty");
    TEST_ASSERT_NULL_MESSAGE(st.item, "item must start empty");
    TEST_ASSERT_NULL_MESSAGE(st.view, "view must start empty");
    TEST_ASSERT_NULL_MESSAGE(st.context, "context must start empty - a stale <repeat> capture here is a free()");
    TEST_ASSERT_NULL(st.parent_attrs);
    TEST_ASSERT_NULL(st.parent_scope);
    TEST_ASSERT_NULL(st.composed_strings);
    TEST_ASSERT_EQUAL_UINT32(0, st.composed_count);
    TEST_ASSERT_EQUAL_UINT32(0, st.composed_cap);

    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_XML_PARSER_SECTION_NONE, (int)st.section,
                                  "a fresh state must not be inside any section");

    /* The two stacks the SAX handlers push onto. */
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, lv_ll_get_len(&st.parent_ll),
                                     "parent_ll must be an initialised, empty list");
    TEST_ASSERT_EQUAL_UINT32(0, lv_ll_get_len(&st.pcdata_ll));

    /* lv_xml_component_scope_init() must have run over the embedded scope:
     * every one of its eleven lists is walked by the metadata handler. */
    TEST_ASSERT_EQUAL_UINT32(0, lv_ll_get_len(&st.scope.style_ll));
    TEST_ASSERT_EQUAL_UINT32(0, lv_ll_get_len(&st.scope.const_ll));
    TEST_ASSERT_EQUAL_UINT32(0, lv_ll_get_len(&st.scope.param_ll));
    TEST_ASSERT_EQUAL_UINT32(0, lv_ll_get_len(&st.scope.gradient_ll));
    TEST_ASSERT_EQUAL_UINT32(0, lv_ll_get_len(&st.scope.subjects_ll));
    TEST_ASSERT_EQUAL_UINT32(0, lv_ll_get_len(&st.scope.subject_expr_ll));
    TEST_ASSERT_EQUAL_UINT32(0, lv_ll_get_len(&st.scope.frag_ll));
    TEST_ASSERT_EQUAL_UINT32(0, lv_ll_get_len(&st.scope.timeline_ll));
    TEST_ASSERT_EQUAL_UINT32(0, lv_ll_get_len(&st.scope.font_ll));
    TEST_ASSERT_EQUAL_UINT32(0, lv_ll_get_len(&st.scope.image_ll));
    TEST_ASSERT_EQUAL_UINT32(0, lv_ll_get_len(&st.scope.event_ll));

    TEST_ASSERT_NULL(st.scope.name);
    TEST_ASSERT_NULL(st.scope.view_def);
    TEST_ASSERT_NULL(st.scope.extends);
    TEST_ASSERT_EQUAL_UINT32(0, st.scope.is_widget);
    TEST_ASSERT_EQUAL_UINT32(0, st.scope.is_screen);

    /* And the accessors agree with the fields. */
    TEST_ASSERT_NULL(lv_xml_state_get_parent(&st));
    TEST_ASSERT_NULL(lv_xml_state_get_item(&st));

    /* Re-initialising an already-initialised state is safe and idempotent -
     * lv_xml_register_component_from_data does exactly this on the globals
     * path. */
    lv_xml_parser_state_init(&st);
    TEST_ASSERT_EQUAL_UINT32(0, lv_ll_get_len(&st.parent_ll));
    TEST_ASSERT_EQUAL_INT(LV_XML_PARSER_SECTION_NONE, (int)st.section);
}

/*===========================================================================
 * The section machine
 *==========================================================================*/

typedef struct {
    const char * tag;
    lv_xml_parser_section_t expect;
} section_case_t;

static const section_case_t SECTION_OPENERS[] = {
    {"api", LV_XML_PARSER_SECTION_API},
    {"gradients", LV_XML_PARSER_SECTION_GRAD},
    {"consts", LV_XML_PARSER_SECTION_CONSTS},
    {"styles", LV_XML_PARSER_SECTION_STYLES},
    {"images", LV_XML_PARSER_SECTION_IMAGES},
    {"fonts", LV_XML_PARSER_SECTION_FONTS},
    {"subjects", LV_XML_PARSER_SECTION_SUBJECTS},
    {"animation", LV_XML_PARSER_SECTION_ANIMATION},
    {"include_timeline", LV_XML_PARSER_SECTION_INCLUDE_TIMELINE},
    {"timeline", LV_XML_PARSER_SECTION_TIMELINE},
    {"view", LV_XML_PARSER_SECTION_VIEW},
};

/** Every block name the metadata parser dispatches on, from a fresh state. */
static void test_start_section_maps_every_block_name_to_its_section(void)
{
    for(size_t i = 0; i < sizeof(SECTION_OPENERS) / sizeof(SECTION_OPENERS[0]); i++) {
        lv_xml_parser_state_t st;
        lv_xml_parser_state_init(&st);

        lv_xml_parser_start_section(&st, SECTION_OPENERS[i].tag);
        TEST_ASSERT_EQUAL_INT_MESSAGE(
            (int)SECTION_OPENERS[i].expect, (int)st.section,
            helix_xml_assert_msgf("<%s> opened the wrong section", SECTION_OPENERS[i].tag));
    }
}

/**
 * A tag that is not a block opener must leave the section alone. This is what
 * lets `<styles>` stay open across the `<style>` children inside it, and what
 * lets a `<view>` body contain arbitrary widget tags.
 */
static void test_start_section_leaves_the_section_alone_for_a_non_block_tag(void)
{
    lv_xml_parser_state_t st;
    lv_xml_parser_state_init(&st);

    lv_xml_parser_start_section(&st, "styles");
    TEST_ASSERT_EQUAL_INT(LV_XML_PARSER_SECTION_STYLES, (int)st.section);

    lv_xml_parser_start_section(&st, "style");
    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_XML_PARSER_SECTION_STYLES, (int)st.section,
                                  "<style> inside <styles> must not change the section");

    lv_xml_parser_start_section(&st, "lv_button");
    TEST_ASSERT_EQUAL_INT(LV_XML_PARSER_SECTION_STYLES, (int)st.section);

    lv_xml_parser_start_section(&st, "");
    TEST_ASSERT_EQUAL_INT(LV_XML_PARSER_SECTION_STYLES, (int)st.section);
}

/**
 * `<stop>` is the one context-sensitive opener: it only means "gradient stop"
 * while a gradient block is open. Anywhere else it is an ordinary tag.
 */
static void test_stop_opens_a_gradient_stop_only_inside_a_gradients_block(void)
{
    lv_xml_parser_state_t st;
    lv_xml_parser_state_init(&st);

    /* Outside <gradients> it is not special. */
    lv_xml_parser_start_section(&st, "stop");
    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_XML_PARSER_SECTION_NONE, (int)st.section,
                                  "<stop> outside <gradients> must not open a stop section");

    lv_xml_parser_start_section(&st, "styles");
    lv_xml_parser_start_section(&st, "stop");
    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_XML_PARSER_SECTION_STYLES, (int)st.section,
                                  "<stop> inside <styles> must not open a stop section");

    /* Inside it, it is. */
    lv_xml_parser_state_init(&st);
    lv_xml_parser_start_section(&st, "gradients");
    lv_xml_parser_start_section(&st, "horizontal");
    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_XML_PARSER_SECTION_GRAD, (int)st.section,
                                  "the gradient TYPE tag must keep the section on GRAD");
    lv_xml_parser_start_section(&st, "stop");
    TEST_ASSERT_EQUAL_INT(LV_XML_PARSER_SECTION_GRAD_STOP, (int)st.section);
}

/** The five closers that really do pop back to "no section". */
static void test_end_section_pops_the_blocks_that_declare_a_closer(void)
{
    static const char * CLOSERS[] = {"params", "consts", "gradients", "styles", "view"};

    for(size_t i = 0; i < sizeof(CLOSERS) / sizeof(CLOSERS[0]); i++) {
        lv_xml_parser_state_t st;
        lv_xml_parser_state_init(&st);
        /* Put the state somewhere non-NONE first so the pop is observable. */
        lv_xml_parser_start_section(&st, "styles");
        TEST_ASSERT_EQUAL_INT(LV_XML_PARSER_SECTION_STYLES, (int)st.section);

        lv_xml_parser_end_section(&st, CLOSERS[i]);
        TEST_ASSERT_EQUAL_INT_MESSAGE(
            LV_XML_PARSER_SECTION_NONE, (int)st.section,
            helix_xml_assert_msgf("</%s> must reset the section", CLOSERS[i]));
    }
}

/**
 * PINS CURRENT BEHAVIOUR - suspected bug: only params/consts/gradients/styles/view
 * are listed in lv_xml_parser_end_section(), so `</api>`, `</fonts>`, `</images>`,
 * `</subjects>`, `</animation>`, `</timeline>` and `</include_timeline>` leave
 * their section LATCHED. Nothing between such a close tag and the next block
 * opener returns to SECTION_NONE, so any stray element there is fed to that
 * section's element processor - e.g. a comment-adjacent tag after `</subjects>`
 * reaches process_subject_element() and is warned about as a malformed subject.
 * In practice it self-corrects because the next tag is almost always another
 * block opener, which is why this has survived.
 */
static void test_end_section_does_not_pop_api_fonts_images_or_subjects(void)
{
    static const section_case_t LATCHED[] = {
        {"api", LV_XML_PARSER_SECTION_API},
        {"fonts", LV_XML_PARSER_SECTION_FONTS},
        {"images", LV_XML_PARSER_SECTION_IMAGES},
        {"subjects", LV_XML_PARSER_SECTION_SUBJECTS},
        {"animation", LV_XML_PARSER_SECTION_ANIMATION},
        {"timeline", LV_XML_PARSER_SECTION_TIMELINE},
        {"include_timeline", LV_XML_PARSER_SECTION_INCLUDE_TIMELINE},
    };

    for(size_t i = 0; i < sizeof(LATCHED) / sizeof(LATCHED[0]); i++) {
        lv_xml_parser_state_t st;
        lv_xml_parser_state_init(&st);

        lv_xml_parser_start_section(&st, LATCHED[i].tag);
        TEST_ASSERT_EQUAL_INT((int)LATCHED[i].expect, (int)st.section);

        lv_xml_parser_end_section(&st, LATCHED[i].tag);
        TEST_ASSERT_EQUAL_INT_MESSAGE(
            (int)LATCHED[i].expect, (int)st.section,
            helix_xml_assert_msgf("</%s> currently leaves the section latched - if this "
                                  "starts failing the closer list was fixed",
                                  LATCHED[i].tag));
    }
}

/**
 * PINS CURRENT BEHAVIOUR - suspected bug: `params` is in the CLOSER list of
 * lv_xml_parser_end_section() but has no matching opener in
 * lv_xml_parser_start_section(). `<params>` therefore opens nothing, while
 * `</params>` closes whatever happened to be open. In an `<api><params>...`
 * document that is exactly wrong: the close tag drops the state out of
 * SECTION_API while `<api>` is still open.
 */
static void test_params_closes_a_section_it_can_never_have_opened(void)
{
    lv_xml_parser_state_t st;
    lv_xml_parser_state_init(&st);

    lv_xml_parser_start_section(&st, "params");
    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_XML_PARSER_SECTION_NONE, (int)st.section,
                                  "<params> opens no section today");

    lv_xml_parser_state_init(&st);
    lv_xml_parser_start_section(&st, "api");
    lv_xml_parser_end_section(&st, "params");
    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_XML_PARSER_SECTION_NONE, (int)st.section,
                                  "</params> currently pops the still-open <api> section");
}

/**
 * Closing a gradient STOP is the second half of the context-sensitive rule, and
 * it reads backwards: `</stop>` leaves the state on GRAD_STOP, and it is the
 * gradient TYPE close tag (`</horizontal>`) that drops back to GRAD. That works
 * only because start_section() re-selects GRAD_STOP for the next `<stop>` and
 * the stop processor is dispatched unconditionally on GRAD_STOP.
 */
static void test_gradient_stop_section_is_left_by_the_gradient_close_tag(void)
{
    lv_xml_parser_state_t st;
    lv_xml_parser_state_init(&st);

    lv_xml_parser_start_section(&st, "gradients");
    lv_xml_parser_start_section(&st, "horizontal");
    lv_xml_parser_start_section(&st, "stop");
    TEST_ASSERT_EQUAL_INT(LV_XML_PARSER_SECTION_GRAD_STOP, (int)st.section);

    lv_xml_parser_end_section(&st, "stop");
    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_XML_PARSER_SECTION_GRAD_STOP, (int)st.section,
                                  "</stop> deliberately stays on GRAD_STOP so the next <stop> works");

    /* A second stop in the same gradient. */
    lv_xml_parser_start_section(&st, "stop");
    TEST_ASSERT_EQUAL_INT(LV_XML_PARSER_SECTION_GRAD_STOP, (int)st.section);
    lv_xml_parser_end_section(&st, "stop");

    /* Closing the gradient type tag is what steps back out. */
    lv_xml_parser_end_section(&st, "horizontal");
    TEST_ASSERT_EQUAL_INT(LV_XML_PARSER_SECTION_GRAD, (int)st.section);

    lv_xml_parser_end_section(&st, "gradients");
    TEST_ASSERT_EQUAL_INT(LV_XML_PARSER_SECTION_NONE, (int)st.section);
}

/**
 * An unbalanced document reaches these functions as close tags with no matching
 * open. There is no depth counter to underflow, so the only requirement is that
 * the state stays a legal section value and that a later, well-formed block
 * still opens correctly.
 */
static void test_unmatched_close_tags_do_not_corrupt_the_section_state(void)
{
    lv_xml_parser_state_t st;
    lv_xml_parser_state_init(&st);

    /* Closers with nothing open. */
    lv_xml_parser_end_section(&st, "styles");
    lv_xml_parser_end_section(&st, "view");
    lv_xml_parser_end_section(&st, "consts");
    lv_xml_parser_end_section(&st, "stop");
    lv_xml_parser_end_section(&st, "gradients");
    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_XML_PARSER_SECTION_NONE, (int)st.section,
                                  "close tags with nothing open must leave the state at NONE");

    /* Closing the same block twice. */
    lv_xml_parser_start_section(&st, "consts");
    lv_xml_parser_end_section(&st, "consts");
    lv_xml_parser_end_section(&st, "consts");
    TEST_ASSERT_EQUAL_INT(LV_XML_PARSER_SECTION_NONE, (int)st.section);

    /* And the machine still works afterwards. */
    lv_xml_parser_start_section(&st, "subjects");
    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_XML_PARSER_SECTION_SUBJECTS, (int)st.section,
                                  "a well-formed block must still open after unbalanced input");

    /* The accessors are untouched by any of it. */
    TEST_ASSERT_NULL(lv_xml_state_get_parent(&st));
    TEST_ASSERT_NULL(lv_xml_state_get_item(&st));
}

/**
 * A truncated document is rejected at registration, before any parser state can
 * be observed - so "the state after a short document" is not a reachable
 * condition through the public API. Pin the boundary instead: nothing is left
 * half-registered.
 */
static void test_a_truncated_document_registers_nothing(void)
{
    log_capture_start();
    lv_result_t res = lv_xml_register_component_from_data(
                          "truncated",
                          "<component><view extends=\"lv_obj\" name=\"r\"><lv_obj>");
    log_capture_stop();

    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_RESULT_INVALID, (int)res,
                                  "an unclosed document must fail registration");
    TEST_ASSERT_TRUE(log_contains("XML parsing error"));
    TEST_ASSERT_NULL_MESSAGE(lv_xml_component_get_scope("truncated"),
                             "a failed registration must leave no scope behind");
    TEST_ASSERT_NULL_MESSAGE(lv_xml_create(helix_test_env_screen(), "truncated", NULL),
                             "a component that failed to register must not be creatable");
}

/*===========================================================================
 * lv_xml_state_get_parent / lv_xml_state_get_item, through a real parse
 *==========================================================================*/

#define PROBE_MAX 16

typedef struct {
    char id[16];
    lv_obj_t * parent_at_create;
    lv_obj_t * item_at_create;
    lv_obj_t * parent_at_apply;
    lv_obj_t * item_at_apply;
    lv_obj_t * created;
} probe_rec_t;

static probe_rec_t g_probe[PROBE_MAX];
static uint32_t g_probe_cnt;

static void probe_reset(void)
{
    memset(g_probe, 0, sizeof(g_probe));
    g_probe_cnt = 0;
}

static void * probe_create(lv_xml_parser_state_t * state, const char ** attrs)
{
    lv_obj_t * parent = (lv_obj_t *)lv_xml_state_get_parent(state);
    lv_obj_t * item_before = (lv_obj_t *)lv_xml_state_get_item(state);

    /* view_start_element_handler guarantees a non-NULL parent before it calls
     * create_cb, so lv_obj_create() is safe here. */
    TEST_ASSERT_NOT_NULL_MESSAGE(parent, "the parser must hand create_cb a real parent");
    lv_obj_t * obj = lv_obj_create(parent);

    if(g_probe_cnt < PROBE_MAX) {
        probe_rec_t * r = &g_probe[g_probe_cnt++];
        const char * id = lv_xml_get_value_of(attrs, "id");
        lv_strlcpy(r->id, id ? id : "?", sizeof(r->id));
        r->parent_at_create = parent;
        r->item_at_create = item_before;
        r->created = obj;
    }
    return obj;
}

static void probe_apply(lv_xml_parser_state_t * state, const char ** attrs)
{
    LV_UNUSED(attrs);
    if(g_probe_cnt == 0 || g_probe_cnt > PROBE_MAX) return;
    probe_rec_t * r = &g_probe[g_probe_cnt - 1];
    r->parent_at_apply = (lv_obj_t *)lv_xml_state_get_parent(state);
    r->item_at_apply = (lv_obj_t *)lv_xml_state_get_item(state);
}

static void probe_register(void)
{
    probe_reset();
    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_RESULT_OK,
                                  (int)lv_xml_register_widget("probe", probe_create, probe_apply),
                                  "the probe widget must register");
}

static probe_rec_t * probe_by_id(const char * id)
{
    for(uint32_t i = 0; i < g_probe_cnt; i++) {
        if(strcmp(g_probe[i].id, id) == 0) return &g_probe[i];
    }
    TEST_FAIL_MESSAGE(helix_xml_assert_msgf("no probe recorded with id \"%s\"", id));
    return NULL;
}

static const char * NEST_XML =
    "<component>"
    "  <view extends=\"lv_obj\" name=\"nest_root\">"
    "    <probe id=\"a\">"
    "      <probe id=\"a1\">"
    "        <probe id=\"a1x\"/>"
    "      </probe>"
    "      <probe id=\"a2\"/>"
    "    </probe>"
    "    <probe id=\"b\"/>"
    "  </view>"
    "</component>";

/**
 * The contract every widget processor is written against:
 *
 *   - inside create_cb, get_parent() is the ENCLOSING element's object and
 *     get_item() is NULL (the handler clears it before dispatching)
 *   - inside apply_cb, get_item() is the object create_cb just returned, and
 *     get_parent() is unchanged
 *
 * Getting the second half wrong is how `<style>`-style child elements (which
 * return get_parent() from their create_cb) end up applying to themselves.
 */
static void test_state_accessors_report_the_enclosing_element_at_every_depth(void)
{
    probe_register();
    ASSERT_XML_REGISTERS("nest", NEST_XML);

    lv_obj_t * root = XML_CREATE(helix_test_env_screen(), "nest", NULL);
    helix_test_pump(30);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(5, g_probe_cnt, "every <probe> element must have been created once");

    probe_rec_t * a   = probe_by_id("a");
    probe_rec_t * a1  = probe_by_id("a1");
    probe_rec_t * a1x = probe_by_id("a1x");

    /* Depth 1, 2, 3: each level's parent is the level above it. */
    TEST_ASSERT_EQUAL_PTR_MESSAGE(root, a->parent_at_create,
                                  "a depth-1 element's parent must be the view root");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(a->created, a1->parent_at_create,
                                  "a depth-2 element's parent must be the depth-1 object");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(a1->created, a1x->parent_at_create,
                                  "a depth-3 element's parent must be the depth-2 object");

    for(uint32_t i = 0; i < g_probe_cnt; i++) {
        probe_rec_t * r = &g_probe[i];
        const char * id = r->id;

        TEST_ASSERT_NULL_MESSAGE(r->item_at_create,
                                 helix_xml_assert_msgf(
                                     "get_item() must be NULL inside create_cb (probe \"%s\") - "
                                     "a stale item here is the PREVIOUS element's object", id));
        TEST_ASSERT_EQUAL_PTR_MESSAGE(r->created, r->item_at_apply,
                                      helix_xml_assert_msgf(
                                          "get_item() inside apply_cb must be the object create_cb "
                                          "returned (probe \"%s\")", id));
        TEST_ASSERT_EQUAL_PTR_MESSAGE(r->parent_at_create, r->parent_at_apply,
                                      helix_xml_assert_msgf(
                                          "get_parent() must not move between create and apply "
                                          "(probe \"%s\")", id));
        /* The reported parent must be the real one, not just self-consistent. */
        TEST_ASSERT_EQUAL_PTR_MESSAGE(r->parent_at_create, lv_obj_get_parent(r->created),
                                      helix_xml_assert_msgf(
                                          "probe \"%s\" was parented somewhere other than the object "
                                          "get_parent() reported", id));
    }
}

/**
 * The half that actually breaks in practice: after a nested subtree closes, the
 * next SIBLING must see the enclosing element again - not the last object that
 * was created, and not the deepest one. A parent stack that pops the wrong
 * number of frames mis-parents every following element onto whatever is left.
 */
static void test_parent_tracking_returns_to_the_enclosing_element_for_siblings(void)
{
    probe_register();
    ASSERT_XML_REGISTERS("nest", NEST_XML);

    lv_obj_t * root = XML_CREATE(helix_test_env_screen(), "nest", NULL);
    helix_test_pump(30);

    probe_rec_t * a   = probe_by_id("a");
    probe_rec_t * a1  = probe_by_id("a1");
    probe_rec_t * a1x = probe_by_id("a1x");
    probe_rec_t * a2  = probe_by_id("a2");
    probe_rec_t * b   = probe_by_id("b");

    /* a2 follows the two-deep <probe id="a1"> subtree and must land back on a. */
    TEST_ASSERT_EQUAL_PTR_MESSAGE(a->created, a2->parent_at_create,
                                  "a sibling after a nested subtree must re-attach to the enclosing element");
    TEST_ASSERT_TRUE_MESSAGE(a2->parent_at_create != a1->created,
                             "a sibling must not inherit its predecessor's object as a parent");
    TEST_ASSERT_TRUE_MESSAGE(a2->parent_at_create != a1x->created,
                             "a sibling must not inherit the deepest object as a parent");

    /* b follows the whole <probe id="a"> subtree and must land back on the root. */
    TEST_ASSERT_EQUAL_PTR_MESSAGE(root, b->parent_at_create,
                                  "a depth-1 sibling after a three-deep subtree must re-attach to the view root");

    /* The resulting tree shape is the same statement, read from the other end. */
    ASSERT_CHILD_COUNT(root, 2);        /* a, b */
    ASSERT_CHILD_COUNT(a->created, 2);  /* a1, a2 */
    ASSERT_CHILD_COUNT(a1->created, 1); /* a1x */
    ASSERT_CHILD_COUNT(a1x->created, 0);
    ASSERT_CHILD_COUNT(a2->created, 0);
    ASSERT_CHILD_COUNT(b->created, 0);
}

/**
 * PINS CURRENT BEHAVIOUR - suspected bug (already described in the LV_LOG_ERROR
 * in view_start_element_handler, but never pinned): an unknown tag pushes NO
 * parent frame, while its close tag pops one anyway. The stack therefore loses
 * one level per unknown element, and every following sibling is parented one
 * level too high - here `after` escapes the component entirely and is created
 * directly on the SCREEN, outside the view root it was written inside.
 *
 * The engine logs loudly and keeps going by design, so this test asserts the
 * damage rather than an error return.
 */
static void test_an_unknown_tag_unbalances_the_parent_stack_for_later_siblings(void)
{
    probe_register();
    ASSERT_XML_REGISTERS("stack_corrupt",
                         "<component>"
                         "  <view extends=\"lv_obj\" name=\"corrupt_root\">"
                         "    <probe id=\"a\">"
                         "      <definitely_not_a_widget>"
                         "        <probe id=\"inner\"/>"
                         "      </definitely_not_a_widget>"
                         "    </probe>"
                         "    <probe id=\"after\"/>"
                         "  </view>"
                         "</component>");

    lv_obj_t * screen = helix_test_env_screen();

    log_capture_start();
    lv_obj_t * root = XML_CREATE(screen, "stack_corrupt", NULL);
    helix_test_pump(30);
    log_capture_stop();

    TEST_ASSERT_TRUE_MESSAGE(
        log_contains("definitely_not_a_widget"),
        "an unknown tag must be reported by name - it is almost always a stale binary");
    TEST_ASSERT_TRUE(log_contains("corrupts the parent stack"));

    probe_rec_t * a     = probe_by_id("a");
    probe_rec_t * inner = probe_by_id("inner");
    probe_rec_t * after = probe_by_id("after");

    /* Up to the unknown tag everything is still correct: the unknown element
     * pushes nothing, so its child sees `a`. */
    TEST_ASSERT_EQUAL_PTR_MESSAGE(root, a->parent_at_create, "the first probe is unaffected");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(a->created, inner->parent_at_create,
                                  "a child of the unknown tag still sees the last real parent");

    /* The damage: </definitely_not_a_widget> popped `a`, then </probe> popped
     * the view root, so the next sibling gets the screen. */
    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        screen, after->parent_at_create,
        "PINS CURRENT BEHAVIOUR: the sibling after an unknown tag currently escapes onto the screen");
    TEST_ASSERT_TRUE_MESSAGE(after->parent_at_create != root,
                             "if this starts failing the parent-stack imbalance was fixed");
    TEST_ASSERT_EQUAL_PTR(screen, lv_obj_get_parent(after->created));
}

/*---------------------------------------------------------------------------
 * main
 *--------------------------------------------------------------------------*/

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_parser_state_init_zeroes_a_poisoned_state_and_inits_every_list);

    RUN_TEST(test_start_section_maps_every_block_name_to_its_section);
    RUN_TEST(test_start_section_leaves_the_section_alone_for_a_non_block_tag);
    RUN_TEST(test_stop_opens_a_gradient_stop_only_inside_a_gradients_block);
    RUN_TEST(test_end_section_pops_the_blocks_that_declare_a_closer);
    RUN_TEST(test_end_section_does_not_pop_api_fonts_images_or_subjects);
    RUN_TEST(test_params_closes_a_section_it_can_never_have_opened);
    RUN_TEST(test_gradient_stop_section_is_left_by_the_gradient_close_tag);
    RUN_TEST(test_unmatched_close_tags_do_not_corrupt_the_section_state);
    RUN_TEST(test_a_truncated_document_registers_nothing);

    RUN_TEST(test_state_accessors_report_the_enclosing_element_at_every_depth);
    RUN_TEST(test_parent_tracking_returns_to_the_enclosing_element_for_siblings);
    RUN_TEST(test_an_unknown_tag_unbalances_the_parent_stack_for_later_siblings);

    return UNITY_END();
}
