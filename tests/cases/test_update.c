/**
 * @file test_update.c
 *
 * src/xml/lv_xml_update.c - retargeting an existing, already-built widget with
 * an XML snippet.
 *
 * The contract is narrow and easy to get wrong:
 *   `<update-lv_label name="my_label" text="new"/>`
 * The tag must be `update-` + the WIDGET's XML name (not the component's), the
 * `name` attribute selects the target, and every remaining attribute is pushed
 * through that widget's ordinary apply_cb - so the update dialect is exactly
 * the create dialect.
 *
 * Two things about targeting that are easy to trip over and are pinned below:
 *
 *  1. The search root is ALWAYS lv_screen_active(). There is no way to scope an
 *     update to a subtree, and a widget on an inactive screen is unreachable.
 *
 *  2. The lookup is lv_obj_get_child_by_name(), which is a PATH walk over
 *     DIRECT children - not the recursive lv_obj_find_by_name() used everywhere
 *     else in the engine. A label nested inside a component is not findable by
 *     its bare name; it needs "component_root/label".
 *
 * Every assertion below reads the live object back after the call. A test that
 * only checked the return value would pass against a function that parsed the
 * XML and then did nothing - which is very nearly what happens on the failure
 * paths, since all of them warn and still return LV_RESULT_OK.
 *
 * NOT TESTED, DELIBERATELY
 *  - NULL into lv_xml_update_from_data(): it goes straight to lv_strlen with no
 *    guard.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include <lvgl.h>

/* Log capture: every rejection path in lv_xml_update.c is a LV_LOG_WARN followed
 * by LV_RESULT_OK, so the log is what tells "declined for reason X" apart from
 * "declined for reason Y". */
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
 * A label parked as a DIRECT child of the active screen, which is the only
 * shape lv_obj_get_child_by_name() finds by a bare name.
 */
static lv_obj_t * make_target_label(const char * name, const char * text)
{
    const char * attrs[] = {"name", name, "text", text, NULL, NULL};
    lv_obj_t * label = XML_CREATE(helix_test_env_screen(), "lv_label", attrs);
    helix_test_pump(30);
    ASSERT_LABEL_TEXT(label, text);
    return label;
}

/*===========================================================================
 * The happy path
 *==========================================================================*/

/** The point of the module: an attribute on a live, named widget changes. */
static void test_an_update_changes_an_attribute_on_a_live_widget(void)
{
    lv_obj_t * label = make_target_label("upd_label", "before");

    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK,
                          (int)lv_xml_update_from_data(
                              "<update-lv_label name=\"upd_label\" text=\"after\"/>"));
    helix_test_pump(30);

    /* Read the live object, not the return value. */
    ASSERT_LABEL_TEXT(label, "after");
}

/** Several attributes in one snippet all land, including generic obj ones. */
static void test_an_update_applies_every_attribute_in_the_snippet(void)
{
    lv_obj_t * label = make_target_label("upd_label", "before");

    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK,
                          (int)lv_xml_update_from_data(
                              "<update-lv_label name=\"upd_label\" text=\"after\" "
                              "style_pad_all=\"17\" hidden=\"true\"/>"));
    helix_test_pump(30);

    ASSERT_LABEL_TEXT(label, "after");
    ASSERT_STYLE_INT(label, LV_STYLE_PAD_TOP, LV_PART_MAIN, 17);
    ASSERT_FLAG(label, LV_OBJ_FLAG_HIDDEN);
}

/** More than one update element in a single snippet, each hitting its own target. */
static void test_one_snippet_can_update_several_widgets(void)
{
    lv_obj_t * first = make_target_label("upd_first", "one");
    lv_obj_t * second = make_target_label("upd_second", "two");

    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK,
                          (int)lv_xml_update_from_data(
                              "<updates>"
                              "  <update-lv_label name=\"upd_first\" text=\"ONE\"/>"
                              "  <update-lv_label name=\"upd_second\" text=\"TWO\"/>"
                              "</updates>"));
    helix_test_pump(30);

    ASSERT_LABEL_TEXT(first, "ONE");
    ASSERT_LABEL_TEXT(second, "TWO");
}

/** An update must not disturb a sibling that was not named. */
static void test_an_update_leaves_other_widgets_alone(void)
{
    lv_obj_t * target = make_target_label("upd_target", "target");
    lv_obj_t * bystander = make_target_label("upd_bystander", "bystander");

    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK,
                          (int)lv_xml_update_from_data(
                              "<update-lv_label name=\"upd_target\" text=\"changed\"/>"));
    helix_test_pump(30);

    ASSERT_LABEL_TEXT(target, "changed");
    ASSERT_LABEL_TEXT(bystander, "bystander");
}

/**
 * The `name` attribute selects the target and must NOT also be applied, or the
 * widget would be renamed to itself on every update and the attribute would
 * leak into apply_cb.
 */
static void test_the_name_attribute_is_consumed_and_not_reapplied(void)
{
    lv_obj_t * label = make_target_label("upd_label", "before");

    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK,
                          (int)lv_xml_update_from_data(
                              "<update-lv_label name=\"upd_label\" text=\"after\"/>"));
    helix_test_pump(30);

    TEST_ASSERT_EQUAL_STRING_MESSAGE("upd_label", lv_obj_get_name(label),
                                     "the update rewrote the target's name");

    /* Still addressable by the same name, so a second update works. */
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK,
                          (int)lv_xml_update_from_data(
                              "<update-lv_label name=\"upd_label\" text=\"again\"/>"));
    helix_test_pump(30);
    ASSERT_LABEL_TEXT(label, "again");
}

/*===========================================================================
 * Targeting
 *==========================================================================*/

/**
 * The lookup is a path walk over DIRECT children, so a widget nested inside a
 * component needs the full path. Both halves are asserted: the bare name does
 * NOT resolve, the path does.
 */
static void test_a_nested_widget_is_reachable_only_through_its_path(void)
{
    ASSERT_XML_REGISTERS("upd_card",
                         "<component>"
                         "  <view extends=\"lv_obj\">"
                         "    <lv_label name=\"upd_nested\" text=\"nested\"/>"
                         "  </view>"
                         "</component>");

    /* The instance name has to come from the creation site: lv_xml_create()
     * overwrites whatever <view name="..."> said with either the caller's
     * `name` attribute or the auto-indexed "<component>_#". */
    const char * card_attrs[] = {"name", "upd_card_root", NULL, NULL};
    lv_obj_t * card = XML_CREATE(helix_test_env_screen(), "upd_card", card_attrs);
    helix_test_pump(30);
    TEST_ASSERT_EQUAL_STRING("upd_card_root", lv_obj_get_name(card));
    lv_obj_t * nested = ASSERT_NAMED(card, "upd_nested");

    /* Bare name: not a direct child of the screen, so not found. */
    log_capture_start();
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK,
                          (int)lv_xml_update_from_data(
                              "<update-lv_label name=\"upd_nested\" text=\"by_bare_name\"/>"));
    log_capture_stop();
    helix_test_pump(30);

    ASSERT_LABEL_TEXT(nested, "nested");
    TEST_ASSERT_TRUE_MESSAGE(log_contains("No widget is found with the name of `upd_nested`"),
                             "a bare nested name resolved - the lookup became recursive");

    /* Full path from the screen: found. */
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK,
                          (int)lv_xml_update_from_data(
                              "<update-lv_label name=\"upd_card_root/upd_nested\" text=\"by_path\"/>"));
    helix_test_pump(30);

    ASSERT_LABEL_TEXT(nested, "by_path");
}

/**
 * PINS CURRENT BEHAVIOUR - suspected bug: every targeting failure in
 * lv_xml_update_from_data() is a LV_LOG_WARN inside the expat start handler,
 * and the handler cannot influence the return value. So a snippet that updated
 * nothing at all is indistinguishable, to the caller, from one that worked. The
 * only way an app can detect a typo'd widget name is by scraping the log.
 */
static void test_updating_a_name_that_does_not_exist_changes_nothing_but_reports_ok(void)
{
    lv_obj_t * label = make_target_label("upd_label", "before");

    log_capture_start();
    lv_result_t res = lv_xml_update_from_data(
                          "<update-lv_label name=\"no_such_widget\" text=\"after\"/>");
    log_capture_stop();
    helix_test_pump(30);

    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_RESULT_OK, (int)res,
                                  "the OK-on-miss behaviour has changed - see the note above");
    TEST_ASSERT_TRUE_MESSAGE(log_contains("No widget is found with the name of `no_such_widget`"),
                             "a missing target was not reported at all");

    /* And nothing was touched. */
    ASSERT_LABEL_TEXT(label, "before");
}

/** A widget that is not on the ACTIVE screen is out of reach. */
static void test_a_widget_on_an_inactive_screen_is_not_reachable(void)
{
    lv_obj_t * other_screen = lv_obj_create(NULL);
    TEST_ASSERT_NOT_NULL(other_screen);

    const char * attrs[] = {"name", "upd_offscreen", "text", "before", NULL, NULL};
    lv_obj_t * label = XML_CREATE(other_screen, "lv_label", attrs);
    helix_test_pump(30);

    log_capture_start();
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK,
                          (int)lv_xml_update_from_data(
                              "<update-lv_label name=\"upd_offscreen\" text=\"after\"/>"));
    log_capture_stop();
    helix_test_pump(30);

    ASSERT_LABEL_TEXT(label, "before");
    TEST_ASSERT_TRUE_MESSAGE(log_contains("No widget is found with the name of `upd_offscreen`"),
                             "the update searched outside the active screen");

    lv_obj_delete(other_screen);
}

/** No `name` attribute: nothing to select, so nothing happens. */
static void test_an_update_without_a_name_attribute_does_nothing(void)
{
    lv_obj_t * label = make_target_label("upd_label", "before");

    log_capture_start();
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK,
                          (int)lv_xml_update_from_data("<update-lv_label text=\"after\"/>"));
    log_capture_stop();
    helix_test_pump(30);

    ASSERT_LABEL_TEXT(label, "before");
    TEST_ASSERT_TRUE_MESSAGE(log_contains("There is no name property"),
                             "a nameless update was accepted");
}

/*===========================================================================
 * Malformed payloads
 *==========================================================================*/

/** A tag that is not prefixed `update-` is refused. */
static void test_a_tag_without_the_update_prefix_is_refused(void)
{
    lv_obj_t * label = make_target_label("upd_label", "before");

    log_capture_start();
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK,
                          (int)lv_xml_update_from_data(
                              "<lv_label name=\"upd_label\" text=\"after\"/>"));
    log_capture_stop();
    helix_test_pump(30);

    ASSERT_LABEL_TEXT(label, "before");
    TEST_ASSERT_TRUE_MESSAGE(log_contains("doesn't start with `update-`"),
                             "a tag with no update- prefix was applied anyway");
}

/** A tag shorter than "update-" hits the length guard rather than reading past it. */
static void test_a_tag_shorter_than_the_prefix_is_refused(void)
{
    lv_obj_t * label = make_target_label("upd_label", "before");

    log_capture_start();
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK,
                          (int)lv_xml_update_from_data("<up name=\"upd_label\" text=\"after\"/>"));
    log_capture_stop();
    helix_test_pump(30);

    ASSERT_LABEL_TEXT(label, "before");
    TEST_ASSERT_TRUE(log_contains("doesn't start with `update-`"));
}

/** `update-` followed by something that is not a registered widget is refused. */
static void test_an_update_naming_an_unknown_widget_is_refused(void)
{
    lv_obj_t * label = make_target_label("upd_label", "before");

    log_capture_start();
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK,
                          (int)lv_xml_update_from_data(
                              "<update-not_a_widget name=\"upd_label\" text=\"after\"/>"));
    log_capture_stop();
    helix_test_pump(30);

    ASSERT_LABEL_TEXT(label, "before");
    TEST_ASSERT_TRUE_MESSAGE(log_contains("not_a_widget is not a known widget"),
                             "an unknown widget name was accepted");
}

/**
 * XML that expat cannot parse is the one failure that reaches the return value.
 *
 * The start TAG itself is truncated here, so expat never completes an element
 * and the start handler never runs - which is what makes "nothing changed" a
 * safe assertion. Truncating one character later, after the tag closes, gives
 * the opposite result; that is
 * test_a_partially_valid_snippet_applies_what_it_parsed_before_failing.
 */
static void test_a_malformed_update_payload_is_rejected(void)
{
    lv_obj_t * label = make_target_label("upd_label", "before");

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        LV_RESULT_INVALID,
        (int)lv_xml_update_from_data("<update-lv_label name=\"upd_label\" text=\"after\""),
        "an unterminated update snippet reported success");

    helix_test_pump(30);
    ASSERT_LABEL_TEXT(label, "before");
}

/** Garbage that is not XML at all is rejected too, and changes nothing. */
static void test_a_payload_that_is_not_xml_is_rejected(void)
{
    lv_obj_t * label = make_target_label("upd_label", "before");

    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_RESULT_INVALID,
                                  (int)lv_xml_update_from_data("this is not xml <<< &"),
                                  "a non-XML update payload reported success");

    helix_test_pump(30);
    ASSERT_LABEL_TEXT(label, "before");
}

/**
 * A snippet that breaks AFTER a valid update element: expat reports the error,
 * but the elements it already dispatched have taken effect. Pinned because it
 * means an update batch is not atomic - a caller cannot treat LV_RESULT_INVALID
 * as "nothing happened".
 */
static void test_a_partially_valid_snippet_applies_what_it_parsed_before_failing(void)
{
    lv_obj_t * label = make_target_label("upd_label", "before");

    TEST_ASSERT_EQUAL_INT(LV_RESULT_INVALID,
                          (int)lv_xml_update_from_data(
                              "<updates>"
                              "  <update-lv_label name=\"upd_label\" text=\"applied\"/>"
                              "  <update-lv_label name=\"upd_label\" text=\"broken\""));
    helix_test_pump(30);

    ASSERT_LABEL_TEXT(label, "applied");
}

/*---------------------------------------------------------------------------
 * main
 *--------------------------------------------------------------------------*/

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_an_update_changes_an_attribute_on_a_live_widget);
    RUN_TEST(test_an_update_applies_every_attribute_in_the_snippet);
    RUN_TEST(test_one_snippet_can_update_several_widgets);
    RUN_TEST(test_an_update_leaves_other_widgets_alone);
    RUN_TEST(test_the_name_attribute_is_consumed_and_not_reapplied);

    RUN_TEST(test_a_nested_widget_is_reachable_only_through_its_path);
    RUN_TEST(test_updating_a_name_that_does_not_exist_changes_nothing_but_reports_ok);
    RUN_TEST(test_a_widget_on_an_inactive_screen_is_not_reachable);
    RUN_TEST(test_an_update_without_a_name_attribute_does_nothing);

    RUN_TEST(test_a_tag_without_the_update_prefix_is_refused);
    RUN_TEST(test_a_tag_shorter_than_the_prefix_is_refused);
    RUN_TEST(test_an_update_naming_an_unknown_widget_is_refused);
    RUN_TEST(test_a_malformed_update_payload_is_rejected);
    RUN_TEST(test_a_payload_that_is_not_xml_is_rejected);
    RUN_TEST(test_a_partially_valid_snippet_applies_what_it_parsed_before_failing);

    return UNITY_END();
}
