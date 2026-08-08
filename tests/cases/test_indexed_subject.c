/**
 * @file test_indexed_subject.c
 *
 * `${...}` name composition inside an attribute value (xml_compose_indexed() in
 * src/xml/lv_xml.c), used to give each iteration of a `<repeat>` its own
 * subject: `bind_text="demo_${i}_v"` -> demo_0_v, demo_1_v, demo_2_v.
 *
 * ---------------------------------------------------------------------------
 * WHAT MAKES THIS ITS OWN FEATURE
 *
 * A whole-value `$name` is a straight parameter substitution and predates all
 * of this. `${name}` is different in two ways that matter: it is EMBEDDED, so
 * the result is a new string built at parse time rather than a value handed
 * through, and its namespace is the union of the `<repeat>` loop index and the
 * component's params - so `status_${grp}_${i}_x` composes a loop index and a
 * caller-supplied prop into one subject name, in one attribute.
 *
 * The failure mode is what makes it worth testing rather than eyeballing: an
 * unresolvable token splices EMPTY and the parse continues. The widget is
 * built, the tree has the right shape, and the only thing that went wrong is
 * that a binding now points at a subject name nobody registered. So the tests
 * assert the resolved TEXT that arrived through each per-iteration binding -
 * distinct values per index, so a composition that dropped the index or reused
 * iteration 0's name cannot pass - and, for the unresolved case, the warning.
 *
 * Not covered here (it belongs to `<repeat>` rather than to composition):
 * reactive `count="a_subject"` rebuilds, and `${expr}` arithmetic splices.
 * ---------------------------------------------------------------------------
 *
 * The subjects are file-static and registered into the GLOBAL scope, which is
 * how a consuming app publishes per-item state. They are re-initialised at the
 * top of every test that uses them, because the preceding test's lv_deinit()
 * reclaimed the pool their observer lists lived in, and deinit'd at the end so
 * nothing observes static storage across the teardown.
 *
 * SPDX-License-Identifier: MIT
 */

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
 * Global string subjects
 *--------------------------------------------------------------------------*/

#define IDX_SUBJECT_POOL 3
#define IDX_SUBJECT_BUF  32

static lv_subject_t g_subs[IDX_SUBJECT_POOL];
static char g_bufs[IDX_SUBJECT_POOL][IDX_SUBJECT_BUF];

/** Init a pool entry and register it globally under @p name. */
static void register_global_string(uint32_t slot, const char * name, const char * value)
{
    lv_subject_init_string(&g_subs[slot], g_bufs[slot], NULL, IDX_SUBJECT_BUF, value);
    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_RESULT_OK,
                                  (int)lv_xml_register_subject(NULL, name, &g_subs[slot]),
                                  helix_xml_assert_msgf("could not register global subject '%s'",
                                                        name));
}

/** Drop every observer off the static subjects before the env tears LVGL down. */
static void release_globals(uint32_t used)
{
    for(uint32_t i = 0; i < used; i++) lv_subject_deinit(&g_subs[i]);
}

/*===========================================================================
 * ${i}
 *==========================================================================*/

static const char * IDX_XML =
    "<component>"
    "  <view extends=\"lv_obj\" name=\"idx_root\">"
    "    <lv_obj name=\"row\">"
    "      <repeat count=\"3\">"
    "        <lv_label bind_text=\"demo_${i}_v\"/>"
    "      </repeat>"
    "    </lv_obj>"
    "  </view>"
    "</component>";

/**
 * Each iteration must bind to its OWN subject. The three subjects carry three
 * different strings, so an implementation that composed the name once and
 * reused it - or that spliced the index empty - lands every label on the same
 * text and fails on the second assertion.
 */
static void test_the_loop_index_composes_a_per_iteration_subject_name(void)
{
    register_global_string(0, "demo_0_v", "zero");
    register_global_string(1, "demo_1_v", "one");
    register_global_string(2, "demo_2_v", "two");

    ASSERT_XML_REGISTERS("idx_i", IDX_XML);

    lv_obj_t * root = XML_CREATE(helix_test_env_screen(), "idx_i", NULL);
    helix_test_pump(30);

    lv_obj_t * row = ASSERT_NAMED(root, "row");
    ASSERT_CHILD_COUNT(row, 3);

    ASSERT_LABEL_TEXT(lv_obj_get_child(row, 0), "zero");
    ASSERT_LABEL_TEXT(lv_obj_get_child(row, 1), "one");
    ASSERT_LABEL_TEXT(lv_obj_get_child(row, 2), "two");

    /* The binding is live, not a one-off text copy: writing to the subject
     * behind iteration 1 must move that label and only that label. */
    lv_subject_copy_string(&g_subs[1], "ONE!");
    helix_test_pump(30);
    ASSERT_LABEL_TEXT(lv_obj_get_child(row, 1), "ONE!");
    ASSERT_LABEL_TEXT(lv_obj_get_child(row, 0), "zero");
    ASSERT_LABEL_TEXT(lv_obj_get_child(row, 2), "two");

    release_globals(3);
}

/*===========================================================================
 * ${param} + ${i}
 *==========================================================================*/

static const char * IDX_PARAM_XML =
    "<component>"
    "  <api><prop name=\"grp\" type=\"string\"/></api>"
    "  <view extends=\"lv_obj\" name=\"idxp_root\">"
    "    <lv_obj name=\"row\">"
    "      <repeat count=\"2\">"
    "        <lv_label bind_text=\"status_${grp}_${i}_x\"/>"
    "      </repeat>"
    "    </lv_obj>"
    "  </view>"
    "</component>";

/**
 * Two tokens of different KINDS in one attribute: a component param supplied by
 * the caller and the loop index. status_${grp}_${i}_x -> status_fan_0_x and
 * status_fan_1_x, so both tokens have to resolve, in the right places, for
 * either label to find anything.
 */
static void test_a_component_param_and_the_loop_index_compose_one_name(void)
{
    register_global_string(0, "status_fan_0_x", "fan-zero");
    register_global_string(1, "status_fan_1_x", "fan-one");

    ASSERT_XML_REGISTERS("idx_param", IDX_PARAM_XML);

    const char * attrs[] = {"grp", "fan", NULL, NULL};
    lv_obj_t * root = XML_CREATE(helix_test_env_screen(), "idx_param", attrs);
    helix_test_pump(30);

    lv_obj_t * row = ASSERT_NAMED(root, "row");
    ASSERT_CHILD_COUNT(row, 2);

    ASSERT_LABEL_TEXT(lv_obj_get_child(row, 0), "fan-zero");
    ASSERT_LABEL_TEXT(lv_obj_get_child(row, 1), "fan-one");

    release_globals(2);
}

/*===========================================================================
 * Unresolvable tokens
 *==========================================================================*/

static const char * IDX_UNRESOLVED_XML =
    "<component>"
    "  <view extends=\"lv_obj\" name=\"idxu_root\">"
    "    <lv_obj name=\"row\">"
    "      <repeat count=\"2\">"
    "        <lv_label text=\"placeholder\" bind_text=\"x_${nope}_${i}\"/>"
    "      </repeat>"
    "    </lv_obj>"
    "  </view>"
    "</component>";

/**
 * `${nope}` is not a param and is not the loop index. The documented behaviour
 * is to splice empty, warn, and carry on - the bodies still expand and the
 * label keeps whatever text it had. That "carry on" is the dangerous half: the
 * tree looks completely correct and the composed name silently points at a
 * subject nobody registered, so the warning is the only signal and is asserted
 * as such.
 */
static void test_an_unresolvable_token_splices_empty_and_warns(void)
{
    ASSERT_XML_REGISTERS("idx_unresolved", IDX_UNRESOLVED_XML);

    log_capture_start();
    lv_obj_t * root = XML_CREATE(helix_test_env_screen(), "idx_unresolved", NULL);
    helix_test_pump(30);
    log_capture_stop();

    /* The parse did not abort partway: both bodies expanded. */
    lv_obj_t * row = ASSERT_NAMED(root, "row");
    ASSERT_CHILD_COUNT(row, 2);

    /* Nothing bound, so the statically declared text survives. */
    ASSERT_LABEL_TEXT(lv_obj_get_child(row, 0), "placeholder");
    ASSERT_LABEL_TEXT(lv_obj_get_child(row, 1), "placeholder");

    TEST_ASSERT_TRUE_MESSAGE(log_contains("${nope} could not be resolved"),
                             "an unresolvable ${name} must name itself in the warning - the "
                             "resulting tree is indistinguishable from a correct one");
}

/**
 * `${i}` outside a `<repeat>` is unresolvable for the same reason: the loop
 * index only exists while a capture is replaying. Same splice-empty-and-warn
 * path, reached without any `<repeat>` in the document at all.
 */
static void test_the_loop_index_outside_a_repeat_is_unresolvable(void)
{
    ASSERT_XML_REGISTERS("idx_no_repeat",
                         "<component>"
                         "  <view extends=\"lv_obj\" name=\"idxnr_root\">"
                         "    <lv_label name=\"lone\" text=\"kept\" bind_text=\"demo_${i}_v\"/>"
                         "  </view>"
                         "</component>");

    log_capture_start();
    lv_obj_t * root = XML_CREATE(helix_test_env_screen(), "idx_no_repeat", NULL);
    helix_test_pump(30);
    log_capture_stop();

    ASSERT_LABEL_TEXT(ASSERT_NAMED(root, "lone"), "kept");
    TEST_ASSERT_TRUE_MESSAGE(log_contains("${i} could not be resolved"),
                             "${i} has no value outside a replaying <repeat>");
}

/*---------------------------------------------------------------------------
 * main
 *--------------------------------------------------------------------------*/

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_the_loop_index_composes_a_per_iteration_subject_name);
    RUN_TEST(test_a_component_param_and_the_loop_index_compose_one_name);
    RUN_TEST(test_an_unresolvable_token_splices_empty_and_warns);
    RUN_TEST(test_the_loop_index_outside_a_repeat_is_unresolvable);

    return UNITY_END();
}
