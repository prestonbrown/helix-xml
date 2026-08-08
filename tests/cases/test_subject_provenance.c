/**
 * @file test_subject_provenance.c
 *
 * Provenance of the records in a component scope's `subjects_ll`, and what
 * teardown is allowed to do to each kind.
 *
 * A scope gets subjects from two completely different places:
 *
 *   owned    - the XML parser allocated the lv_subject_t for a `<subject>` /
 *              `<subject_expr>` element and handed it over via
 *              lv_xml_register_subject_owned(). The scope must deinit and free
 *              it at teardown or it leaks.
 *   borrowed - application code called the PUBLIC lv_xml_register_subject()
 *              with a pointer to storage it owns (a C++ `static inline
 *              lv_subject_t` member, a manager-owned field, ...). The scope
 *              holds the pointer only.
 *
 * ---------------------------------------------------------------------------
 * WHY THIS FILE EXISTS
 *
 * lv_xml_component_unregister() used to free every record unconditionally, so
 * tearing a scope down freed the consuming application's statics. Under
 * LV_USE_STDLIB_MALLOC = LV_STDLIB_CLIB that is libc free() on a __DATA
 * address: "malloc: pointer being freed was not allocated" -> SIGABRT. The real
 * trigger was a hot reload of a modal whose scope borrowed two static
 * lv_subject_t members.
 *
 * The three failure modes, in the order a naive fix hits them:
 *   1. free a borrowed subject         -> heap abort
 *   2. deinit a borrowed subject       -> observers ripped off widgets in OTHER
 *                                         live components, silently unbound
 *   3. stop freeing owned subjects too -> leak
 * plus the hot-reload consequence: a borrowed subject must still be resolvable
 * in the scope after an unregister/re-register cycle, or every bind_* naming it
 * resolves to nothing and the component comes back live but inert.
 *
 * lv_xml_unregister_subject() has to make exactly the same distinction when it
 * removes ONE record by name; freeing unconditionally there aborts on
 * application storage, freeing nothing leaks every parser-allocated subject
 * removed by name.
 * ---------------------------------------------------------------------------
 *
 * HOW THESE TESTS ASSERT, given that the interesting failures are memory
 * corruption rather than wrong values:
 *
 *  - the `owned` flag itself, read straight off the private
 *    lv_xml_subject_t record. This is the one direct observation of provenance
 *    that exists; everything else is a consequence of it.
 *  - EXACT observer counts, walked off `lv_subject_t.subs_ll`.
 *    lv_subject_deinit() rips every observer off, so a count that survives a
 *    teardown proves the subject was not deinit'd, and a count that drops to
 *    zero on widget delete proves the widget's own detach path still works.
 *  - heap accounting across steady-state cycles (lv_mem_monitor free_size), for
 *    the owned side: a per-cycle leak shows up as free_size falling
 *    monotonically. This is also what catches a dangling-observer corruption -
 *    a delete that walked into a freed subject would not return the heap to
 *    where the previous cycle left it.
 *
 * "It did not crash" is never an assertion here.
 *
 * ---------------------------------------------------------------------------
 * RELATIONSHIP TO THE REST OF THE SUITE
 *
 * test_component.c covers scope teardown from the component's side (heap
 * accounting over a full register/create/destroy/unregister cycle, and one
 * borrowed-subject survival check) and test_registries.c covers
 * lv_xml_unregister_subject() on the GLOBAL scope. Neither covers: the `owned`
 * flag directly, the cross-component consequence of deinit'ing a borrowed
 * subject, lv_xml_unregister_subject() on a COMPONENT scope for either
 * provenance, or the hot-reload snapshot/restore round trip. Those are what
 * this file adds.
 * ---------------------------------------------------------------------------
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <string.h>

#include "helpers/helix_log_capture.h"
#include "helpers/helix_test_env.h"
#include "helpers/helix_test_pump.h"
#include "helpers/xml_assert.h"

/* The `owned` flag is the whole subject of this file and it lives on the
 * private record, so the private header is a deliberate dependency here. */
#include "xml/lv_xml_component_private.h"

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
 * Subject storage
 *
 * File-static, never stack: a subject registered into a scope routinely
 * outlives the test body. Widgets observing it are deleted in tearDown, after
 * the function has returned, so a stack subject would be a use-after-free in
 * the harness rather than a finding about the engine. Every test re-inits the
 * ones it uses - lv_subject_init_* resets subs_ll, so no observer list from a
 * previous LVGL cycle is ever walked.
 *--------------------------------------------------------------------------*/

static lv_subject_t s_borrowed;
static lv_subject_t s_shared;
static lv_subject_t s_other;

/*---------------------------------------------------------------------------
 * Helpers
 *--------------------------------------------------------------------------*/

/** Counts how many times an observer fired, to prove it is still attached. */
typedef struct {
    uint32_t fired;
    int32_t last_value;
} observer_probe_t;

static void probe_cb(lv_observer_t * observer, lv_subject_t * subject)
{
    observer_probe_t * probe = (observer_probe_t *)lv_observer_get_user_data(observer);
    probe->fired++;
    probe->last_value = lv_subject_get_int(subject);
}

/**
 * How many observers are attached to @p subject right now.
 *
 * lv_subject_deinit() empties subs_ll, so this is the direct, exact way to
 * tell "the scope left my subject alone" from "the scope deinit'd it" - and to
 * tell a widget that detached cleanly on delete from one that never did.
 */
static uint32_t subject_observer_count(lv_subject_t * subject)
{
    uint32_t n = 0;
    void * node;
    for(node = lv_ll_get_head(&subject->subs_ll); node != NULL;
        node = lv_ll_get_next(&subject->subs_ll, node)) {
        n++;
    }
    return n;
}

/** Provenance of a name in @p scope's OWN list: 1 owned, 0 borrowed, -1 absent.
 *
 * Deliberately does not fall back to the "globals" scope the way
 * lv_xml_get_subject() does - a test asking "is this record still in this
 * scope" must not be answered by a same-named global. */
static int scope_subject_provenance(lv_xml_component_scope_t * scope, const char * name)
{
    lv_xml_subject_t * s;
    LV_LL_READ(&scope->subjects_ll, s) {
        if(lv_streq(s->name, name)) return s->owned ? 1 : 0;
    }
    return -1;
}

static size_t heap_free_size(void)
{
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    return mon.free_size;
}

#define ASSERT_OWNED(scope, name)                                                        \
    TEST_ASSERT_EQUAL_INT_MESSAGE(                                                       \
        1, scope_subject_provenance((scope), (name)),                                    \
        helix_xml_assert_msgf("subject \"%s\" is not recorded as OWNED by the scope "     \
                              "(1=owned, 0=borrowed, -1=absent)", (name)))

#define ASSERT_BORROWED(scope, name)                                                     \
    TEST_ASSERT_EQUAL_INT_MESSAGE(                                                       \
        0, scope_subject_provenance((scope), (name)),                                    \
        helix_xml_assert_msgf("subject \"%s\" is not recorded as BORROWED by the scope "  \
                              "(1=owned, 0=borrowed, -1=absent)", (name)))

#define ASSERT_ABSENT(scope, name)                                                       \
    TEST_ASSERT_EQUAL_INT_MESSAGE(                                                       \
        -1, scope_subject_provenance((scope), (name)),                                   \
        helix_xml_assert_msgf("subject \"%s\" is still in the scope's own list", (name)))

#define ASSERT_OBSERVER_COUNT(subject, n, why)                                           \
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)(n), subject_observer_count(subject), (why))

/*---------------------------------------------------------------------------
 * Fixtures
 *--------------------------------------------------------------------------*/

/* No <subject> at all: every record this scope ends up holding is borrowed. */
static const char * BORROW_ONLY_XML =
    "<component>"
    "  <view extends=\"lv_obj\" name=\"borrow_root\">"
    "    <lv_obj name=\"box\"/>"
    "  </view>"
    "</component>";

/* Both parser-allocated types: an int, and a string (whose two 256-byte buffers
 * only the ownership walk in lv_xml_subject_record_release() ever frees). */
static const char * OWNED_SUBJECTS_XML =
    "<component>"
    "  <subjects>"
    "    <subject name=\"owned_int\" type=\"int\" value=\"7\"/>"
    "    <subject name=\"owned_str\" type=\"string\" value=\"hello\"/>"
    "  </subjects>"
    "  <view extends=\"lv_obj\" name=\"owned_root\">"
    "    <lv_obj name=\"box\"/>"
    "  </view>"
    "</component>";

/* A view that BINDS to a subject by name, so an observer really lands on it.
 * bind_flag_if_eq is deliberate: it is a pre-existing bind_* tag, not one of
 * the newer expression tags, so it proves the teardown rules cover the whole
 * family. The flag is read back with lv_obj_has_flag - a value the XML set,
 * never a measured pixel. */
static const char * BIND_BORROWED_XML =
    "<component>"
    "  <view extends=\"lv_obj\" name=\"bind_borrowed_root\">"
    "    <lv_obj name=\"box\">"
    "      <bind_flag_if_eq subject=\"shared\" flag=\"hidden\" ref_value=\"1\"/>"
    "    </lv_obj>"
    "  </view>"
    "</component>";

/* Same binding, but against a subject the SCOPE owns. */
static const char * BIND_OWNED_XML =
    "<component>"
    "  <subjects>"
    "    <subject name=\"s\" type=\"int\" value=\"0\"/>"
    "  </subjects>"
    "  <view extends=\"lv_obj\" name=\"bind_owned_root\">"
    "    <lv_obj name=\"box\">"
    "      <bind_flag_if_eq subject=\"s\" flag=\"hidden\" ref_value=\"1\"/>"
    "    </lv_obj>"
    "  </view>"
    "</component>";

/*===========================================================================
 * 1. The flag itself
 *==========================================================================*/

/**
 * The two registration paths must land in the same list with different
 * provenance. Everything else in this file is a consequence of this one bit,
 * so it is worth asserting on its own: a change that made the parser use the
 * public entry point (or made the public one claim ownership) would break every
 * other test here in a confusing way, and this one in an obvious way.
 */
static void test_the_parser_records_its_own_subjects_as_owned_and_the_public_api_as_borrowed(void)
{
    ASSERT_XML_REGISTERS("prov_mixed", OWNED_SUBJECTS_XML);
    lv_xml_component_scope_t * scope = lv_xml_component_get_scope("prov_mixed");
    TEST_ASSERT_NOT_NULL(scope);

    /* Parser-allocated, from <subjects>. */
    ASSERT_OWNED(scope, "owned_int");
    ASSERT_OWNED(scope, "owned_str");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(7, lv_subject_get_int(lv_xml_get_subject(scope, "owned_int")),
                                    "the <subject> did not carry its declared value");

    /* Application storage, through the public entry point, into the SAME list. */
    lv_subject_init_int(&s_borrowed, 41);
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK,
                          (int)lv_xml_register_subject(scope, "borrowed_flag", &s_borrowed));
    ASSERT_BORROWED(scope, "borrowed_flag");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(&s_borrowed, lv_xml_get_subject(scope, "borrowed_flag"),
                                  "the scope handed back a different subject than was registered");

    /* Registering one does not disturb the other's provenance. */
    ASSERT_OWNED(scope, "owned_int");

    lv_subject_deinit(&s_borrowed);
}

/**
 * PINS CURRENT BEHAVIOUR - suspected bug: re-registering an OWNED name through
 * the public lv_xml_register_subject() overwrites both the pointer and the
 * `owned` flag in place (register_subject_impl in lv_xml.c), and never releases
 * the lv_subject_t the scope was holding. The documented intent is "whoever
 * registered last is the authority on who owns the storage now", which is right
 * for the pointer; the parser-allocated subject that was there is simply
 * dropped on the floor, so for a `<subject type="string">` that leaks the
 * lv_subject_t plus its two 256-byte buffers.
 *
 * Not reachable from XML - it needs application code to register over a name a
 * component declared - but that is exactly what a C++ owner re-binding a name
 * after a hot reload does. Asserted as the flag flip only; the leak itself is
 * deliberately NOT asserted, because a test that demands the leak would have to
 * be rewritten the day it is fixed.
 */
static void test_re_registering_an_owned_name_through_the_public_api_makes_it_borrowed(void)
{
    ASSERT_XML_REGISTERS("prov_reclaim", OWNED_SUBJECTS_XML);
    lv_xml_component_scope_t * scope = lv_xml_component_get_scope("prov_reclaim");
    TEST_ASSERT_NOT_NULL(scope);
    ASSERT_OWNED(scope, "owned_int");

    lv_subject_init_int(&s_other, 123);
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK,
                          (int)lv_xml_register_subject(scope, "owned_int", &s_other));

    ASSERT_BORROWED(scope, "owned_int");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(&s_other, lv_xml_get_subject(scope, "owned_int"),
                                  "the second registration of the name must win");

    /* The consequence that matters: teardown must now leave the caller's
     * storage alone, even though the name arrived from XML. Freeing this
     * file-static address would abort outright. */
    observer_probe_t probe = {0, 0};
    TEST_ASSERT_NOT_NULL(lv_subject_add_observer(&s_other, probe_cb, &probe));

    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_component_unregister("prov_reclaim"));

    lv_subject_set_int(&s_other, 124);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, probe.fired,
                                     "teardown deinit'd a subject that was re-registered as borrowed");
    TEST_ASSERT_EQUAL_INT32(124, probe.last_value);

    lv_subject_deinit(&s_other);
}

/*===========================================================================
 * 2. Scope teardown
 *==========================================================================*/

/**
 * Tearing down the scope that borrowed a subject must leave the subject
 * completely intact: same value, existing observers still attached, and still
 * able to take a NEW observer and drive it.
 *
 * Pre-fix this aborted the process at the unregister call - lv_free() on a
 * __DATA address.
 */
static void test_scope_teardown_does_not_free_a_borrowed_subject(void)
{
    ASSERT_XML_REGISTERS("borrow_survive", BORROW_ONLY_XML);
    lv_xml_component_scope_t * scope = lv_xml_component_get_scope("borrow_survive");
    TEST_ASSERT_NOT_NULL(scope);

    lv_subject_init_int(&s_borrowed, 41);
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK,
                          (int)lv_xml_register_subject(scope, "borrowed_flag", &s_borrowed));
    ASSERT_BORROWED(scope, "borrowed_flag");

    observer_probe_t before = {0, 0};
    TEST_ASSERT_NOT_NULL(lv_subject_add_observer(&s_borrowed, probe_cb, &before));
    ASSERT_OBSERVER_COUNT(&s_borrowed, 1, "the probe observer did not attach");

    lv_subject_set_int(&s_borrowed, 42);
    TEST_ASSERT_EQUAL_UINT32(2, before.fired);

    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_component_unregister("borrow_survive"));
    TEST_ASSERT_NULL(lv_xml_component_get_scope("borrow_survive"));

    /* Not deinit'd: the observer list is untouched. */
    ASSERT_OBSERVER_COUNT(&s_borrowed, 1,
                          "scope teardown deinit'd a BORROWED subject - every observer on it, "
                          "including ones held by other live components, was detached");
    /* Not freed, and not reset: the value is still what was last written. */
    TEST_ASSERT_EQUAL_INT32_MESSAGE(42, lv_subject_get_int(&s_borrowed),
                                    "the borrowed subject's value did not survive scope teardown");

    /* Still fully functional afterwards - the existing observer fires, and a
     * new one can be attached and driven. */
    lv_subject_set_int(&s_borrowed, 99);
    TEST_ASSERT_EQUAL_UINT32(3, before.fired);
    TEST_ASSERT_EQUAL_INT32(99, before.last_value);

    observer_probe_t after = {0, 0};
    TEST_ASSERT_NOT_NULL(lv_subject_add_observer(&s_borrowed, probe_cb, &after));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, after.fired,
                                     "lv_subject_add_observer must fire once with the current value");
    TEST_ASSERT_EQUAL_INT32(99, after.last_value);
    ASSERT_OBSERVER_COUNT(&s_borrowed, 2, "the second observer did not attach");

    lv_subject_deinit(&s_borrowed);
}

/**
 * Failure mode 2, the one that does not crash: deinit'ing a borrowed subject
 * instead of freeing it. lv_subject_deinit() walks subs_ll and removes EVERY
 * observer, including the ones live widgets in OTHER components hold, leaving
 * them silently unbound - the component still renders, it just stops reacting.
 *
 * Two components borrow the same application-owned subject. Only the lender is
 * torn down; the user's live instance must keep working.
 */
static void test_scope_teardown_leaves_another_components_observers_attached(void)
{
    ASSERT_XML_REGISTERS("borrow_user", BIND_BORROWED_XML);
    ASSERT_XML_REGISTERS("borrow_lender", BORROW_ONLY_XML);

    lv_xml_component_scope_t * user_scope = lv_xml_component_get_scope("borrow_user");
    lv_xml_component_scope_t * lender_scope = lv_xml_component_get_scope("borrow_lender");
    TEST_ASSERT_NOT_NULL(user_scope);
    TEST_ASSERT_NOT_NULL(lender_scope);

    lv_subject_init_int(&s_shared, 0);
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_register_subject(user_scope, "shared", &s_shared));
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_register_subject(lender_scope, "shared", &s_shared));
    ASSERT_BORROWED(user_scope, "shared");
    ASSERT_BORROWED(lender_scope, "shared");

    lv_obj_t * inst = XML_CREATE(helix_test_env_screen(), "borrow_user", NULL);
    helix_test_pump(30);
    lv_obj_t * box = ASSERT_NAMED(inst, "box");

    /* One observer from the binding, one from the probe, and nothing else. */
    observer_probe_t probe = {0, 0};
    TEST_ASSERT_NOT_NULL(lv_subject_add_observer(&s_shared, probe_cb, &probe));
    ASSERT_OBSERVER_COUNT(&s_shared, 2, "expected the bind_flag observer plus the probe");

    /* The binding is live before teardown. */
    lv_subject_set_int(&s_shared, 1);
    ASSERT_FLAG(box, LV_OBJ_FLAG_HIDDEN);
    lv_subject_set_int(&s_shared, 0);
    ASSERT_NO_FLAG(box, LV_OBJ_FLAG_HIDDEN);
    TEST_ASSERT_EQUAL_UINT32(3, probe.fired);

    /* Tear down only the LENDER. It borrowed `shared`, so it must neither free
     * it (heap abort) nor deinit it (which would strip the user's observer). */
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_component_unregister("borrow_lender"));

    ASSERT_OBSERVER_COUNT(&s_shared, 2,
                          "unregistering the LENDER detached observers belonging to another "
                          "component's live widgets");

    /* And they are not merely present, they still work. */
    lv_subject_set_int(&s_shared, 1);
    ASSERT_FLAG(box, LV_OBJ_FLAG_HIDDEN);
    lv_subject_set_int(&s_shared, 0);
    ASSERT_NO_FLAG(box, LV_OBJ_FLAG_HIDDEN);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(5, probe.fired,
                                     "the probe observer stopped firing after an unrelated scope "
                                     "was torn down");

    lv_obj_delete(inst);
    helix_test_pump(30);
    ASSERT_OBSERVER_COUNT(&s_shared, 1,
                          "deleting the instance did not detach its bind_flag observer");

    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_component_unregister("borrow_user"));
    lv_subject_deinit(&s_shared);
}

/**
 * The other half of the rule: parser-allocated subjects must NOT be mistaken
 * for borrowed ones, or a fix for the abort above turns straight into a leak.
 *
 * Each cycle allocates two lv_subject_t plus the string subject's two 256-byte
 * buffers, all of which only the ownership walk in
 * lv_xml_subject_record_release() ever frees. Measured as a delta between
 * steady-state cycles, so a one-off allocation elsewhere in LVGL cannot be
 * mistaken for a leak; a per-cycle leak shows up as free_size falling
 * monotonically. Cycle 0 is a warm-up and discarded (see the same note in
 * test_component.c).
 */
static void test_scope_teardown_frees_the_subjects_the_scope_owns(void)
{
    size_t after_cycle[5];
    int i;

    for(i = 0; i < 5; i++) {
        ASSERT_XML_REGISTERS("owned_cycle", OWNED_SUBJECTS_XML);
        lv_xml_component_scope_t * scope = lv_xml_component_get_scope("owned_cycle");
        TEST_ASSERT_NOT_NULL(scope);
        ASSERT_OWNED(scope, "owned_int");
        ASSERT_OWNED(scope, "owned_str");

        /* A fresh scope every cycle: the subject is re-parsed from the XML and
         * is back at its declared value, not carried over from the last one. */
        lv_subject_t * fresh = lv_xml_get_subject(scope, "owned_int");
        TEST_ASSERT_NOT_NULL(fresh);
        TEST_ASSERT_EQUAL_INT32_MESSAGE(7, lv_subject_get_int(fresh),
                                        "the re-registered scope did not re-parse its <subject>");
        TEST_ASSERT_EQUAL_STRING("hello", lv_subject_get_string(lv_xml_get_subject(scope, "owned_str")));

        /* Dirty it, so a scope that survived teardown would be visible above. */
        lv_subject_set_int(fresh, 1000 + i);

        TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_component_unregister("owned_cycle"));
        TEST_ASSERT_NULL(lv_xml_component_get_scope("owned_cycle"));

        after_cycle[i] = heap_free_size();
    }

    for(i = 2; i < 5; i++) {
        TEST_ASSERT_EQUAL_size_t_MESSAGE(
            after_cycle[1], after_cycle[i],
            helix_xml_assert_msgf(
                "register/unregister cycle %d did not return the heap to where cycle 1 left it - "
                "the scope stopped freeing the subjects it OWNS", i));
    }
}

/**
 * The same rule down the OTHER teardown path. Re-registering a name does not go
 * through lv_xml_component_unregister(): it inserts the new scope and then
 * component_scope_drop_others() unlinks and frees every older scope holding
 * that name. That is a second call site for component_scope_free(), reachable
 * without an explicit unregister, and it is the one a reloader that simply
 * re-registers on every file save takes.
 *
 * So a borrowed subject registered into the OLD scope must survive the
 * replacement - and must be gone from the new scope's list, because the new
 * scope was parsed from XML that never mentioned it. That second half is why a
 * reloader has to snapshot and restore (see the round-trip test below); here it
 * is asserted directly so a change that started carrying records across would
 * be caught rather than silently making the snapshot redundant.
 */
static void test_re_registering_over_a_scope_does_not_free_its_borrowed_subject(void)
{
    ASSERT_XML_REGISTERS("replace_borrow", BORROW_ONLY_XML);
    lv_xml_component_scope_t * old_scope = lv_xml_component_get_scope("replace_borrow");
    TEST_ASSERT_NOT_NULL(old_scope);

    lv_subject_init_int(&s_borrowed, 17);
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK,
                          (int)lv_xml_register_subject(old_scope, "cpp_flag", &s_borrowed));
    ASSERT_BORROWED(old_scope, "cpp_flag");

    observer_probe_t probe = {0, 0};
    TEST_ASSERT_NOT_NULL(lv_subject_add_observer(&s_borrowed, probe_cb, &probe));
    ASSERT_OBSERVER_COUNT(&s_borrowed, 1, "the probe observer did not attach");

    /* Replace the definition WITHOUT unregistering first. The old scope is
     * freed inside registration; `old_scope` is dangling from here on and is
     * deliberately never dereferenced again. */
    ASSERT_XML_REGISTERS("replace_borrow", OWNED_SUBJECTS_XML);

    lv_xml_component_scope_t * fresh = lv_xml_component_get_scope("replace_borrow");
    TEST_ASSERT_NOT_NULL(fresh);
    ASSERT_OWNED(fresh, "owned_int");
    ASSERT_ABSENT(fresh, "cpp_flag");

    /* The application's storage is untouched by the scope that was dropped. */
    ASSERT_OBSERVER_COUNT(&s_borrowed, 1,
                          "replacing a component definition deinit'd a subject the REPLACED scope "
                          "only borrowed");
    TEST_ASSERT_EQUAL_INT32(17, lv_subject_get_int(&s_borrowed));
    lv_subject_set_int(&s_borrowed, 18);
    TEST_ASSERT_EQUAL_UINT32(2, probe.fired);
    TEST_ASSERT_EQUAL_INT32(18, probe.last_value);

    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_component_unregister("replace_borrow"));
    lv_subject_deinit(&s_borrowed);
}

/*===========================================================================
 * 3. lv_xml_unregister_subject(): the same split, one record at a time
 *==========================================================================*/

/**
 * Removing ONE borrowed subject by name drops the record and nothing else. This
 * is the shape an indexed subject pool uses on every rebuild: the pool owns the
 * lv_subject_t array, hands the engine pointers into it, and reclaims slots by
 * name. Freeing there would abort on the pool's storage.
 *
 * test_registries.c covers this on the GLOBAL scope; here it is a COMPONENT
 * scope, and the record's absence is checked against the scope's OWN list
 * rather than through lv_xml_get_subject(), which falls back to globals and so
 * cannot distinguish "removed" from "shadowed by a global of the same name".
 */
static void test_unregister_subject_leaves_a_borrowed_subject_intact(void)
{
    ASSERT_XML_REGISTERS("unreg_borrowed", BORROW_ONLY_XML);
    lv_xml_component_scope_t * scope = lv_xml_component_get_scope("unreg_borrowed");
    TEST_ASSERT_NOT_NULL(scope);

    lv_subject_init_int(&s_borrowed, 5);
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK,
                          (int)lv_xml_register_subject(scope, "pool_slot_0", &s_borrowed));
    ASSERT_BORROWED(scope, "pool_slot_0");

    observer_probe_t probe = {0, 0};
    TEST_ASSERT_NOT_NULL(lv_subject_add_observer(&s_borrowed, probe_cb, &probe));
    TEST_ASSERT_EQUAL_UINT32(1, probe.fired);

    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_unregister_subject(scope, "pool_slot_0"));

    /* The name is gone from the registry... */
    ASSERT_ABSENT(scope, "pool_slot_0");
    log_capture_start();
    lv_subject_t * looked_up = lv_xml_get_subject(scope, "pool_slot_0");
    log_capture_stop();
    TEST_ASSERT_NULL_MESSAGE(looked_up, "the removed name still resolves");

    /* ...but the subject is untouched: not deinit'd (the observer is still on
     * it and still fires) and not freed, which is what lets the caller deinit
     * it afterwards on its own terms. */
    ASSERT_OBSERVER_COUNT(&s_borrowed, 1,
                          "lv_xml_unregister_subject deinit'd a BORROWED subject");
    TEST_ASSERT_EQUAL_INT32(5, lv_subject_get_int(&s_borrowed));

    lv_subject_set_int(&s_borrowed, 6);
    TEST_ASSERT_EQUAL_UINT32(2, probe.fired);
    TEST_ASSERT_EQUAL_INT32(6, probe.last_value);

    lv_subject_deinit(&s_borrowed);
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_component_unregister("unreg_borrowed"));
}

/**
 * The mirror case, and the one that is only reachable through this function:
 * each cycle removes both parser-allocated subjects BY NAME instead of tearing
 * the scope down, so the leak this pins cannot be caught by any scope-teardown
 * test. Scope teardown afterwards must then find nothing left to release rather
 * than re-freeing.
 */
static void test_unregister_subject_frees_an_owned_subject(void)
{
    size_t after_cycle[5];
    int i;

    for(i = 0; i < 5; i++) {
        ASSERT_XML_REGISTERS("unreg_owned", OWNED_SUBJECTS_XML);
        lv_xml_component_scope_t * scope = lv_xml_component_get_scope("unreg_owned");
        TEST_ASSERT_NOT_NULL(scope);
        ASSERT_OWNED(scope, "owned_int");

        TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_unregister_subject(scope, "owned_int"));
        TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_unregister_subject(scope, "owned_str"));
        ASSERT_ABSENT(scope, "owned_int");
        ASSERT_ABSENT(scope, "owned_str");

        /* Removing the same name twice must report failure, not double-free. */
        TEST_ASSERT_EQUAL_INT_MESSAGE(
            LV_RESULT_INVALID, (int)lv_xml_unregister_subject(scope, "owned_int"),
            "removing an already-removed name must fail - succeeding here means the record "
            "was left in the list and its subject is about to be freed twice");

        /* Teardown now has no subjects left to release; it must not re-free. */
        TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_component_unregister("unreg_owned"));

        after_cycle[i] = heap_free_size();
    }

    for(i = 2; i < 5; i++) {
        TEST_ASSERT_EQUAL_size_t_MESSAGE(
            after_cycle[1], after_cycle[i],
            helix_xml_assert_msgf(
                "cycle %d did not return the heap to where cycle 1 left it - "
                "lv_xml_unregister_subject is leaking the subjects the scope OWNS", i));
    }
}

/*===========================================================================
 * 4. Hot-reload round trip
 *==========================================================================*/

/* What a hot reloader has to do around a re-registration, reimplemented here in
 * C: the consuming application's version walks the same private list. The name
 * is COPIED, because the record's own copy is freed the moment the scope goes. */
typedef struct {
    char name[64];
    lv_subject_t * subject;
} borrowed_entry_t;

static uint32_t snapshot_borrowed(lv_xml_component_scope_t * scope, borrowed_entry_t * out,
                                  uint32_t max)
{
    uint32_t n = 0;
    lv_xml_subject_t * s;
    if(scope == NULL) return 0;
    LV_LL_READ(&scope->subjects_ll, s) {
        if(s->owned || s->name == NULL || s->subject == NULL) continue;
        if(n >= max) break;
        lv_strlcpy(out[n].name, s->name, sizeof(out[n].name));
        out[n].subject = s->subject;
        n++;
    }
    return n;
}

static uint32_t restore_borrowed(lv_xml_component_scope_t * scope, const borrowed_entry_t * in,
                                 uint32_t count)
{
    uint32_t restored = 0;
    uint32_t i;
    if(scope == NULL) return 0;
    for(i = 0; i < count; i++) {
        if(lv_xml_register_subject(scope, in[i].name, in[i].subject) == LV_RESULT_OK) restored++;
    }
    return restored;
}

/**
 * The consequence the whole ownership split exists to protect, end to end.
 *
 * A hot reload is unregister + re-register. The XML-declared subject must be
 * re-parsed from the NEW source (new object, new value); the borrowed one must
 * come back as the SAME object under the same name, or every bind_* naming it
 * resolves to nothing and the reloaded component is live but inert - which
 * renders identically to a working one, so nothing short of driving the subject
 * detects it.
 *
 * Deliberately no "the fresh scope pointer differs from the old one" assertion:
 * the old scope really is freed and the allocator hands the same block straight
 * back, so the pointers routinely match. The re-parsed value is the honest
 * proof of a fresh registration.
 */
static void test_a_hot_reload_cycle_carries_borrowed_subjects_into_the_fresh_scope(void)
{
    static const char * V1 =
        "<component>"
        "  <subjects>"
        "    <subject name=\"xml_owned\" type=\"int\" value=\"3\"/>"
        "  </subjects>"
        "  <view extends=\"lv_obj\" name=\"reload_root\">"
        "    <lv_obj name=\"box\">"
        "      <bind_flag_if_eq subject=\"cpp_flag\" flag=\"hidden\" ref_value=\"1\"/>"
        "    </lv_obj>"
        "  </view>"
        "</component>";
    static const char * V2 =
        "<component>"
        "  <subjects>"
        "    <subject name=\"xml_owned\" type=\"int\" value=\"4\"/>"
        "  </subjects>"
        "  <view extends=\"lv_obj\" name=\"reload_root\">"
        "    <lv_obj name=\"box\" style_pad_all=\"8\">"
        "      <bind_flag_if_eq subject=\"cpp_flag\" flag=\"hidden\" ref_value=\"1\"/>"
        "    </lv_obj>"
        "  </view>"
        "</component>";

    borrowed_entry_t snapshot[8];
    uint32_t taken;

    ASSERT_XML_REGISTERS("reload_rt", V1);

    lv_subject_init_int(&s_borrowed, 0);
    lv_xml_component_scope_t * scope = lv_xml_component_get_scope("reload_rt");
    TEST_ASSERT_NOT_NULL(scope);
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK,
                          (int)lv_xml_register_subject(scope, "cpp_flag", &s_borrowed));

    /* v1 resolves both provenances. */
    ASSERT_BORROWED(scope, "cpp_flag");
    ASSERT_OWNED(scope, "xml_owned");
    TEST_ASSERT_EQUAL_INT32(3, lv_subject_get_int(lv_xml_get_subject(scope, "xml_owned")));

    /* --- the reload cycle --- */
    taken = snapshot_borrowed(scope, snapshot, 8);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, taken,
                                     "the snapshot must pick up the borrowed subject and ONLY it - "
                                     "an owned <subject> re-registered through the public API would "
                                     "silently become borrowed and then leak");
    TEST_ASSERT_EQUAL_STRING("cpp_flag", snapshot[0].name);
    TEST_ASSERT_EQUAL_PTR(&s_borrowed, snapshot[0].subject);

    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_component_unregister("reload_rt"));
    ASSERT_XML_REGISTERS("reload_rt", V2);

    lv_xml_component_scope_t * fresh = lv_xml_component_get_scope("reload_rt");
    TEST_ASSERT_NOT_NULL(fresh);
    TEST_ASSERT_EQUAL_UINT32(1, restore_borrowed(fresh, snapshot, taken));
    /* --- end of cycle --- */

    /* The XML-declared subject was re-parsed from v2: new value. */
    lv_subject_t * xml_owned = lv_xml_get_subject(fresh, "xml_owned");
    TEST_ASSERT_NOT_NULL(xml_owned);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(4, lv_subject_get_int(xml_owned),
                                    "the reloaded scope kept the OLD definition's subject value");
    ASSERT_OWNED(fresh, "xml_owned");

    /* The borrowed one is the SAME object, still resolvable, still borrowed. */
    TEST_ASSERT_EQUAL_PTR_MESSAGE(&s_borrowed, lv_xml_get_subject(fresh, "cpp_flag"),
                                  "the borrowed subject did not survive the reload cycle");
    ASSERT_BORROWED(fresh, "cpp_flag");

    /* And it actually binds: an instance built from the reloaded definition
     * reacts to the application-owned subject. */
    lv_obj_t * inst = XML_CREATE(helix_test_env_screen(), "reload_rt", NULL);
    helix_test_pump(30);
    lv_obj_t * box = ASSERT_NAMED(inst, "box");
    ASSERT_OBSERVER_COUNT(&s_borrowed, 1, "the reloaded definition's bind_flag never attached");

    lv_subject_set_int(&s_borrowed, 1);
    ASSERT_FLAG(box, LV_OBJ_FLAG_HIDDEN);
    lv_subject_set_int(&s_borrowed, 0);
    ASSERT_NO_FLAG(box, LV_OBJ_FLAG_HIDDEN);

    /* v2's own edit came through too, so this really is the new definition. */
    ASSERT_STYLE_INT(box, LV_STYLE_PAD_TOP, LV_PART_MAIN, 8);

    lv_obj_delete(inst);
    helix_test_pump(30);
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_component_unregister("reload_rt"));
    lv_subject_deinit(&s_borrowed);
}

/*===========================================================================
 * 5. Observer teardown when a scope is destroyed
 *==========================================================================*/

/**
 * The dangling-observer path, with the observer counted rather than inferred.
 *
 * Widgets created from a component keep observers on the subjects the scope
 * resolved for them. If unregister frees a subject WITHOUT first detaching
 * those observers, a widget deleted afterwards - the order a hot reload
 * produces, since the panel rebuild deletes old widgets AFTER the component was
 * unregistered - calls lv_observer_remove() on freed memory.
 *
 * With a BORROWED subject the whole sequence is directly observable, because
 * the subject outlives everything: the observer must still be attached after
 * the component is gone (unregister must not deinit it), and must be gone once
 * the widget is deleted (the widget's own detach path still runs). bind_flag_if_eq
 * is a pre-existing bind_* tag, so this covers the family, not just the newer
 * expression tags.
 */
static void test_deleting_a_widget_after_its_scope_is_gone_detaches_its_observer(void)
{
    ASSERT_XML_REGISTERS("teardown_borrowed", BIND_BORROWED_XML);
    lv_xml_component_scope_t * scope = lv_xml_component_get_scope("teardown_borrowed");
    TEST_ASSERT_NOT_NULL(scope);

    lv_subject_init_int(&s_shared, 0);
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_register_subject(scope, "shared", &s_shared));

    lv_obj_t * inst = XML_CREATE(helix_test_env_screen(), "teardown_borrowed", NULL);
    helix_test_pump(30);
    lv_obj_t * box = ASSERT_NAMED(inst, "box");
    ASSERT_OBSERVER_COUNT(&s_shared, 1, "bind_flag_if_eq did not attach an observer");

    /* Live before teardown. */
    lv_subject_set_int(&s_shared, 1);
    ASSERT_FLAG(box, LV_OBJ_FLAG_HIDDEN);
    lv_subject_set_int(&s_shared, 0);
    ASSERT_NO_FLAG(box, LV_OBJ_FLAG_HIDDEN);

    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_component_unregister("teardown_borrowed"));
    TEST_ASSERT_NULL(lv_xml_component_get_scope("teardown_borrowed"));

    /* The widget outlives its component and its observer is still attached -
     * the subject was borrowed, so teardown had no business touching either. */
    ASSERT_OBSERVER_COUNT(&s_shared, 1,
                          "scope teardown detached the live widget's observer from a borrowed subject");
    lv_subject_set_int(&s_shared, 1);
    ASSERT_FLAG(box, LV_OBJ_FLAG_HIDDEN);

    /* Deleting the instance now is what used to reach into freed memory. The
     * observer count going to zero proves the widget's detach path ran against
     * a subject that was still there to detach from. */
    lv_obj_delete(inst);
    helix_test_pump(30);
    ASSERT_OBSERVER_COUNT(&s_shared, 0,
                          "the deleted widget left its observer attached - the next notification "
                          "would call into a freed widget");

    /* The subject still works with nobody watching. */
    lv_subject_set_int(&s_shared, 0);
    TEST_ASSERT_EQUAL_INT32(0, lv_subject_get_int(&s_shared));

    lv_subject_deinit(&s_shared);
}

/**
 * Same order, but the subject is OWNED by the scope, so it is freed at
 * unregister and the observer count cannot be read afterwards. The heap is the
 * observable instead: an unregister that freed the subject without detaching
 * the observer leaves the widget's LV_EVENT_DELETE walking a reclaimed block,
 * so the cycle does not return the heap to where the previous one left it.
 *
 * test_component.c runs the same accounting for bind_text with the instance
 * deleted BEFORE the unregister (the documented order); this is the inverted,
 * hot-reload order, on a different bind_* family.
 */
static void test_unregistering_before_deleting_the_instance_returns_the_heap(void)
{
    size_t after_cycle[5];
    int i;

    for(i = 0; i < 5; i++) {
        ASSERT_XML_REGISTERS("teardown_owned", BIND_OWNED_XML);
        lv_xml_component_scope_t * scope = lv_xml_component_get_scope("teardown_owned");
        TEST_ASSERT_NOT_NULL(scope);
        ASSERT_OWNED(scope, "s");

        lv_obj_t * inst = XML_CREATE(helix_test_env_screen(), "teardown_owned", NULL);
        helix_test_pump(30);
        lv_obj_t * box = ASSERT_NAMED(inst, "box");

        lv_subject_t * s = lv_xml_get_subject(scope, "s");
        TEST_ASSERT_NOT_NULL(s);
        ASSERT_OBSERVER_COUNT(s, 1, "bind_flag_if_eq did not attach an observer");

        /* The binding is live before teardown - without this the rest of the
         * cycle would pass just as well on a component that never bound. */
        lv_subject_set_int(s, 1);
        ASSERT_FLAG(box, LV_OBJ_FLAG_HIDDEN);
        lv_subject_set_int(s, 0);
        ASSERT_NO_FLAG(box, LV_OBJ_FLAG_HIDDEN);

        /* Unregister FIRST (frees `s`), delete the instance SECOND. */
        TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_component_unregister("teardown_owned"));
        lv_obj_delete(inst);
        helix_test_pump(30);
        ASSERT_CHILD_COUNT(helix_test_env_screen(), 0);

        after_cycle[i] = heap_free_size();
    }

    for(i = 2; i < 5; i++) {
        TEST_ASSERT_EQUAL_size_t_MESSAGE(
            after_cycle[1], after_cycle[i],
            helix_xml_assert_msgf(
                "cycle %d did not return the heap to where cycle 1 left it - unregister is "
                "either leaking the subject it owns or freeing it with observers still attached", i));
    }
}

/*---------------------------------------------------------------------------
 * main
 *--------------------------------------------------------------------------*/

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_the_parser_records_its_own_subjects_as_owned_and_the_public_api_as_borrowed);
    RUN_TEST(test_re_registering_an_owned_name_through_the_public_api_makes_it_borrowed);

    RUN_TEST(test_scope_teardown_does_not_free_a_borrowed_subject);
    RUN_TEST(test_scope_teardown_leaves_another_components_observers_attached);
    RUN_TEST(test_scope_teardown_frees_the_subjects_the_scope_owns);
    RUN_TEST(test_re_registering_over_a_scope_does_not_free_its_borrowed_subject);

    RUN_TEST(test_unregister_subject_leaves_a_borrowed_subject_intact);
    RUN_TEST(test_unregister_subject_frees_an_owned_subject);

    RUN_TEST(test_a_hot_reload_cycle_carries_borrowed_subjects_into_the_fresh_scope);

    RUN_TEST(test_deleting_a_widget_after_its_scope_is_gone_detaches_its_observer);
    RUN_TEST(test_unregistering_before_deleting_the_instance_returns_the_heap);

    return UNITY_END();
}
