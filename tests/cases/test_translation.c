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

/* Log capture: lv_xml_register_translation_from_data() returns LV_RESULT_OK for
 * every per-row problem, so the warning is the only observable difference. */
#include "helpers/helix_log_capture.h"
#include "helpers/helix_test_env.h"
#include "helpers/helix_test_pump.h"
#include "helpers/xml_assert.h"

#if LV_USE_TRANSLATION

#include <others/translation/lv_translation.h>
#include <core/lv_global.h>
#include <misc/lv_ll.h>

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
 * Helpers
 *--------------------------------------------------------------------------*/

/**
 * How many translation packs are currently registered.
 *
 * There is no public accessor, and an EMPTY pack has no behavioural signature
 * at all, so the orphan-pack test below reaches into the global list directly.
 * That is the only way to see the leak it guards.
 */
static uint32_t pack_count(void)
{
    return lv_ll_get_len(&LV_GLOBAL_DEFAULT()->translation_packs_ll);
}

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

/**
 * A rejected registration must not leave a pack behind.
 *
 * lv_xml_register_translation_from_data() used to call
 * lv_translation_add_dynamic() BEFORE parsing and had no way to undo it -
 * lv_translation has no remove-a-pack API - so every malformed registration
 * left an empty orphan in packs_ll that lv_translation_get() then walked past
 * on every single lookup for the rest of the process. The document is now
 * checked for well-formedness first and the pack is created only if that pass
 * succeeds.
 *
 * The pack count is the assertion because an EMPTY pack has no other
 * observable: it contributes no languages and no tags, so every lookup answers
 * identically with or without it. That is precisely what made the leak silent.
 */
static void test_a_rejected_registration_leaves_no_orphan_pack(void)
{
    uint32_t before = pack_count();

    TEST_ASSERT_EQUAL_INT(LV_RESULT_INVALID,
                          (int)lv_xml_register_translation_from_data(
                              "<translations languages=\"en\">"
                              "  <translation tag=\"x\" en=\"X\"/>"
                              "</translations"));
    TEST_ASSERT_EQUAL_INT(LV_RESULT_INVALID,
                          (int)lv_xml_register_translation_from_data("not xml at all <<< &"));
    TEST_ASSERT_EQUAL_INT(LV_RESULT_INVALID,
                          (int)lv_xml_register_translation_from_file(TRANS_MALFORMED));

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(before, pack_count(),
                                     "a rejected registration left an orphan pack behind");

    /* The control: a GOOD registration must still create exactly one pack, so
     * the fix cannot be "never create a pack". */
    register_pack(PACK_COMPLETE);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(before + 1, pack_count(),
                                     "a valid registration did not create a pack");

    lv_translation_set_language("en");
    ASSERT_TR("dog", "Dog");
}

/*---------------------------------------------------------------------------
 * A filesystem driver that fails its reads while still reporting a full count
 *
 * lv_xml_register_translation_from_file() checked only `rn != file_size` and
 * threw the lv_fs_read() result away. A driver that fails the read but leaves
 * the count at the requested size therefore got its garbage buffer parsed as
 * XML. lv_malloc() does not zero, so "garbage" here is whatever was on the
 * heap - the real-world version of this is a short/failed read on a flaky SD
 * card handing the parser uninitialised memory.
 *
 * The driver below is the smallest thing that reproduces that: every read
 * answers LV_FS_RES_HW_ERR and sets *br to the full request.
 *--------------------------------------------------------------------------*/

#define BADFS_LETTER 'Q'

static void * badfs_open_cb(lv_fs_drv_t * drv, const char * path, lv_fs_mode_t mode)
{
    LV_UNUSED(drv);
    LV_UNUSED(path);
    LV_UNUSED(mode);
    static int dummy_handle;
    return &dummy_handle;
}

static lv_fs_res_t badfs_close_cb(lv_fs_drv_t * drv, void * file_p)
{
    LV_UNUSED(drv);
    LV_UNUSED(file_p);
    return LV_FS_RES_OK;
}

static lv_fs_res_t badfs_read_cb(lv_fs_drv_t * drv, void * file_p, void * buf, uint32_t btr, uint32_t * br)
{
    LV_UNUSED(drv);
    LV_UNUSED(file_p);
    LV_UNUSED(buf);
    *br = btr; /* claims a complete read ... */
    return LV_FS_RES_HW_ERR; /* ... and reports it failed */
}

static lv_fs_res_t badfs_seek_cb(lv_fs_drv_t * drv, void * file_p, uint32_t pos, lv_fs_whence_t whence)
{
    LV_UNUSED(drv);
    LV_UNUSED(file_p);
    LV_UNUSED(pos);
    LV_UNUSED(whence);
    return LV_FS_RES_OK;
}

static lv_fs_res_t badfs_tell_cb(lv_fs_drv_t * drv, void * file_p, uint32_t * pos_p)
{
    LV_UNUSED(drv);
    LV_UNUSED(file_p);
    *pos_p = 64; /* a non-zero "file size" so a buffer is actually allocated */
    return LV_FS_RES_OK;
}

static lv_fs_drv_t badfs_drv;

static void badfs_register(void)
{
    lv_fs_drv_init(&badfs_drv);
    badfs_drv.letter = BADFS_LETTER;
    badfs_drv.open_cb = badfs_open_cb;
    badfs_drv.close_cb = badfs_close_cb;
    badfs_drv.read_cb = badfs_read_cb;
    badfs_drv.seek_cb = badfs_seek_cb;
    badfs_drv.tell_cb = badfs_tell_cb;
    lv_fs_drv_register(&badfs_drv);
}

/**
 * A failed read is a failed registration, even when the byte count looks right.
 *
 * The count-only check accepted this and handed an uninitialised lv_malloc()
 * buffer to expat. Whether that then "parsed" was up to the heap contents, so
 * the old code's outcome here was not even deterministic.
 */
static void test_a_failed_read_is_rejected_even_when_the_byte_count_matches(void)
{
    badfs_register();

    uint32_t before = pack_count();

    log_capture_start();
    lv_result_t res = lv_xml_register_translation_from_file("Q:anything.xml");
    log_capture_stop();

    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_RESULT_INVALID, (int)res,
                                  "a read that reported LV_FS_RES_HW_ERR was accepted");
    TEST_ASSERT_TRUE_MESSAGE(log_contains("Couldn't read"),
                             "a failed read was not reported");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(before, pack_count(),
                                     "a failed read still created a pack");
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
    RUN_TEST(test_a_rejected_registration_leaves_no_orphan_pack);
    RUN_TEST(test_a_failed_read_is_rejected_even_when_the_byte_count_matches);

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
