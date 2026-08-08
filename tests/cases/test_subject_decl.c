/**
 * @file test_subject_decl.c
 *
 * Declaring subjects in a component's `<subjects>` section.
 *
 * Two syntaxes reach process_subject_element() in src/xml/lv_xml_component.c:
 *
 *   <int    name="x" value="7"/>              tag-per-type (upstream LVGL)
 *   <subject name="x" type="int" value="7"/>  type attribute (HelixScreen's
 *                                             ui_xml convention)
 *
 * The function receives the TAG NAME as its `type` argument, so the second form
 * used to arrive as type="subject", match no branch, and leave the subject at
 * LV_SUBJECT_TYPE_INVALID. Nothing failed: registration succeeded, every bind
 * against it silently stuck at its default and every lv_subject_set_* was a
 * no-op. process_subject_element() now prefers the `type` ATTRIBUTE when one is
 * present, and these tests hold that down.
 *
 * Asserting on subject->type alone would not be enough - a subject can be the
 * right type and still be dead - so each type test also round-trips a value
 * through it. An INVALID subject no-ops both the get and the set.
 *
 * NOT COVERED HERE, deliberately: what happens to these subjects when the
 * component is unregistered. That is scope-ownership behaviour and it already
 * has tests - see test_unregistering_a_component_removes_its_scope_owned_subject
 * and test_unregistering_a_component_does_not_free_a_borrowed_subject in
 * cases/test_component.c.
 *
 * SPDX-License-Identifier: MIT
 */

#include <lvgl.h>

#include "helpers/helix_log_capture.h"
#include "helpers/helix_test_env.h"
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

#define SUBJECT_COMPONENT(decls)                                                         \
    "<component>"                                                                        \
    "  <subjects>" decls "</subjects>"                                                   \
    "  <view extends=\"lv_obj\"><lv_obj/></view>"                                        \
    "</component>"

/** Register a component and evaluate to its scope. */
#define REGISTER_SCOPE(name, xml)                                                        \
    __extension__({                                                                      \
        ASSERT_XML_REGISTERS((name), (xml));                                             \
        lv_xml_component_scope_t * hx_scope_ = lv_xml_component_get_scope((name));       \
        TEST_ASSERT_NOT_NULL_MESSAGE(hx_scope_,                                          \
                                     "component registered but has no scope");           \
        hx_scope_;                                                                       \
    })

/** Look a subject up in a scope and assert it is there. */
#define ASSERT_SUBJECT(scope, subject_name)                                              \
    __extension__({                                                                      \
        lv_subject_t * hx_subj_ = lv_xml_get_subject((scope), (subject_name));           \
        TEST_ASSERT_NOT_NULL_MESSAGE(                                                    \
            hx_subj_,                                                                    \
            helix_xml_assert_msgf("subject \"%s\" was not registered by its <subjects> "  \
                                  "declaration", (subject_name)));                       \
        hx_subj_;                                                                        \
    })

/*===========================================================================
 * The type= attribute form
 *==========================================================================*/

/**
 * The regression this file exists for. `type="int"` must produce a real int
 * subject that carries its initial value and accepts writes.
 */
static void test_an_int_subject_declared_with_a_type_attribute_is_typed_and_usable(void)
{
    lv_xml_component_scope_t * scope =
        REGISTER_SCOPE("subj_int",
                       SUBJECT_COMPONENT("<subject name=\"s_int\" type=\"int\" value=\"7\"/>"));

    lv_subject_t * s = ASSERT_SUBJECT(scope, "s_int");

    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_SUBJECT_TYPE_INT, (int)s->type,
                                  "the type= attribute did not take effect - the subject is not an int");
    TEST_ASSERT_EQUAL_INT_MESSAGE(7, lv_subject_get_int(s),
                                  "value= was not applied to the subject");

    /* An INVALID subject would no-op this and read back 7. */
    lv_subject_set_int(s, 42);
    TEST_ASSERT_EQUAL_INT_MESSAGE(42, lv_subject_get_int(s),
                                  "the subject did not accept a write - it is not really typed");
}

/** Same for strings, which take a different init path (two heap buffers). */
static void test_a_string_subject_declared_with_a_type_attribute_is_typed_and_usable(void)
{
    lv_xml_component_scope_t * scope =
        REGISTER_SCOPE("subj_str",
                       SUBJECT_COMPONENT("<subject name=\"s_str\" type=\"string\" value=\"hi\"/>"));

    lv_subject_t * s = ASSERT_SUBJECT(scope, "s_str");

    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_SUBJECT_TYPE_STRING, (int)s->type,
                                  "the type= attribute did not take effect - the subject is not a string");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("hi", lv_subject_get_string(s),
                                     "value= was not applied to the subject");

    lv_subject_copy_string(s, "there");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("there", lv_subject_get_string(s),
                                     "the subject did not accept a write - it is not really typed");
}

/** And for colors, the third branch the attribute form has to reach. */
static void test_a_color_subject_declared_with_a_type_attribute_is_typed_and_usable(void)
{
    lv_xml_component_scope_t * scope =
        REGISTER_SCOPE("subj_color",
                       SUBJECT_COMPONENT("<subject name=\"s_col\" type=\"color\" value=\"0xFF0000\"/>"));

    lv_subject_t * s = ASSERT_SUBJECT(scope, "s_col");

    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_SUBJECT_TYPE_COLOR, (int)s->type,
                                  "the type= attribute did not take effect - the subject is not a color");

    lv_color_t c = lv_subject_get_color(s);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0xFF, c.red, "value= produced the wrong red channel");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x00, c.green, "value= produced the wrong green channel");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x00, c.blue, "value= produced the wrong blue channel");
}

/** Several declarations in one <subjects> block all land, independently. */
static void test_several_subjects_in_one_block_are_all_registered(void)
{
    lv_xml_component_scope_t * scope =
        REGISTER_SCOPE("subj_many",
                       SUBJECT_COMPONENT("<subject name=\"s_a\" type=\"int\"    value=\"1\"/>"
                                         "<subject name=\"s_b\" type=\"int\"    value=\"2\"/>"
                                         "<subject name=\"s_c\" type=\"string\" value=\"c\"/>"));

    TEST_ASSERT_EQUAL_INT(1, lv_subject_get_int(ASSERT_SUBJECT(scope, "s_a")));
    TEST_ASSERT_EQUAL_INT(2, lv_subject_get_int(ASSERT_SUBJECT(scope, "s_b")));
    TEST_ASSERT_EQUAL_STRING("c", lv_subject_get_string(ASSERT_SUBJECT(scope, "s_c")));
}

/*===========================================================================
 * The tag-per-type form, and which one wins
 *==========================================================================*/

/**
 * The upstream form carries no type= at all, so the fix must not have broken it:
 * with the attribute absent, the TAG NAME is still what selects the type.
 */
static void test_the_tag_per_type_declaration_form_still_works(void)
{
    lv_xml_component_scope_t * scope =
        REGISTER_SCOPE("subj_tagform",
                       SUBJECT_COMPONENT("<int    name=\"s_int\" value=\"7\"/>"
                                         "<string name=\"s_str\" value=\"hi\"/>"));

    lv_subject_t * si = ASSERT_SUBJECT(scope, "s_int");
    lv_subject_t * ss = ASSERT_SUBJECT(scope, "s_str");

    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_SUBJECT_TYPE_INT, (int)si->type,
                                  "the tag-per-type form stopped selecting the type");
    TEST_ASSERT_EQUAL_INT(7, lv_subject_get_int(si));
    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_SUBJECT_TYPE_STRING, (int)ss->type,
                                  "the tag-per-type form stopped selecting the type");
    TEST_ASSERT_EQUAL_STRING("hi", lv_subject_get_string(ss));
}

/**
 * When both are present the ATTRIBUTE wins. Nobody should write this, but it is
 * the precedence rule the fix installed, and it is the reason `<subject ...>`
 * works at all: for that tag the tag name is the useless string "subject".
 */
static void test_the_type_attribute_overrides_the_tag_name(void)
{
    lv_xml_component_scope_t * scope =
        REGISTER_SCOPE("subj_override",
                       SUBJECT_COMPONENT("<int name=\"s_x\" type=\"string\" value=\"hi\"/>"));

    lv_subject_t * s = ASSERT_SUBJECT(scope, "s_x");

    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_SUBJECT_TYPE_STRING, (int)s->type,
                                  "the tag name won over an explicit type= attribute");
    TEST_ASSERT_EQUAL_STRING("hi", lv_subject_get_string(s));
}

/*===========================================================================
 * Bad declarations
 *==========================================================================*/

/**
 * A type nobody implements matches no branch, so the lv_zalloc'd subject would
 * be registered UNINITIALISED (LV_SUBJECT_TYPE_INVALID == 0): every bind against
 * it sticks at its default and every lv_subject_set_* is a silent no-op - the
 * exact failure mode `type=` exists to prevent, reachable through a plain typo
 * (`type="integer"`).
 *
 * So: warn, and register nothing. A missing subject is diagnosable at the
 * binding site; an invalid one looks registered and quietly does nothing. One
 * bad row must not take the rest of the block with it.
 */
static void test_a_subject_with_an_unknown_type_warns_and_is_not_registered(void)
{
    log_capture_start();
    lv_xml_component_scope_t * scope =
        REGISTER_SCOPE("subj_badtype",
                       SUBJECT_COMPONENT("<subject name=\"s_bad\" type=\"integer\" value=\"7\"/>"
                                         "<subject name=\"s_ok\" type=\"int\" value=\"1\"/>"));
    log_capture_stop();

    TEST_ASSERT_NULL_MESSAGE(lv_xml_get_subject(scope, "s_bad"),
                             "a subject with an unimplemented type= was registered anyway, so "
                             "every bind against it silently does nothing");
    TEST_ASSERT_TRUE_MESSAGE(log_contains("s_bad"),
                             "an unknown subject type was not reported by name");
    TEST_ASSERT_TRUE_MESSAGE(log_contains("integer"),
                             "the offending type was not named in the diagnostic");

    /* One bad row is not fatal - the following declaration still registered. */
    TEST_ASSERT_EQUAL_INT(1, lv_subject_get_int(ASSERT_SUBJECT(scope, "s_ok")));
}

/** No name= to register it under: skipped, with a warning. */
static void test_a_subject_without_a_name_is_skipped(void)
{
    log_capture_start();
    ASSERT_XML_REGISTERS("subj_noname",
                         SUBJECT_COMPONENT("<subject type=\"int\" value=\"7\"/>"
                                           "<subject name=\"s_ok\" type=\"int\" value=\"1\"/>"));
    log_capture_stop();

    TEST_ASSERT_TRUE_MESSAGE(log_contains("'name' is missing from a subject"),
                             "a nameless subject declaration was accepted silently");

    /* One bad row is not fatal - the following declaration still registered. */
    lv_xml_component_scope_t * scope = lv_xml_component_get_scope("subj_noname");
    TEST_ASSERT_NOT_NULL(scope);
    TEST_ASSERT_EQUAL_INT(1, lv_subject_get_int(ASSERT_SUBJECT(scope, "s_ok")));
}

/**
 * No value= means no initial state, and the engine refuses rather than guessing
 * a zero. The subject must not exist afterwards.
 */
static void test_a_subject_without_a_value_is_skipped(void)
{
    log_capture_start();
    ASSERT_XML_REGISTERS("subj_novalue",
                         SUBJECT_COMPONENT("<subject name=\"s_novalue\" type=\"int\"/>"
                                           "<subject name=\"s_ok\" type=\"int\" value=\"1\"/>"));
    log_capture_stop();

    TEST_ASSERT_TRUE_MESSAGE(log_contains("'value' is missing from a subject"),
                             "a valueless subject declaration was accepted silently");

    lv_xml_component_scope_t * scope = lv_xml_component_get_scope("subj_novalue");
    TEST_ASSERT_NOT_NULL(scope);
    TEST_ASSERT_NULL_MESSAGE(lv_xml_get_subject(scope, "s_novalue"),
                             "a subject with no value= was registered anyway");
    TEST_ASSERT_EQUAL_INT(1, lv_subject_get_int(ASSERT_SUBJECT(scope, "s_ok")));
}

/*===========================================================================
 * End to end
 *==========================================================================*/

/**
 * The point of declaring a subject in XML: something binds to it. A typed
 * subject drives the binding; an INVALID one would leave the label empty and
 * ignore the write.
 */
static void test_a_declared_subject_drives_a_binding_in_the_same_component(void)
{
    ASSERT_XML_REGISTERS("subj_bound",
                         "<component>"
                         "  <subjects>"
                         "    <subject name=\"s_msg\" type=\"string\" value=\"first\"/>"
                         "  </subjects>"
                         "  <view extends=\"lv_obj\">"
                         "    <lv_label name=\"msg\" bind_text=\"s_msg\"/>"
                         "  </view>"
                         "</component>");

    lv_obj_t * root = XML_CREATE(helix_test_env_screen(), "subj_bound", NULL);
    lv_obj_t * msg = ASSERT_NAMED(root, "msg");
    ASSERT_LABEL_TEXT(msg, "first");

    lv_xml_component_scope_t * scope = lv_xml_component_get_scope("subj_bound");
    TEST_ASSERT_NOT_NULL(scope);
    lv_subject_copy_string(ASSERT_SUBJECT(scope, "s_msg"), "second");

    ASSERT_LABEL_TEXT(msg, "second");
}

/*---------------------------------------------------------------------------
 * main
 *--------------------------------------------------------------------------*/

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_an_int_subject_declared_with_a_type_attribute_is_typed_and_usable);
    RUN_TEST(test_a_string_subject_declared_with_a_type_attribute_is_typed_and_usable);
    RUN_TEST(test_a_color_subject_declared_with_a_type_attribute_is_typed_and_usable);
    RUN_TEST(test_several_subjects_in_one_block_are_all_registered);

    RUN_TEST(test_the_tag_per_type_declaration_form_still_works);
    RUN_TEST(test_the_type_attribute_overrides_the_tag_name);

    RUN_TEST(test_a_subject_with_an_unknown_type_warns_and_is_not_registered);
    RUN_TEST(test_a_subject_without_a_name_is_skipped);
    RUN_TEST(test_a_subject_without_a_value_is_skipped);

    RUN_TEST(test_a_declared_subject_drives_a_binding_in_the_same_component);

    return UNITY_END();
}
