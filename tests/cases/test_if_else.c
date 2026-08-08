/**
 * @file test_if_else.c
 *
 * Structural conditionals: `<if cond="...">` / `<else/>`.
 *
 * ---------------------------------------------------------------------------
 * WHAT `<if>` ACTUALLY IS
 *
 * Not visibility. `<bind_flag_if_eq>` builds both subtrees and hides one;
 * `<if>` builds ONLY the selected branch. So every assertion in this file is
 * about EXISTENCE - `ASSERT_NAMED` / `ASSERT_NO_NAMED` - never about
 * LV_OBJ_FLAG_HIDDEN, and never about pixels.
 *
 * Mechanically, `<if>` opens a capture on its start tag: body elements are
 * buffered as events rather than built, an `<else/>` marker records the index
 * where the false-body starts (`else_split`), and `</if>` compiles the cond and
 * replays exactly one of the two event slices. If the cond mentions at least one
 * subject the capture is RETAINED in the component scope's frag_ll and the cond
 * is bound, so a later change replays the other slice.
 *
 * ---------------------------------------------------------------------------
 * THE THREE THINGS THAT MAKE THIS FILE NON-OBVIOUS
 *
 *  1. THE REBUILD IS SYNCHRONOUS; ONLY THE TEARDOWN IS DEFERRED.
 *     xml_frag_rebuild() reparents the outgoing roots onto a hidden "condemned"
 *     object under lv_layer_top() and then expands the new slice immediately.
 *     So a `lv_obj_find_by_name(view, ...)` straight after `lv_subject_set_int()`
 *     already sees the NEW branch - the old widgets are off the view's subtree,
 *     just not freed yet. The pump is what frees them. Tests here therefore
 *     assert BOTH sides of the pump where the distinction is the point (see
 *     test_rapid_cond_churn_coalesces_to_the_final_value), and pump before the
 *     final assertion everywhere else so a regression that makes the rebuild
 *     genuinely async is still caught rather than silently passing.
 *
 *  2. `<else>` PUSHES NOTHING ONTO THE PARENT STACK.
 *     Its start handler returns before the stack push, so its end handler must
 *     return before the unconditional pop. Both halves are guarded, and a stray
 *     `<else/>` that is the LAST child cannot tell a working guard from a broken
 *     one - the damage only shows on the elements that FOLLOW it. Hence the
 *     dedicated sibling test.
 *
 *  3. THE COND SUBJECT OUTLIVES THE INSTANCE.
 *     A scope subject is shared across instances and a global outlives the
 *     component entirely, so the bind's lifetime is tied to the instance's view
 *     root via LV_EVENT_DELETE. Three teardown orders exist - delete then
 *     unregister, unregister then delete, and re-instantiate - and each frees a
 *     different subset. Those tests mutate the shared subject in the dangerous
 *     window afterwards and assert the engine still behaves; under a sanitizer
 *     they are also the UAF/double-free detectors.
 * ---------------------------------------------------------------------------
 *
 * NOT TESTED HERE, DELIBERATELY
 *  - `<if>` with no `cond` at all, and the bare `<else> outside <if>` WARNING
 *    text: both already live in cases/test_malformed.c, which additionally
 *    proves the parser is not left wedged for the next document. This file
 *    tests the resulting TREE; that one tests the parse verdict.
 *  - OOM paths (retain failure, bind failure). They need an allocator that can
 *    be made to fail on the Nth call, which this harness does not have.
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
 * Fixtures
 *
 * One shape throughout: a view root, a plain `root` under it, and an `<if>`
 * whose two branches create `t` (true) and `f` (false). Nothing is a label and
 * nothing carries text - the only question any test asks is which of the two
 * exists.
 *--------------------------------------------------------------------------*/

/** cond is a scope subject, so `value=` seeds the branch taken at create time. */
#define IF_COMPONENT(subject_value, body)                                      \
    "<component>"                                                              \
    "  <subjects>"                                                             \
    "    <subject name=\"c\" type=\"int\" value=\"" subject_value "\"/>"        \
    "  </subjects>"                                                            \
    "  <view extends=\"lv_obj\" name=\"if_view\">"                             \
    "    <lv_obj name=\"root\">" body "</lv_obj>"                              \
    "  </view>"                                                                \
    "</component>"

#define IF_BODY_ELSE_SELFCLOSE                                                 \
    "<if cond=\"c gt 0\"><lv_obj name=\"t\"/><else/><lv_obj name=\"f\"/></if>"

#define IF_BODY_ELSE_OPENCLOSE                                                 \
    "<if cond=\"c gt 0\"><lv_obj name=\"t\"/><else></else><lv_obj name=\"f\"/></if>"

static const char * COMP_STATIC_TRUE  = IF_COMPONENT("1", IF_BODY_ELSE_SELFCLOSE);
static const char * COMP_STATIC_FALSE = IF_COMPONENT("0", IF_BODY_ELSE_SELFCLOSE);

static const char * COMP_OPENCLOSE_TRUE  = IF_COMPONENT("1", IF_BODY_ELSE_OPENCLOSE);
static const char * COMP_OPENCLOSE_FALSE = IF_COMPONENT("0", IF_BODY_ELSE_OPENCLOSE);

static const char * COMP_NO_ELSE_FALSE =
    IF_COMPONENT("0", "<if cond=\"c gt 0\"><lv_obj name=\"t\"/></if>");

static const char * COMP_NO_ELSE_TRUE =
    IF_COMPONENT("1", "<if cond=\"c gt 0\"><lv_obj name=\"t\"/></if>");

static const char * COMP_DOUBLE_ELSE =
    IF_COMPONENT("0",
                 "<if cond=\"c gt 0\">"
                 "<lv_obj name=\"t\"/>"
                 "<else/><lv_obj name=\"f1\"/>"
                 "<else/><lv_obj name=\"f2\"/>"
                 "</if>");

/** A stray `<else/>` as the only child - the case a broken end-guard survives. */
static const char * COMP_STRAY_ELSE_ONLY =
    "<component>"
    "  <view extends=\"lv_obj\" name=\"if_view\">"
    "    <lv_obj name=\"root\"><else/></lv_obj>"
    "  </view>"
    "</component>";

/** A stray `<else/>` with a sibling AFTER it - the case that catches it. */
static const char * COMP_STRAY_ELSE_SIBLING =
    "<component>"
    "  <view extends=\"lv_obj\" name=\"if_view\">"
    "    <lv_obj name=\"root\">"
    "      <lv_obj name=\"a\"/><else/><lv_obj name=\"b\"/>"
    "    </lv_obj>"
    "  </view>"
    "</component>";

/** Three subjects, cond is `a and (b gt c)`. */
static const char * COMP_MULTI_SUBJECT =
    "<component>"
    "  <subjects>"
    "    <subject name=\"a\" type=\"int\" value=\"1\"/>"
    "    <subject name=\"b\" type=\"int\" value=\"1\"/>"
    "    <subject name=\"c\" type=\"int\" value=\"0\"/>"
    "  </subjects>"
    "  <view extends=\"lv_obj\" name=\"if_view\">"
    "    <lv_obj name=\"root\">"
    "      <if cond=\"a and b gt c\"><lv_obj name=\"t\"/><else/><lv_obj name=\"f\"/></if>"
    "    </lv_obj>"
    "  </view>"
    "</component>";

/**
 * cond is a GLOBAL subject rather than a scope one, so it outlives both the
 * instance and the component registration. The lifetime tests need that: a
 * scope subject dies with the scope, which would hide the very window they are
 * trying to open.
 */
#define GLOBAL_COND_NAME "g_if_cond"

static const char * COMP_GLOBAL_COND =
    "<component>"
    "  <view extends=\"lv_obj\" name=\"if_view\">"
    "    <lv_obj name=\"root\">"
    "      <if cond=\"" GLOBAL_COND_NAME " gt 0\">"
    "<lv_obj name=\"t\"/><else/><lv_obj name=\"f\"/></if>"
    "    </lv_obj>"
    "  </view>"
    "</component>";

/*
 * File-static so it survives the lv_deinit() at the end of each test. Every
 * test that uses it re-inits it first: lv_subject_init_int() resets subs_ll, so
 * a stale observer list left by a previous LVGL cycle is dropped rather than
 * walked.
 */
static lv_subject_t s_global_cond;

/** Re-init the global cond and publish it into the global XML scope. */
static void global_cond_register(int32_t initial)
{
    lv_subject_init_int(&s_global_cond, initial);
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        LV_RESULT_OK,
        (int)lv_xml_register_subject(NULL, GLOBAL_COND_NAME, &s_global_cond),
        "could not register the cond subject into the global scope");
}

/*---------------------------------------------------------------------------
 * Static cond: exactly one branch is BUILT
 *--------------------------------------------------------------------------*/

/**
 * A true cond builds the true-body and does not build the false-body at all.
 *
 * `ASSERT_NO_NAMED(view, "f")` is the load-bearing half. A `<if>` implemented as
 * "build both, hide one" would pass an existence check on `t` and fail here.
 */
static void test_cond_true_builds_only_the_true_body(void)
{
    ASSERT_XML_REGISTERS("if_static_true", COMP_STATIC_TRUE);

    lv_obj_t * view = XML_CREATE(helix_test_env_screen(), "if_static_true", NULL);
    helix_test_pump(30);

    lv_obj_t * root = ASSERT_NAMED(view, "root");
    ASSERT_NAMED(view, "t");
    ASSERT_NO_NAMED(view, "f");
    /* Exactly one child: the true-body, and nothing else left over. */
    ASSERT_CHILD_COUNT(root, 1);
}

/** The mirror image: a false cond builds only the false-body. */
static void test_cond_false_builds_only_the_false_body(void)
{
    ASSERT_XML_REGISTERS("if_static_false", COMP_STATIC_FALSE);

    lv_obj_t * view = XML_CREATE(helix_test_env_screen(), "if_static_false", NULL);
    helix_test_pump(30);

    lv_obj_t * root = ASSERT_NAMED(view, "root");
    ASSERT_NO_NAMED(view, "t");
    ASSERT_NAMED(view, "f");
    ASSERT_CHILD_COUNT(root, 1);
}

/**
 * `<else>` is optional. With a false cond and no `<else>`, the whole `<if>`
 * expands to nothing - and, crucially, the component still LOADS: the empty
 * slice must not be mistaken for a parse failure.
 */
static void test_missing_else_with_false_cond_builds_nothing_and_still_loads(void)
{
    ASSERT_XML_REGISTERS("if_no_else_false", COMP_NO_ELSE_FALSE);

    lv_obj_t * view = XML_CREATE(helix_test_env_screen(), "if_no_else_false", NULL);
    helix_test_pump(30);

    lv_obj_t * root = ASSERT_NAMED(view, "root");
    ASSERT_NO_NAMED(view, "t");
    ASSERT_CHILD_COUNT(root, 0);
}

/**
 * The same body with a TRUE cond must still build the true-body. Without this
 * pair, an `<if>` that dropped every no-`<else>` body on the floor would pass
 * the test above.
 */
static void test_missing_else_with_true_cond_still_builds_the_true_body(void)
{
    ASSERT_XML_REGISTERS("if_no_else_true", COMP_NO_ELSE_TRUE);

    lv_obj_t * view = XML_CREATE(helix_test_env_screen(), "if_no_else_true", NULL);
    helix_test_pump(30);

    lv_obj_t * root = ASSERT_NAMED(view, "root");
    ASSERT_NAMED(view, "t");
    ASSERT_CHILD_COUNT(root, 1);
}

/*---------------------------------------------------------------------------
 * `<else/>` vs `<else></else>`
 *
 * expat reports a self-closing tag as start-then-end, so both spellings reach
 * the same two handlers. Both handlers guard on the tag name independently,
 * though, so a guard added to only one of them would make the spellings differ.
 * Checked on both branches: a broken end-guard shows up as a shifted split,
 * which is invisible on whichever branch happens to still line up.
 *--------------------------------------------------------------------------*/

/** Both spellings, false cond: false-body only, either way. */
static void test_both_else_spellings_split_identically_on_the_false_branch(void)
{
    ASSERT_XML_REGISTERS("if_selfclose_false", COMP_STATIC_FALSE);
    ASSERT_XML_REGISTERS("if_openclose_false", COMP_OPENCLOSE_FALSE);

    lv_obj_t * screen = helix_test_env_screen();

    lv_obj_t * a = XML_CREATE(screen, "if_selfclose_false", NULL);
    lv_obj_t * b = XML_CREATE(screen, "if_openclose_false", NULL);
    helix_test_pump(30);

    ASSERT_NO_NAMED(a, "t");
    ASSERT_NAMED(a, "f");
    ASSERT_CHILD_COUNT(ASSERT_NAMED(a, "root"), 1);

    ASSERT_NO_NAMED(b, "t");
    ASSERT_NAMED(b, "f");
    ASSERT_CHILD_COUNT(ASSERT_NAMED(b, "root"), 1);
}

/** Both spellings, true cond: true-body only, either way. */
static void test_both_else_spellings_split_identically_on_the_true_branch(void)
{
    ASSERT_XML_REGISTERS("if_selfclose_true", COMP_STATIC_TRUE);
    ASSERT_XML_REGISTERS("if_openclose_true", COMP_OPENCLOSE_TRUE);

    lv_obj_t * screen = helix_test_env_screen();

    lv_obj_t * a = XML_CREATE(screen, "if_selfclose_true", NULL);
    lv_obj_t * b = XML_CREATE(screen, "if_openclose_true", NULL);
    helix_test_pump(30);

    ASSERT_NAMED(a, "t");
    ASSERT_NO_NAMED(a, "f");
    ASSERT_CHILD_COUNT(ASSERT_NAMED(a, "root"), 1);

    ASSERT_NAMED(b, "t");
    ASSERT_NO_NAMED(b, "f");
    ASSERT_CHILD_COUNT(ASSERT_NAMED(b, "root"), 1);
}

/*---------------------------------------------------------------------------
 * A second `<else>`
 *--------------------------------------------------------------------------*/

/**
 * `else_split` is written once and then latched by `has_else`, so a second
 * `<else/>` warns and is otherwise inert: it does not re-split, and it is not
 * buffered as a body event either. The false-body is therefore everything after
 * the FIRST marker - both `f1` AND `f2`.
 *
 * The `f2` assertion is what distinguishes "ignored the marker" from "took the
 * second split", which would have produced `f2` alone.
 */
static void test_second_else_warns_and_the_first_split_wins(void)
{
    log_capture_start();
    ASSERT_XML_REGISTERS("if_double_else", COMP_DOUBLE_ELSE);
    lv_obj_t * view = XML_CREATE(helix_test_env_screen(), "if_double_else", NULL);
    helix_test_pump(30);
    log_capture_stop();

    TEST_ASSERT_TRUE_MESSAGE(log_contains("more than one <else>"),
                             "a second <else> must warn; it is a source-level mistake, not a dialect");

    lv_obj_t * root = ASSERT_NAMED(view, "root");
    ASSERT_NO_NAMED(view, "t");
    ASSERT_NAMED(view, "f1");
    ASSERT_NAMED(view, "f2");
    /* Both false-body objects landed, and nothing extra. */
    ASSERT_CHILD_COUNT(root, 2);
}

/*---------------------------------------------------------------------------
 * A stray `<else>` outside any `<if>`
 *--------------------------------------------------------------------------*/

/**
 * With no capture open, `<else>` is a tag the engine has no widget for. It warns
 * and is dropped; the component around it still loads and still builds.
 */
static void test_a_stray_else_outside_an_if_warns_and_is_ignored(void)
{
    log_capture_start();
    ASSERT_XML_REGISTERS("if_stray_only", COMP_STRAY_ELSE_ONLY);
    lv_obj_t * view = XML_CREATE(helix_test_env_screen(), "if_stray_only", NULL);
    helix_test_pump(30);
    log_capture_stop();

    TEST_ASSERT_TRUE_MESSAGE(log_contains("<else> outside <if>"),
                             "a stray <else> must warn rather than be silently dropped");

    lv_obj_t * root = ASSERT_NAMED(view, "root");
    /* Dropped, not turned into a widget. */
    ASSERT_CHILD_COUNT(root, 0);
}

/**
 * The discriminator the test above cannot be.
 *
 * A stray `<else/>` pushes no parent-stack frame on its start tag, so its end
 * tag must pop none. If `</else>` fell through to the generic end-handler's
 * unconditional pop it would remove the ENCLOSING element's still-open frame one
 * event early, and every following sibling would be mis-parented - `b` would
 * become a child of `if_view` instead of `root`, leaving `root` holding only `a`.
 *
 * A stray `<else/>` that is the last child cannot show this, because there is no
 * following sibling to mis-parent. Hence the explicit parent assertions.
 */
static void test_a_stray_else_does_not_mis_parent_the_following_siblings(void)
{
    ASSERT_XML_REGISTERS("if_stray_sibling", COMP_STRAY_ELSE_SIBLING);

    lv_obj_t * view = XML_CREATE(helix_test_env_screen(), "if_stray_sibling", NULL);
    helix_test_pump(30);

    lv_obj_t * root = ASSERT_NAMED(view, "root");
    lv_obj_t * a = ASSERT_NAMED(view, "a");
    lv_obj_t * b = ASSERT_NAMED(view, "b");

    TEST_ASSERT_EQUAL_PTR_MESSAGE(root, lv_obj_get_parent(a),
                                  "'a' is not a direct child of root");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(root, lv_obj_get_parent(b),
                                  "'b' was mis-parented - </else> popped the parent stack early");
    ASSERT_CHILD_COUNT(root, 2);
}

/*---------------------------------------------------------------------------
 * Reactive cond
 *--------------------------------------------------------------------------*/

/**
 * A cond that mentions a subject stays live: changing the subject replays the
 * other slice. Flip both ways, twice, so a one-shot rebuild that fires once and
 * then stops observing is caught.
 */
static void test_reactive_cond_flips_the_built_branch_both_ways(void)
{
    ASSERT_XML_REGISTERS("if_reactive", COMP_STATIC_TRUE);

    lv_xml_component_scope_t * scope = lv_xml_component_get_scope("if_reactive");
    TEST_ASSERT_NOT_NULL(scope);

    lv_obj_t * view = XML_CREATE(helix_test_env_screen(), "if_reactive", NULL);
    lv_subject_t * c = lv_xml_get_subject(scope, "c");
    TEST_ASSERT_NOT_NULL_MESSAGE(c, "the scope subject driving the cond is missing");

    helix_test_pump(30);
    ASSERT_NAMED(view, "t");
    ASSERT_NO_NAMED(view, "f");

    lv_subject_set_int(c, 0);
    helix_test_pump(50);
    ASSERT_NO_NAMED(view, "t");
    ASSERT_NAMED(view, "f");
    ASSERT_CHILD_COUNT(ASSERT_NAMED(view, "root"), 1);

    lv_subject_set_int(c, 1);
    helix_test_pump(50);
    ASSERT_NAMED(view, "t");
    ASSERT_NO_NAMED(view, "f");
    ASSERT_CHILD_COUNT(ASSERT_NAMED(view, "root"), 1);
}

/**
 * `a and b gt c` binds THREE subjects, and a change to any one of them must
 * re-evaluate the whole expression.
 *
 * The last step is the one worth having: `c` is the operand furthest from the
 * top of the parse tree, and a dependency collector that only walked the first
 * operand of each node would still pass every earlier step here.
 */
static void test_a_multi_subject_cond_re_evaluates_on_every_operand(void)
{
    ASSERT_XML_REGISTERS("if_multi", COMP_MULTI_SUBJECT);

    lv_xml_component_scope_t * scope = lv_xml_component_get_scope("if_multi");
    TEST_ASSERT_NOT_NULL(scope);

    lv_obj_t * view = XML_CREATE(helix_test_env_screen(), "if_multi", NULL);
    lv_subject_t * a = lv_xml_get_subject(scope, "a");
    lv_subject_t * b = lv_xml_get_subject(scope, "b");
    lv_subject_t * c = lv_xml_get_subject(scope, "c");
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_NOT_NULL(c);

    /* a=1, b=1, c=0 -> 1 and (1 gt 0) -> true */
    helix_test_pump(30);
    ASSERT_NAMED(view, "t");
    ASSERT_NO_NAMED(view, "f");

    /* First operand alone forces false. */
    lv_subject_set_int(a, 0);
    helix_test_pump(50);
    ASSERT_NO_NAMED(view, "t");
    ASSERT_NAMED(view, "f");

    lv_subject_set_int(a, 1);
    helix_test_pump(50);
    ASSERT_NAMED(view, "t");

    /* Third operand alone forces false: b(1) gt c(5) is 0. */
    lv_subject_set_int(c, 5);
    helix_test_pump(50);
    ASSERT_NO_NAMED(view, "t");
    ASSERT_NAMED(view, "f");

    /* ...and back to true, again through `c` alone. */
    lv_subject_set_int(c, 0);
    helix_test_pump(50);
    ASSERT_NAMED(view, "t");
    ASSERT_NO_NAMED(view, "f");

    /* `b` is genuinely bound too, not merely present in the source text. */
    lv_subject_set_int(b, -1);
    helix_test_pump(50);
    ASSERT_NO_NAMED(view, "t");
    ASSERT_NAMED(view, "f");
}

/**
 * Three flips before a single pump.
 *
 * Each rebuild reads the CURRENT subject value and reparents the outgoing roots
 * off the view's subtree synchronously, so the live tree already reflects the
 * final value (0 -> false) before anything is pumped - asserted here to pin that
 * the rebuild is not deferred. Only the FREEING of the intermediate expansions
 * is deferred, and the pump is what performs it; the tree must be unchanged
 * afterwards. A rebuild that freed its predecessor synchronously, or one that
 * left an intermediate expansion attached, fails one half or the other.
 */
static void test_rapid_cond_churn_coalesces_to_the_final_value(void)
{
    ASSERT_XML_REGISTERS("if_churn", COMP_STATIC_TRUE);

    lv_xml_component_scope_t * scope = lv_xml_component_get_scope("if_churn");
    TEST_ASSERT_NOT_NULL(scope);

    lv_obj_t * view = XML_CREATE(helix_test_env_screen(), "if_churn", NULL);
    lv_subject_t * c = lv_xml_get_subject(scope, "c");
    TEST_ASSERT_NOT_NULL(c);

    helix_test_pump(30);
    ASSERT_NAMED(view, "t");

    lv_subject_set_int(c, 0);
    lv_subject_set_int(c, 1);
    lv_subject_set_int(c, 0);

    lv_obj_t * root = ASSERT_NAMED(view, "root");
    ASSERT_NO_NAMED(view, "t");
    ASSERT_NAMED(view, "f");
    /* Not three stacked expansions - exactly one. */
    ASSERT_CHILD_COUNT(root, 1);

    /* Drain the deferred teardowns of the two intermediate expansions. */
    helix_test_pump(80);
    ASSERT_NO_NAMED(view, "t");
    ASSERT_NAMED(view, "f");
    ASSERT_CHILD_COUNT(root, 1);
}

/*---------------------------------------------------------------------------
 * Lifetime: the cond subject outlives the instance
 *
 * Each of these opens a window in which the shared subject changes while some
 * part of the `<if>` machinery has already been freed, and then proves the
 * engine is still in a coherent state afterwards. Under a sanitizer they are
 * also UAF / double-free detectors; without one, the assertions below are what
 * make them fail.
 *--------------------------------------------------------------------------*/

/**
 * Delete the instance, keep the component registered, then change the cond.
 *
 * Deleting the view root fires two LV_EVENT_DELETE hooks on the SAME object:
 * xml_frag_instance_delete_cb (drops the record) and the expr bind's own
 * expr_bind_delete_cb (detaches the cond observers and frees the bind). If the
 * second did not fire, the mutation below would reach a dangling observer; if it
 * fired twice, the bind context would be double-freed.
 *
 * The assertion that makes this more than a crash-test: a FRESH instance created
 * afterwards must build the branch matching the NEW value. That is only true if
 * the registration survived intact and the subject change actually landed.
 */
static void test_a_cond_change_after_the_instance_is_deleted_is_harmless(void)
{
    global_cond_register(1);
    ASSERT_XML_REGISTERS("if_after_delete", COMP_GLOBAL_COND);

    lv_obj_t * screen = helix_test_env_screen();
    lv_obj_t * view = XML_CREATE(screen, "if_after_delete", NULL);
    helix_test_pump(30);
    ASSERT_NAMED(view, "t");

    lv_obj_delete(view);
    helix_test_pump(30);
    ASSERT_CHILD_COUNT(screen, 0);

    /* The dangerous window: the shared subject changes with no instance alive. */
    lv_subject_set_int(&s_global_cond, 0);
    helix_test_pump(30);

    /* Still registered, still correct, and now reflecting the new value. */
    TEST_ASSERT_NOT_NULL_MESSAGE(lv_xml_component_get_scope("if_after_delete"),
                                 "the component lost its registration when its instance was deleted");
    lv_obj_t * view2 = XML_CREATE(screen, "if_after_delete", NULL);
    helix_test_pump(30);
    ASSERT_NO_NAMED(view2, "t");
    ASSERT_NAMED(view2, "f");
}

/**
 * The documented teardown order: delete the instance FIRST, then unregister.
 *
 * The instance-delete path already freed the bind, so the unregister sweep must
 * find the frag_ll entry gone and not free it a second time. Changing the global
 * afterwards must reach nothing at all.
 */
static void test_unregister_after_deleting_the_instance_detaches_the_cond(void)
{
    global_cond_register(1);
    ASSERT_XML_REGISTERS("if_unreg_after", COMP_GLOBAL_COND);

    lv_obj_t * screen = helix_test_env_screen();
    lv_obj_t * view = XML_CREATE(screen, "if_unreg_after", NULL);
    helix_test_pump(30);
    ASSERT_NAMED(view, "t");

    lv_obj_delete(view);
    helix_test_pump(20);
    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_RESULT_OK,
                                  (int)lv_xml_component_unregister("if_unreg_after"),
                                  "unregistering a component whose instance is already gone failed");
    TEST_ASSERT_NULL(lv_xml_component_get_scope("if_unreg_after"));

    /* Must reach no record and no observer. */
    lv_subject_set_int(&s_global_cond, 0);
    helix_test_pump(20);

    /* Nothing was resurrected onto the screen by the change. */
    ASSERT_CHILD_COUNT(screen, 0);
}

/**
 * The harder order: unregister WHILE the instance is still alive.
 *
 * Unregistering does not free the scope while instances of it are on screen -
 * the widgets hold raw lv_style_t pointers into it - so the record, its bind and
 * the cond observers all survive with it and the orphaned instance keeps
 * flipping. Asserting the flip is what proves nothing was torn out from under a
 * live instance; the old contract swept the record here and left the instance
 * inert, which was only "safe" because the styles it kept using were freed too.
 *
 * Deleting it afterwards is the other half: the bind's delete-cb and the record's
 * are both still armed on the view root, and they must unwind exactly once.
 */
static void test_unregister_while_the_instance_is_alive_keeps_the_cond_live(void)
{
    global_cond_register(1);
    ASSERT_XML_REGISTERS("if_unreg_alive", COMP_GLOBAL_COND);

    lv_obj_t * screen = helix_test_env_screen();
    lv_obj_t * view = XML_CREATE(screen, "if_unreg_alive", NULL);
    helix_test_pump(30);
    ASSERT_NAMED(view, "t");

    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_RESULT_OK,
                                  (int)lv_xml_component_unregister("if_unreg_alive"),
                                  "unregistering a component with a live instance failed");
    TEST_ASSERT_NULL_MESSAGE(lv_xml_component_get_scope("if_unreg_alive"),
                             "a retired scope must stop being findable immediately");

    lv_subject_set_int(&s_global_cond, 0);
    helix_test_pump(30);

    /* Still bound: the deferred scope kept the record, the bind and the captured
     * body alive for the instance that is still using them. */
    ASSERT_NAMED(view, "f");
    ASSERT_NO_NAMED(view, "t");

    /* And the delete hooks unwind exactly once: this must not double-free. */
    lv_obj_delete(view);
    helix_test_pump(30);
    ASSERT_CHILD_COUNT(screen, 0);
}

/**
 * Create, delete, create again against the same registration.
 *
 * The record and its bind are per-INSTANCE, so the second instantiation must
 * start from a clean slate. If the first instance's bind were still attached, the
 * change below would drive a rebuild against freed roots; if the second instance
 * failed to bind at all, it would never flip.
 *
 * Asserting that the SECOND instance flips is what covers both directions.
 */
static void test_re_instantiation_does_not_accumulate_stale_observers(void)
{
    global_cond_register(1);
    ASSERT_XML_REGISTERS("if_reinstantiate", COMP_GLOBAL_COND);

    lv_obj_t * screen = helix_test_env_screen();

    lv_obj_t * first = XML_CREATE(screen, "if_reinstantiate", NULL);
    helix_test_pump(30);
    ASSERT_NAMED(first, "t");
    lv_obj_delete(first);
    helix_test_pump(30);
    ASSERT_CHILD_COUNT(screen, 0);

    lv_obj_t * second = XML_CREATE(screen, "if_reinstantiate", NULL);
    helix_test_pump(30);
    ASSERT_NAMED(second, "t");

    lv_subject_set_int(&s_global_cond, 0);
    helix_test_pump(30);
    ASSERT_NO_NAMED(second, "t");
    ASSERT_NAMED(second, "f");
    ASSERT_CHILD_COUNT(ASSERT_NAMED(second, "root"), 1);

    /* Only the live instance was ever touched: the screen still holds just it. */
    ASSERT_CHILD_COUNT(screen, 1);
}

/*---------------------------------------------------------------------------
 * main
 *--------------------------------------------------------------------------*/

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_cond_true_builds_only_the_true_body);
    RUN_TEST(test_cond_false_builds_only_the_false_body);
    RUN_TEST(test_missing_else_with_false_cond_builds_nothing_and_still_loads);
    RUN_TEST(test_missing_else_with_true_cond_still_builds_the_true_body);

    RUN_TEST(test_both_else_spellings_split_identically_on_the_false_branch);
    RUN_TEST(test_both_else_spellings_split_identically_on_the_true_branch);

    RUN_TEST(test_second_else_warns_and_the_first_split_wins);
    RUN_TEST(test_a_stray_else_outside_an_if_warns_and_is_ignored);
    RUN_TEST(test_a_stray_else_does_not_mis_parent_the_following_siblings);

    RUN_TEST(test_reactive_cond_flips_the_built_branch_both_ways);
    RUN_TEST(test_a_multi_subject_cond_re_evaluates_on_every_operand);
    RUN_TEST(test_rapid_cond_churn_coalesces_to_the_final_value);

    RUN_TEST(test_a_cond_change_after_the_instance_is_deleted_is_harmless);
    RUN_TEST(test_unregister_after_deleting_the_instance_detaches_the_cond);
    RUN_TEST(test_unregister_while_the_instance_is_alive_keeps_the_cond_live);
    RUN_TEST(test_re_instantiation_does_not_accumulate_stale_observers);

    return UNITY_END();
}
