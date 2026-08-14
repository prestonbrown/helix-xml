/**
 * @file test_component.c
 *
 * src/xml/lv_xml_component.c: the component registry, the `<api>`/`<prop>`
 * contract, the `<consts>` `#` sigil, nesting, and scope teardown.
 *
 * This file is the declarative surface of the engine. A component's `<api>` is
 * the only typed contract a consuming app has: every panel, modal and widget in
 * HelixScreen is a `<prop>` list plus a view that references it with `$name`.
 * None of the resolution paths can report a failure to the caller - an
 * unresolved prop or an unknown const is silently blanked and the widget keeps
 * its default - so the *observable* result (the value that reached the widget,
 * plus the warning that was or was not logged) IS the contract, and that is
 * what is asserted here.
 *
 * ---------------------------------------------------------------------------
 * NOT TESTED, DELIBERATELY
 *
 *  - lv_xml_component_get_scope(NULL) is guarded and returns NULL, but
 *    lv_xml_register_component_from_data(NULL, ...) is not: it lv_streq()s the
 *    name immediately.
 *  - `<subject type="pointer">`: process_subject_element() has int/float/color/
 *    string branches only, so a pointer subject warns and is not registered at
 *    all (see test_subject_decl.c). Registry-level pointer subjects are covered
 *    in test_registries.c.
 * ---------------------------------------------------------------------------
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <string.h>

/* Log capture: every resolution failure here is reported ONLY as a LV_LOG_WARN -
 * the attribute is dropped and the widget silently keeps its default, which is
 * indistinguishable from "the XML never mentioned it". The log line is the
 * assertion. */
#include "helpers/helix_log_capture.h"
#include "helpers/helix_test_env.h"
#include "helpers/helix_test_pump.h"
#include "helpers/xml_assert.h"

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

/*---------------------------------------------------------------------------
 * Colour assertion
 *
 * A style property the XML under test literally set - not a measured pixel -
 * so this is inside the rule at the top of xml_assert.h. There is no
 * ASSERT_STYLE_COLOR there because lv_style_value_t's colour member cannot be
 * compared with TEST_ASSERT_EQUAL_INT32.
 *--------------------------------------------------------------------------*/

/* `style_pad_all` is a shorthand: there is no LV_STYLE_PAD_ALL, it writes the
 * four side properties. Check all four so a partial application is caught. */
#define ASSERT_PAD_ALL(obj, n)                                                           \
    do {                                                                                 \
        ASSERT_STYLE_INT((obj), LV_STYLE_PAD_TOP, LV_PART_MAIN, (n));                    \
        ASSERT_STYLE_INT((obj), LV_STYLE_PAD_BOTTOM, LV_PART_MAIN, (n));                 \
        ASSERT_STYLE_INT((obj), LV_STYLE_PAD_LEFT, LV_PART_MAIN, (n));                   \
        ASSERT_STYLE_INT((obj), LV_STYLE_PAD_RIGHT, LV_PART_MAIN, (n));                  \
    } while(0)

#define ASSERT_BG_COLOR(obj, hex)                                                      \
    do {                                                                                 \
        lv_obj_t * hx_obj_ = (lv_obj_t *)(obj);                                          \
        lv_color_t hx_got_ = lv_obj_get_style_bg_color(hx_obj_, LV_PART_MAIN);           \
        lv_color_t hx_exp_ = lv_color_hex((hex));                                        \
        TEST_ASSERT_TRUE_MESSAGE(                                                        \
            lv_color_eq(hx_got_, hx_exp_),                                               \
            helix_xml_assert_msgf(                                                       \
                "wrong bg_color on \"%s\": expected %02x%02x%02x, got %02x%02x%02x",      \
                helix_xml_assert_name_of(hx_obj_),                                       \
                hx_exp_.red, hx_exp_.green, hx_exp_.blue,                                \
                hx_got_.red, hx_got_.green, hx_got_.blue));                              \
    } while(0)

/*---------------------------------------------------------------------------
 * Fixtures
 *--------------------------------------------------------------------------*/

/* One prop of every type the `<api>` vocabulary has, each wired to a widget
 * property that can be read back without measuring anything:
 *   string  -> label text
 *   int     -> style_pad_all
 *   bool    -> the HIDDEN flag
 *   color   -> style_bg_color
 *   subject -> bind_text, i.e. the prop carries a SUBJECT NAME, not a value */
static const char * PROP_DEMO_XML =
    "<component>"
    "  <api>"
    "    <prop name=\"title\" type=\"string\" default=\"Default Title\"/>"
    "    <prop name=\"pad\" type=\"int\" default=\"7\"/>"
    "    <prop name=\"is_hidden\" type=\"bool\" default=\"true\"/>"
    "    <prop name=\"tint\" type=\"color\" default=\"0xFF0000\"/>"
    "    <prop name=\"src\" type=\"subject\" default=\"comp_text_subject\"/>"
    "  </api>"
    "  <view extends=\"lv_obj\" name=\"prop_root\">"
    "    <lv_label name=\"prop_title\" text=\"$title\"/>"
    "    <lv_label name=\"prop_bound\" bind_text=\"$src\"/>"
    "    <lv_obj name=\"prop_box\" style_pad_all=\"$pad\" hidden=\"$is_hidden\""
    "            style_bg_color=\"$tint\"/>"
    "  </view>"
    "</component>";

/* A prop that is declared but has NO default, and a reference to a name that
 * was never declared at all. Both must end up dropped rather than literal. */
static const char * PROP_MISSING_XML =
    "<component>"
    "  <api>"
    "    <prop name=\"declared_no_default\" type=\"string\"/>"
    "  </api>"
    "  <view extends=\"lv_obj\" name=\"missing_root\">"
    "    <lv_label name=\"missing_declared\" text=\"$declared_no_default\"/>"
    "    <lv_label name=\"missing_undeclared\" text=\"$never_declared\"/>"
    "  </view>"
    "</component>";

static const char * CONST_DEMO_XML =
    "<component>"
    "  <consts>"
    "    <string name=\"greeting\" value=\"Hello const\"/>"
    "    <int name=\"gap\" value=\"9\"/>"
    "  </consts>"
    "  <view extends=\"lv_obj\" name=\"const_root\">"
    "    <lv_label name=\"const_label\" text=\"#greeting\"/>"
    "    <lv_obj name=\"const_box\" style_pad_all=\"#gap\"/>"
    "  </view>"
    "</component>";

static const char * CONST_UNKNOWN_XML =
    "<component>"
    "  <consts>"
    "    <string name=\"known\" value=\"ok\"/>"
    "  </consts>"
    "  <view extends=\"lv_obj\" name=\"unknown_const_root\">"
    "    <lv_label name=\"unknown_const_label\" text=\"#not_a_const\"/>"
    "  </view>"
    "</component>";

/* Nesting: `outer_comp` instantiates `inner_comp` and forwards its own prop. */
static const char * INNER_COMP_XML =
    "<component>"
    "  <api>"
    "    <prop name=\"caption\" type=\"string\" default=\"inner default\"/>"
    "  </api>"
    "  <view extends=\"lv_obj\" name=\"inner_root\">"
    "    <lv_label name=\"inner_label\" text=\"$caption\"/>"
    "  </view>"
    "</component>";

static const char * OUTER_COMP_XML =
    "<component>"
    "  <api>"
    "    <prop name=\"outer_caption\" type=\"string\" default=\"outer default\"/>"
    "  </api>"
    "  <view extends=\"lv_obj\" name=\"outer_root\">"
    "    <inner_comp name=\"nested\" caption=\"$outer_caption\"/>"
    "  </view>"
    "</component>";

/* Everything a scope can own that costs heap: a name, a view_def, an extends,
 * params, consts and a parser-allocated (OWNED) string subject with its two
 * 256-byte buffers. Used by the teardown-accounting test. */
static const char * OWNS_EVERYTHING_XML =
    "<component>"
    "  <api>"
    "    <prop name=\"p\" type=\"string\" default=\"d\"/>"
    "  </api>"
    "  <consts>"
    "    <string name=\"c\" value=\"v\"/>"
    "  </consts>"
    "  <subjects>"
    "    <subject name=\"scope_owned_text\" type=\"string\" value=\"owned\"/>"
    "  </subjects>"
    "  <view extends=\"lv_obj\" name=\"owns_root\">"
    "    <lv_label name=\"owns_label\" bind_text=\"scope_owned_text\"/>"
    "  </view>"
    "</component>";

/*---------------------------------------------------------------------------
 * Shared subjects
 *
 * File-static, never heap: they must survive lv_deinit(), and every test that
 * uses one re-inits it first (lv_subject_init_* resets subs_ll, so a stale
 * observer list from a previous LVGL cycle is dropped rather than walked).
 *--------------------------------------------------------------------------*/

static lv_subject_t s_text_subject;
static char s_text_buf[64];
static char s_text_prev[64];

static lv_subject_t s_alt_subject;
static char s_alt_buf[64];
static char s_alt_prev[64];

static lv_subject_t s_borrowed_subject;

static void init_text_subject(const char * value)
{
    lv_subject_init_string(&s_text_subject, s_text_buf, s_text_prev, sizeof(s_text_buf), value);
    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_RESULT_OK,
                                  (int)lv_xml_register_subject(NULL, "comp_text_subject", &s_text_subject),
                                  "couldn't register the shared string subject globally");
}

/*===========================================================================
 * <api> / <prop>
 *==========================================================================*/

/** Every declared prop type falls back to its `default` when nothing is passed. */
static void test_each_prop_type_falls_back_to_its_declared_default(void)
{
    init_text_subject("from subject");
    ASSERT_XML_REGISTERS("prop_demo", PROP_DEMO_XML);

    lv_obj_t * inst = XML_CREATE(helix_test_env_screen(), "prop_demo", NULL);
    helix_test_pump(30);

    ASSERT_LABEL_TEXT(ASSERT_NAMED(inst, "prop_title"), "Default Title");
    ASSERT_LABEL_TEXT(ASSERT_NAMED(inst, "prop_bound"), "from subject");

    lv_obj_t * box = ASSERT_NAMED(inst, "prop_box");
    ASSERT_PAD_ALL(box, 7);
    ASSERT_FLAG(box, LV_OBJ_FLAG_HIDDEN);
    ASSERT_BG_COLOR(box, 0xFF0000);
}

/** A value passed to lv_xml_create() beats the `default` for every type. */
static void test_a_prop_passed_at_create_time_overrides_its_default(void)
{
    init_text_subject("from subject");

    lv_subject_init_string(&s_alt_subject, s_alt_buf, s_alt_prev, sizeof(s_alt_buf), "alt subject");
    lv_xml_register_subject(NULL, "comp_alt_subject", &s_alt_subject);

    ASSERT_XML_REGISTERS("prop_demo", PROP_DEMO_XML);

    const char * attrs[] = {
        "title", "Overridden",
        "pad", "13",
        "is_hidden", "false",
        "tint", "#00FF00",       /* the `#RRGGBB` spelling must survive resolve_consts */
        "src", "comp_alt_subject",
        NULL, NULL
    };
    lv_obj_t * inst = XML_CREATE(helix_test_env_screen(), "prop_demo", attrs);
    helix_test_pump(30);

    ASSERT_LABEL_TEXT(ASSERT_NAMED(inst, "prop_title"), "Overridden");
    ASSERT_LABEL_TEXT(ASSERT_NAMED(inst, "prop_bound"), "alt subject");

    lv_obj_t * box = ASSERT_NAMED(inst, "prop_box");
    ASSERT_PAD_ALL(box, 13);
    ASSERT_NO_FLAG(box, LV_OBJ_FLAG_HIDDEN);
    ASSERT_BG_COLOR(box, 0x00FF00);
}

/**
 * A `type="subject"` prop carries a subject NAME, not a value: the binding it
 * feeds must stay live afterwards. Anything that merely copied the string once
 * at build time would pass the first assertion and fail the second.
 */
static void test_a_subject_typed_prop_produces_a_live_binding(void)
{
    init_text_subject("first");
    ASSERT_XML_REGISTERS("prop_demo", PROP_DEMO_XML);

    lv_obj_t * inst = XML_CREATE(helix_test_env_screen(), "prop_demo", NULL);
    helix_test_pump(30);

    lv_obj_t * bound = ASSERT_NAMED(inst, "prop_bound");
    ASSERT_LABEL_TEXT(bound, "first");

    lv_subject_copy_string(&s_text_subject, "second");
    helix_test_pump(30);
    ASSERT_LABEL_TEXT(bound, "second");
}

/**
 * A `$name` that no `<prop>` declares is reported and the attribute is dropped,
 * so the widget keeps its own default. The literal "$never_declared" must never
 * reach the widget.
 */
static void test_a_prop_referenced_but_never_declared_warns_and_drops_the_attribute(void)
{
    ASSERT_XML_REGISTERS("prop_missing", PROP_MISSING_XML);

    log_capture_start();
    lv_obj_t * inst = XML_CREATE(helix_test_env_screen(), "prop_missing", NULL);
    helix_test_pump(30);
    log_capture_stop();

    TEST_ASSERT_TRUE_MESSAGE(
        log_contains("'never_declared' parameter is not defined on 'prop_missing'"),
        "an undeclared prop reference must be reported - the dropped attribute is invisible otherwise");

    ASSERT_LABEL_TEXT(ASSERT_NAMED(inst, "missing_undeclared"), LV_LABEL_DEFAULT_TEXT);
}

/**
 * The other half of the undeclared-prop case: a value IS supplied at create time
 * for a name no `<prop>` declares - an ordinary typo in a caller's attribute list.
 *
 * get_param_type() returns NULL, and resolve_params() used to warn and then fall
 * straight through to `lv_streq(type, "style")` because a value was present and
 * did not start with '$'. lv_strcmp dereferences unconditionally, so this was a
 * NULL-deref crash reachable from perfectly ordinary XML. It must warn and drop
 * the attribute, exactly like the no-value case above.
 */
static void test_a_value_supplied_for_an_undeclared_prop_warns_and_does_not_crash(void)
{
    ASSERT_XML_REGISTERS("prop_missing", PROP_MISSING_XML);

    /* "never_declared" appears in the view as $never_declared but in no <api>. */
    const char * attrs[] = {"never_declared", "supplied by the caller", NULL, NULL};

    log_capture_start();
    lv_obj_t * inst = XML_CREATE(helix_test_env_screen(), "prop_missing", attrs);
    helix_test_pump(30);
    log_capture_stop();

    TEST_ASSERT_TRUE_MESSAGE(
        log_contains("'never_declared' parameter is not defined on 'prop_missing'"),
        "supplying a value for an undeclared prop must still be reported");

    /* Dropped, not applied: an undeclared prop is not part of the component's
     * contract, so its value must not reach the widget. */
    ASSERT_LABEL_TEXT(ASSERT_NAMED(inst, "missing_undeclared"), LV_LABEL_DEFAULT_TEXT);
}

/**
 * Same crash, reached through the `$prop|ref` form that hidden_if_prop_eq uses.
 * The pipe path resolves only the name before the '|', so an undeclared name
 * there hit the identical NULL `type`.
 */
static void test_an_undeclared_prop_with_a_pipe_suffix_warns_and_does_not_crash(void)
{
    static const char * PIPE_MISSING_XML =
        "<component>"
        "  <api>"
        "    <prop name=\"declared\" type=\"string\" default=\"d\"/>"
        "  </api>"
        "  <view extends=\"lv_obj\" name=\"pipe_root\">"
        "    <lv_obj name=\"pipe_box\" hidden_if_prop_eq=\"$nope|yes\"/>"
        "  </view>"
        "</component>";

    ASSERT_XML_REGISTERS("pipe_missing", PIPE_MISSING_XML);

    const char * attrs[] = {"nope", "yes", NULL, NULL};

    log_capture_start();
    lv_obj_t * inst = XML_CREATE(helix_test_env_screen(), "pipe_missing", attrs);
    helix_test_pump(30);
    log_capture_stop();

    TEST_ASSERT_NOT_NULL_MESSAGE(inst, "creating with an undeclared piped prop must not crash");
    TEST_ASSERT_TRUE_MESSAGE(
        log_contains("'nope' parameter is not defined on 'pipe_missing'"),
        "an undeclared prop behind a pipe suffix must still be reported");
}

/**
 * Declared, but with neither a `default` nor a passed value: dropped silently.
 * No warning here - the prop exists, it just has nothing to resolve to.
 */
static void test_a_declared_prop_with_no_default_and_no_value_is_dropped_silently(void)
{
    ASSERT_XML_REGISTERS("prop_missing", PROP_MISSING_XML);

    log_capture_start();
    lv_obj_t * inst = XML_CREATE(helix_test_env_screen(), "prop_missing", NULL);
    helix_test_pump(30);
    log_capture_stop();

    ASSERT_LABEL_TEXT(ASSERT_NAMED(inst, "missing_declared"), LV_LABEL_DEFAULT_TEXT);
    TEST_ASSERT_FALSE_MESSAGE(
        log_contains("'declared_no_default' parameter is not defined"),
        "a DECLARED prop must not be reported as undefined just because it has no value");
}

/*===========================================================================
 * <consts> and the `#` sigil
 *==========================================================================*/

/** `#name` in an attribute value resolves against the component's own consts. */
static void test_consts_resolve_in_attribute_values(void)
{
    ASSERT_XML_REGISTERS("const_demo", CONST_DEMO_XML);

    lv_obj_t * inst = XML_CREATE(helix_test_env_screen(), "const_demo", NULL);
    helix_test_pump(30);

    ASSERT_LABEL_TEXT(ASSERT_NAMED(inst, "const_label"), "Hello const");
    ASSERT_PAD_ALL(ASSERT_NAMED(inst, "const_box"), 9);
}

/** The registered consts are readable back out of the scope by name. */
static void test_registered_consts_are_readable_through_the_scope(void)
{
    ASSERT_XML_REGISTERS("const_demo", CONST_DEMO_XML);

    lv_xml_component_scope_t * scope = lv_xml_component_get_scope("const_demo");
    TEST_ASSERT_NOT_NULL(scope);

    TEST_ASSERT_EQUAL_STRING("Hello const", lv_xml_get_const(scope, "greeting"));
    TEST_ASSERT_EQUAL_STRING("9", lv_xml_get_const(scope, "gap"));

    /* Component consts are private: the global scope must not see them. */
    TEST_ASSERT_NULL_MESSAGE(lv_xml_get_const_silent(NULL, "greeting"),
                             "a component's const leaked into the global scope");
}

/**
 * An unresolvable `#name` drops the attribute and says where it was.
 *
 * Which lookup is used matters: resolve_consts() calls the SILENT variant and
 * emits its own message with the component and attribute names in it. If it
 * used lv_xml_get_const() instead, the generic "No constant was found" line
 * would also appear and the actionable one would be duplicated.
 */
static void test_an_unknown_const_is_reported_once_with_its_location(void)
{
    ASSERT_XML_REGISTERS("const_unknown", CONST_UNKNOWN_XML);

    log_capture_start();
    lv_obj_t * inst = XML_CREATE(helix_test_env_screen(), "const_unknown", NULL);
    helix_test_pump(30);
    log_capture_stop();

    TEST_ASSERT_TRUE_MESSAGE(
        log_contains("Unknown const `#not_a_const` in component `const_unknown` (attribute `text`)"),
        "the unknown-const warning must name the component and the attribute");
    TEST_ASSERT_FALSE_MESSAGE(
        log_contains("No constant was found with name"),
        "resolve_consts must use lv_xml_get_const_silent - the loud variant duplicates the report");

    ASSERT_LABEL_TEXT(ASSERT_NAMED(inst, "unknown_const_label"), LV_LABEL_DEFAULT_TEXT);
}

/** lv_xml_update_const() changes what instances built afterwards resolve to. */
static void test_update_const_changes_what_later_instances_resolve(void)
{
    ASSERT_XML_REGISTERS("const_demo", CONST_DEMO_XML);
    lv_xml_component_scope_t * scope = lv_xml_component_get_scope("const_demo");
    TEST_ASSERT_NOT_NULL(scope);

    lv_obj_t * before = XML_CREATE(helix_test_env_screen(), "const_demo", NULL);
    helix_test_pump(30);
    ASSERT_LABEL_TEXT(ASSERT_NAMED(before, "const_label"), "Hello const");

    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_RESULT_OK,
                                  (int)lv_xml_update_const(scope, "greeting", "Updated const"),
                                  "lv_xml_update_const() failed on an existing const");
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_update_const(scope, "gap", "21"));

    lv_obj_t * after = XML_CREATE(helix_test_env_screen(), "const_demo", NULL);
    helix_test_pump(30);
    ASSERT_LABEL_TEXT(ASSERT_NAMED(after, "const_label"), "Updated const");
    ASSERT_PAD_ALL(ASSERT_NAMED(after, "const_box"), 21);

    /* Already-built widgets are not retroactively re-resolved. */
    ASSERT_LABEL_TEXT(ASSERT_NAMED(before, "const_label"), "Hello const");
}

/*===========================================================================
 * Nesting
 *==========================================================================*/

/** A nested component uses its OWN default when the parent forwards nothing. */
static void test_a_nested_component_falls_back_to_its_own_default(void)
{
    ASSERT_XML_REGISTERS("inner_comp", INNER_COMP_XML);

    lv_obj_t * inst = XML_CREATE(helix_test_env_screen(), "inner_comp", NULL);
    helix_test_pump(30);

    ASSERT_LABEL_TEXT(ASSERT_NAMED(inst, "inner_label"), "inner default");
}

/**
 * A prop value crosses one nesting level: outer's `$outer_caption` becomes
 * inner's `caption`, which becomes the label text. With no value passed the
 * OUTER default (not the inner one) has to win, because the outer component
 * did supply a value - its own default - to the inner one.
 */
static void test_a_prop_value_passes_through_a_nesting_level(void)
{
    ASSERT_XML_REGISTERS("inner_comp", INNER_COMP_XML);
    ASSERT_XML_REGISTERS("outer_comp", OUTER_COMP_XML);

    lv_obj_t * defaulted = XML_CREATE(helix_test_env_screen(), "outer_comp", NULL);
    helix_test_pump(30);
    ASSERT_LABEL_TEXT(ASSERT_NAMED(defaulted, "inner_label"), "outer default");

    const char * attrs[] = {"outer_caption", "passed through", NULL, NULL};
    lv_obj_t * passed = XML_CREATE(helix_test_env_screen(), "outer_comp", attrs);
    helix_test_pump(30);
    ASSERT_LABEL_TEXT(ASSERT_NAMED(passed, "inner_label"), "passed through");

    /* The nested instance really is nested, not hoisted to the screen. */
    lv_obj_t * nested = ASSERT_NAMED(passed, "nested");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(passed, lv_obj_get_parent(nested),
                                  "the nested component was not parented inside the outer view");
}

/*===========================================================================
 * Registry accessors
 *==========================================================================*/

typedef struct {
    const char * names[16];
    uint32_t count;
} name_collector_t;

static void collect_name_cb(const char * name, void * user_data)
{
    name_collector_t * c = (name_collector_t *)user_data;
    if(c->count < 16) c->names[c->count] = name;
    c->count++;
}

static bool collector_has(const name_collector_t * c, const char * n)
{
    uint32_t max = c->count < 16 ? c->count : 16;
    for(uint32_t i = 0; i < max; i++) {
        if(c->names[i] && strcmp(c->names[i], n) == 0) return true;
    }
    return false;
}

/** get_scope resolves registered names only, and NULL is a guarded no-hit. */
static void test_get_scope_resolves_only_registered_names(void)
{
    TEST_ASSERT_NULL(lv_xml_component_get_scope("const_demo"));
    TEST_ASSERT_NULL_MESSAGE(lv_xml_component_get_scope(NULL),
                             "lv_xml_component_get_scope(NULL) must be a no-hit, not a match");

    ASSERT_XML_REGISTERS("const_demo", CONST_DEMO_XML);

    lv_xml_component_scope_t * scope = lv_xml_component_get_scope("const_demo");
    TEST_ASSERT_NOT_NULL(scope);
    /* Same name, same scope - the lookup is not handing out fresh copies. */
    TEST_ASSERT_EQUAL_PTR(scope, lv_xml_component_get_scope("const_demo"));

    /* The built-in scope is a registry entry like any other. */
    TEST_ASSERT_NOT_NULL_MESSAGE(lv_xml_component_get_scope("globals"),
                                 "the built-in \"globals\" scope is missing from the registry");
}

/** foreach enumerates every scope, the built-in one included, newest first. */
static void test_component_foreach_visits_globals_and_every_registered_component(void)
{
    name_collector_t empty = {{NULL}, 0};
    lv_xml_component_foreach(collect_name_cb, &empty);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, empty.count,
                                     "a fresh registry must hold exactly the \"globals\" scope");
    TEST_ASSERT_TRUE(collector_has(&empty, "globals"));

    ASSERT_XML_REGISTERS("inner_comp", INNER_COMP_XML);
    ASSERT_XML_REGISTERS("const_demo", CONST_DEMO_XML);

    name_collector_t c = {{NULL}, 0};
    lv_xml_component_foreach(collect_name_cb, &c);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(3, c.count, "foreach visited the wrong number of scopes");
    TEST_ASSERT_TRUE(collector_has(&c, "globals"));
    TEST_ASSERT_TRUE(collector_has(&c, "inner_comp"));
    TEST_ASSERT_TRUE(collector_has(&c, "const_demo"));

    /* Documented order: most recently registered first. */
    TEST_ASSERT_EQUAL_STRING_MESSAGE("const_demo", c.names[0],
                                     "foreach must yield the most recently registered scope first");

    /* A NULL callback is a guarded no-op, not a crash. */
    lv_xml_component_foreach(NULL, &c);
    TEST_ASSERT_EQUAL_UINT32(3, c.count);

    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_component_unregister("inner_comp"));

    name_collector_t after = {{NULL}, 0};
    lv_xml_component_foreach(collect_name_cb, &after);
    TEST_ASSERT_EQUAL_UINT32(2, after.count);
    TEST_ASSERT_FALSE_MESSAGE(collector_has(&after, "inner_comp"),
                              "an unregistered component is still enumerated");
}

static const char * DUP_V1 =
    "<component><view extends=\"lv_obj\" name=\"dup_root\">"
    "<lv_label name=\"dup_label\" text=\"version one\"/></view></component>";
static const char * DUP_V2 =
    "<component><view extends=\"lv_obj\" name=\"dup_root\">"
    "<lv_label name=\"dup_label\" text=\"version two\"/></view></component>";

/**
 * Registering a name that is already in use REPLACES the old definition; it does
 * not shadow it. The registry is a plain list with no uniqueness constraint, and
 * registration used to lv_ll_ins_head() unconditionally while lookup returned
 * the first match from the head - so the newest definition won lookups while the
 * old one stayed in the registry holding its whole heap (name, view_def, consts,
 * subjects). foreach reported the name twice, ONE unregister only exposed the
 * previous definition again, and nothing short of lv_xml_deinit() reclaimed the
 * shadowed scope. HELIX_HOT_RELOAD re-registers on every file save, so it
 * accumulated a full scope per save.
 *
 * Freeing the old scope is the same operation lv_xml_component_unregister()
 * already performs and carries the same precondition: delete instances of the
 * old definition before (or promptly after) replacing it, because the scope's
 * lv_style_t storage dies with it.
 */
static void test_re_registering_a_name_replaces_the_previous_definition(void)
{
    ASSERT_XML_REGISTERS("dup_comp", DUP_V1);
    TEST_ASSERT_NOT_NULL(lv_xml_component_get_scope("dup_comp"));

    ASSERT_XML_REGISTERS("dup_comp", DUP_V2);

    /* Exactly one scope answers to the name - globals plus dup_comp, no shadow.
     * (The old scope pointer is freed by now, so it is deliberately not compared:
     * the allocator is free to hand the same address straight back.) */
    name_collector_t c = {{NULL}, 0};
    lv_xml_component_foreach(collect_name_cb, &c);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, c.count,
                                     "expected globals + ONE dup_comp scope; the previous "
                                     "definition is still in the registry, shadowed");

    /* The newest definition is what builds. */
    lv_obj_t * newest = XML_CREATE(helix_test_env_screen(), "dup_comp", NULL);
    helix_test_pump(30);
    ASSERT_LABEL_TEXT(ASSERT_NAMED(newest, "dup_label"), "version two");

    /* Documented teardown order: instances first, then the component. */
    lv_obj_clean(helix_test_env_screen());
    helix_test_pump(30);

    /* ONE unregister empties the name - there is no older definition to resurface. */
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_component_unregister("dup_comp"));
    TEST_ASSERT_NULL_MESSAGE(lv_xml_component_get_scope("dup_comp"),
                             "one unregister did not remove the component - a shadowed "
                             "definition resurfaced");
    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_RESULT_INVALID, (int)lv_xml_component_unregister("dup_comp"),
                                  "unregistering an absent component must report failure");
}

/**
 * A re-registration that FAILS must leave the definition already in place
 * standing. HELIX_HOT_RELOAD fires on every save, including on a file caught
 * half-written, and losing the working component to a truncated read would take
 * the running panel down with it.
 *
 * The fixture has no `<view>` at all, so extract_view_content() returns NULL and
 * registration reports LV_RESULT_INVALID - after the metadata parse has already
 * run, which is the point: the replacement is committed only once the new
 * definition is known to be complete.
 */
static void test_a_failed_re_registration_leaves_the_previous_definition_intact(void)
{
    static const char * NO_VIEW = "<component><consts><px name=\"x\" value=\"1\"/></consts></component>";

    ASSERT_XML_REGISTERS("dup_comp", DUP_V1);

    log_capture_start();
    lv_result_t res = lv_xml_register_component_from_data("dup_comp", NO_VIEW);
    log_capture_stop();
    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_RESULT_INVALID, (int)res,
                                  "a component with no <view> must not register");

    TEST_ASSERT_NOT_NULL_MESSAGE(lv_xml_component_get_scope("dup_comp"),
                                 "the failed re-registration destroyed the working definition");

    name_collector_t c = {{NULL}, 0};
    lv_xml_component_foreach(collect_name_cb, &c);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, c.count,
                                     "the failed re-registration left a scope behind");

    lv_obj_t * still_v1 = XML_CREATE(helix_test_env_screen(), "dup_comp", NULL);
    helix_test_pump(30);
    ASSERT_LABEL_TEXT(ASSERT_NAMED(still_v1, "dup_label"), "version one");
}

/*===========================================================================
 * Unregister: what the scope owns vs what it merely borrows
 *==========================================================================*/

static size_t heap_free_size(void)
{
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    return mon.free_size;
}

/**
 * A full register -> create -> destroy -> unregister cycle must return every
 * byte it took. The fixture is deliberately the fattest scope the parser can
 * build - params, consts and a `<subject type="string">`, which is OWNED by the
 * scope and carries two 256-byte buffers that only the ownership walk in
 * lv_xml_subject_record_release() frees.
 *
 * Measured as a delta between whole cycles rather than against a cold baseline,
 * so a one-off allocation elsewhere in LVGL cannot be mistaken for a leak. A
 * per-cycle leak shows up as free_size falling monotonically.
 */
static void test_unregistering_a_component_returns_its_scope_memory_to_the_heap(void)
{
    /* Cycle 0 is a warm-up and its figure is discarded: the first pass through
     * this code costs 24 bytes that later passes do not, from one-time state
     * inside LVGL rather than from the scope (the figure is then flat forever).
     * Only the STEADY-STATE cycles are compared, which is what a leak in the
     * component teardown would move. */
    size_t after_cycle[5];

    for(int i = 0; i < 5; i++) {
        ASSERT_XML_REGISTERS("owns_everything", OWNS_EVERYTHING_XML);

        lv_obj_t * inst = XML_CREATE(helix_test_env_screen(), "owns_everything", NULL);
        helix_test_pump(30);
        ASSERT_LABEL_TEXT(ASSERT_NAMED(inst, "owns_label"), "owned");

        /* Widgets first: their observers sit on the scope-owned subject, and the
         * teardown order the engine documents deletes instances before the
         * component is unregistered. */
        lv_obj_clean(helix_test_env_screen());
        helix_test_pump(30);

        TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_component_unregister("owns_everything"));
        after_cycle[i] = heap_free_size();
    }

    for(int i = 2; i < 5; i++) {
        TEST_ASSERT_EQUAL_size_t_MESSAGE(
            after_cycle[1], after_cycle[i],
            helix_xml_assert_msgf(
                "register/create/unregister cycle %d did not return the heap to where cycle 1 "
                "left it - the scope is not freeing everything it owns", i));
    }
}

/** After unregistering, the scope-owned subject's name is gone from the registry. */
static void test_unregistering_a_component_removes_its_scope_owned_subject(void)
{
    ASSERT_XML_REGISTERS("owns_everything", OWNS_EVERYTHING_XML);

    lv_xml_component_scope_t * scope = lv_xml_component_get_scope("owns_everything");
    TEST_ASSERT_NOT_NULL(scope);
    TEST_ASSERT_NOT_NULL_MESSAGE(lv_xml_get_subject(scope, "scope_owned_text"),
                                 "<subject> did not register into the component's scope");

    /* It is scope-private: the global space never saw it. */
    log_capture_start();
    lv_subject_t * from_global = lv_xml_get_subject(NULL, "scope_owned_text");
    log_capture_stop();
    TEST_ASSERT_NULL_MESSAGE(from_global, "a component's <subject> leaked into the global scope");

    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_component_unregister("owns_everything"));
    TEST_ASSERT_NULL(lv_xml_component_get_scope("owns_everything"));
}

static uint32_t s_observer_hits;

static void count_observer_cb(lv_observer_t * observer, lv_subject_t * subject)
{
    LV_UNUSED(observer);
    LV_UNUSED(subject);
    s_observer_hits++;
}

/**
 * The ownership split, from the other side: a subject registered through the
 * PUBLIC lv_xml_register_subject() is borrowed, so unregistering the component
 * that holds the name must not deinit or free it.
 *
 * "Still alive" is proved by an observer registered from outside the engine.
 * lv_subject_deinit() rips every observer off the subject, so if unregister
 * treated a borrowed subject as owned, the callback would stop firing - and
 * lv_free() on this file-static address would abort outright.
 */
static void test_unregistering_a_component_does_not_free_a_borrowed_subject(void)
{
    ASSERT_XML_REGISTERS("inner_comp", INNER_COMP_XML);
    lv_xml_component_scope_t * scope = lv_xml_component_get_scope("inner_comp");
    TEST_ASSERT_NOT_NULL(scope);

    lv_subject_init_int(&s_borrowed_subject, 10);
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK,
                          (int)lv_xml_register_subject(scope, "borrowed_one", &s_borrowed_subject));

    s_observer_hits = 0;
    lv_subject_add_observer(&s_borrowed_subject, count_observer_cb, NULL);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, s_observer_hits,
                                     "lv_subject_add_observer() should fire once on registration");

    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_component_unregister("inner_comp"));

    /* The storage is intact and still observed. */
    lv_subject_set_int(&s_borrowed_subject, 42);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, s_observer_hits,
                                     "unregistering the component detached observers from a BORROWED subject");
    TEST_ASSERT_EQUAL_INT32(42, lv_subject_get_int(&s_borrowed_subject));

    lv_subject_deinit(&s_borrowed_subject);
}

/**
 * Unregistering while an instance is still on screen must leave that instance
 * safe to delete: the scope-owned subject is deinit'd (which detaches the
 * label's observer) before it is freed, so the widget's own LV_EVENT_DELETE
 * cannot reach into freed memory. The delete happens in tearDown; a regression
 * here shows up as a crash rather than an assertion failure, which is the point.
 */
static void test_unregistering_with_a_live_instance_leaves_it_safe_to_delete(void)
{
    ASSERT_XML_REGISTERS("owns_everything", OWNS_EVERYTHING_XML);

    lv_obj_t * inst = XML_CREATE(helix_test_env_screen(), "owns_everything", NULL);
    helix_test_pump(30);
    lv_obj_t * label = ASSERT_NAMED(inst, "owns_label");
    ASSERT_LABEL_TEXT(label, "owned");

    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_component_unregister("owns_everything"));

    /* The widget outlives its component and still holds the last value. */
    ASSERT_LABEL_TEXT(label, "owned");

    lv_obj_delete(inst);
    helix_test_pump(30);
    ASSERT_CHILD_COUNT(helix_test_env_screen(), 0);
}

/*===========================================================================
 * Deferred scope teardown: a scope with live instances is not freed
 *
 * A component's `<styles>` live in the scope, and lv_obj_add_style() records the
 * RAW lv_style_t* - it does not copy. So every widget an instance built points
 * into scope->style_ll for as long as it is on screen, and freeing the scope out
 * from under it is a use-after-free on the next layout or draw pass.
 *
 * This used to be masked: re-registering a name SHADOWED the old scope and
 * leaked it, which accidentally kept the styles alive. Making re-registration a
 * real replacement turned that leak into a live UAF, and HELIX_HOT_RELOAD
 * re-registers on every file save - with the panel it belongs to still on
 * screen. The scope is therefore retired (out of the registry, unfindable) but
 * held until its last instance is deleted.
 *
 * HOW THESE TESTS PROVE IT, given there is no public "is this scope alive?":
 *
 *  - DEFER_V1_XML is deliberately FAT (a shared style, consts, and a
 *    `<subject type="string">` whose two 256-byte buffers only the scope's own
 *    ownership walk frees) and DEFER_V2_XML deliberately LEAN. Registering v2
 *    over v1 therefore moves lv_mem free_size in opposite directions depending
 *    on whether v1 was freed: DOWN if it was held (v2 allocated, nothing
 *    returned), UP if it was freed (v1 returned more than v2 took). That sign is
 *    the assertion, and it is unambiguous.
 *  - ASSERT_STYLE_INT on the stale instance is the one that proves the BUG is
 *    fixed rather than that a counter moved: it reads a property value back
 *    through the raw lv_style_t* the widget is still holding.
 *==========================================================================*/

/* lv_mem_monitor() only reports anything under the BUILTIN allocator. An
 * LV_STDLIB_CLIB build - which is what an ASAN run needs, so the sanitizer can
 * see LVGL's allocations at all - answers zero to everything, and a heap
 * assertion there would fail on the configuration rather than on the behaviour.
 * Gate the accounting so the BEHAVIOURAL assertions below it still run: reading
 * a property back through a stale instance's raw lv_style_t* is the assertion
 * ASAN is there to adjudicate. */
#if LV_USE_STDLIB_MALLOC == LV_STDLIB_BUILTIN
    #define ASSERT_HEAP(cond, msg) TEST_ASSERT_TRUE_MESSAGE((cond), (msg))
#else
    #define ASSERT_HEAP(cond, msg) do { (void)(msg); } while(0)
#endif

/* v1: everything that costs the scope real heap AND a shared style the instance
 * will hold a raw pointer to. */
static const char * DEFER_V1_XML =
    "<component>"
    "  <consts><string name=\"cap\" value=\"version one\"/></consts>"
    "  <styles>"
    "    <style name=\"boxy\" pad_all=\"11\" radius=\"37\" border_width=\"5\"/>"
    "  </styles>"
    "  <subjects>"
    "    <subject name=\"defer_text\" type=\"string\" value=\"v1\"/>"
    "  </subjects>"
    "  <view extends=\"lv_obj\" name=\"defer_root\">"
    "    <lv_obj name=\"defer_box\"><style name=\"boxy\"/></lv_obj>"
    "    <lv_label name=\"defer_label\" text=\"#cap\"/>"
    "  </view>"
    "</component>";

/* v2: the same name, a different definition, and far leaner than v1. */
static const char * DEFER_V2_XML =
    "<component>"
    "  <styles><style name=\"boxy\" pad_all=\"22\"/></styles>"
    "  <view extends=\"lv_obj\" name=\"defer_root\">"
    "    <lv_obj name=\"defer_box\"><style name=\"boxy\"/></lv_obj>"
    "    <lv_label name=\"defer_label\" text=\"version two\"/>"
    "  </view>"
    "</component>";

/** The `defer_box` of an instance, with its shared style still readable. */
static void assert_defer_box_pad(lv_obj_t * inst, int32_t expected, const char * why)
{
    lv_obj_t * box = ASSERT_NAMED(inst, "defer_box");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(expected, lv_obj_get_style_pad_top(box, LV_PART_MAIN), why);
    ASSERT_STYLE_INT(box, LV_STYLE_PAD_TOP, LV_PART_MAIN, expected);
}

/**
 * Re-register a name while an instance of it is on screen: the old scope must
 * NOT be freed, and the stale instance's shared style must still read back the
 * value v1 wrote.
 *
 * Without the deferral the second half of this test reads a freed lv_style_t -
 * which is exactly the shape that does not fault deterministically, so the heap
 * assertion above it is what makes the failure reliable and ASAN is what makes
 * it unambiguous.
 */
static void test_re_registering_with_a_live_instance_holds_the_old_scope(void)
{
    ASSERT_XML_REGISTERS("defer_comp", DEFER_V1_XML);

    lv_obj_t * inst = XML_CREATE(helix_test_env_screen(), "defer_comp", NULL);
    helix_test_pump(30);
    assert_defer_box_pad(inst, 11, "the v1 shared style never reached the instance");

    const size_t before = heap_free_size();
    ASSERT_XML_REGISTERS("defer_comp", DEFER_V2_XML);
    const size_t after = heap_free_size();

    ASSERT_HEAP(
        after < before,
        "free_size ROSE across a re-registration that had a live instance - the old scope "
        "was freed while widgets still held raw lv_style_t pointers into it");

    /* The load-bearing assertion: read a real property value back through the
     * pointer the widget is still holding. */
    assert_defer_box_pad(inst, 11,
                         "the stale instance's shared style no longer reads back - its scope "
                         "was freed underneath it");
    ASSERT_LABEL_TEXT(ASSERT_NAMED(inst, "defer_label"), "version one");
}

/**
 * The mirror image, and the "unchanged when nothing is live" guarantee: with no
 * instance on screen the old scope is freed immediately, so free_size moves the
 * OTHER way - v1 returns more than the leaner v2 takes.
 */
static void test_re_registering_with_no_live_instance_frees_the_old_scope_at_once(void)
{
    ASSERT_XML_REGISTERS("defer_comp", DEFER_V1_XML);

    const size_t before = heap_free_size();
    ASSERT_XML_REGISTERS("defer_comp", DEFER_V2_XML);
    const size_t after = heap_free_size();

    ASSERT_HEAP(
        after > before,
        "free_size FELL across a re-registration with no live instance - the old scope was "
        "deferred when it should have been freed on the spot");
}

/** A deferred scope is out of the registry the moment it is replaced: a lookup
 *  must find the new definition, and foreach must see the name exactly once. */
static void test_a_lookup_after_a_deferred_replacement_finds_the_new_definition(void)
{
    ASSERT_XML_REGISTERS("defer_comp", DEFER_V1_XML);
    lv_obj_t * stale = XML_CREATE(helix_test_env_screen(), "defer_comp", NULL);
    helix_test_pump(30);

    ASSERT_XML_REGISTERS("defer_comp", DEFER_V2_XML);

    name_collector_t c = {{NULL}, 0};
    lv_xml_component_foreach(collect_name_cb, &c);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, c.count,
                                     "expected globals + ONE defer_comp; the deferred scope is "
                                     "still enumerated, so it is still findable");

    /* What builds now is v2 - the held scope is memory only, not a definition. */
    lv_obj_t * fresh = XML_CREATE(helix_test_env_screen(), "defer_comp", NULL);
    helix_test_pump(30);
    ASSERT_LABEL_TEXT(ASSERT_NAMED(fresh, "defer_label"), "version two");
    assert_defer_box_pad(fresh, 22, "a lookup after the replacement built the OLD definition");

    /* ...while the stale instance still reads v1. Both definitions coexist. */
    assert_defer_box_pad(stale, 11, "the stale instance stopped reading its own definition");
}

/**
 * Deleting the last instance is what releases the held scope. The release is
 * pushed onto lv_async_call() rather than run from the LV_EVENT_DELETE handler:
 * LVGL sends that event to the view root BEFORE destroying the subtree, and
 * every child's destructor still reads style properties off itself and its
 * ancestors. So the pump is not decoration here - it is when the free happens.
 */
static void test_deleting_the_last_instance_releases_the_deferred_scope(void)
{
    ASSERT_XML_REGISTERS("defer_comp", DEFER_V1_XML);
    lv_obj_t * inst = XML_CREATE(helix_test_env_screen(), "defer_comp", NULL);
    helix_test_pump(30);

    ASSERT_XML_REGISTERS("defer_comp", DEFER_V2_XML);
    const size_t held = heap_free_size();

    lv_obj_delete(inst);
    helix_test_pump(30);
    const size_t released = heap_free_size();

    /* The scope-owned `<subject type="string">` alone carries two 256-byte
     * buffers, so anything at or below that is the scope not coming back. */
    ASSERT_HEAP(
        released > held + 512,
        "deleting the last instance did not return the deferred scope - it is now leaked "
        "until lv_xml_deinit()");
}

/** Two live instances: the scope is released only when the SECOND one dies. */
static void test_a_deferred_scope_outlives_all_but_the_last_instance(void)
{
    ASSERT_XML_REGISTERS("defer_comp", DEFER_V1_XML);
    lv_obj_t * a = XML_CREATE(helix_test_env_screen(), "defer_comp", NULL);
    lv_obj_t * b = XML_CREATE(helix_test_env_screen(), "defer_comp", NULL);
    helix_test_pump(30);

    ASSERT_XML_REGISTERS("defer_comp", DEFER_V2_XML);
    const size_t held = heap_free_size();

    lv_obj_delete(a);
    helix_test_pump(30);
    const size_t after_first = heap_free_size();

    /* b is still using the scope's styles, so they had better still be there. */
    assert_defer_box_pad(b, 11,
                         "the scope was released while a second instance was still using it");

    lv_obj_delete(b);
    helix_test_pump(30);
    const size_t after_second = heap_free_size();

    /* a and b are identical trees, so the two deletes return the same widget
     * memory. The scope rides on the second one and nothing else does. */
    const size_t first_delta = after_first - held;
    const size_t second_delta = after_second - after_first;
    ASSERT_HEAP(
        second_delta > first_delta + 512,
        helix_xml_assert_msgf(
            "the two deletes returned comparable amounts (%u then %u bytes) - the scope was "
            "not being held for the second instance",
            (unsigned)first_delta, (unsigned)second_delta));
}

/**
 * A nested component instance is an instance of ITS OWN scope, not its host's.
 * The count is taken in lv_xml_create_in_scope(), which both the top-level path
 * (lv_xml_create) and the nested one (lv_xml_component_process) funnel through -
 * miss that and hot-reloading a leaf component while a panel that embeds it is
 * on screen frees the leaf's styles under the panel.
 */
static void test_a_nested_instance_counts_against_its_own_scope(void)
{
    ASSERT_XML_REGISTERS("defer_comp", DEFER_V1_XML);
    ASSERT_XML_REGISTERS("defer_host",
                         "<component><view extends=\"lv_obj\" name=\"host_root\">"
                         "<defer_comp name=\"embedded\"/></view></component>");

    lv_obj_t * host = XML_CREATE(helix_test_env_screen(), "defer_host", NULL);
    helix_test_pump(30);
    lv_obj_t * nested = ASSERT_NAMED(host, "embedded");
    assert_defer_box_pad(nested, 11, "the nested instance never got the v1 style");

    /* Only the LEAF is re-registered; the host is untouched and stays on screen. */
    const size_t before = heap_free_size();
    ASSERT_XML_REGISTERS("defer_comp", DEFER_V2_XML);
    const size_t held = heap_free_size();

    ASSERT_HEAP(held < before,
                "the leaf's scope was freed although a NESTED instance of it was "
                "still on screen - the nested create path is not counted");
    assert_defer_box_pad(nested, 11,
                         "the nested instance's shared style no longer reads back");

    /* Deleting the HOST takes the nested root with it, which is what releases
     * the leaf's scope. */
    lv_obj_delete(host);
    helix_test_pump(30);
    ASSERT_HEAP(heap_free_size() > held + 512,
                "deleting the host did not release the leaf's deferred scope");
}

/**
 * The actual HELIX_HOT_RELOAD loop, run to a steady state: save the file,
 * re-register over a live panel, let the panel be rebuilt and the old instance
 * deleted. Every round has to give back exactly what it took.
 *
 * A deferral that never fires is the failure mode this catches, and it is a
 * quiet one - the engine keeps working, it just accumulates a full scope per
 * save, which is precisely the leak the replacement was introduced to fix.
 * Compared cycle-to-cycle, discarding cycle 0, for the reason documented on the
 * unregister accounting test above.
 */
static void test_repeated_hot_reload_with_a_live_instance_reaches_a_steady_heap(void)
{
    size_t after_cycle[5];

    for(int i = 0; i < 5; i++) {
        ASSERT_XML_REGISTERS("defer_comp", DEFER_V1_XML);
        lv_obj_t * inst = XML_CREATE(helix_test_env_screen(), "defer_comp", NULL);
        helix_test_pump(30);

        /* The save: the old definition is retired with its instance still up. */
        ASSERT_XML_REGISTERS("defer_comp", DEFER_V2_XML);
        assert_defer_box_pad(inst, 11, "the stale instance lost its style mid-reload");

        /* The rebuild: the panel drops the old instance, which releases v1. */
        lv_obj_delete(inst);
        helix_test_pump(30);

        TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_component_unregister("defer_comp"));
        after_cycle[i] = heap_free_size();
    }

    for(int i = 2; i < 5; i++) {
        ASSERT_HEAP(
            after_cycle[1] == after_cycle[i],
            helix_xml_assert_msgf(
                "hot-reload cycle %d did not return the heap to where cycle 1 left it (%u vs %u) "
                "- a deferred scope is never being released", i,
                (unsigned)after_cycle[i], (unsigned)after_cycle[1]));
    }
}

/**
 * lv_xml_deinit() FORCE-frees a deferred scope, live instances or not. It runs at
 * process (and per-test) teardown where everything is going away anyway, and
 * honouring the deferral there would leak the scope permanently - nothing would
 * ever decrement it again, and the per-test isolation this whole suite depends on
 * would drift a scope per cycle.
 *
 * The instance is left ON SCREEN across the deinit on purpose: its view root
 * still carries the delete hook whose user data lives inside the scope being
 * freed, so the force path has to disarm it. If it does not, the lv_obj_clean()
 * at the end fires that hook on freed memory.
 *
 * Measured as CONTROL vs DEFERRED rather than cycle-to-cycle, because an
 * lv_xml_deinit()/lv_xml_init() round trip with widgets on screen moves the heap
 * by a few dozen bytes on its own (LVGL-side, present with the XML engine doing
 * nothing at all). Both halves below run the identical sequence except for when
 * the instance is deleted, so everything but the held scope cancels out and the
 * remaining difference is v1's payload - roughly 1 KB against that noise floor.
 */
static void test_deinit_force_frees_a_scope_that_still_has_live_instances(void)
{
    /* CONTROL: the instance is gone before v1 is replaced, so v1 is freed on the
     * spot and lv_xml_deinit() has only v2 + the engine's own registries left. */
    ASSERT_XML_REGISTERS("defer_comp", DEFER_V1_XML);
    lv_obj_t * inst = XML_CREATE(helix_test_env_screen(), "defer_comp", NULL);
    helix_test_pump(30);
    lv_obj_delete(inst);
    helix_test_pump(30);
    ASSERT_XML_REGISTERS("defer_comp", DEFER_V2_XML);

    const size_t control_before = heap_free_size();
    lv_xml_deinit();
    const size_t control_returned = heap_free_size() - control_before;

    lv_xml_init();
    lv_obj_clean(helix_test_env_screen());
    helix_test_pump(30);

    /* DEFERRED: same sequence, instance still on screen when v1 is replaced. */
    ASSERT_XML_REGISTERS("defer_comp", DEFER_V1_XML);
    inst = XML_CREATE(helix_test_env_screen(), "defer_comp", NULL);
    helix_test_pump(30);
    assert_defer_box_pad(inst, 11, "the v1 shared style never reached the instance");
    ASSERT_XML_REGISTERS("defer_comp", DEFER_V2_XML);

    const size_t deferred_before = heap_free_size();
    lv_xml_deinit();
    const size_t deferred_returned = heap_free_size() - deferred_before;

    ASSERT_HEAP(
        deferred_returned > control_returned + 512,
        helix_xml_assert_msgf(
            "lv_xml_deinit() returned %u bytes with a deferred scope held vs %u without it - "
            "it is honouring the deferral instead of forcing the free, so the scope is leaked "
            "for good",
            (unsigned)deferred_returned, (unsigned)control_returned));

    /* The orphaned widgets are still on screen and their delete hooks pointed
     * into the scope that was just force-freed. Deleting them must not reach
     * into it - a plain run survives this by luck, ASAN does not. */
    lv_xml_init();
    lv_obj_clean(helix_test_env_screen());
    helix_test_pump(30);
    ASSERT_CHILD_COUNT(helix_test_env_screen(), 0);
}

/*===========================================================================
 * Naming the instance root
 *
 * Three names compete for the root object of a component instance, and the
 * order between them is a contract: lv_obj_find_by_name() is how every panel
 * reaches into a component it placed.
 *
 *   1. the instance site      <my_comp name="header"/>
 *   2. the component's view   <view name="chrome">
 *   3. the "<component>_#" indexed default
 *
 * Both creation paths have to agree - lv_xml_create() for a top-level instance,
 * lv_xml_component_process() for one nested inside another component's view -
 * so every case below is asserted through both.
 *==========================================================================*/

/** Register `inner`, plus a `wrapper` that instantiates it with `inner_attrs`. */
static void register_naming_pair(const char * inner_view_attrs, const char * inner_attrs)
{
    char inner_xml[256];
    char wrapper_xml[256];

    lv_snprintf(inner_xml, sizeof(inner_xml),
                "<component><view extends=\"lv_obj\"%s>"
                "<lv_label name=\"leaf\" text=\"L\"/>"
                "</view></component>", inner_view_attrs);
    lv_snprintf(wrapper_xml, sizeof(wrapper_xml),
                "<component><view extends=\"lv_obj\" name=\"wrap_root\">"
                "<inner%s/>"
                "</view></component>", inner_attrs);

    ASSERT_XML_REGISTERS("inner", inner_xml);
    ASSERT_XML_REGISTERS("wrapper", wrapper_xml);
}

/** The nested `inner` instance inside a created `wrapper` root. */
static lv_obj_t * nested_inner(lv_obj_t * wrapper_root)
{
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, lv_obj_get_child_count(wrapper_root),
                                     "the wrapper must hold exactly the nested instance");
    return lv_obj_get_child(wrapper_root, 0);
}

/**
 * With no name anywhere, the root falls back to the indexed default. Named as a
 * test because the default used to be written twice - once into a buffer that
 * was then thrown away in lv_xml_create_in_scope(), once for real in each
 * caller - and deleting the dead half must not take the live one with it.
 */
static void test_an_unnamed_root_gets_the_indexed_component_name(void)
{
    register_naming_pair("", "");

    lv_obj_t * screen = helix_test_env_screen();

    lv_obj_t * top = XML_CREATE(screen, "inner", NULL);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("inner_#", lv_obj_get_name(top),
                                     "a top-level instance lost its default indexed name");

    lv_obj_t * wrap = XML_CREATE(screen, "wrapper", NULL);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("inner_#", lv_obj_get_name(nested_inner(wrap)),
                                     "a nested instance lost its default indexed name");

    /* The indexed form is what makes the instances findable. */
    TEST_ASSERT_EQUAL_PTR(top, lv_obj_find_by_name(screen, "inner_0"));
}

/**
 * `<view name="...">` on the component's own root used to be overwritten by the
 * indexed default, silently: setting a name on your own view did nothing at all
 * and nothing said so. It is now kept when the instance site does not name the
 * object.
 */
static void test_a_view_name_survives_when_the_instance_site_does_not_name_it(void)
{
    register_naming_pair(" name=\"chrome\"", "");

    lv_obj_t * screen = helix_test_env_screen();

    lv_obj_t * top = XML_CREATE(screen, "inner", NULL);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("chrome", lv_obj_get_name(top),
                                     "<view name=\"chrome\"> was discarded on a top-level instance");
    TEST_ASSERT_EQUAL_PTR(top, lv_obj_find_by_name(screen, "chrome"));

    lv_obj_t * wrap = XML_CREATE(screen, "wrapper", NULL);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("chrome", lv_obj_get_name(nested_inner(wrap)),
                                     "<view name=\"chrome\"> was discarded on a nested instance");
}

/**
 * The instance site is the more specific statement and still wins - callers rely
 * on being able to name the thing they placed - but the attribute it displaces
 * was written on purpose, so losing it is reported rather than silent.
 */
static void test_the_instance_site_name_beats_the_view_name_and_reports_it(void)
{
    register_naming_pair(" name=\"chrome\"", " name=\"header\"");

    lv_obj_t * screen = helix_test_env_screen();
    static const char * site_attrs[] = {"name", "header", NULL};

    log_capture_start();
    lv_obj_t * top = XML_CREATE(screen, "inner", site_attrs);
    log_capture_stop();

    TEST_ASSERT_EQUAL_STRING_MESSAGE("header", lv_obj_get_name(top),
                                     "the instance-site name did not win on a top-level instance");
    TEST_ASSERT_TRUE_MESSAGE(log_contains("chrome"),
                             "the displaced <view name> was dropped silently");
    TEST_ASSERT_TRUE_MESSAGE(log_contains("takes precedence"),
                             "the override was not explained in the log");

    /* Re-register so the nested half meets a scope that has not warned yet: the
     * report is once per scope, and the two halves exercise two different call
     * sites (lv_xml_create() above, lv_xml_component_process() below). */
    register_naming_pair(" name=\"chrome\"", " name=\"header\"");

    log_capture_start();
    lv_obj_t * wrap = XML_CREATE(screen, "wrapper", NULL);
    log_capture_stop();

    TEST_ASSERT_EQUAL_STRING_MESSAGE("header", lv_obj_get_name(nested_inner(wrap)),
                                     "the instance-site name did not win on a nested instance");
    TEST_ASSERT_TRUE_MESSAGE(log_contains("chrome"),
                             "the displaced <view name> was dropped silently when nested");
}

/**
 * The clash is a static property of the component definition, so repeating it on
 * every instantiation only costs log room - one HelixScreen debug bundle carried
 * 363 copies, 109 of them for one component, inside a ring that has to hold the
 * whole session. Report it once per scope; re-registering reports it again.
 */
static void test_the_displaced_view_name_is_reported_once_per_scope(void)
{
    register_naming_pair(" name=\"chrome\"", " name=\"header\"");

    lv_obj_t * screen = helix_test_env_screen();
    static const char * site_attrs[] = {"name", "header", NULL};

    log_capture_start();
    XML_CREATE(screen, "inner", site_attrs);
    log_capture_stop();
    TEST_ASSERT_TRUE_MESSAGE(log_contains("takes precedence"),
                             "the first instantiation did not report the override");

    log_capture_start();
    XML_CREATE(screen, "inner", site_attrs);
    XML_CREATE(screen, "wrapper", NULL);
    log_capture_stop();
    TEST_ASSERT_FALSE_MESSAGE(log_contains("takes precedence"),
                              "the override was re-reported on later instantiations of the same "
                              "scope");

    /* A fresh registration is a fresh definition, and may have been corrected -
     * or not. Either way it is worth one report. */
    register_naming_pair(" name=\"chrome\"", " name=\"header\"");

    log_capture_start();
    XML_CREATE(screen, "inner", site_attrs);
    log_capture_stop();
    TEST_ASSERT_TRUE_MESSAGE(log_contains("takes precedence"),
                             "a re-registered component never reported its override again");
}

/** Agreeing names are not a conflict and must not be reported as one. */
static void test_matching_view_and_instance_names_are_not_reported(void)
{
    register_naming_pair(" name=\"chrome\"", " name=\"chrome\"");

    log_capture_start();
    lv_obj_t * wrap = XML_CREATE(helix_test_env_screen(), "wrapper", NULL);
    log_capture_stop();

    TEST_ASSERT_EQUAL_STRING("chrome", lv_obj_get_name(nested_inner(wrap)));
    TEST_ASSERT_FALSE_MESSAGE(log_contains("takes precedence"),
                              "an instance-site name identical to the view name was reported "
                              "as an override");
}

/*---------------------------------------------------------------------------
 * main
 *--------------------------------------------------------------------------*/

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_each_prop_type_falls_back_to_its_declared_default);
    RUN_TEST(test_a_prop_passed_at_create_time_overrides_its_default);
    RUN_TEST(test_a_subject_typed_prop_produces_a_live_binding);
    RUN_TEST(test_a_prop_referenced_but_never_declared_warns_and_drops_the_attribute);
    RUN_TEST(test_a_value_supplied_for_an_undeclared_prop_warns_and_does_not_crash);
    RUN_TEST(test_an_undeclared_prop_with_a_pipe_suffix_warns_and_does_not_crash);
    RUN_TEST(test_a_declared_prop_with_no_default_and_no_value_is_dropped_silently);

    RUN_TEST(test_consts_resolve_in_attribute_values);
    RUN_TEST(test_registered_consts_are_readable_through_the_scope);
    RUN_TEST(test_an_unknown_const_is_reported_once_with_its_location);
    RUN_TEST(test_update_const_changes_what_later_instances_resolve);

    RUN_TEST(test_a_nested_component_falls_back_to_its_own_default);
    RUN_TEST(test_a_prop_value_passes_through_a_nesting_level);

    RUN_TEST(test_an_unnamed_root_gets_the_indexed_component_name);
    RUN_TEST(test_a_view_name_survives_when_the_instance_site_does_not_name_it);
    RUN_TEST(test_the_instance_site_name_beats_the_view_name_and_reports_it);
    RUN_TEST(test_the_displaced_view_name_is_reported_once_per_scope);
    RUN_TEST(test_matching_view_and_instance_names_are_not_reported);

    RUN_TEST(test_get_scope_resolves_only_registered_names);
    RUN_TEST(test_component_foreach_visits_globals_and_every_registered_component);
    RUN_TEST(test_re_registering_a_name_replaces_the_previous_definition);
    RUN_TEST(test_a_failed_re_registration_leaves_the_previous_definition_intact);

    RUN_TEST(test_unregistering_a_component_returns_its_scope_memory_to_the_heap);
    RUN_TEST(test_unregistering_a_component_removes_its_scope_owned_subject);
    RUN_TEST(test_unregistering_a_component_does_not_free_a_borrowed_subject);
    RUN_TEST(test_unregistering_with_a_live_instance_leaves_it_safe_to_delete);

    RUN_TEST(test_re_registering_with_a_live_instance_holds_the_old_scope);
    RUN_TEST(test_re_registering_with_no_live_instance_frees_the_old_scope_at_once);
    RUN_TEST(test_a_lookup_after_a_deferred_replacement_finds_the_new_definition);
    RUN_TEST(test_deleting_the_last_instance_releases_the_deferred_scope);
    RUN_TEST(test_a_deferred_scope_outlives_all_but_the_last_instance);
    RUN_TEST(test_a_nested_instance_counts_against_its_own_scope);
    RUN_TEST(test_repeated_hot_reload_with_a_live_instance_reaches_a_steady_heap);
    RUN_TEST(test_deinit_force_frees_a_scope_that_still_has_live_instances);

    return UNITY_END();
}
