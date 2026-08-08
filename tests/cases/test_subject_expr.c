/**
 * @file test_subject_expr.c
 *
 * `<subject_expr name="x" expr="...">` in src/xml/lv_xml_component.c: a derived
 * int subject whose value is recomputed from an expression whenever any subject
 * the expression references changes.
 *
 * ---------------------------------------------------------------------------
 * SCOPE: THE DERIVED SUBJECT, NOT THE EXPRESSION LANGUAGE
 *
 * lv_xml_expr.c - the lexer, the precedence table, short-circuiting, the
 * dependency list, bind/unbind - is test_expr.c's subject and is not repeated
 * here. This file covers what the `<subject_expr>` element adds on top:
 *
 *   - the derived subject is registered in the DECLARING COMPONENT's scope
 *     (not globally), seeded with the current value, and kept in sync
 *   - each of the four ways the element can decline to register anything
 *     (no name, no expr, an uncompilable expr, a forward reference) really
 *     does register nothing, rather than registering a subject stuck at 0
 *   - the teardown path: the observers it plants on its input subjects are
 *     removed at unregister, including - especially - when the input is a
 *     GLOBAL subject that outlives the component
 *
 * That last one is the only reason `lv_xml_subject_expr_t` retains observer
 * handles at all. When the inputs are in the component's own scope, teardown
 * frees inputs and ctx together and a leaked observer is never fired again. A
 * global input outlives the scope, so an observer left attached to it fires
 * subject_expr_observer_cb on a freed ctx the next time anything writes to that
 * global. Same-scope tests cannot see that; the global-input test can.
 * ---------------------------------------------------------------------------
 *
 * Several of these paths are LV_LOG_WARN-then-return: registration of the
 * component as a whole still succeeds and the only difference between "declined
 * and reported" and "silently broken" is the warning, so the log is asserted
 * alongside the absent subject.
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
 * Local helpers
 *--------------------------------------------------------------------------*/

/** The scope of a registered component, asserting it exists. */
static lv_xml_component_scope_t * must_scope(const char * component)
{
    lv_xml_component_scope_t * s = lv_xml_component_get_scope(component);
    TEST_ASSERT_NOT_NULL_MESSAGE(s, helix_xml_assert_msgf("no scope for '%s'", component));
    return s;
}

/** A scoped subject, asserting it exists. */
static lv_subject_t * must_subject(const char * component, const char * subject)
{
    lv_subject_t * s = lv_xml_get_subject(must_scope(component), subject);
    TEST_ASSERT_NOT_NULL_MESSAGE(s, helix_xml_assert_msgf("no subject '%s' in '%s'",
                                                          subject, component));
    return s;
}

static void assert_unregisters(const char * component)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_RESULT_OK, (int)lv_xml_component_unregister(component),
                                  helix_xml_assert_msgf("unregistering '%s' failed", component));
}

/*===========================================================================
 * The happy path
 *==========================================================================*/

static const char * DERIVED_XML =
    "<component>"
    "  <subjects>"
    "    <subject name=\"p_active\" type=\"int\" value=\"0\"/>"
    "    <subject name=\"p_prog\"   type=\"int\" value=\"0\"/>"
    "    <subject_expr name=\"show_card\" expr=\"p_active and p_prog gt 0\"/>"
    "  </subjects>"
    "  <view extends=\"lv_obj\" name=\"se_root\"/>"
    "</component>";

/**
 * The derived subject tracks the expression across every corner of the truth
 * table, including the way back down: a one-shot implementation that only ever
 * latched 1 would pass until the last step.
 */
static void test_subject_expr_recomputes_when_any_input_changes(void)
{
    ASSERT_XML_REGISTERS("se_derived", DERIVED_XML);

    lv_subject_t * active = must_subject("se_derived", "p_active");
    lv_subject_t * prog = must_subject("se_derived", "p_prog");
    lv_subject_t * show = must_subject("se_derived", "show_card");

    /* Seeded from the inputs' declared values at parse time, not left at a
     * default - both inputs are 0, so the conjunction is 0. */
    TEST_ASSERT_EQUAL_INT32_MESSAGE(0, lv_subject_get_int(show),
                                    "the derived subject must be seeded by evaluating expr");

    lv_subject_set_int(active, 1);
    helix_test_pump(30);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(0, lv_subject_get_int(show),
                                    "p_prog is still 0, so the conjunction is still false");

    lv_subject_set_int(prog, 40);
    helix_test_pump(30);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(1, lv_subject_get_int(show),
                                    "both operands are now true");

    lv_subject_set_int(active, 0);
    helix_test_pump(30);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(0, lv_subject_get_int(show),
                                    "the derived value must fall back to false, not latch");

    /* Unregistering exercises the subject_expr_ll teardown: the compiled expr
     * and the shared observer ctx are freed there, the derived subject itself
     * by the ordinary subjects_ll cleanup. */
    assert_unregisters("se_derived");
}

/** The derived subject is scoped to the declaring component, not global. */
static void test_derived_subject_is_registered_in_the_declaring_component_scope(void)
{
    ASSERT_XML_REGISTERS("se_scoped", DERIVED_XML);

    TEST_ASSERT_NOT_NULL(lv_xml_get_subject(must_scope("se_scoped"), "show_card"));

    /* The globals scope must not have picked it up. Passing the globals scope
     * explicitly, because lv_xml_get_subject(NULL, ...) falls back to globals
     * anyway and would not distinguish the two. */
    lv_xml_component_scope_t * globals = lv_xml_component_get_scope("globals");
    TEST_ASSERT_NOT_NULL_MESSAGE(globals, "the globals scope must exist after lv_xml_init()");
    TEST_ASSERT_NULL_MESSAGE(lv_xml_get_subject(globals, "show_card"),
                             "<subject_expr> must not leak into the global namespace");

    assert_unregisters("se_scoped");
}

/*===========================================================================
 * Declining to register
 *
 * All four are LV_LOG_WARN + return: the component still registers, so the only
 * observable difference from a working declaration is the missing subject plus
 * the warning. Both halves are asserted.
 *==========================================================================*/

static void test_subject_expr_without_an_expr_registers_nothing(void)
{
    log_capture_start();
    ASSERT_XML_REGISTERS("se_no_expr",
                         "<component>"
                         "  <subjects>"
                         "    <subject name=\"p_a\" type=\"int\" value=\"0\"/>"
                         "    <subject_expr name=\"derived\"/>"
                         "  </subjects>"
                         "  <view extends=\"lv_obj\" name=\"ne_root\"/>"
                         "</component>");
    log_capture_stop();

    TEST_ASSERT_NULL_MESSAGE(lv_xml_get_subject(must_scope("se_no_expr"), "derived"),
                             "a <subject_expr> with no expr must not register a subject");
    /* The sibling <subject> still made it, so this is a per-element bail-out
     * rather than the whole <subjects> block being abandoned. */
    TEST_ASSERT_NOT_NULL_MESSAGE(lv_xml_get_subject(must_scope("se_no_expr"), "p_a"),
                                 "the rest of the <subjects> block must survive");
    TEST_ASSERT_TRUE_MESSAGE(log_contains("'expr' is missing from subject_expr 'derived'"),
                             "declining silently would be indistinguishable from working");

    assert_unregisters("se_no_expr");
}

static void test_subject_expr_without_a_name_registers_nothing(void)
{
    log_capture_start();
    ASSERT_XML_REGISTERS("se_no_name",
                         "<component>"
                         "  <subjects>"
                         "    <subject name=\"p_a\" type=\"int\" value=\"7\"/>"
                         "    <subject_expr expr=\"p_a\"/>"
                         "  </subjects>"
                         "  <view extends=\"lv_obj\" name=\"nn_root\"/>"
                         "</component>");
    log_capture_stop();

    /* There is no name to look the subject up by, so the warning IS the
     * assertion - together with the sibling subject having survived. */
    TEST_ASSERT_TRUE_MESSAGE(log_contains("'name' is missing from a subject_expr"),
                             "a nameless <subject_expr> must warn");
    TEST_ASSERT_NOT_NULL(lv_xml_get_subject(must_scope("se_no_name"), "p_a"));

    assert_unregisters("se_no_name");
}

static void test_subject_expr_with_an_uncompilable_expr_registers_nothing(void)
{
    log_capture_start();
    ASSERT_XML_REGISTERS("se_bad",
                         "<component>"
                         "  <subjects>"
                         "    <subject name=\"p_a\" type=\"int\" value=\"0\"/>"
                         "    <subject_expr name=\"derived\" expr=\"p_a +\"/>"
                         "  </subjects>"
                         "  <view extends=\"lv_obj\" name=\"bad_root\"/>"
                         "</component>");
    log_capture_stop();

    TEST_ASSERT_NULL_MESSAGE(lv_xml_get_subject(must_scope("se_bad"), "derived"),
                             "a subject stuck at 0 would be worse than no subject: every "
                             "binding on it would read as a valid false");
    TEST_ASSERT_TRUE(log_contains("subject_expr 'derived': failed to compile"));

    assert_unregisters("se_bad");
}

/**
 * Forward references are documented as unsupported: every name in `expr` must
 * already be registered when the element is parsed. The <subjects> block below
 * is a legal-looking declaration in the wrong ORDER, and the failure is silent
 * apart from the log - which is why it is worth pinning explicitly rather than
 * leaving it as an instance of the uncompilable-expr case.
 */
static void test_a_forward_reference_to_a_later_subject_does_not_register(void)
{
    log_capture_start();
    ASSERT_XML_REGISTERS("se_forward",
                         "<component>"
                         "  <subjects>"
                         "    <subject_expr name=\"derived\" expr=\"later gt 0\"/>"
                         "    <subject name=\"later\" type=\"int\" value=\"5\"/>"
                         "  </subjects>"
                         "  <view extends=\"lv_obj\" name=\"fwd_root\"/>"
                         "</component>");
    log_capture_stop();

    /* `later` exists by the time the component is registered ... */
    TEST_ASSERT_NOT_NULL(lv_xml_get_subject(must_scope("se_forward"), "later"));
    /* ... but it did not exist when the line above it was parsed. */
    TEST_ASSERT_NULL_MESSAGE(lv_xml_get_subject(must_scope("se_forward"), "derived"),
                             "a forward reference must not register a derived subject");
    TEST_ASSERT_TRUE(log_contains("expr: unknown subject 'later'"));

    assert_unregisters("se_forward");
}

/*===========================================================================
 * Teardown with a GLOBAL input
 *==========================================================================*/

/* Static storage: this subject must outlive the component that observes it,
 * which is the whole point of the test. Re-initialised inside the test body,
 * because the previous test's lv_deinit() reclaimed the pool its observer list
 * lived in. */
static lv_subject_t g_expr_global_input;

/**
 * A <subject_expr> whose input is a GLOBAL subject. Only the subject_expr_ll
 * teardown removes the observer sitting on that still-live global; if it did
 * not, writing to the global after unregister would fire the callback on a
 * freed ctx. Under ASAN that is a use-after-free; without it, usually a silent
 * corruption - so the write after unregister is the assertion, and the checks
 * before it exist to prove the observer really was live and really was doing
 * something (otherwise "no crash afterwards" would be vacuous).
 */
static void test_an_observer_on_a_global_input_is_removed_at_unregister(void)
{
    lv_subject_init_int(&g_expr_global_input, 0);
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK,
                          (int)lv_xml_register_subject(NULL, "g_expr_in", &g_expr_global_input));

    ASSERT_XML_REGISTERS("se_global",
                         "<component>"
                         "  <subjects>"
                         "    <subject_expr name=\"g_derived\" expr=\"g_expr_in gt 5\"/>"
                         "  </subjects>"
                         "  <view extends=\"lv_obj\" name=\"g_root\"/>"
                         "</component>");

    lv_subject_t * derived = must_subject("se_global", "g_derived");

    /* The observer is live and the expression really resolved the global. */
    lv_subject_set_int(&g_expr_global_input, 10);
    helix_test_pump(30);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(1, lv_subject_get_int(derived),
                                    "a global subject must be resolvable from <subject_expr>");

    lv_subject_set_int(&g_expr_global_input, 2);
    helix_test_pump(30);
    TEST_ASSERT_EQUAL_INT32(0, lv_subject_get_int(derived));

    assert_unregisters("se_global");

    /* The global outlived the component. Writing to it must not reach the
     * freed ctx. `derived` is gone, so there is nothing left to read back -
     * surviving this write, and the pump that follows it, is the result. */
    lv_subject_set_int(&g_expr_global_input, 99);
    helix_test_pump(30);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(99, lv_subject_get_int(&g_expr_global_input),
                                    "the global subject itself must be untouched by the teardown");

    /* Leave nothing observing static storage across the lv_deinit() in tearDown. */
    lv_subject_deinit(&g_expr_global_input);
}

/*---------------------------------------------------------------------------
 * main
 *--------------------------------------------------------------------------*/

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_subject_expr_recomputes_when_any_input_changes);
    RUN_TEST(test_derived_subject_is_registered_in_the_declaring_component_scope);

    RUN_TEST(test_subject_expr_without_an_expr_registers_nothing);
    RUN_TEST(test_subject_expr_without_a_name_registers_nothing);
    RUN_TEST(test_subject_expr_with_an_uncompilable_expr_registers_nothing);
    RUN_TEST(test_a_forward_reference_to_a_later_subject_does_not_register);

    RUN_TEST(test_an_observer_on_a_global_input_is_removed_at_unregister);

    return UNITY_END();
}
