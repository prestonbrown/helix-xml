/**
 * @file test_dropdown_binds.c
 *
 * lv_dropdown's subject bindings: bind_options fills the option list from a
 * string subject and keeps it current, alongside bind_value.
 *
 * SPDX-License-Identifier: MIT
 */

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

static char g_opts[128];
static char g_opts_prev[128];

static lv_subject_t * options_subject(const char * initial)
{
    lv_subject_t * s = helix_test_subject_string(g_opts, g_opts_prev, sizeof(g_opts), initial);
    TEST_ASSERT_EQUAL(LV_RESULT_OK, lv_xml_register_subject(NULL, "dd_opts", s));
    return s;
}

static void test_bind_options_fills_and_follows_the_subject(void)
{
    lv_subject_t * opts = options_subject("A\nB");
    ASSERT_XML_REGISTERS("dd_opts_comp",
                         "<component><view name=\"r\"><lv_dropdown name=\"dd\" bind_options=\"dd_opts\"/>"
                         "</view></component>");
    lv_obj_t * dd = ASSERT_NAMED(XML_CREATE(helix_test_env_screen(), "dd_opts_comp", NULL), "dd");

    TEST_ASSERT_EQUAL_STRING("A\nB", lv_dropdown_get_options(dd));
    lv_subject_set_string(opts, "X\nY\nZ");
    TEST_ASSERT_EQUAL_STRING("X\nY\nZ", lv_dropdown_get_options(dd));
}

static void test_new_options_keep_a_selection_that_still_fits(void)
{
    lv_subject_t * opts = options_subject("A\nB\nC");
    lv_subject_t * sel = helix_test_subject_int(2);
    TEST_ASSERT_EQUAL(LV_RESULT_OK, lv_xml_register_subject(NULL, "dd_sel", sel));
    ASSERT_XML_REGISTERS("dd_opts_sel",
                         "<component><view name=\"r\">"
                         "<lv_dropdown name=\"dd\" bind_value=\"dd_sel\" bind_options=\"dd_opts\"/>"
                         "</view></component>");
    lv_obj_t * dd = ASSERT_NAMED(XML_CREATE(helix_test_env_screen(), "dd_opts_sel", NULL), "dd");

    TEST_ASSERT_EQUAL(2, lv_dropdown_get_selected(dd));
    lv_subject_set_string(opts, "P\nQ\nR\nS");
    TEST_ASSERT_EQUAL(2, lv_dropdown_get_selected(dd));
    lv_subject_set_string(opts, "only");
    TEST_ASSERT_EQUAL(0, lv_dropdown_get_selected(dd));
}

static void test_an_empty_bind_options_or_bind_value_is_no_binding(void)
{
    log_capture_start();
    ASSERT_XML_REGISTERS("dd_opts_empty",
                         "<component><view name=\"r\"><lv_dropdown name=\"dd\" options=\"K\" bind_options=\"\" bind_value=\"\"/>"
                         "</view></component>");
    lv_obj_t * dd = ASSERT_NAMED(XML_CREATE(helix_test_env_screen(), "dd_opts_empty", NULL), "dd");
    log_capture_stop();
    TEST_ASSERT_EQUAL_STRING("K", lv_dropdown_get_options(dd));
    TEST_ASSERT_FALSE_MESSAGE(log_contains("doesn't exist"), g_log_buf);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_bind_options_fills_and_follows_the_subject);
    RUN_TEST(test_new_options_keep_a_selection_that_still_fits);
    RUN_TEST(test_an_empty_bind_options_or_bind_value_is_no_binding);
    return UNITY_END();
}
