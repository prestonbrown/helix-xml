/**
 * @file test_registries.c
 *
 * The five global name registries in src/xml/lv_xml.c: subjects, consts, fonts,
 * images and event callbacks.
 *
 * Every one of them is a flat `name -> thing` linked list hung off a component
 * scope, with the built-in `"globals"` scope standing in whenever the caller
 * passes NULL. They are the entire seam between a consuming app's C++ and its
 * XML: a subject that fails to register is a binding that silently never
 * updates, an event callback that fails to register is a button that silently
 * does nothing. None of the lookups can report "absent" to the caller in a way
 * that is distinguishable from a legitimate result - `lv_xml_get_font` returns
 * the default font, `lv_xml_get_const` returns NULL which is also a legal
 * value - so for the `_silent` pairs the LOG is the only observable difference,
 * and it is asserted on both sides.
 *
 * ---------------------------------------------------------------------------
 * NOT TESTED, DELIBERATELY
 *
 *  - NULL `name` into any register_* or get_* other than lv_xml_get_image
 *    (which guards) and lv_xml_get_event_cb (which does not - see below).
 *    They all reach lv_streq, which dereferences unconditionally.
 *  - A corrupted font-list node: lv_xml_search_font_ll's HEAP_CORRUPTION path
 *    needs a pointer with bits 56..47 neither all-zero nor all-one, which
 *    cannot be produced without writing into the engine's private structs.
 *  - lv_xml_register_image() with a src whose first byte is >= 0x80
 *    (LV_IMAGE_SRC_SYMBOL): stored verbatim like the VARIABLE case, so it adds
 *    no distinct behaviour over the two cases that are tested.
 *  - lv_xml_register_image() with a src in STATIC storage (the normal way an
 *    app ships a compiled-in lv_image_dsc_t). Scope teardown lv_free()s every
 *    image src unconditionally, so a non-heap address aborts inside tlsf rather
 *    than failing an assertion. See the note on
 *    test_registering_a_variable_image_stores_the_pointer_unchanged, which pins
 *    the storage rule using heap-allocated storage instead.
 * ---------------------------------------------------------------------------
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <string.h>

/* Log capture: the whole point of the `_silent` variants is that they do not
 * warn. That is unobservable except through the log, so every silent/loud pair
 * below is asserted from both directions: the loud one MUST emit, the silent
 * one MUST NOT, for the exact same absent name. */
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
 *--------------------------------------------------------------------------*/

/* A component with no metadata at all: only needed for its SCOPE, so the
 * scope-vs-globals behaviour of each registry can be exercised. */
static const char * BARE_COMPONENT_XML =
    "<component>"
    "  <view extends=\"lv_obj\" name=\"bare_root\"/>"
    "</component>";

static lv_xml_component_scope_t * register_bare_scope(const char * name)
{
    ASSERT_XML_REGISTERS(name, BARE_COMPONENT_XML);
    lv_xml_component_scope_t * scope = lv_xml_component_get_scope(name);
    TEST_ASSERT_NOT_NULL_MESSAGE(scope, "the component registered but has no scope");
    return scope;
}

/* Subject storage. File-static rather than stack: a subject can outlive the
 * function that registered it (an observer attached to it is torn down in
 * tearDown, after the test body has returned), and it must survive lv_deinit().
 * Every test re-inits the ones it uses - lv_subject_init_* resets subs_ll, so no
 * observer list from a previous LVGL cycle is ever walked. */
static lv_subject_t s_int_subject;
static lv_subject_t s_other_int_subject;
static lv_subject_t s_string_subject;
static char s_string_buf[64];
static char s_string_prev[64];
static lv_subject_t s_pointer_subject;
static lv_subject_t s_color_subject;
static lv_subject_t s_float_subject;

static int s_pointee = 5;

/* A font is only ever stored and handed back by name - nothing in the registry
 * reads through the pointer - so a zeroed lv_font_t is a legitimate distinct
 * identity to register. It is never used for drawing. */
static lv_font_t s_fake_font;

static void cb_alpha(lv_event_t * e)
{
    LV_UNUSED(e);
}

static void cb_beta(lv_event_t * e)
{
    LV_UNUSED(e);
}

static void cb_gamma(lv_event_t * e)
{
    LV_UNUSED(e);
}

/*===========================================================================
 * Subjects
 *==========================================================================*/

/** Registering makes the name resolve to that exact subject, and no other. */
static void test_registering_a_subject_makes_it_findable_by_name(void)
{
    lv_subject_init_int(&s_int_subject, 11);

    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_RESULT_OK,
                                  (int)lv_xml_register_subject(NULL, "reg_int", &s_int_subject),
                                  "lv_xml_register_subject() into the global scope failed");

    lv_subject_t * got = lv_xml_get_subject(NULL, "reg_int");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(&s_int_subject, got,
                                  "the registry handed back a different subject than was registered");

    /* It is the live subject, not a snapshot. */
    lv_subject_set_int(&s_int_subject, 99);
    TEST_ASSERT_EQUAL_INT32(99, lv_subject_get_int(got));
}

/**
 * All five subject types the engine can carry. Only `int` and `string` are
 * reachable from `<subject>` XML; pointer, color and float exist for
 * application code registering its own storage, and the registry must be
 * type-agnostic for all of them.
 */
static void test_every_subject_type_the_engine_supports_round_trips(void)
{
    lv_subject_init_int(&s_int_subject, 7);
    lv_subject_init_string(&s_string_subject, s_string_buf, s_string_prev,
                           sizeof(s_string_buf), "hello");
    lv_subject_init_pointer(&s_pointer_subject, &s_pointee);
    lv_subject_init_color(&s_color_subject, lv_color_hex(0x123456));
    lv_subject_init_float(&s_float_subject, 1.5f);

    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_register_subject(NULL, "t_int", &s_int_subject));
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_register_subject(NULL, "t_string", &s_string_subject));
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_register_subject(NULL, "t_pointer", &s_pointer_subject));
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_register_subject(NULL, "t_color", &s_color_subject));
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_register_subject(NULL, "t_float", &s_float_subject));

    TEST_ASSERT_EQUAL_INT32(7, lv_subject_get_int(lv_xml_get_subject(NULL, "t_int")));
    TEST_ASSERT_EQUAL_STRING("hello", lv_subject_get_string(lv_xml_get_subject(NULL, "t_string")));
    TEST_ASSERT_EQUAL_PTR(&s_pointee, lv_subject_get_pointer(lv_xml_get_subject(NULL, "t_pointer")));
    TEST_ASSERT_TRUE_MESSAGE(
        lv_color_eq(lv_color_hex(0x123456), lv_subject_get_color(lv_xml_get_subject(NULL, "t_color"))),
        "the colour subject did not round-trip through the registry");
    TEST_ASSERT_EQUAL_FLOAT(1.5f, lv_subject_get_float(lv_xml_get_subject(NULL, "t_float")));

    /* Each name resolves to its OWN subject - no cross-talk between types. */
    TEST_ASSERT_EQUAL_PTR(&s_string_subject, lv_xml_get_subject(NULL, "t_string"));
    TEST_ASSERT_EQUAL_PTR(&s_float_subject, lv_xml_get_subject(NULL, "t_float"));
}

/**
 * Re-registering a name REPLACES the pointer in the existing record rather than
 * pushing a second one. Proved by unregistering exactly once: if a duplicate
 * record had been added, the shadowed first registration would reappear.
 */
static void test_registering_a_duplicate_subject_name_replaces_the_pointer(void)
{
    lv_subject_init_int(&s_int_subject, 1);
    lv_subject_init_int(&s_other_int_subject, 2);

    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_register_subject(NULL, "dup", &s_int_subject));
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_register_subject(NULL, "dup", &s_other_int_subject));

    TEST_ASSERT_EQUAL_PTR_MESSAGE(&s_other_int_subject, lv_xml_get_subject(NULL, "dup"),
                                  "the second registration of a name must win");

    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_unregister_subject(NULL, "dup"));

    log_capture_start();
    lv_subject_t * after = lv_xml_get_subject(NULL, "dup");
    log_capture_stop();
    TEST_ASSERT_NULL_MESSAGE(after,
                             "one unregister left a record behind - the duplicate was appended, not replaced");
}

static void test_getting_an_absent_subject_returns_null_and_warns(void)
{
    log_capture_start();
    lv_subject_t * got = lv_xml_get_subject(NULL, "no_such_subject");
    log_capture_stop();

    TEST_ASSERT_NULL(got);
    TEST_ASSERT_TRUE_MESSAGE(log_contains("No subject was found with name \"no_such_subject\""),
                             "an absent subject must be reported - NULL alone is not actionable");
}

static void test_unregistering_a_subject_removes_it_from_the_global_scope(void)
{
    lv_subject_init_int(&s_int_subject, 4);
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_register_subject(NULL, "goner", &s_int_subject));
    TEST_ASSERT_NOT_NULL(lv_xml_get_subject(NULL, "goner"));

    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_RESULT_OK, (int)lv_xml_unregister_subject(NULL, "goner"),
                                  "unregistering a registered subject reported failure");

    log_capture_start();
    lv_subject_t * got = lv_xml_get_subject(NULL, "goner");
    log_capture_stop();
    TEST_ASSERT_NULL_MESSAGE(got, "the subject name survived unregistering");

    /* The slot is genuinely free: the same name can be registered again. */
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_register_subject(NULL, "goner", &s_int_subject));
    TEST_ASSERT_EQUAL_PTR(&s_int_subject, lv_xml_get_subject(NULL, "goner"));
}

static void test_unregistering_an_absent_subject_reports_invalid(void)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_RESULT_INVALID,
                                  (int)lv_xml_unregister_subject(NULL, "never_registered"),
                                  "unregistering a name that was never registered must fail, not succeed");
}

static uint32_t s_observer_hits;

static void count_observer_cb(lv_observer_t * observer, lv_subject_t * subject)
{
    LV_UNUSED(observer);
    LV_UNUSED(subject);
    s_observer_hits++;
}

/**
 * A subject registered through the public entry point is BORROWED: unregistering
 * the name drops the record but must not deinit the subject. lv_subject_deinit()
 * detaches every observer, so an observer that keeps firing afterwards is the
 * proof - and lv_free() on this file-static address would abort outright.
 */
static void test_unregistering_a_borrowed_subject_does_not_deinit_it(void)
{
    lv_subject_init_int(&s_int_subject, 0);
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_register_subject(NULL, "borrowed", &s_int_subject));

    s_observer_hits = 0;
    lv_subject_add_observer(&s_int_subject, count_observer_cb, NULL);
    TEST_ASSERT_EQUAL_UINT32(1, s_observer_hits);

    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_unregister_subject(NULL, "borrowed"));

    lv_subject_set_int(&s_int_subject, 8);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, s_observer_hits,
                                     "unregistering the NAME detached observers from a borrowed subject");
    TEST_ASSERT_EQUAL_INT32(8, lv_subject_get_int(&s_int_subject));

    lv_subject_deinit(&s_int_subject);
}

/** A scoped lookup falls back to the global scope; the reverse never happens. */
static void test_a_component_scope_sees_global_subjects_but_not_the_other_way(void)
{
    lv_xml_component_scope_t * scope = register_bare_scope("bare_subj");

    lv_subject_init_int(&s_int_subject, 3);
    lv_subject_init_int(&s_other_int_subject, 4);

    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_register_subject(NULL, "global_one", &s_int_subject));
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_register_subject(scope, "scoped_one", &s_other_int_subject));

    TEST_ASSERT_EQUAL_PTR_MESSAGE(&s_int_subject, lv_xml_get_subject(scope, "global_one"),
                                  "a scoped lookup must fall back to the global scope");
    TEST_ASSERT_EQUAL_PTR(&s_other_int_subject, lv_xml_get_subject(scope, "scoped_one"));

    log_capture_start();
    lv_subject_t * leaked = lv_xml_get_subject(NULL, "scoped_one");
    log_capture_stop();
    TEST_ASSERT_NULL_MESSAGE(leaked, "a component-scoped subject was visible from the global scope");
}

/*===========================================================================
 * Consts
 *==========================================================================*/

static void test_registering_a_const_makes_it_readable(void)
{
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_register_const(NULL, "pad_md", "12"));
    TEST_ASSERT_EQUAL_STRING("12", lv_xml_get_const(NULL, "pad_md"));
    TEST_ASSERT_EQUAL_STRING("12", lv_xml_get_const_silent(NULL, "pad_md"));

    /* The value is copied, not aliased: a caller's buffer may be reused. */
    char scratch[8];
    lv_strlcpy(scratch, "77", sizeof(scratch));
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_register_const(NULL, "from_buf", scratch));
    lv_strlcpy(scratch, "XX", sizeof(scratch));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("77", lv_xml_get_const(NULL, "from_buf"),
                                     "lv_xml_register_const stored the caller's pointer instead of a copy");
}

/** Re-registering an existing const is a documented no-op: first write wins. */
static void test_registering_a_duplicate_const_keeps_the_first_value(void)
{
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_register_const(NULL, "dup_const", "first"));
    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_RESULT_OK, (int)lv_xml_register_const(NULL, "dup_const", "second"),
                                  "a duplicate const registration reports success and changes nothing");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("first", lv_xml_get_const(NULL, "dup_const"),
                                     "lv_xml_register_const must not overwrite - lv_xml_update_const does that");
}

/** update_const replaces the value where register_const would refuse to. */
static void test_update_const_replaces_an_existing_value(void)
{
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_register_const(NULL, "theme", "dark"));

    log_capture_start();
    lv_result_t res = lv_xml_update_const(NULL, "theme", "light");
    log_capture_stop();

    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)res);
    TEST_ASSERT_EQUAL_STRING("light", lv_xml_get_const(NULL, "theme"));
    TEST_ASSERT_FALSE_MESSAGE(log_contains("not found for update"),
                              "updating an EXISTING const must not report it as missing");
}

/** Updating a name that was never registered warns and registers it instead. */
static void test_update_const_registers_an_absent_name_and_warns(void)
{
    log_capture_start();
    lv_result_t res = lv_xml_update_const(NULL, "brand_new", "value");
    log_capture_stop();

    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)res);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("value", lv_xml_get_const(NULL, "brand_new"),
                                     "update_const on an absent name must register it");
    TEST_ASSERT_TRUE_MESSAGE(log_contains("Const `brand_new` not found for update, registering as new"),
                             "creating a const through update_const must be reported - it is usually a typo");
}

/** The silent/loud pair, on the same absent name, in the same test. */
static void test_get_const_warns_for_an_absent_name_but_the_silent_variant_does_not(void)
{
    log_capture_start();
    const char * loud = lv_xml_get_const(NULL, "absent_const");
    log_capture_stop();
    TEST_ASSERT_NULL(loud);
    TEST_ASSERT_TRUE_MESSAGE(log_contains("No constant was found with name \"absent_const\""),
                             "lv_xml_get_const must warn - NULL is also a legal value");

    log_capture_start();
    const char * quiet = lv_xml_get_const_silent(NULL, "absent_const");
    log_capture_stop();
    TEST_ASSERT_NULL(quiet);
    TEST_ASSERT_FALSE_MESSAGE(log_contains("absent_const"),
                              "lv_xml_get_const_silent must not warn - that is its entire reason to exist");
}

/*===========================================================================
 * Fonts
 *==========================================================================*/

/** lv_xml_init() seeds the registry, and further fonts resolve by identity. */
static void test_registering_a_font_makes_it_findable_by_name(void)
{
    TEST_ASSERT_EQUAL_PTR_MESSAGE(lv_font_get_default(),
                                  lv_xml_get_font_silent(NULL, "lv_font_default"),
                                  "lv_xml_init() must register the default font as \"lv_font_default\"");

    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_register_font(NULL, "my_font", &s_fake_font));

    TEST_ASSERT_EQUAL_PTR_MESSAGE(&s_fake_font, lv_xml_get_font(NULL, "my_font"),
                                  "the registry handed back a different font than was registered");
    TEST_ASSERT_EQUAL_PTR(&s_fake_font, lv_xml_get_font_silent(NULL, "my_font"));

    /* Registering a name twice is a no-op, so the first font stays. */
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_register_font(NULL, "my_font", lv_font_get_default()));
    TEST_ASSERT_EQUAL_PTR_MESSAGE(&s_fake_font, lv_xml_get_font_silent(NULL, "my_font"),
                                  "a duplicate font registration must not replace the first");
}

/**
 * The loud variant substitutes the default font so the UI still renders; the
 * silent one returns NULL so a caller can tell "present" from "absent". Both
 * halves matter: a caller doing tier-aware fallback on the loud variant would
 * never see a miss.
 */
static void test_get_font_falls_back_and_warns_but_the_silent_variant_returns_null(void)
{
    log_capture_start();
    const lv_font_t * loud = lv_xml_get_font(NULL, "absent_font");
    log_capture_stop();

    TEST_ASSERT_EQUAL_PTR_MESSAGE(lv_font_get_default(), loud,
                                  "lv_xml_get_font must substitute the default font for an absent name");
    TEST_ASSERT_TRUE(log_contains("No font was found with name \"absent_font\""));

    log_capture_start();
    const lv_font_t * quiet = lv_xml_get_font_silent(NULL, "absent_font");
    log_capture_stop();

    TEST_ASSERT_NULL_MESSAGE(quiet,
                             "lv_xml_get_font_silent must return NULL, not the default font");
    TEST_ASSERT_FALSE_MESSAGE(log_contains("absent_font"),
                              "lv_xml_get_font_silent must not warn");
}

/** A component scope falls back to globals for fonts too. */
static void test_a_component_scope_falls_back_to_global_fonts(void)
{
    lv_xml_component_scope_t * scope = register_bare_scope("bare_font");

    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_register_font(NULL, "global_font", &s_fake_font));
    TEST_ASSERT_EQUAL_PTR(&s_fake_font, lv_xml_get_font_silent(scope, "global_font"));

    /* A scope-local font shadows nothing globally. */
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_register_font(scope, "scoped_font", &s_fake_font));
    TEST_ASSERT_EQUAL_PTR(&s_fake_font, lv_xml_get_font_silent(scope, "scoped_font"));
    TEST_ASSERT_NULL_MESSAGE(lv_xml_get_font_silent(NULL, "scoped_font"),
                             "a component-scoped font was visible from the global scope");
}

/*===========================================================================
 * Images
 *==========================================================================*/

/** A file source is COPIED and prefixed with the default asset path. */
static void test_registering_a_file_image_stores_a_prefixed_copy(void)
{
    char path[32];
    lv_strlcpy(path, "images/logo.png", sizeof(path));

    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_register_image(NULL, "logo", path));

    const void * got = lv_xml_get_image(NULL, "logo");
    TEST_ASSERT_NOT_NULL(got);
    TEST_ASSERT_EQUAL_STRING("images/logo.png", (const char *)got);
    TEST_ASSERT_TRUE_MESSAGE((const void *)path != got,
                             "a file image source must be copied, not aliased to the caller's buffer");

    /* The prefix is applied at REGISTRATION time, so only later registrations
     * pick up a change. */
    lv_xml_set_default_asset_path("A:ui/");
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_register_image(NULL, "logo2", "images/logo2.png"));
    TEST_ASSERT_EQUAL_STRING("A:ui/images/logo2.png", (const char *)lv_xml_get_image(NULL, "logo2"));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("images/logo.png", (const char *)lv_xml_get_image(NULL, "logo"),
                                     "changing the asset path must not rewrite already-registered images");
}

/**
 * A variable (lv_image_dsc_t) source is stored verbatim - no copy, no prefix.
 *
 * PINS CURRENT BEHAVIOUR - suspected bug: the descriptor here is lv_malloc'd
 * and then deliberately never freed by this test, because scope teardown frees
 * it. component_scope_free() (lv_xml_component.c) does an unconditional
 * `lv_free((char *)image->src)` on every image_ll record, but only the FILE
 * branch of lv_xml_register_image() allocates that pointer - a VARIABLE source
 * belongs to the caller. Registering a compiled-in `lv_image_dsc_t` (a static,
 * or a C-array image, which is the normal way an app ships artwork) therefore
 * hands a non-heap address to lv_free() at teardown: an immediate tlsf abort,
 * verified by writing this test with a `static lv_image_dsc_t` first. Using
 * heap storage is what makes the case testable at all; the ownership rule
 * itself is still wrong.
 */
static void test_registering_a_variable_image_stores_the_pointer_unchanged(void)
{
    /* Zeroed, so its first byte is < 0x20 and lv_image_src_get_type() classifies
     * it as LV_IMAGE_SRC_VARIABLE. Never decoded - only stored and returned. */
    lv_image_dsc_t * dsc = lv_malloc(sizeof(lv_image_dsc_t));
    TEST_ASSERT_NOT_NULL(dsc);
    lv_memzero(dsc, sizeof(*dsc));

    lv_xml_set_default_asset_path("A:ui/");
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_register_image(NULL, "dsc_img", dsc));

    TEST_ASSERT_EQUAL_PTR_MESSAGE(dsc, lv_xml_get_image(NULL, "dsc_img"),
                                  "a variable image source must be stored as-is, not copied or prefixed");

    /* No lv_free(dsc) - scope teardown takes it. See the note above. */
}

/**
 * An absent name warns, but the EMPTY name is silent by design: unset `bind_src`
 * attributes and string subjects defaulting to "" hit this path dozens of times
 * per frame during a rebuild, and the warning was pure noise.
 */
static void test_an_absent_image_warns_but_an_empty_or_null_name_is_silent(void)
{
    log_capture_start();
    const void * absent = lv_xml_get_image(NULL, "no_such_image");
    log_capture_stop();
    TEST_ASSERT_NULL(absent);
    TEST_ASSERT_TRUE(log_contains("No image was found with name \"no_such_image\""));

    log_capture_start();
    const void * empty = lv_xml_get_image(NULL, "");
    const void * null_name = lv_xml_get_image(NULL, NULL);
    log_capture_stop();

    TEST_ASSERT_NULL(empty);
    TEST_ASSERT_NULL(null_name);
    TEST_ASSERT_FALSE_MESSAGE(log_contains("No image was found"),
                              "an empty or NULL image name must be silently skipped, not warned about");
}

/*===========================================================================
 * Event callbacks
 *==========================================================================*/

typedef struct {
    const char * names[8];
    lv_event_cb_t cbs[8];
    uint32_t count;
} evt_collector_t;

static void collect_evt_cb(const char * name, lv_event_cb_t cb, void * user_data)
{
    evt_collector_t * c = (evt_collector_t *)user_data;
    if(c->count < 8) {
        c->names[c->count] = name;
        c->cbs[c->count] = cb;
    }
    c->count++;
}

static lv_event_cb_t collector_lookup(const evt_collector_t * c, const char * name)
{
    uint32_t max = c->count < 8 ? c->count : 8;
    for(uint32_t i = 0; i < max; i++) {
        if(c->names[i] && strcmp(c->names[i], name) == 0) return c->cbs[i];
    }
    return NULL;
}

static void test_registering_an_event_cb_makes_it_findable_by_name(void)
{
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_register_event_cb(NULL, "on_click", cb_alpha));
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_register_event_cb(NULL, "on_long_press", cb_beta));

    TEST_ASSERT_EQUAL_PTR(cb_alpha, lv_xml_get_event_cb(NULL, "on_click"));
    TEST_ASSERT_EQUAL_PTR(cb_beta, lv_xml_get_event_cb(NULL, "on_long_press"));
}

/**
 * Last write wins, deliberately: a shared XML component used by two C++ owners
 * with different user_data layouts used to silently keep the first owner's
 * handler and crash when it cast the second owner's struct.
 */
static void test_re_registering_an_event_cb_name_replaces_the_callback(void)
{
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_register_event_cb(NULL, "shared", cb_alpha));
    TEST_ASSERT_EQUAL_PTR(cb_alpha, lv_xml_get_event_cb(NULL, "shared"));

    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_register_event_cb(NULL, "shared", cb_beta));
    TEST_ASSERT_EQUAL_PTR_MESSAGE(cb_beta, lv_xml_get_event_cb(NULL, "shared"),
                                  "the second registration of an event cb name must win");

    /* Replaced in place, not appended: foreach still reports a single entry. */
    evt_collector_t c = {{NULL}, {NULL}, 0};
    lv_xml_event_cb_foreach(NULL, collect_evt_cb, &c);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, c.count,
                                     "re-registering a name appended a record instead of replacing it");
}

/**
 * The empty name is a real, retrievable key. Components declare optional
 * callbacks as `callback="$on_x"` with `default=""`, so an unset one resolves to
 * `""` and is looked up like any other name - registering `""` is how an app
 * supplies the no-op handler for all of them at once.
 */
static void test_the_empty_event_cb_name_is_a_registrable_key(void)
{
    log_capture_start();
    lv_event_cb_t before = lv_xml_get_event_cb(NULL, "");
    log_capture_stop();
    TEST_ASSERT_NULL_MESSAGE(before, "\"\" must not resolve before anything is registered under it");
    TEST_ASSERT_TRUE_MESSAGE(log_contains("No event was found with name \"\""),
                             "an unregistered \"\" lookup is reported like any other miss");

    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_register_event_cb(NULL, "", cb_gamma));

    log_capture_start();
    lv_event_cb_t after = lv_xml_get_event_cb(NULL, "");
    log_capture_stop();
    TEST_ASSERT_EQUAL_PTR_MESSAGE(cb_gamma, after, "\"\" did not resolve to the callback registered under it");
    TEST_ASSERT_FALSE_MESSAGE(log_contains("No event was found"),
                              "a successful \"\" lookup must not warn");

    /* It is its own key - it does not become a catch-all for other names. */
    log_capture_start();
    lv_event_cb_t other = lv_xml_get_event_cb(NULL, "on_click");
    log_capture_stop();
    TEST_ASSERT_NULL_MESSAGE(other, "registering \"\" must not answer lookups for other names");
}

static void test_getting_an_absent_event_cb_returns_null_and_warns(void)
{
    log_capture_start();
    lv_event_cb_t got = lv_xml_get_event_cb(NULL, "no_such_cb");
    log_capture_stop();

    TEST_ASSERT_NULL(got);
    TEST_ASSERT_TRUE_MESSAGE(log_contains("No event was found with name \"no_such_cb\""),
                             "an absent callback must be reported - the XML that referenced it just went dead");
}

static void test_event_cb_foreach_visits_every_registration_in_the_scope(void)
{
    evt_collector_t empty = {{NULL}, {NULL}, 0};
    lv_xml_event_cb_foreach(NULL, collect_evt_cb, &empty);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, empty.count, "a fresh global scope has no event callbacks");

    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_register_event_cb(NULL, "one", cb_alpha));
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_register_event_cb(NULL, "two", cb_beta));
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_register_event_cb(NULL, "", cb_gamma));

    evt_collector_t c = {{NULL}, {NULL}, 0};
    lv_xml_event_cb_foreach(NULL, collect_evt_cb, &c);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(3, c.count, "foreach visited the wrong number of callbacks");
    TEST_ASSERT_EQUAL_PTR(cb_alpha, collector_lookup(&c, "one"));
    TEST_ASSERT_EQUAL_PTR(cb_beta, collector_lookup(&c, "two"));
    TEST_ASSERT_EQUAL_PTR_MESSAGE(cb_gamma, collector_lookup(&c, ""),
                                  "foreach skipped the empty-name registration");

    /* A NULL callback is a guarded no-op. */
    lv_xml_event_cb_foreach(NULL, NULL, &c);
    TEST_ASSERT_EQUAL_UINT32(3, c.count);
}

/**
 * foreach is scope-local by contract while get_event_cb falls back to globals.
 * The asymmetry is deliberate and documented on the header; a caller enumerating
 * a component's own registrations must not be handed the global ones as well.
 */
static void test_event_cb_foreach_does_not_fall_back_to_globals_but_get_does(void)
{
    lv_xml_component_scope_t * scope = register_bare_scope("bare_evt");

    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_register_event_cb(NULL, "global_cb", cb_alpha));
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_register_event_cb(scope, "scoped_cb", cb_beta));

    /* Lookup DOES fall back. */
    TEST_ASSERT_EQUAL_PTR_MESSAGE(cb_alpha, lv_xml_get_event_cb(scope, "global_cb"),
                                  "a scoped lookup must fall back to the global scope");

    /* Enumeration does NOT. */
    evt_collector_t scoped = {{NULL}, {NULL}, 0};
    lv_xml_event_cb_foreach(scope, collect_evt_cb, &scoped);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, scoped.count,
                                     "foreach on a component scope must not include global registrations");
    TEST_ASSERT_EQUAL_PTR(cb_beta, collector_lookup(&scoped, "scoped_cb"));

    evt_collector_t global = {{NULL}, {NULL}, 0};
    lv_xml_event_cb_foreach(NULL, collect_evt_cb, &global);
    TEST_ASSERT_EQUAL_UINT32(1, global.count);
    TEST_ASSERT_EQUAL_PTR(cb_alpha, collector_lookup(&global, "global_cb"));
}

/**
 * PINS CURRENT BEHAVIOUR - suspected bug: `event_ll` is the one registry
 * component_scope_free() (lv_xml_component.c) never walks. Every other list on
 * the scope - consts, params, fonts, images, styles, gradients, subjects,
 * timelines - is freed and lv_ll_clear()ed there; event_ll is not mentioned at
 * all, so each registration's lv_ll node and its lv_strdup'd name outlive the
 * component. Nothing but lv_xml_deinit() reclaims them. HelixScreen registers
 * event callbacks per component and HELIX_HOT_RELOAD re-registers components on
 * every file save, so this accumulates across a dev session.
 *
 * Asserted as "the heap does not come back", which is what actually happens. If
 * component_scope_free() is fixed to free event_ll, this test fails - flip it to
 * TEST_ASSERT_EQUAL_size_t then and keep it as the regression guard.
 */
static void test_unregistering_a_component_leaks_its_event_cb_records(void)
{
    size_t after_cycle[4];

    for(int i = 0; i < 4; i++) {
        lv_xml_component_scope_t * scope = register_bare_scope("leaky_evt");
        TEST_ASSERT_EQUAL_INT(LV_RESULT_OK,
                              (int)lv_xml_register_event_cb(scope, "a_callback_name", cb_alpha));
        TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_component_unregister("leaky_evt"));

        lv_mem_monitor_t mon;
        lv_mem_monitor(&mon);
        after_cycle[i] = mon.free_size;
    }

    /* Cycle 0 is a warm-up (see the same note in test_component.c); cycles 1..3
     * are steady state, and each one loses ground to the leaked record. */
    for(int i = 2; i < 4; i++) {
        TEST_ASSERT_TRUE_MESSAGE(
            after_cycle[i] < after_cycle[i - 1],
            "component_scope_free() now frees event_ll - the leak this test pins is fixed. "
            "Change these to TEST_ASSERT_EQUAL_size_t(after_cycle[1], after_cycle[i]).");
    }
}

/*---------------------------------------------------------------------------
 * main
 *--------------------------------------------------------------------------*/

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_registering_a_subject_makes_it_findable_by_name);
    RUN_TEST(test_every_subject_type_the_engine_supports_round_trips);
    RUN_TEST(test_registering_a_duplicate_subject_name_replaces_the_pointer);
    RUN_TEST(test_getting_an_absent_subject_returns_null_and_warns);
    RUN_TEST(test_unregistering_a_subject_removes_it_from_the_global_scope);
    RUN_TEST(test_unregistering_an_absent_subject_reports_invalid);
    RUN_TEST(test_unregistering_a_borrowed_subject_does_not_deinit_it);
    RUN_TEST(test_a_component_scope_sees_global_subjects_but_not_the_other_way);

    RUN_TEST(test_registering_a_const_makes_it_readable);
    RUN_TEST(test_registering_a_duplicate_const_keeps_the_first_value);
    RUN_TEST(test_update_const_replaces_an_existing_value);
    RUN_TEST(test_update_const_registers_an_absent_name_and_warns);
    RUN_TEST(test_get_const_warns_for_an_absent_name_but_the_silent_variant_does_not);

    RUN_TEST(test_registering_a_font_makes_it_findable_by_name);
    RUN_TEST(test_get_font_falls_back_and_warns_but_the_silent_variant_returns_null);
    RUN_TEST(test_a_component_scope_falls_back_to_global_fonts);

    RUN_TEST(test_registering_a_file_image_stores_a_prefixed_copy);
    RUN_TEST(test_registering_a_variable_image_stores_the_pointer_unchanged);
    RUN_TEST(test_an_absent_image_warns_but_an_empty_or_null_name_is_silent);

    RUN_TEST(test_registering_an_event_cb_makes_it_findable_by_name);
    RUN_TEST(test_re_registering_an_event_cb_name_replaces_the_callback);
    RUN_TEST(test_the_empty_event_cb_name_is_a_registrable_key);
    RUN_TEST(test_getting_an_absent_event_cb_returns_null_and_warns);
    RUN_TEST(test_event_cb_foreach_visits_every_registration_in_the_scope);
    RUN_TEST(test_event_cb_foreach_does_not_fall_back_to_globals_but_get_does);
    RUN_TEST(test_unregistering_a_component_leaks_its_event_cb_records);

    return UNITY_END();
}
