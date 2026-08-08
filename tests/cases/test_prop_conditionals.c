/**
 * @file test_prop_conditionals.c
 *
 * `hidden_if_prop_eq` / `hidden_if_prop_not_eq` and the `$prop|ref` value form
 * they take, split across resolve_params() in src/xml/lv_xml.c and the obj
 * parser in src/xml/parsers/lv_xml_obj_parser.c.
 *
 * ---------------------------------------------------------------------------
 * THIS IS A PARSE-TIME CONDITIONAL, NOT A BINDING
 *
 * Everything else in this area of the engine is reactive: <bind_flag_if> keeps
 * a compiled expression and an observer, and re-decides whenever a subject
 * moves. `hidden_if_prop_eq` does not. It compares two STRINGS once, while the
 * element is being parsed, and either adds LV_OBJ_FLAG_HIDDEN or does not. No
 * expression, no subject, no observer, nothing to change its mind afterwards.
 *
 * That is the feature, not a limitation - it is how a component branches on its
 * own `<api>` props, which are fixed for the lifetime of the instance. But it
 * reads exactly like a binding in the markup, so the distinction is pinned
 * directly: one test drives a prop-pipe hide and a <bind_flag_if> hide side by
 * side through the same event, and asserts they behave DIFFERENTLY.
 *
 * The `|` in the value is why this needs its own coverage. `$hp|true` is a
 * param reference and a literal comparand sharing one attribute, so it has to
 * survive two passes: resolve_params() resolves only the name before the pipe
 * and carries `|true` through verbatim, then the obj parser splits on the pipe.
 * The regression that motivated the split (bundle ET5ACW4S) had resolve_params
 * treating "hp|true" as one param name: the lookup failed, the attribute was
 * blanked, and the hide silently never happened - no error, no crash, just a
 * description row that would not hide. Every failure in this file's territory
 * looks like that, which is why the assertions are on the flag and never on
 * whether the widget was built.
 * ---------------------------------------------------------------------------
 *
 * SPDX-License-Identifier: MIT
 */

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
 * Fixtures
 *
 * The child exposes one string prop with a default, and hides `target` by
 * comparing it against the literal `true`. The parent instantiates it three
 * ways - explicitly equal, explicitly different, and not at all - so one build
 * exercises the supplied-value path and the default path together.
 *--------------------------------------------------------------------------*/

static const char * EQ_CHILD_XML =
    "<component>"
    "  <api>"
    "    <prop name=\"hp\" type=\"string\" default=\"false\"/>"
    "  </api>"
    "  <view extends=\"lv_obj\" name=\"eq_child_root\">"
    "    <lv_obj name=\"target\" hidden_if_prop_eq=\"$hp|true\"/>"
    "  </view>"
    "</component>";

static const char * EQ_PARENT_XML =
    "<component>"
    "  <view extends=\"lv_obj\" name=\"eq_parent_root\">"
    "    <pc_eq_child name=\"c_true\" hp=\"true\"/>"
    "    <pc_eq_child name=\"c_false\" hp=\"false\"/>"
    "    <pc_eq_child name=\"c_default\"/>"
    "  </view>"
    "</component>";

static const char * NEQ_CHILD_XML =
    "<component>"
    "  <api>"
    "    <prop name=\"hp\" type=\"string\" default=\"false\"/>"
    "  </api>"
    "  <view extends=\"lv_obj\" name=\"neq_child_root\">"
    "    <lv_obj name=\"target\" hidden_if_prop_not_eq=\"$hp|true\"/>"
    "  </view>"
    "</component>";

static const char * NEQ_PARENT_XML =
    "<component>"
    "  <view extends=\"lv_obj\" name=\"neq_parent_root\">"
    "    <pc_neq_child name=\"c_true\" hp=\"true\"/>"
    "    <pc_neq_child name=\"c_false\" hp=\"false\"/>"
    "  </view>"
    "</component>";

/** The `target` inside the named child instance under @p root. */
static lv_obj_t * target_of(lv_obj_t * root, const char * child_name)
{
    lv_obj_t * child = ASSERT_NAMED(root, child_name);
    return ASSERT_NAMED(child, "target");
}

/** Register the _eq pair and build the parent. */
static lv_obj_t * build_eq_tree(void)
{
    ASSERT_XML_REGISTERS("pc_eq_child", EQ_CHILD_XML);
    ASSERT_XML_REGISTERS("pc_eq_parent", EQ_PARENT_XML);

    lv_obj_t * root = XML_CREATE(helix_test_env_screen(), "pc_eq_parent", NULL);
    helix_test_pump(30);
    return root;
}

/** Register the _not_eq pair and build the parent. */
static lv_obj_t * build_neq_tree(void)
{
    ASSERT_XML_REGISTERS("pc_neq_child", NEQ_CHILD_XML);
    ASSERT_XML_REGISTERS("pc_neq_parent", NEQ_PARENT_XML);

    lv_obj_t * root = XML_CREATE(helix_test_env_screen(), "pc_neq_parent", NULL);
    helix_test_pump(30);
    return root;
}

/*===========================================================================
 * hidden_if_prop_eq
 *==========================================================================*/

/** hp == "true" -> "true|true" -> equal -> hidden. */
static void test_prop_eq_hides_when_the_resolved_prop_equals_the_reference(void)
{
    lv_obj_t * root = build_eq_tree();
    ASSERT_FLAG(target_of(root, "c_true"), LV_OBJ_FLAG_HIDDEN);
}

/**
 * hp == "false" -> "false|true" -> differ -> NOT hidden.
 *
 * This is the assertion the ET5ACW4S regression could not have failed and the
 * one above could: a blanked attribute also leaves the widget visible. The two
 * only pin the feature together.
 */
static void test_prop_eq_leaves_the_widget_visible_when_the_prop_differs(void)
{
    lv_obj_t * root = build_eq_tree();
    ASSERT_NO_FLAG(target_of(root, "c_false"), LV_OBJ_FLAG_HIDDEN);
}

/** Unset -> the `<prop>` default "false" -> "false|true" -> differ -> visible. */
static void test_prop_eq_falls_back_to_the_declared_default_when_unset(void)
{
    lv_obj_t * root = build_eq_tree();
    ASSERT_NO_FLAG(target_of(root, "c_default"), LV_OBJ_FLAG_HIDDEN);
}

/*===========================================================================
 * hidden_if_prop_not_eq
 *==========================================================================*/

/** not_eq hides on DIFFERENCE: hp == "false" against ref "true" -> hidden. */
static void test_prop_not_eq_hides_when_the_resolved_prop_differs(void)
{
    lv_obj_t * root = build_neq_tree();
    ASSERT_FLAG(target_of(root, "c_false"), LV_OBJ_FLAG_HIDDEN);
}

/** ... and leaves it alone on equality - the exact mirror of _eq. */
static void test_prop_not_eq_leaves_the_widget_visible_when_the_prop_equals(void)
{
    lv_obj_t * root = build_neq_tree();
    ASSERT_NO_FLAG(target_of(root, "c_true"), LV_OBJ_FLAG_HIDDEN);
}

/*===========================================================================
 * Parse-time, not reactive
 *==========================================================================*/

/*
 * Two siblings that both start hidden, by the two different mechanisms:
 *
 *   pinned  - hidden_if_prop_eq="$hp|true", decided once while parsing
 *   bound   - <bind_flag_if cond="s" flag="hidden">, decided by an observer
 *
 * hp is "true" and s is 1, so the tree is built with both hidden.
 */
static const char * MIXED_XML =
    "<component>"
    "  <api>"
    "    <prop name=\"hp\" type=\"string\" default=\"false\"/>"
    "  </api>"
    "  <subjects>"
    "    <subject name=\"s\" type=\"int\" value=\"1\"/>"
    "  </subjects>"
    "  <view extends=\"lv_obj\" name=\"mixed_root\">"
    "    <lv_obj name=\"pinned\" hidden_if_prop_eq=\"$hp|true\"/>"
    "    <lv_obj name=\"bound\">"
    "      <bind_flag_if cond=\"s\" flag=\"hidden\"/>"
    "    </lv_obj>"
    "  </view>"
    "</component>";

/**
 * PINS INTENTIONAL BEHAVIOUR, and the point of the whole feature: a prop-pipe
 * hide is evaluated ONCE, at parse time. Nothing re-asserts it afterwards.
 *
 * Proving a negative needs a control, so the reactive binding runs alongside it
 * through the same events. Both widgets are hidden by their own mechanism, both
 * are then un-hidden imperatively, and then the subject is cycled. The bound
 * widget is hidden again by its observer; the pinned one stays visible, because
 * there is nothing left holding its decision.
 *
 * Without the control this test would pass against an engine in which BOTH
 * mechanisms were inert.
 */
static void test_a_prop_pipe_hide_is_decided_once_and_never_reasserted(void)
{
    ASSERT_XML_REGISTERS("pc_mixed", MIXED_XML);

    const char * attrs[] = {"hp", "true", NULL, NULL};
    lv_obj_t * root = XML_CREATE(helix_test_env_screen(), "pc_mixed", attrs);
    helix_test_pump(30);

    lv_obj_t * pinned = ASSERT_NAMED(root, "pinned");
    lv_obj_t * bound = ASSERT_NAMED(root, "bound");

    /* Both mechanisms fired at build time. */
    ASSERT_FLAG(pinned, LV_OBJ_FLAG_HIDDEN);
    ASSERT_FLAG(bound, LV_OBJ_FLAG_HIDDEN);

    /* Clear both by hand, so neither can pass the final assertions by inertia. */
    lv_obj_remove_flag(pinned, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(bound, LV_OBJ_FLAG_HIDDEN);
    ASSERT_NO_FLAG(pinned, LV_OBJ_FLAG_HIDDEN);
    ASSERT_NO_FLAG(bound, LV_OBJ_FLAG_HIDDEN);

    /* Cycle the subject so the reactive binding is forced to re-decide. */
    lv_xml_component_scope_t * scope = lv_xml_component_get_scope("pc_mixed");
    TEST_ASSERT_NOT_NULL(scope);
    lv_subject_t * s = lv_xml_get_subject(scope, "s");
    TEST_ASSERT_NOT_NULL(s);

    lv_subject_set_int(s, 0);
    helix_test_pump(30);
    lv_subject_set_int(s, 1);
    helix_test_pump(30);

    /* The control: the observer put its flag back. */
    ASSERT_FLAG(bound, LV_OBJ_FLAG_HIDDEN);
    /* The behaviour under test: nothing re-evaluated the prop comparison. */
    ASSERT_NO_FLAG(pinned, LV_OBJ_FLAG_HIDDEN);
}

/*===========================================================================
 * Malformed values
 *==========================================================================*/

/**
 * PINS CURRENT BEHAVIOUR - suspected bug: a `hidden_if_prop_eq` value with no
 * `|` in it is a silent no-op. lv_strchr() returns NULL, the branch falls
 * through without adding the flag and without any diagnostic, so
 * `hidden_if_prop_eq="$hp"` - the natural thing to write when the reference
 * value is being added later, or when copying the attribute name off
 * `hidden_if_empty` - reads as "hide if hp is set" and does nothing at all.
 * Every other malformed-attribute path in this engine at least LV_LOG_WARNs.
 * Not fixed here: adding the warning is an engine change.
 */
static void test_a_prop_conditional_without_a_pipe_is_a_silent_no_op(void)
{
    ASSERT_XML_REGISTERS("pc_nopipe_child",
                         "<component>"
                         "  <api>"
                         "    <prop name=\"hp\" type=\"string\" default=\"false\"/>"
                         "  </api>"
                         "  <view extends=\"lv_obj\" name=\"np_child_root\">"
                         "    <lv_obj name=\"target\" hidden_if_prop_eq=\"$hp\"/>"
                         "  </view>"
                         "</component>");

    const char * attrs[] = {"hp", "true", NULL, NULL};
    lv_obj_t * root = XML_CREATE(helix_test_env_screen(), "pc_nopipe_child", attrs);
    helix_test_pump(30);

    ASSERT_NO_FLAG(ASSERT_NAMED(root, "target"), LV_OBJ_FLAG_HIDDEN);
}

/**
 * The other half of the `|` handling: an EMPTY reference after the pipe is a
 * real comparison against the empty string, not a malformed value. An unset
 * prop with no default resolves to "", so `$hp|` hides - which is exactly what
 * `hidden_if_empty` spells more directly.
 */
static void test_an_empty_reference_after_the_pipe_compares_against_the_empty_string(void)
{
    ASSERT_XML_REGISTERS("pc_empty_ref",
                         "<component>"
                         "  <api>"
                         "    <prop name=\"hp\" type=\"string\"/>"
                         "  </api>"
                         "  <view extends=\"lv_obj\" name=\"er_child_root\">"
                         "    <lv_obj name=\"target\" hidden_if_prop_eq=\"$hp|\"/>"
                         "  </view>"
                         "</component>");

    /* Unset and with no declared default -> resolves to "" -> equal -> hidden. */
    lv_obj_t * unset = XML_CREATE(helix_test_env_screen(), "pc_empty_ref", NULL);
    helix_test_pump(30);
    ASSERT_FLAG(ASSERT_NAMED(unset, "target"), LV_OBJ_FLAG_HIDDEN);

    /* Supplied and non-empty -> differs from "" -> visible. */
    const char * attrs[] = {"hp", "something", NULL, NULL};
    lv_obj_t * set = XML_CREATE(helix_test_env_screen(), "pc_empty_ref", attrs);
    helix_test_pump(30);
    ASSERT_NO_FLAG(ASSERT_NAMED(set, "target"), LV_OBJ_FLAG_HIDDEN);
}

/*---------------------------------------------------------------------------
 * main
 *--------------------------------------------------------------------------*/

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_prop_eq_hides_when_the_resolved_prop_equals_the_reference);
    RUN_TEST(test_prop_eq_leaves_the_widget_visible_when_the_prop_differs);
    RUN_TEST(test_prop_eq_falls_back_to_the_declared_default_when_unset);

    RUN_TEST(test_prop_not_eq_hides_when_the_resolved_prop_differs);
    RUN_TEST(test_prop_not_eq_leaves_the_widget_visible_when_the_prop_equals);

    RUN_TEST(test_a_prop_pipe_hide_is_decided_once_and_never_reasserted);

    RUN_TEST(test_a_prop_conditional_without_a_pipe_is_a_silent_no_op);
    RUN_TEST(test_an_empty_reference_after_the_pipe_compares_against_the_empty_string);

    return UNITY_END();
}
