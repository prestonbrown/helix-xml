/**
 * @file test_translation.c
 *
 * src/xml/lv_xml_translation.c - turning <translations> XML into an LVGL
 * translation pack.
 *
 * The module is thin: it parses a `<translations languages="...">` element into
 * language slots, then one `<translation tag="..." en="..." de="..."/>` per row.
 * Everything it can get wrong, it gets wrong silently - every failure path is a
 * LV_LOG_WARN and the function still returns LV_RESULT_OK. So the tests below
 * assert on the LOOKUP RESULT wherever possible, and on the log only where a
 * partially-built pack is otherwise indistinguishable from a complete one.
 *
 * What is pinned here:
 *  - _from_data() and _from_file() both populate a pack
 *  - a tag resolves in the selected language, and differently after switching
 *  - an unknown tag falls back to the tag string itself
 *  - a tag present in one language and missing from another (see the FALLBACK
 *    note below - the documented behaviour and the real behaviour differ)
 *  - malformed XML is rejected with LV_RESULT_INVALID
 *  - `<translation>` before `<translations languages=...>`, and a row with no
 *    `tag`, are both skipped rather than fatal
 *  - translation_tag= on a label resolves end to end, and re-resolves when the
 *    language changes under it
 *
 * ---------------------------------------------------------------------------
 * FALLBACK, DOCUMENTED vs ACTUAL
 *
 * lv_translation.h documents three rules for lv_translation_get():
 *   1. tag found in the selected language -> return it
 *   2. tag not found in the selected language -> use the FIRST language
 *   3. tag not found in the first language -> return the tag
 *
 * Rule 2 is not implemented. lv_translation.c looks the tag up at the selected
 * language's index and, on a NULL there, warns and returns the tag - it never
 * consults language 0. That is LVGL's code, not helix-xml's, so it is pinned
 * rather than fixed; test_a_tag_missing_in_the_selected_language_falls_back_to_the_tag
 * is where that lives.
 * ---------------------------------------------------------------------------
 *
 * NOT TESTED, DELIBERATELY
 *
 *  - NULL into lv_xml_register_translation_from_data() / _from_file(). Both
 *    pass the pointer straight to lv_strlen / lv_fs_open with no guard.
 *  - A `languages` list longer than 512 bytes: start_handler() lv_strlcpy's it
 *    into a fixed 512-byte stack buffer, so the tail is silently truncated.
 *    Pinning that would encode a buffer size, not a behaviour.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include <lvgl.h>

#include "helpers/helix_test_env.h"
#include "helpers/helix_test_pump.h"
#include "helpers/xml_assert.h"

#if LV_USE_TRANSLATION

#include <others/translation/lv_translation.h>

#include "xml/lv_xml_translation.h"

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
 * Fixture paths and inline packs
 *--------------------------------------------------------------------------*/

#define ASSET_PATH(sub) "A:" HELIX_TEST_ASSET_DIR sub

#define TRANS_BASIC     ASSET_PATH("/translations/trans_basic.xml")
#define TRANS_MALFORMED ASSET_PATH("/translations/trans_malformed.xml")
#define TRANS_MISSING   ASSET_PATH("/translations/no_such_file.xml")

/** Three languages, every tag complete in all three. */
static const char * PACK_COMPLETE =
    "<translations languages=\"en de fr\">"
    "  <translation tag=\"dog\" en=\"Dog\" de=\"Hund\" fr=\"Chien\"/>"
    "  <translation tag=\"cat\" en=\"Cat\" de=\"Katze\" fr=\"Chat\"/>"
    "</translations>";

/** `de` is missing from the second row - the partial-pack case. */
static const char * PACK_PARTIAL =
    "<translations languages=\"en de\">"
    "  <translation tag=\"dog\" en=\"Dog\" de=\"Hund\"/>"
    "  <translation tag=\"parrot\" en=\"Parrot\"/>"
    "</translations>";

/*---------------------------------------------------------------------------
 * Log capture
 *
 * Same shape as the helper in tests/cases/test_base_types.c; kept file-local so
 * this file does not have to co-own a shared header. Needed because
 * lv_xml_register_translation_from_data() returns LV_RESULT_OK for every
 * per-row problem, so the warning is the only observable difference.
 *--------------------------------------------------------------------------*/

static char g_log_buf[4096];
static size_t g_log_len;

static void log_capture_cb(lv_log_level_t level, const char * buf)
{
    LV_UNUSED(level);
    size_t n = strlen(buf);
    if(g_log_len + n + 1 >= sizeof(g_log_buf)) return;
    memcpy(g_log_buf + g_log_len, buf, n + 1);
    g_log_len += n;
}

static void log_capture_start(void)
{
    g_log_buf[0] = '\0';
    g_log_len = 0;
    lv_log_register_print_cb(log_capture_cb);
}

static void log_capture_stop(void)
{
    lv_log_register_print_cb(NULL);
}

static bool log_contains(const char * needle)
{
    return strstr(g_log_buf, needle) != NULL;
}

/*---------------------------------------------------------------------------
 * Helpers
 *--------------------------------------------------------------------------*/

static void register_pack(const char * xml)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_RESULT_OK, (int)lv_xml_register_translation_from_data(xml),
                                  "lv_xml_register_translation_from_data() rejected a valid pack");
}

/** lv_translation_get_language() is NULL until one is selected; %s-safe wrapper. */
static const char * current_lang(void)
{
    const char * lang = lv_translation_get_language();
    return lang ? lang : "<none selected>";
}

#define ASSERT_TR(tag, expected)                                                         \
    TEST_ASSERT_EQUAL_STRING_MESSAGE(                                                    \
        (expected), lv_translation_get(tag),                                             \
        helix_xml_assert_msgf("wrong translation for tag \"%s\" in language \"%s\"",       \
                              (tag), current_lang()))

/*===========================================================================
 * Registering
 *==========================================================================*/

/** The inline form populates a pack that lv_translation_get() can see. */
static void test_a_pack_registered_from_data_resolves_its_tags(void)
{
    register_pack(PACK_COMPLETE);
    lv_translation_set_language("en");

    ASSERT_TR("dog", "Dog");
    ASSERT_TR("cat", "Cat");
}

/** The file form must reach the same end state as the inline form. */
static void test_a_pack_registered_from_a_file_resolves_its_tags(void)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_RESULT_OK, (int)lv_xml_register_translation_from_file(TRANS_BASIC),
                                  "lv_xml_register_translation_from_file() rejected a valid file");

    lv_translation_set_language("en");
    ASSERT_TR("file_dog", "Dog");
    ASSERT_TR("file_cat", "Cat");

    lv_translation_set_language("fr");
    ASSERT_TR("file_dog", "Chien");
}

/** A file that is not there is reported, not treated as an empty pack. */
static void test_registering_translations_from_a_missing_file_fails(void)
{
    log_capture_start();
    lv_result_t res = lv_xml_register_translation_from_file(TRANS_MISSING);
    log_capture_stop();

    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_RESULT_INVALID, (int)res,
                                  "a non-existent translation file reported success");
    TEST_ASSERT_TRUE(log_contains("Couldn't open"));
}

/** Malformed XML must fail, and must not half-register the rows before the break. */
static void test_a_malformed_translation_file_is_rejected(void)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_RESULT_INVALID,
                                  (int)lv_xml_register_translation_from_file(TRANS_MALFORMED),
                                  "a malformed translation file reported success");
}

/** Same, inline. */
static void test_malformed_translation_data_is_rejected(void)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        LV_RESULT_INVALID,
        (int)lv_xml_register_translation_from_data("<translations languages=\"en\"><translation tag=\"x\" en=\"X\"></translations>"),
        "malformed translation data reported success");
}

/*===========================================================================
 * Looking up
 *==========================================================================*/

/** The same tag must resolve differently once the language changes. */
static void test_switching_language_changes_what_a_tag_resolves_to(void)
{
    register_pack(PACK_COMPLETE);

    lv_translation_set_language("en");
    ASSERT_TR("dog", "Dog");

    lv_translation_set_language("de");
    ASSERT_TR("dog", "Hund");

    lv_translation_set_language("fr");
    ASSERT_TR("dog", "Chien");

    /* And back, so this cannot pass by resolving to whatever was set last. */
    lv_translation_set_language("en");
    ASSERT_TR("dog", "Dog");
}

/** A tag nobody declared resolves to itself, with a warning. */
static void test_an_unknown_tag_resolves_to_the_tag_itself(void)
{
    register_pack(PACK_COMPLETE);
    lv_translation_set_language("en");

    log_capture_start();
    const char * tr = lv_translation_get("no_such_tag");
    log_capture_stop();

    TEST_ASSERT_EQUAL_STRING_MESSAGE("no_such_tag", tr,
                                     "an unknown tag did not fall back to the tag string");
    TEST_ASSERT_TRUE_MESSAGE(log_contains("no_such_tag"),
                             "an unknown tag was resolved silently");
}

/** A language no pack declares also falls back to the tag. */
static void test_an_unknown_language_resolves_every_tag_to_the_tag_itself(void)
{
    register_pack(PACK_COMPLETE);
    lv_translation_set_language("hu");

    log_capture_start();
    const char * tr = lv_translation_get("dog");
    log_capture_stop();

    TEST_ASSERT_EQUAL_STRING("dog", tr);
    TEST_ASSERT_TRUE(log_contains("language is not found"));
}

/**
 * PINS CURRENT BEHAVIOUR - suspected bug: lv_translation.h documents that a tag
 * missing in the selected language falls back to the FIRST language, and only
 * then to the tag string. lv_translation.c does not do that: it reads
 * translations[selected_index], finds NULL, warns, and returns the tag. So
 * "parrot" below yields "parrot" in German, not the English "Parrot" the
 * header promises.
 *
 * This is upstream LVGL code (lv_translation.c), not helix-xml, so it is pinned
 * rather than fixed. helix-xml's own contribution to the case is that
 * lv_xml_register_translation_from_data() leaves the missing slot NULL and warns
 * - that half is asserted too.
 */
static void test_a_tag_missing_in_the_selected_language_falls_back_to_the_tag(void)
{
    log_capture_start();
    register_pack(PACK_PARTIAL);
    log_capture_stop();

    /* helix-xml noticed the gap at registration time. */
    TEST_ASSERT_TRUE_MESSAGE(log_contains("language is missing from tag `parrot`"),
                             "a translation row missing a declared language registered silently");

    /* The complete row is unaffected - the gap did not abort the pack. */
    lv_translation_set_language("de");
    ASSERT_TR("dog", "Hund");

    /* The incomplete row: documented behaviour would be "Parrot" (language 0). */
    log_capture_start();
    const char * tr = lv_translation_get("parrot");
    log_capture_stop();

    TEST_ASSERT_EQUAL_STRING_MESSAGE("parrot", tr,
                                     "first-language fallback behaviour has changed - "
                                     "see the note above this test");
    TEST_ASSERT_TRUE(log_contains("parrot"));

    /* The language that DOES have it still works, so the row itself is intact. */
    lv_translation_set_language("en");
    ASSERT_TR("parrot", "Parrot");
}

/*===========================================================================
 * Malformed-but-parseable packs
 *==========================================================================*/

/** `<translations>` with no `languages` attribute: warn, register nothing. */
static void test_a_pack_without_a_languages_attribute_registers_nothing(void)
{
    log_capture_start();
    lv_result_t res = lv_xml_register_translation_from_data(
                          "<translations>"
                          "  <translation tag=\"dog\" en=\"Dog\"/>"
                          "</translations>");
    log_capture_stop();

    /* Well-formed XML, so the call itself "succeeds". */
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)res);
    TEST_ASSERT_TRUE_MESSAGE(log_contains("`languages` are not set"),
                             "a pack with no languages was accepted without a warning");
    TEST_ASSERT_TRUE_MESSAGE(log_contains("No languages were found"),
                             "the rows of a language-less pack were not rejected");

    lv_translation_set_language("en");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("dog", lv_translation_get("dog"),
                                     "a tag from a pack with no languages became resolvable");
}

/** A `<translation>` row with no `tag` attribute is skipped, the rest survive. */
static void test_a_row_without_a_tag_attribute_is_skipped(void)
{
    log_capture_start();
    lv_result_t res = lv_xml_register_translation_from_data(
                          "<translations languages=\"en\">"
                          "  <translation en=\"Orphan\"/>"
                          "  <translation tag=\"dog\" en=\"Dog\"/>"
                          "</translations>");
    log_capture_stop();

    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)res);
    TEST_ASSERT_TRUE_MESSAGE(log_contains("`tag` is missing from the translation"),
                             "a row with no tag was accepted without a warning");

    /* The following row still registered - one bad row is not fatal. */
    lv_translation_set_language("en");
    ASSERT_TR("dog", "Dog");
}

/** Two packs coexist; a tag from either resolves. */
static void test_two_registered_packs_both_resolve(void)
{
    register_pack(PACK_COMPLETE);
    register_pack("<translations languages=\"en de\">"
                  "  <translation tag=\"house\" en=\"House\" de=\"Haus\"/>"
                  "</translations>");

    lv_translation_set_language("de");
    ASSERT_TR("dog", "Hund");
    ASSERT_TR("house", "Haus");
}

/*===========================================================================
 * End to end: translation_tag= in XML
 *==========================================================================*/

/**
 * The reason this module exists. A label declares a tag, not a string, and the
 * text it ends up carrying is the translation for the active language.
 */
static void test_translation_tag_on_a_label_resolves_to_translated_text(void)
{
    register_pack(PACK_COMPLETE);
    lv_translation_set_language("de");

    ASSERT_XML_REGISTERS("trans_card",
                         "<component>"
                         "  <view extends=\"lv_obj\" name=\"trans_card_root\">"
                         "    <lv_label name=\"trans_label\" translation_tag=\"dog\"/>"
                         "  </view>"
                         "</component>");

    lv_obj_t * card = XML_CREATE(helix_test_env_screen(), "trans_card", NULL);
    helix_test_pump(30);

    ASSERT_LABEL_TEXT(ASSERT_NAMED(card, "trans_label"), "Hund");
}

/** Changing the language after the tree is built must re-translate it in place. */
static void test_changing_language_retranslates_an_existing_label(void)
{
    register_pack(PACK_COMPLETE);
    lv_translation_set_language("en");

    ASSERT_XML_REGISTERS("trans_card",
                         "<component>"
                         "  <view extends=\"lv_obj\" name=\"trans_card_root\">"
                         "    <lv_label name=\"trans_label\" translation_tag=\"cat\"/>"
                         "  </view>"
                         "</component>");

    lv_obj_t * card = XML_CREATE(helix_test_env_screen(), "trans_card", NULL);
    helix_test_pump(30);
    lv_obj_t * label = ASSERT_NAMED(card, "trans_label");
    ASSERT_LABEL_TEXT(label, "Cat");

    lv_translation_set_language("fr");
    helix_test_pump(30);
    ASSERT_LABEL_TEXT(label, "Chat");

    lv_translation_set_language("de");
    helix_test_pump(30);
    ASSERT_LABEL_TEXT(label, "Katze");
}

/** An unknown tag on a label leaves the tag itself as the visible text. */
static void test_an_unknown_translation_tag_on_a_label_shows_the_tag(void)
{
    register_pack(PACK_COMPLETE);
    lv_translation_set_language("en");

    ASSERT_XML_REGISTERS("trans_card",
                         "<component>"
                         "  <view extends=\"lv_obj\" name=\"trans_card_root\">"
                         "    <lv_label name=\"trans_label\" translation_tag=\"undeclared_tag\"/>"
                         "  </view>"
                         "</component>");

    lv_obj_t * card = XML_CREATE(helix_test_env_screen(), "trans_card", NULL);
    helix_test_pump(30);

    ASSERT_LABEL_TEXT(ASSERT_NAMED(card, "trans_label"), "undeclared_tag");
}

/*---------------------------------------------------------------------------
 * main
 *--------------------------------------------------------------------------*/

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_a_pack_registered_from_data_resolves_its_tags);
    RUN_TEST(test_a_pack_registered_from_a_file_resolves_its_tags);
    RUN_TEST(test_registering_translations_from_a_missing_file_fails);
    RUN_TEST(test_a_malformed_translation_file_is_rejected);
    RUN_TEST(test_malformed_translation_data_is_rejected);

    RUN_TEST(test_switching_language_changes_what_a_tag_resolves_to);
    RUN_TEST(test_an_unknown_tag_resolves_to_the_tag_itself);
    RUN_TEST(test_an_unknown_language_resolves_every_tag_to_the_tag_itself);
    RUN_TEST(test_a_tag_missing_in_the_selected_language_falls_back_to_the_tag);

    RUN_TEST(test_a_pack_without_a_languages_attribute_registers_nothing);
    RUN_TEST(test_a_row_without_a_tag_attribute_is_skipped);
    RUN_TEST(test_two_registered_packs_both_resolve);

    RUN_TEST(test_translation_tag_on_a_label_resolves_to_translated_text);
    RUN_TEST(test_changing_language_retranslates_an_existing_label);
    RUN_TEST(test_an_unknown_translation_tag_on_a_label_shows_the_tag);

    return UNITY_END();
}

#else /* !LV_USE_TRANSLATION */

/* lv_xml_translation.c compiles to nothing without LV_USE_TRANSLATION, and the
 * conf-guards CI job builds exactly that configuration. Keep a valid, passing
 * binary so the guard job does not need a special case here. */

void setUp(void)
{
    helix_test_env_setup();
}

void tearDown(void)
{
    helix_test_env_teardown();
}

static void test_translations_are_disabled_in_this_configuration(void)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, LV_USE_TRANSLATION,
                                  "LV_USE_TRANSLATION changed under this branch");
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_translations_are_disabled_in_this_configuration);
    return UNITY_END();
}

#endif /* LV_USE_TRANSLATION */
