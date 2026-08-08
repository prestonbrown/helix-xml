/**
 * @file test_smoke.c
 *
 * Proof that the harness itself works. If this file fails, nothing else in the
 * suite means anything.
 *
 * What it pins down:
 *  - repeated helix_test_env_setup()/teardown() cycles really do isolate one
 *    test from the next (the spike; see helix_test_env.h for what LVGL's and
 *    the engine's deinit paths actually support)
 *  - lv_xml_init()/lv_xml_deinit() round-trips
 *  - an inline component registers, creates, and its named children are findable
 *  - lv_xml_component_unregister() actually unregisters
 *  - helix_test_pump() runs without wedging
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
 *--------------------------------------------------------------------------*/

/* Deliberately minimal: an obj with two named children, one of them a label.
 * Nothing here depends on sizes, fonts or the theme. */
static const char * SMOKE_CARD_XML =
    "<component>"
    "  <view extends=\"lv_obj\" name=\"smoke_card_root\">"
    "    <lv_label name=\"smoke_title\" text=\"Hello\"/>"
    "    <lv_obj name=\"smoke_body\"/>"
    "  </view>"
    "</component>";

/*---------------------------------------------------------------------------
 * The isolation spike
 *--------------------------------------------------------------------------*/

/**
 * The load-bearing assumption of the whole suite: helix_test_env_setup() /
 * helix_test_env_teardown() give each test a clean world. If that were not
 * true, every test after the first would run against state left by its
 * predecessor and the suite would be worthless.
 *
 * setUp() has already opened one cycle by the time this body runs. Close it,
 * run several more nested cycles doing real work, and leave one open for
 * tearDown().
 *
 * Each iteration checks the three things isolation actually means here:
 *  - the screen is new and empty
 *  - the component registry is empty, so registering the same name again works
 *    rather than colliding
 *  - a full register -> create -> find -> render pass still succeeds on the
 *    Nth cycle exactly as on the first
 */
static void test_repeated_env_cycles_are_clean(void)
{
    helix_test_env_teardown(); /* close the cycle setUp() opened */

    for(int i = 0; i < 5; i++) {
        helix_test_env_setup();

        lv_obj_t * screen = helix_test_env_screen();
        TEST_ASSERT_NOT_NULL_MESSAGE(screen, "no active screen after env setup");

        /* Deliberately NOT asserting that the screen pointer differs from the
         * previous cycle's. Each cycle is a full lv_init()/lv_deinit(), and
         * lv_deinit() resets LVGL's builtin allocator pool, so the next screen
         * very reasonably lands on the same address. Address reuse is evidence
         * of stronger isolation, not weaker - what matters is the state, which
         * is what the rest of this loop checks. */

        /* A fresh cycle starts with a fresh screen - if teardown left anything
         * behind, this count would creep up across iterations. */
        ASSERT_CHILD_COUNT(screen, 0);

        /* Registering the same component name every iteration only works if
         * lv_xml_deinit()/lv_xml_init() really emptied the registry. */
        TEST_ASSERT_NULL_MESSAGE(lv_xml_component_get_scope("smoke_card"),
                                 "component registry carried over from the previous cycle");
        ASSERT_XML_REGISTERS("smoke_card", SMOKE_CARD_XML);

        /* Do real work so the cycle is not trivially empty: build a widget
         * tree, render it, and let timers run. */
        lv_obj_t * card = XML_CREATE(screen, "smoke_card", NULL);
        ASSERT_NAMED(card, "smoke_title");
        helix_test_pump(30);

        ASSERT_CHILD_COUNT(screen, 1);

        helix_test_env_teardown();
    }

    /* Reopen so the outer tearDown() has something to close. */
    helix_test_env_setup();
}

/*---------------------------------------------------------------------------
 * XML engine round trip
 *--------------------------------------------------------------------------*/

/** lv_xml_init()/lv_xml_deinit() must be re-enterable within one LVGL cycle. */
static void test_xml_init_deinit_round_trip(void)
{
    for(int i = 0; i < 3; i++) {
        lv_xml_deinit();
        lv_xml_init();

        /* The registry must be empty again after each round trip. */
        TEST_ASSERT_NULL_MESSAGE(lv_xml_component_get_scope("smoke_card"),
                                 "component registry survived lv_xml_deinit()");

        ASSERT_XML_REGISTERS("smoke_card", SMOKE_CARD_XML);
        TEST_ASSERT_NOT_NULL_MESSAGE(lv_xml_component_get_scope("smoke_card"),
                                     "component did not appear in the registry after registering");
    }
}

/*---------------------------------------------------------------------------
 * Register -> create -> find
 *--------------------------------------------------------------------------*/

static void test_inline_component_creates_findable_children(void)
{
    lv_obj_t * screen = helix_test_env_screen();

    ASSERT_XML_REGISTERS("smoke_card", SMOKE_CARD_XML);

    lv_obj_t * card = XML_CREATE(screen, "smoke_card", NULL);
    helix_test_pump(30);

    /* The component root landed on the screen. */
    ASSERT_CHILD_COUNT(screen, 1);
    TEST_ASSERT_EQUAL_PTR_MESSAGE(screen, lv_obj_get_parent(card),
                                  "component root was not parented to the screen");

    /* Both named children are reachable by name, and the label carries the
     * text the XML declared. */
    lv_obj_t * title = ASSERT_NAMED(card, "smoke_title");
    ASSERT_LABEL_TEXT(title, "Hello");
    ASSERT_NAMED(card, "smoke_body");
    ASSERT_CHILD_COUNT(card, 2);

    /* And a name that was never declared must not resolve. */
    ASSERT_NO_NAMED(card, "smoke_nonexistent");
}

/** Attributes passed to lv_xml_create() reach the created widget. */
static void test_create_applies_attributes(void)
{
    lv_obj_t * screen = helix_test_env_screen();

    const char * attrs[] = {"name", "direct_label", "text", "From attrs", NULL, NULL};
    lv_obj_t * label = XML_CREATE(screen, "lv_label", attrs);
    helix_test_pump(30);

    ASSERT_LABEL_TEXT(label, "From attrs");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(label, ASSERT_NAMED(screen, "direct_label"),
                                  "lv_obj_find_by_name() found a different widget than the one created");
}

/*---------------------------------------------------------------------------
 * Unregister
 *--------------------------------------------------------------------------*/

static void test_component_unregister_removes_it(void)
{
    ASSERT_XML_REGISTERS("smoke_card", SMOKE_CARD_XML);
    TEST_ASSERT_NOT_NULL(lv_xml_component_get_scope("smoke_card"));

    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_RESULT_OK,
                                  (int)lv_xml_component_unregister("smoke_card"),
                                  "lv_xml_component_unregister() reported failure for a registered component");

    TEST_ASSERT_NULL_MESSAGE(lv_xml_component_get_scope("smoke_card"),
                             "component still in the registry after unregistering");

    /* Creating it again must now fail rather than silently succeed. */
    TEST_ASSERT_NULL_MESSAGE(lv_xml_create(helix_test_env_screen(), "smoke_card", NULL),
                             "unregistered component was still creatable");

    /* Re-registering the same name must work - this is what proves unregister
     * released the slot rather than just hiding it. */
    ASSERT_XML_REGISTERS("smoke_card", SMOKE_CARD_XML);
}

/*---------------------------------------------------------------------------
 * Harness plumbing
 *--------------------------------------------------------------------------*/

/** The asset dir is injected by CMake and must point somewhere real. */
static void test_asset_dir_is_injected(void)
{
    TEST_ASSERT_NOT_NULL(HELIX_TEST_ASSET_DIR);
    TEST_ASSERT_TRUE_MESSAGE(HELIX_TEST_ASSET_DIR[0] == '/',
                             "HELIX_TEST_ASSET_DIR is not an absolute path");
}

/** The pump must advance LVGL's clock and return. */
static void test_pump_advances_the_clock(void)
{
    uint32_t before = lv_tick_get();
    helix_test_pump(100);
    uint32_t after = lv_tick_get();

    TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(before + 100, after,
                                                "helix_test_pump() did not advance lv_tick by the requested amount");
}

/*---------------------------------------------------------------------------
 * main
 *--------------------------------------------------------------------------*/

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_repeated_env_cycles_are_clean);
    RUN_TEST(test_xml_init_deinit_round_trip);
    RUN_TEST(test_inline_component_creates_findable_children);
    RUN_TEST(test_create_applies_attributes);
    RUN_TEST(test_component_unregister_removes_it);
    RUN_TEST(test_asset_dir_is_injected);
    RUN_TEST(test_pump_advances_the_clock);

    return UNITY_END();
}
