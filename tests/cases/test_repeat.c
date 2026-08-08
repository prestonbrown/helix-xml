/**
 * @file test_repeat.c
 *
 * `<repeat count="...">` - the XML engine's loop construct.
 *
 * A `<repeat>` element creates no widget of its own. On the start tag the parser
 * switches into capture mode and buffers every SAX event of the body; on the
 * matching `</repeat>` it resolves `count` and REPLAYS that buffer `count` times
 * through the ordinary element handlers, so the body materializes as `count`
 * sibling groups under whatever parent enclosed the `<repeat>`.
 *
 * Two families of behaviour are pinned here, and they are genuinely different
 * code paths:
 *
 *  1. STATIC count - a literal, a `#const`, or a missing/unresolvable name. The
 *     body is expanded once at parse time and the capture buffer is freed. The
 *     hazard is the per-iteration replay: the captured attribute arrays are
 *     replayed N times, and `$i`, `$param` and `#const` are each resolved into a
 *     THROWAWAY shallow copy so the buffered originals keep their sigils intact
 *     for the next iteration. Get that wrong and every iteration reads the same
 *     value, or reads nothing.
 *
 *  2. SUBJECT-BOUND count - `count` names a live subject. The capture is retained
 *     in a fragment record, an observer is wired to the count subject, and every
 *     change re-materializes the whole expansion. That rebuild is half
 *     synchronous and half asynchronous, which is the single biggest trap in
 *     this file - see the note on pumping below.
 *
 * ---------------------------------------------------------------------------
 * PUMPING: WHAT IS SYNCHRONOUS AND WHAT IS NOT
 *
 * On a count change, xml_frag_rebuild() does two things:
 *   - teardown: every root of the previous expansion is REPARENTED into an
 *     off-tree "condemned" sink, then that sink is lv_obj_delete_async()'d. The
 *     reparent is synchronous (the roots leave the live parent's child list
 *     immediately); the actual destruction is not.
 *   - expand: the new roots are created synchronously, and APPENDED to the end
 *     of the parent's child list.
 *
 * So the parent's child COUNT is already correct before any pump. What is still
 * outstanding is the condemned sink, which only dies inside lv_timer_handler().
 * Every test here therefore pumps after mutating a count: without it the sinks
 * pile up on lv_layer_top() and a later assertion is reading a tree the engine
 * has not finished with. helix_test_pump() is mandatory before any assertion
 * that could be affected by that teardown.
 *
 * ORDERING CONSTRAINT (pinned by
 * test_a_subject_bound_rebuild_appends_its_children_at_the_end): because a
 * rebuild appends, a subject-bound `<repeat>` must be the LAST child of its
 * parent, or the only child of a dedicated container. Put a sibling after it and
 * the first rebuild moves that sibling to the FRONT.
 * ---------------------------------------------------------------------------
 *
 * PORTED FROM: HelixScreen's tests/unit/test_xml_repeat.cpp and
 * tests/unit/test_xml_repeat_subject_count.cpp. The Catch2 SUCCEED() markers in
 * the latter (which assert nothing at all) are replaced here by a direct read of
 * the count subject's subscriber list - a use-after-free guard that actually
 * fails when the observer teardown is removed.
 *
 * NO ASSERTION HERE DEPENDS ON MEASURED GEOMETRY. Child counts, tree positions,
 * label text and a style value the XML itself declared - nothing else.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>

#include <lvgl.h>

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
 * Helpers
 *--------------------------------------------------------------------------*/

/**
 * How many observers are currently subscribed to @p s.
 *
 * lv_subject_t is a fully-defined public struct and `subs_ll` is its subscriber
 * list, so this needs no private header. It is the only way to prove an observer
 * was REMOVED rather than merely "did not crash this time": a leaked observer on
 * a subject that outlives the widgets it rebuilds is a use-after-free waiting
 * for the next value change, and under a plain "mutate and survive" test it
 * reads as a pass.
 */
static uint32_t subject_observer_count(const lv_subject_t * s)
{
    return lv_ll_get_len(&s->subs_ll);
}

/** Fetch a component-scope subject by name, asserting both scope and subject exist. */
static lv_subject_t * scope_subject(const char * component, const char * subject)
{
    lv_xml_component_scope_t * scope = lv_xml_component_get_scope(component);
    TEST_ASSERT_NOT_NULL_MESSAGE(scope,
                                 helix_xml_assert_msgf("no registered scope for component \"%s\"",
                                                       component));
    lv_subject_t * s = lv_xml_get_subject(scope, subject);
    TEST_ASSERT_NOT_NULL_MESSAGE(s,
                                 helix_xml_assert_msgf("component \"%s\" declares no subject \"%s\"",
                                                       component, subject));
    return s;
}

/*---------------------------------------------------------------------------
 * Fixtures - static count
 *--------------------------------------------------------------------------*/

static const char * REPEAT_LITERAL_XML =
    "<component>"
    "  <view>"
    "    <lv_obj name='root'>"
    "      <repeat count='4'>"
    "        <lv_obj name='item'/>"
    "      </repeat>"
    "    </lv_obj>"
    "  </view>"
    "</component>";

static const char * REPEAT_ZERO_XML =
    "<component>"
    "  <view>"
    "    <lv_obj name='root'>"
    "      <repeat count='0'>"
    "        <lv_obj name='item'/>"
    "      </repeat>"
    "    </lv_obj>"
    "  </view>"
    "</component>";

static const char * REPEAT_ONE_XML =
    "<component>"
    "  <view>"
    "    <lv_obj name='root'>"
    "      <repeat count='1'>"
    "        <lv_obj name='item'/>"
    "      </repeat>"
    "    </lv_obj>"
    "  </view>"
    "</component>";

static const char * REPEAT_CONST_XML =
    "<component>"
    "  <consts><const name='rows' value='5'/></consts>"
    "  <view>"
    "    <lv_obj name='root'>"
    "      <repeat count='#rows'>"
    "        <lv_obj name='item'/>"
    "      </repeat>"
    "    </lv_obj>"
    "  </view>"
    "</component>";

/* Two widgets per iteration so `$i`, `$param` and `#const` are all live at once:
 * all three have to survive the per-iteration replay INDEPENDENTLY. If the replay
 * mutated the buffered attribute arrays in place, the `$i` labels would all read
 * the same value (or empty), `#const` would stop re-resolving after the first
 * iteration, and `$param` would drop. */
static const char * REPEAT_INDEX_XML =
    "<component>"
    "  <api><prop name='label' type='string'/></api>"
    "  <consts><const name='pad' value='7'/></consts>"
    "  <view>"
    "    <lv_obj name='root'>"
    "      <repeat count='3'>"
    "        <lv_label name='lbl' text='$i' style_pad_all='#pad'/>"
    "        <lv_label name='plbl' text='$label'/>"
    "      </repeat>"
    "    </lv_obj>"
    "  </view>"
    "</component>";

/* No `count` attribute at all. The point is the PARENT STACK: `<repeat>` pushes
 * no frame, so the intercepted `</repeat>` must not pop one either. If it did,
 * `sib` would be built against a popped parent and end up on the screen rather
 * than under `root`. */
static const char * REPEAT_NO_COUNT_XML =
    "<component>"
    "  <view>"
    "    <lv_obj name='root'>"
    "      <repeat>"
    "        <lv_label name='item'/>"
    "      </repeat>"
    "      <lv_button name='sib'/>"
    "    </lv_obj>"
    "  </view>"
    "</component>";

/*---------------------------------------------------------------------------
 * Fixtures - subject-bound count
 *--------------------------------------------------------------------------*/

/* Component-scope subject: a fresh `n` per registered component. */
static const char * REPEAT_SUBJECT_XML =
    "<component>"
    "  <subjects><subject name='n' type='int' value='2'/></subjects>"
    "  <view>"
    "    <lv_obj name='root'>"
    "      <repeat count='n'><lv_obj name='item'/></repeat>"
    "    </lv_obj>"
    "  </view>"
    "</component>";

/* Same shape, but a sibling FOLLOWS the repeat - which is exactly the layout the
 * ordering constraint forbids. Used to pin what goes wrong. */
static const char * REPEAT_SUBJECT_WITH_TAIL_XML =
    "<component>"
    "  <subjects><subject name='n' type='int' value='2'/></subjects>"
    "  <view>"
    "    <lv_obj name='root'>"
    "      <repeat count='n'><lv_obj name='item'/></repeat>"
    "      <lv_label name='tail' text='tail'/>"
    "    </lv_obj>"
    "  </view>"
    "</component>";

/* The count subject lives in the GLOBAL scope here, so it outlives both the
 * instance and the component registration - which is what makes a leaked
 * observer reachable, and therefore fatal, long after everything it rebuilds is
 * gone. File-static so it survives lv_deinit(); every test re-inits it first. */
static lv_subject_t s_global_count;

static const char * REPEAT_GLOBAL_XML =
    "<component>"
    "  <view>"
    "    <lv_obj name='root'>"
    "      <repeat count='rep_global_count'><lv_obj name='item'/></repeat>"
    "    </lv_obj>"
    "  </view>"
    "</component>";

/** Re-init the global count subject and (re-)register it under its XML name. */
static void init_global_count(int32_t value)
{
    lv_subject_init_int(&s_global_count, value);
    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_RESULT_OK,
                                  (int)lv_xml_register_subject(NULL, "rep_global_count",
                                                               &s_global_count),
                                  "couldn't register the global count subject");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, subject_observer_count(&s_global_count),
                                     "a freshly inited subject already has subscribers");
}

/*===========================================================================
 * Static count
 *==========================================================================*/

/** The base case: `count='4'` builds four copies of the body. */
static void test_a_literal_count_creates_that_many_children(void)
{
    ASSERT_XML_REGISTERS("rep_literal", REPEAT_LITERAL_XML);

    lv_obj_t * v = XML_CREATE(helix_test_env_screen(), "rep_literal", NULL);
    helix_test_pump(30);

    ASSERT_CHILD_COUNT(ASSERT_NAMED(v, "root"), 4);
}

/** `count='0'` builds nothing, and must not fall over doing it. */
static void test_a_count_of_zero_creates_nothing(void)
{
    ASSERT_XML_REGISTERS("rep_zero", REPEAT_ZERO_XML);

    lv_obj_t * v = XML_CREATE(helix_test_env_screen(), "rep_zero", NULL);
    helix_test_pump(30);

    lv_obj_t * root = ASSERT_NAMED(v, "root");
    ASSERT_CHILD_COUNT(root, 0);
    /* And the body's widget really was never built - a count of 0 that still
     * expanded once would leave `item` findable. */
    ASSERT_NO_NAMED(root, "item");
}

/** `count='1'` is the boundary between "nothing" and "a loop": exactly one. */
static void test_a_count_of_one_creates_exactly_one_child(void)
{
    ASSERT_XML_REGISTERS("rep_one", REPEAT_ONE_XML);

    lv_obj_t * v = XML_CREATE(helix_test_env_screen(), "rep_one", NULL);
    helix_test_pump(30);

    lv_obj_t * root = ASSERT_NAMED(v, "root");
    ASSERT_CHILD_COUNT(root, 1);
    ASSERT_NAMED(root, "item");
}

/** `count` may be a `#const` reference, resolved against the component's consts. */
static void test_a_const_count_expands_to_the_const_value(void)
{
    ASSERT_XML_REGISTERS("rep_const", REPEAT_CONST_XML);

    lv_obj_t * v = XML_CREATE(helix_test_env_screen(), "rep_const", NULL);
    helix_test_pump(30);

    ASSERT_CHILD_COUNT(ASSERT_NAMED(v, "root"), 5);
}

/**
 * The #1 hazard: destructive in-place mutation of the captured attributes across
 * iterations.
 *
 * Each iteration must resolve `$i` to ITS OWN index, `#pad` to the const, and
 * `$label` to the value passed at create time - and the buffered originals must
 * come out of every iteration unchanged so the next one sees the sigils again.
 * All three are checked per iteration rather than once, because the failure mode
 * is specifically "iteration 0 is right and the rest repeat it".
 */
static void test_index_param_and_const_resolve_independently_each_iteration(void)
{
    ASSERT_XML_REGISTERS("rep_index", REPEAT_INDEX_XML);

    const char * attrs[] = {"label", "hi", NULL, NULL};
    lv_obj_t * v = XML_CREATE(helix_test_env_screen(), "rep_index", attrs);
    helix_test_pump(30);

    lv_obj_t * root = ASSERT_NAMED(v, "root");
    /* 3 iterations x 2 labels, interleaved: [lbl0, plbl0, lbl1, plbl1, ...]. */
    ASSERT_CHILD_COUNT(root, 6);

    for(uint32_t k = 0; k < 3; k++) {
        lv_obj_t * lbl = lv_obj_get_child(root, (int32_t)(k * 2));       /* $i / #const */
        lv_obj_t * plbl = lv_obj_get_child(root, (int32_t)(k * 2 + 1));  /* $param */
        TEST_ASSERT_NOT_NULL(lbl);
        TEST_ASSERT_NOT_NULL(plbl);

        char expect[8];
        snprintf(expect, sizeof expect, "%u", (unsigned)k);

        /* $i is per-iteration, not frozen at the first value. */
        ASSERT_LABEL_TEXT(lbl, expect);
        /* pad_all writes all four sides; read one back. #const must re-resolve on
         * every iteration, not just the first. */
        ASSERT_STYLE_INT(lbl, LV_STYLE_PAD_LEFT, LV_PART_MAIN, 7);
        /* $param resolves against the create-time attrs on every iteration. */
        ASSERT_LABEL_TEXT(plbl, "hi");
    }
}

/**
 * A `<repeat>` with no `count` expands zero times - and, critically, leaves the
 * parent stack balanced so a FOLLOWING sibling is still built under `root`.
 *
 * `<repeat>` pushes no parent frame, so the intercepted `</repeat>` must not pop
 * one. If it did, `sib` would be parented to whatever was beneath - the screen -
 * and the bug would look like a layout problem rather than a parser one.
 */
static void test_a_repeat_without_count_expands_zero_times_and_keeps_the_sibling_parented(void)
{
    log_capture_start();
    ASSERT_XML_REGISTERS("rep_no_count", REPEAT_NO_COUNT_XML);

    lv_obj_t * v = XML_CREATE(helix_test_env_screen(), "rep_no_count", NULL);
    log_capture_stop();
    helix_test_pump(30);

    /* The missing attribute is reported rather than silently treated as 1. */
    TEST_ASSERT_TRUE_MESSAGE(log_contains("<repeat> is missing the required 'count' attribute"),
                             "a countless <repeat> was accepted without a warning");

    lv_obj_t * root = ASSERT_NAMED(v, "root");
    lv_obj_t * sib = ASSERT_NAMED(v, "sib");

    TEST_ASSERT_EQUAL_PTR_MESSAGE(root, lv_obj_get_parent(sib),
                                  "the sibling after </repeat> was mis-parented - the close tag "
                                  "popped a parent frame the open tag never pushed");
    /* Only `sib`: the repeat body expanded nothing. */
    ASSERT_CHILD_COUNT(root, 1);
    ASSERT_NO_NAMED(root, "item");
}

/*===========================================================================
 * Subject-bound count
 *==========================================================================*/

/**
 * The headline behaviour: `count` naming a subject expands to its current value,
 * and re-expands whenever that value changes - upward, and back down to zero.
 */
static void test_a_subject_count_expands_then_rebuilds_when_it_changes(void)
{
    ASSERT_XML_REGISTERS("rep_subj", REPEAT_SUBJECT_XML);
    lv_subject_t * n = scope_subject("rep_subj", "n");

    lv_obj_t * v = XML_CREATE(helix_test_env_screen(), "rep_subj", NULL);
    helix_test_pump(30);

    lv_obj_t * root = ASSERT_NAMED(v, "root");
    ASSERT_CHILD_COUNT(root, 2);                      /* the subject's initial value */

    lv_subject_set_int(n, 5);
    helix_test_pump(50);                              /* drain the async teardown sink */
    ASSERT_CHILD_COUNT(root, 5);

    lv_subject_set_int(n, 0);
    helix_test_pump(50);
    ASSERT_CHILD_COUNT(root, 0);
    ASSERT_NO_NAMED(root, "item");
}

/**
 * Several changes before a single drain.
 *
 * The rebuild itself is synchronous - only the teardown sink is deferred - so the
 * live tree already shows the LAST value before anything is pumped. The pump then
 * has three separate condemned sinks to free, and must do so without corrupting
 * LVGL's event list or disturbing the live expansion.
 */
static void test_rapid_count_churn_coalesces_to_the_final_value(void)
{
    ASSERT_XML_REGISTERS("rep_churn", REPEAT_SUBJECT_XML);
    lv_subject_t * n = scope_subject("rep_churn", "n");

    lv_obj_t * v = XML_CREATE(helix_test_env_screen(), "rep_churn", NULL);
    helix_test_pump(30);

    lv_obj_t * root = ASSERT_NAMED(v, "root");
    ASSERT_CHILD_COUNT(root, 2);

    lv_subject_set_int(n, 1);
    lv_subject_set_int(n, 8);
    lv_subject_set_int(n, 3);
    ASSERT_CHILD_COUNT(root, 3);      /* synchronous rebuild: already the final value */

    helix_test_pump(80);              /* free all three condemned sinks */
    ASSERT_CHILD_COUNT(root, 3);      /* ...without taking the live expansion with them */
    ASSERT_NAMED(root, "item");
}

/**
 * Repeated 0 -> N -> 0 cycles.
 *
 * Emptying and refilling is where a teardown that half-frees, or an expansion
 * that appends to a stale root array, shows up: the counts drift after the first
 * cycle rather than failing outright on it.
 */
static void test_zero_to_n_to_zero_cycles_leave_a_consistent_tree(void)
{
    ASSERT_XML_REGISTERS("rep_cycle", REPEAT_SUBJECT_XML);
    lv_subject_t * n = scope_subject("rep_cycle", "n");

    lv_obj_t * v = XML_CREATE(helix_test_env_screen(), "rep_cycle", NULL);
    helix_test_pump(30);
    lv_obj_t * root = ASSERT_NAMED(v, "root");

    for(int cycle = 0; cycle < 3; cycle++) {
        lv_subject_set_int(n, 0);
        helix_test_pump(40);
        ASSERT_CHILD_COUNT(root, 0);

        lv_subject_set_int(n, 4);
        helix_test_pump(40);
        ASSERT_CHILD_COUNT(root, 4);
    }

    lv_subject_set_int(n, 0);
    helix_test_pump(40);
    ASSERT_CHILD_COUNT(root, 0);
}

/**
 * PINS A DOCUMENTED CONSTRAINT, not a bug: a rebuild APPENDS its new children to
 * the end of the parent's child list, so anything declared after the `<repeat>`
 * ends up in FRONT of the expansion from the first rebuild onwards.
 *
 * That is why a subject-bound `<repeat>` has to be the last child of its parent,
 * or the only child of a dedicated container. The initial parse gets the order
 * right, which is what makes this so easy to miss: the layout only scrambles once
 * the count actually changes.
 */
static void test_a_subject_bound_rebuild_appends_its_children_at_the_end(void)
{
    ASSERT_XML_REGISTERS("rep_tail", REPEAT_SUBJECT_WITH_TAIL_XML);
    lv_subject_t * n = scope_subject("rep_tail", "n");

    lv_obj_t * v = XML_CREATE(helix_test_env_screen(), "rep_tail", NULL);
    helix_test_pump(30);

    lv_obj_t * root = ASSERT_NAMED(v, "root");
    lv_obj_t * tail = ASSERT_NAMED(root, "tail");

    /* Initial parse: 2 items then the tail, in source order. */
    ASSERT_CHILD_COUNT(root, 3);
    TEST_ASSERT_EQUAL_PTR_MESSAGE(tail, lv_obj_get_child(root, 2),
                                  "the initial parse did not lay the body out in source order");

    lv_subject_set_int(n, 3);
    helix_test_pump(50);

    /* After a rebuild: the three new items are appended, so the tail is now first. */
    ASSERT_CHILD_COUNT(root, 4);
    TEST_ASSERT_EQUAL_PTR_MESSAGE(tail, lv_obj_get_child(root, 0),
                                  "the rebuilt expansion did not append at the end - if this "
                                  "now holds source order, the ordering constraint documented "
                                  "at the top of this file has been lifted");
}

/*===========================================================================
 * Observer lifetime - the use-after-free guards
 *
 * All three of these hang on the same fact: the count subject is GLOBAL, so it
 * outlives the widgets its observer rebuilds. Every one of them checks the
 * subject's subscriber list directly. A test that only mutated the subject and
 * declared success would pass against a leaked observer, because a stale
 * observer's rebuild frequently does not crash on the first try - it corrupts,
 * and something unrelated dies later.
 *==========================================================================*/

/**
 * Deleting the instance must take the count observer with it.
 *
 * The observer is bound to the instance's view root, so LVGL's own
 * unsubscribe-on-delete removes it. If it were bound only to the component scope
 * it would stay live with a dangling roots[] array, and the mutation below would
 * drive a rebuild that reparents and re-lays-out freed widgets. That window -
 * instance deleted, component still registered - is unbounded in a real app.
 */
static void test_a_count_change_after_instance_delete_does_not_reach_a_dangling_observer(void)
{
    init_global_count(3);
    ASSERT_XML_REGISTERS("rep_after_delete", REPEAT_GLOBAL_XML);

    lv_obj_t * v = XML_CREATE(helix_test_env_screen(), "rep_after_delete", NULL);
    helix_test_pump(30);
    ASSERT_CHILD_COUNT(ASSERT_NAMED(v, "root"), 3);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, subject_observer_count(&s_global_count),
                                     "the subject-bound repeat did not subscribe to its count");

    lv_obj_delete(v);            /* instance gone; component still registered */
    helix_test_pump(30);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, subject_observer_count(&s_global_count),
                                     "the count observer outlived the instance whose widgets it "
                                     "rebuilds - the next value change is a use-after-free");

    /* The dangerous window: the shared subject changes while no instance is alive. */
    lv_subject_set_int(&s_global_count, 7);
    helix_test_pump(30);
    TEST_ASSERT_EQUAL_UINT32(0, subject_observer_count(&s_global_count));

    lv_xml_component_unregister("rep_after_delete");
}

/**
 * Unregistering the component must take the count observer with it, even in the
 * source's documented teardown order (delete the instance FIRST, then
 * unregister). Also pins that the post-unregister mutation is inert.
 */
static void test_unregistering_a_count_subject_removes_its_global_observer(void)
{
    init_global_count(3);
    ASSERT_XML_REGISTERS("rep_unreg", REPEAT_GLOBAL_XML);

    lv_obj_t * v = XML_CREATE(helix_test_env_screen(), "rep_unreg", NULL);
    helix_test_pump(30);
    lv_obj_t * root = ASSERT_NAMED(v, "root");
    ASSERT_CHILD_COUNT(root, 3);
    TEST_ASSERT_EQUAL_UINT32(1, subject_observer_count(&s_global_count));

    /* The observer is live: the global drives a real rebuild. */
    lv_subject_set_int(&s_global_count, 6);
    helix_test_pump(50);
    ASSERT_CHILD_COUNT(root, 6);

    lv_obj_delete(v);
    helix_test_pump(20);
    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_RESULT_OK,
                                  (int)lv_xml_component_unregister("rep_unreg"),
                                  "unregistering a component with a subject-bound repeat failed");

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, subject_observer_count(&s_global_count),
                                     "an observer is still attached to the global count subject "
                                     "after the component was unregistered - its record is freed, "
                                     "so the next change fires on freed memory");

    /* Mutating the global now must not reach the freed record. */
    lv_subject_set_int(&s_global_count, 99);
    helix_test_pump(30);
    TEST_ASSERT_EQUAL_UINT32(0, subject_observer_count(&s_global_count));
}

/**
 * The other teardown order: unregister while the instance is still ALIVE. This
 * is the scope-sweep path (lv_xml_frag_record_free), a different function from
 * the instance-delete path above, and it has to detach the observer BEFORE the
 * record it points at is freed.
 *
 * Deleting the now-orphaned instance afterwards must also stay clean: the sweep
 * has to have removed the instance's pending delete callback too, or it fires on
 * a freed record.
 */
static void test_unregistering_while_the_instance_is_alive_removes_the_count_observer(void)
{
    init_global_count(3);
    ASSERT_XML_REGISTERS("rep_unreg_live", REPEAT_GLOBAL_XML);

    lv_obj_t * v = XML_CREATE(helix_test_env_screen(), "rep_unreg_live", NULL);
    helix_test_pump(30);
    lv_obj_t * root = ASSERT_NAMED(v, "root");
    ASSERT_CHILD_COUNT(root, 3);
    TEST_ASSERT_EQUAL_UINT32(1, subject_observer_count(&s_global_count));

    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_component_unregister("rep_unreg_live"));

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, subject_observer_count(&s_global_count),
                                     "the scope sweep left the count observer attached while "
                                     "freeing the record it points at");

    /* The widgets are still on screen and untouched; the repeat is simply inert now. */
    lv_subject_set_int(&s_global_count, 9);
    helix_test_pump(30);
    ASSERT_CHILD_COUNT(root, 3);

    /* And the orphaned instance still deletes cleanly. */
    lv_obj_delete(v);
    helix_test_pump(30);
    TEST_ASSERT_EQUAL_UINT32(0, subject_observer_count(&s_global_count));
}

/**
 * The view definition is re-parsed on every lv_xml_create(), so each instance
 * appends a fresh record and a fresh observer. If the first instance's record
 * were not reclaimed on its delete, a later count change would fire teardown on
 * BOTH - and the dead one's roots[] point at freed widgets.
 *
 * The subscriber count is the direct evidence: exactly one observer after the
 * second instantiation, never two.
 */
static void test_reinstantiation_does_not_accumulate_stale_observers(void)
{
    init_global_count(2);
    ASSERT_XML_REGISTERS("rep_reinst", REPEAT_GLOBAL_XML);

    lv_obj_t * v1 = XML_CREATE(helix_test_env_screen(), "rep_reinst", NULL);
    helix_test_pump(30);
    ASSERT_CHILD_COUNT(ASSERT_NAMED(v1, "root"), 2);
    TEST_ASSERT_EQUAL_UINT32(1, subject_observer_count(&s_global_count));

    lv_obj_delete(v1);
    helix_test_pump(30);
    TEST_ASSERT_EQUAL_UINT32(0, subject_observer_count(&s_global_count));

    lv_obj_t * v2 = XML_CREATE(helix_test_env_screen(), "rep_reinst", NULL);
    helix_test_pump(30);
    lv_obj_t * root2 = ASSERT_NAMED(v2, "root");
    ASSERT_CHILD_COUNT(root2, 2);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, subject_observer_count(&s_global_count),
                                     "observers accumulated across instantiations - the dead "
                                     "instance's record still rebuilds onto freed roots");

    /* The rebuild must reach only the live second instance. */
    lv_subject_set_int(&s_global_count, 5);
    helix_test_pump(30);
    ASSERT_CHILD_COUNT(root2, 5);

    lv_obj_delete(v2);
    helix_test_pump(30);
    lv_xml_component_unregister("rep_reinst");
}

/*---------------------------------------------------------------------------
 * main
 *--------------------------------------------------------------------------*/

int main(void)
{
    UNITY_BEGIN();

    /* Static count */
    RUN_TEST(test_a_literal_count_creates_that_many_children);
    RUN_TEST(test_a_count_of_zero_creates_nothing);
    RUN_TEST(test_a_count_of_one_creates_exactly_one_child);
    RUN_TEST(test_a_const_count_expands_to_the_const_value);
    RUN_TEST(test_index_param_and_const_resolve_independently_each_iteration);
    RUN_TEST(test_a_repeat_without_count_expands_zero_times_and_keeps_the_sibling_parented);

    /* Subject-bound count */
    RUN_TEST(test_a_subject_count_expands_then_rebuilds_when_it_changes);
    RUN_TEST(test_rapid_count_churn_coalesces_to_the_final_value);
    RUN_TEST(test_zero_to_n_to_zero_cycles_leave_a_consistent_tree);
    RUN_TEST(test_a_subject_bound_rebuild_appends_its_children_at_the_end);

    /* Observer lifetime */
    RUN_TEST(test_a_count_change_after_instance_delete_does_not_reach_a_dangling_observer);
    RUN_TEST(test_unregistering_a_count_subject_removes_its_global_observer);
    RUN_TEST(test_unregistering_while_the_instance_is_alive_removes_the_count_observer);
    RUN_TEST(test_reinstantiation_does_not_accumulate_stale_observers);

    return UNITY_END();
}
