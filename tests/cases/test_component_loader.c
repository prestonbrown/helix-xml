/**
 * @file test_component_loader.c
 *
 * lv_xml_set_component_loader(): a component nobody registered up front is
 * registered the first time a lookup names it - an lv_xml_create() call, a
 * nested tag, or an `extends=` base - and a name with no definition still fails
 * exactly as an unregistered one does without a loader.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include "helpers/helix_log_capture.h"
#include "helpers/helix_test_env.h"
#include "helpers/xml_assert.h"

/* The definitions the test loader can find, standing in for files on disk. */
static const char * LAZY_INNER_XML =
    "<component>"
    "  <view extends=\"lv_obj\">"
    "    <lv_label name=\"inner_label\" text=\"from lazy_inner\"/>"
    "  </view>"
    "</component>";

static const char * LAZY_OUTER_XML =
    "<component>"
    "  <view extends=\"lv_obj\">"
    "    <lazy_inner name=\"nested\"/>"
    "  </view>"
    "</component>";

static const char * LAZY_BASE_XML =
    "<component>"
    "  <view extends=\"lv_obj\">"
    "    <lv_label name=\"base_label\" text=\"from lazy_base\"/>"
    "  </view>"
    "</component>";

static const char * LAZY_DERIVED_XML =
    "<component>"
    "  <view extends=\"lazy_base\">"
    "    <lv_label name=\"derived_label\" text=\"from lazy_derived\"/>"
    "  </view>"
    "</component>";

static const struct {
    const char * name;
    const char ** xml;
} sources[] = {
    {"lazy_inner", &LAZY_INNER_XML},
    {"lazy_outer", &LAZY_OUTER_XML},
    {"lazy_base", &LAZY_BASE_XML},
    {"lazy_derived", &LAZY_DERIVED_XML},
};

#define MAX_CALLS 16
static const char * calls[MAX_CALLS];
static uint32_t call_count;

static void test_loader(const char * name)
{
    if(call_count < MAX_CALLS) calls[call_count] = lv_strdup(name);
    call_count++;
    for(size_t i = 0; i < sizeof(sources) / sizeof(sources[0]); i++) {
        if(strcmp(sources[i].name, name) == 0) {
            lv_xml_register_component_from_data(name, *sources[i].xml);
            return;
        }
    }
}

static uint32_t calls_for(const char * name)
{
    uint32_t n = 0;
    for(uint32_t i = 0; i < call_count && i < MAX_CALLS; i++) {
        if(strcmp(calls[i], name) == 0) n++;
    }
    return n;
}

void setUp(void)
{
    helix_test_env_setup();
    call_count = 0;
    lv_xml_set_component_loader(test_loader);
}

void tearDown(void)
{
    lv_xml_set_component_loader(NULL);
    for(uint32_t i = 0; i < call_count && i < MAX_CALLS; i++) lv_free((void *)calls[i]);
    helix_test_env_teardown();
}

typedef struct {
    const char * name;
    bool found;
} presence_t;

static void presence_cb(const char * name, void * ud)
{
    presence_t * p = ud;
    if(strcmp(name, p->name) == 0) p->found = true;
}

/* Asks the registry without going through get_scope(), which would load. */
static bool is_registered(const char * name)
{
    presence_t p = {name, false};
    lv_xml_component_foreach(presence_cb, &p);
    return p.found;
}

static void test_create_registers_an_unregistered_component_on_first_use(void)
{
    TEST_ASSERT_FALSE(is_registered("lazy_inner"));

    lv_obj_t * obj = XML_CREATE(helix_test_env_screen(), "lazy_inner", NULL);
    ASSERT_LABEL_TEXT(ASSERT_NAMED(obj, "inner_label"), "from lazy_inner");
    TEST_ASSERT_TRUE(is_registered("lazy_inner"));
    TEST_ASSERT_EQUAL_UINT32(1, calls_for("lazy_inner"));

    /* Registered now: a second create is an ordinary hit. */
    XML_CREATE(helix_test_env_screen(), "lazy_inner", NULL);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, calls_for("lazy_inner"),
                                     "the loader ran again for a component it already registered");
}

static void test_a_nested_tag_registers_its_component_on_first_use(void)
{
    lv_obj_t * outer = XML_CREATE(helix_test_env_screen(), "lazy_outer", NULL);

    lv_obj_t * nested = ASSERT_NAMED(outer, "nested");
    ASSERT_LABEL_TEXT(ASSERT_NAMED(nested, "inner_label"), "from lazy_inner");
    TEST_ASSERT_EQUAL_UINT32(1, calls_for("lazy_outer"));
    TEST_ASSERT_EQUAL_UINT32(1, calls_for("lazy_inner"));
}

static void test_an_extends_base_registers_on_first_use(void)
{
    lv_obj_t * obj = XML_CREATE(helix_test_env_screen(), "lazy_derived", NULL);

    ASSERT_LABEL_TEXT(ASSERT_NAMED(obj, "base_label"), "from lazy_base");
    ASSERT_LABEL_TEXT(ASSERT_NAMED(obj, "derived_label"), "from lazy_derived");
    TEST_ASSERT_TRUE(is_registered("lazy_base"));
}

static void test_a_name_with_no_definition_fails_as_loudly_as_without_a_loader(void)
{
    log_capture_start();
    lv_obj_t * obj = lv_xml_create(helix_test_env_screen(), "no_such_component", NULL);
    log_capture_stop();

    TEST_ASSERT_NULL(obj);
    TEST_ASSERT_TRUE_MESSAGE(log_contains("no_such_component"),
                             "a create naming nothing loadable must still be reported");
    TEST_ASSERT_EQUAL_UINT32(1, calls_for("no_such_component"));
    TEST_ASSERT_FALSE(is_registered("no_such_component"));
}

static void test_registered_components_and_globals_never_reach_the_loader(void)
{
    ASSERT_XML_REGISTERS("eager", LAZY_INNER_XML);

    XML_CREATE(helix_test_env_screen(), "eager", NULL);
    TEST_ASSERT_NOT_NULL(lv_xml_component_get_scope("globals"));

    TEST_ASSERT_EQUAL_UINT32(0, call_count);
}

static void test_unregistering_an_unloaded_name_does_not_load_it(void)
{
    TEST_ASSERT_EQUAL_INT(LV_RESULT_INVALID, lv_xml_component_unregister("lazy_inner"));

    TEST_ASSERT_EQUAL_UINT32(0, call_count);
    TEST_ASSERT_FALSE(is_registered("lazy_inner"));
}

static void test_a_built_in_widget_name_never_reaches_the_loader(void)
{
    XML_CREATE(helix_test_env_screen(), "lazy_inner", NULL);

    TEST_ASSERT_NULL(lv_xml_component_get_scope("lv_label"));
    TEST_ASSERT_NULL(lv_xml_component_get_scope("lv_obj"));

    TEST_ASSERT_EQUAL_UINT32(0, calls_for("lv_label"));
    TEST_ASSERT_EQUAL_UINT32(0, calls_for("lv_obj"));
}

static void test_find_scope_answers_without_loading(void)
{
    TEST_ASSERT_NULL(lv_xml_component_find_scope("lazy_inner"));
    TEST_ASSERT_EQUAL_UINT32(0, call_count);
    TEST_ASSERT_FALSE(is_registered("lazy_inner"));

    XML_CREATE(helix_test_env_screen(), "lazy_inner", NULL);

    TEST_ASSERT_NOT_NULL(lv_xml_component_find_scope("lazy_inner"));
    TEST_ASSERT_EQUAL_UINT32(1, call_count);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_create_registers_an_unregistered_component_on_first_use);
    RUN_TEST(test_a_nested_tag_registers_its_component_on_first_use);
    RUN_TEST(test_an_extends_base_registers_on_first_use);
    RUN_TEST(test_a_name_with_no_definition_fails_as_loudly_as_without_a_loader);
    RUN_TEST(test_registered_components_and_globals_never_reach_the_loader);
    RUN_TEST(test_unregistering_an_unloaded_name_does_not_load_it);
    RUN_TEST(test_a_built_in_widget_name_never_reaches_the_loader);
    RUN_TEST(test_find_scope_answers_without_loading);
    return UNITY_END();
}
