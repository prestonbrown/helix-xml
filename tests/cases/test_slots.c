/**
 * @file test_slots.c
 *
 * Component slots: `<slot>` declarations in `<api>`, where an instance's
 * children land, the `<component-slot>` form, default content, slots reached
 * through `extends`, and lv_xml_component_get_slot() for host code.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <string.h>

#include "helpers/helix_log_capture.h"
#include "helpers/helix_test_env.h"
#include "helpers/helix_test_subject.h"
#include "helpers/helix_test_pump.h"
#include "helpers/xml_assert.h"

void setUp(void)
{
    helix_test_env_setup();
}

void tearDown(void)
{
    helix_test_env_teardown();
}

/*---------------------------------------------------------------------------
 * Components
 *--------------------------------------------------------------------------*/

/* A card with a default `body` slot holding default content, and a `footer`
 * slot that targets an object with a different name. */
static const char * CARD =
    "<component>"
    "  <api>"
    "    <slot name=\"body\" default=\"true\"/>"
    "    <slot name=\"footer\" target=\"footer_row\"/>"
    "  </api>"
    "  <view name=\"card_root\" flex_flow=\"column\">"
    "    <lv_label name=\"title\" text=\"Title\"/>"
    "    <lv_obj name=\"body\">"
    "      <lv_label name=\"placeholder\" text=\"Nothing here\"/>"
    "    </lv_obj>"
    "    <lv_obj name=\"footer_row\">"
    "      <lv_label name=\"footer_default\" text=\"Footer\"/>"
    "    </lv_obj>"
    "  </view>"
    "</component>";

/* No slots: children keep landing on the root. */
static const char * PLAIN =
    "<component>"
    "  <view name=\"plain_root\">"
    "    <lv_obj name=\"inner\"/>"
    "  </view>"
    "</component>";

static lv_obj_t * card_with(const char * children_xml)
{
    static char buf[2048];
    lv_snprintf(buf, sizeof(buf),
                "<component><view name=\"host\">"
                "<slot_card name=\"card\">%s</slot_card>"
                "</view></component>",
                children_xml);
    ASSERT_XML_REGISTERS("slot_host", buf);
    lv_obj_t * view = XML_CREATE(helix_test_env_screen(), "slot_host", NULL);
    helix_test_pump(30);
    return ASSERT_NAMED(view, "card");
}

/*---------------------------------------------------------------------------
 * Default slot
 *--------------------------------------------------------------------------*/

static void test_an_instance_child_lands_in_the_default_slot(void)
{
    ASSERT_XML_REGISTERS("slot_card", CARD);
    lv_obj_t * card = card_with("<lv_label name=\"mine\" text=\"Mine\"/>");

    lv_obj_t * body = ASSERT_NAMED(card, "body");
    lv_obj_t * mine = ASSERT_NAMED(card, "mine");
    TEST_ASSERT_EQUAL_PTR(body, lv_obj_get_parent(mine));
}

static void test_filling_a_slot_replaces_its_default_content(void)
{
    ASSERT_XML_REGISTERS("slot_card", CARD);
    lv_obj_t * card = card_with("<lv_label name=\"one\"/><lv_label name=\"two\"/>");

    lv_obj_t * body = ASSERT_NAMED(card, "body");
    ASSERT_NO_NAMED(card, "placeholder");
    ASSERT_CHILD_COUNT(body, 2);
    TEST_ASSERT_EQUAL_PTR(ASSERT_NAMED(card, "one"), lv_obj_get_child(body, 0));
    TEST_ASSERT_EQUAL_PTR(ASSERT_NAMED(card, "two"), lv_obj_get_child(body, 1));
}

static void test_an_unfilled_slot_keeps_its_default_content(void)
{
    ASSERT_XML_REGISTERS("slot_card", CARD);
    lv_obj_t * card = card_with("");

    ASSERT_NAMED(card, "placeholder");
    ASSERT_NAMED(card, "footer_default");
}

static void test_element_children_still_apply_to_the_instance_root(void)
{
    ASSERT_XML_REGISTERS("slot_card", CARD);
    lv_subject_t * s = helix_test_subject_int(0);
    TEST_ASSERT_EQUAL(LV_RESULT_OK, lv_xml_register_subject(NULL, "slot_hide", s));

    lv_obj_t * card = card_with(
                          "<bind_flag_if_eq subject=\"slot_hide\" flag=\"hidden\" ref_value=\"1\"/>"
                          "<lv_label name=\"mine\"/>");
    lv_obj_t * body = ASSERT_NAMED(card, "body");

    lv_subject_set_int(s, 1);
    ASSERT_FLAG(card, LV_OBJ_FLAG_HIDDEN);
    ASSERT_NO_FLAG(body, LV_OBJ_FLAG_HIDDEN);
    /* The bind is not a widget, so it filled nothing: the placeholder went
     * because of the label, and the label is the body's only child. */
    ASSERT_CHILD_COUNT(body, 1);
}

static void test_a_component_without_slots_keeps_children_on_its_root(void)
{
    ASSERT_XML_REGISTERS("slot_plain", PLAIN);
    ASSERT_XML_REGISTERS("slot_plain_host",
                         "<component><view name=\"host\">"
                         "<slot_plain name=\"p\"><lv_obj name=\"mine\"/></slot_plain>"
                         "</view></component>");
    lv_obj_t * view = XML_CREATE(helix_test_env_screen(), "slot_plain_host", NULL);

    lv_obj_t * p = ASSERT_NAMED(view, "p");
    TEST_ASSERT_EQUAL_PTR(p, lv_obj_get_parent(ASSERT_NAMED(view, "mine")));
    ASSERT_NAMED(view, "inner");
}

static void test_repeated_children_all_land_in_the_slot(void)
{
    ASSERT_XML_REGISTERS("slot_card", CARD);
    lv_obj_t * card = card_with("<repeat count=\"3\"><lv_obj name=\"r_${i}\"/></repeat>");

    lv_obj_t * body = ASSERT_NAMED(card, "body");
    ASSERT_CHILD_COUNT(body, 3);
    TEST_ASSERT_EQUAL_PTR(body, lv_obj_get_parent(ASSERT_NAMED(card, "r_2")));
}

/*---------------------------------------------------------------------------
 * Named slots
 *--------------------------------------------------------------------------*/

static void test_the_component_slot_form_fills_a_named_slot_at_its_target(void)
{
    ASSERT_XML_REGISTERS("slot_card", CARD);
    lv_obj_t * card = card_with(
                          "<slot_card-footer><lv_button name=\"ok\"/></slot_card-footer>"
                          "<lv_label name=\"mine\"/>");

    lv_obj_t * footer = ASSERT_NAMED(card, "footer_row");
    TEST_ASSERT_EQUAL_PTR(footer, lv_obj_get_parent(ASSERT_NAMED(card, "ok")));
    ASSERT_NO_NAMED(card, "footer_default");
    /* The default slot is filled independently of the named one. */
    TEST_ASSERT_EQUAL_PTR(ASSERT_NAMED(card, "body"), lv_obj_get_parent(ASSERT_NAMED(card, "mine")));
}

static const char * CARD_HIDE_EMPTY =
    "<component>"
    "  <api><slot name=\"footer\" hide_empty=\"true\"/></api>"
    "  <view name=\"card_root\" flex_flow=\"column\">"
    "    <lv_obj name=\"footer\"/>"
    "  </view>"
    "</component>";

static void test_a_hide_empty_slot_is_hidden_until_it_is_filled(void)
{
    ASSERT_XML_REGISTERS("slot_hide_card", CARD_HIDE_EMPTY);
    ASSERT_XML_REGISTERS("slot_hide_host",
                         "<component><view name=\"host\">"
                         "<slot_hide_card name=\"empty\"/>"
                         "<slot_hide_card name=\"full\"><slot_hide_card-footer><lv_obj/></slot_hide_card-footer>"
                         "</slot_hide_card>"
                         "</view></component>");
    lv_obj_t * view = XML_CREATE(helix_test_env_screen(), "slot_hide_host", NULL);

    ASSERT_FLAG(ASSERT_NAMED(ASSERT_NAMED(view, "empty"), "footer"), LV_OBJ_FLAG_HIDDEN);
    ASSERT_NO_FLAG(ASSERT_NAMED(ASSERT_NAMED(view, "full"), "footer"), LV_OBJ_FLAG_HIDDEN);
}

static void test_a_slot_tags_attributes_style_its_host(void)
{
    ASSERT_XML_REGISTERS("slot_card", CARD);
    lv_obj_t * card = card_with(
                          "<slot_card-footer style_pad_left=\"7\" flex_flow=\"row\"><lv_obj name=\"x\"/></slot_card-footer>");

    lv_obj_t * footer = ASSERT_NAMED(card, "footer_row");
    ASSERT_STYLE_INT(footer, LV_STYLE_PAD_LEFT, LV_PART_MAIN, 7);
    TEST_ASSERT_EQUAL(LV_FLEX_FLOW_ROW, lv_obj_get_style_flex_flow(footer, LV_PART_MAIN));
}

static void test_a_slot_tag_cannot_rename_its_host(void)
{
    ASSERT_XML_REGISTERS("slot_card", CARD);
    lv_obj_t * card = card_with("<slot_card-footer name=\"renamed\"><lv_obj/></slot_card-footer>");

    ASSERT_NAMED(card, "footer_row");
    ASSERT_NO_NAMED(card, "renamed");
}

static void test_an_undeclared_slot_tag_with_a_named_object_still_resolves_and_warns(void)
{
    ASSERT_XML_REGISTERS("slot_card", CARD);
    log_capture_start();
    lv_obj_t * card = card_with("<slot_card-title><lv_obj name=\"extra\"/></slot_card-title>");
    log_capture_stop();

    TEST_ASSERT_EQUAL_PTR(ASSERT_NAMED(card, "title"), lv_obj_get_parent(ASSERT_NAMED(card, "extra")));
    TEST_ASSERT_TRUE_MESSAGE(log_contains("does not declare a slot 'title'"), g_log_buf);
}

static void test_a_slot_tag_naming_nothing_reports_the_slot_not_a_stale_binary(void)
{
    ASSERT_XML_REGISTERS("slot_card", CARD);
    log_capture_start();
    lv_obj_t * card = card_with("<slot_card-nope><lv_obj name=\"lost\"/></slot_card-nope>");
    log_capture_stop();

    TEST_ASSERT_TRUE_MESSAGE(log_contains("has no slot 'nope'"), g_log_buf);
    TEST_ASSERT_FALSE_MESSAGE(log_contains("STALE BINARY"), g_log_buf);
    /* Its contents still attach to the nearest real ancestor. */
    ASSERT_NAMED(card, "lost");
}

/*---------------------------------------------------------------------------
 * extends
 *--------------------------------------------------------------------------*/

static void test_an_extending_view_fills_the_extended_components_default_slot(void)
{
    ASSERT_XML_REGISTERS("slot_card", CARD);
    ASSERT_XML_REGISTERS("slot_ext",
                         "<component><view extends=\"slot_card\" name=\"ext\">"
                         "<lv_label name=\"ext_child\"/>"
                         "</view></component>");
    lv_obj_t * ext = XML_CREATE(helix_test_env_screen(), "slot_ext", NULL);
    helix_test_pump(30);

    lv_obj_t * body = ASSERT_NAMED(ext, "body");
    TEST_ASSERT_EQUAL_PTR(body, lv_obj_get_parent(ASSERT_NAMED(ext, "ext_child")));
    ASSERT_NO_NAMED(ext, "placeholder");
    ASSERT_NAMED(ext, "title");
}

static void test_an_instance_of_an_extender_inherits_the_slots(void)
{
    ASSERT_XML_REGISTERS("slot_card", CARD);
    ASSERT_XML_REGISTERS("slot_ext_empty",
                         "<component><view extends=\"slot_card\" name=\"ext\"/></component>");
    ASSERT_XML_REGISTERS("slot_ext_host",
                         "<component><view name=\"host\">"
                         "<slot_ext_empty name=\"e\"><lv_obj name=\"outer_child\"/></slot_ext_empty>"
                         "</view></component>");
    lv_obj_t * view = XML_CREATE(helix_test_env_screen(), "slot_ext_host", NULL);

    lv_obj_t * e = ASSERT_NAMED(view, "e");
    TEST_ASSERT_EQUAL_PTR(ASSERT_NAMED(e, "body"), lv_obj_get_parent(ASSERT_NAMED(e, "outer_child")));
}

/*---------------------------------------------------------------------------
 * Host code
 *--------------------------------------------------------------------------*/

static void test_host_code_gets_each_declared_slot_by_name(void)
{
    ASSERT_XML_REGISTERS("slot_card", CARD);
    lv_obj_t * card = XML_CREATE(helix_test_env_screen(), "slot_card", NULL);

    TEST_ASSERT_EQUAL_PTR(ASSERT_NAMED(card, "body"), lv_xml_component_get_slot(card, "body"));
    TEST_ASSERT_EQUAL_PTR(ASSERT_NAMED(card, "footer_row"), lv_xml_component_get_slot(card, "footer"));
    /* NULL names the default slot. */
    TEST_ASSERT_EQUAL_PTR(ASSERT_NAMED(card, "body"), lv_xml_component_get_slot(card, NULL));
    TEST_ASSERT_NULL(lv_xml_component_get_slot(card, "title"));
    TEST_ASSERT_NULL(lv_xml_component_get_slot(ASSERT_NAMED(card, "title"), "body"));
}

static void test_host_code_reaches_an_extended_components_slot(void)
{
    ASSERT_XML_REGISTERS("slot_card", CARD);
    ASSERT_XML_REGISTERS("slot_ext_empty",
                         "<component><view extends=\"slot_card\" name=\"ext\"/></component>");
    lv_obj_t * ext = XML_CREATE(helix_test_env_screen(), "slot_ext_empty", NULL);

    TEST_ASSERT_EQUAL_PTR(ASSERT_NAMED(ext, "body"), lv_xml_component_get_slot(ext, NULL));
}

static void test_a_slot_whose_target_is_missing_warns_at_creation(void)
{
    ASSERT_XML_REGISTERS("slot_broken",
                         "<component><api><slot name=\"body\" default=\"true\"/></api>"
                         "<view name=\"r\"><lv_obj name=\"not_body\"/></view></component>");
    log_capture_start();
    lv_obj_t * r = XML_CREATE(helix_test_env_screen(), "slot_broken", NULL);
    log_capture_stop();

    TEST_ASSERT_TRUE_MESSAGE(log_contains("slot 'body'"), g_log_buf);
    TEST_ASSERT_NULL(lv_xml_component_get_slot(r, NULL));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_an_instance_child_lands_in_the_default_slot);
    RUN_TEST(test_filling_a_slot_replaces_its_default_content);
    RUN_TEST(test_an_unfilled_slot_keeps_its_default_content);
    RUN_TEST(test_element_children_still_apply_to_the_instance_root);
    RUN_TEST(test_a_component_without_slots_keeps_children_on_its_root);
    RUN_TEST(test_repeated_children_all_land_in_the_slot);

    RUN_TEST(test_the_component_slot_form_fills_a_named_slot_at_its_target);
    RUN_TEST(test_a_hide_empty_slot_is_hidden_until_it_is_filled);
    RUN_TEST(test_a_slot_tags_attributes_style_its_host);
    RUN_TEST(test_a_slot_tag_cannot_rename_its_host);
    RUN_TEST(test_an_undeclared_slot_tag_with_a_named_object_still_resolves_and_warns);
    RUN_TEST(test_a_slot_tag_naming_nothing_reports_the_slot_not_a_stale_binary);

    RUN_TEST(test_an_extending_view_fills_the_extended_components_default_slot);
    RUN_TEST(test_an_instance_of_an_extender_inherits_the_slots);

    RUN_TEST(test_host_code_gets_each_declared_slot_by_name);
    RUN_TEST(test_host_code_reaches_an_extended_components_slot);
    RUN_TEST(test_a_slot_whose_target_is_missing_warns_at_creation);

    return UNITY_END();
}
