/**
 * @file test_load.c
 *
 * src/xml/lv_xml_load.c - loading XML packs off a filesystem.
 *
 * The whole point of this module is that a consuming app ships a directory of
 * .xml files and calls one function. Everything interesting is therefore about
 * what happens to the OTHER files when one of them is wrong: a pack is loaded
 * once, at startup, and a single bad file must not cost the app its entire UI.
 *
 * What is pinned here:
 *  - every component in a directory registers, including one level down
 *  - the root TAG decides what a file becomes (component / screen / globals vs
 *    translations), not the filename
 *  - non-.xml files are skipped
 *  - a malformed file warns and the scan carries on - both shapes of malformed,
 *    see below
 *  - a missing directory, a file passed where a directory belongs, and a path
 *    with no driver letter all fail cleanly
 *  - lv_xml_set_default_asset_path() prefixes file-based image sources, and
 *    lv_xml_load_all_from_path() sets it to the directory it scanned
 *
 * TWO SHAPES OF MALFORMED. load_from_path() sniffs the root tag with its own
 * expat pass that calls XML_StopParser() on the FIRST start element. So it can
 * only ever see breakage that occurs at or before that first tag
 * (broken_syntax.xml). A file whose start tag is fine but whose body is broken
 * (broken_body.xml) sails past the sniffer and is rejected later, inside
 * lv_xml_register_component_from_data(). Both are exercised.
 *
 * ---------------------------------------------------------------------------
 * NOT TESTED, AND WHY
 *
 *  - lv_xml_load_all_from_data(), lv_xml_load_all_from_file() and
 *    lv_xml_unload(). All three are inside `#if LV_USE_FS_FROGFS` in both
 *    lv_xml_load.c and lv_xml_load.h, and tests/lv_conf.h has
 *    LV_USE_FS_FROGFS 0 - they do not exist in this build. They are not
 *    "untested because nobody wrote a test": reaching them needs a frogfs
 *    binary IMAGE (magic + entry table + hash table, see
 *    lvgl/src/libs/frogfs/src/frogfs_format.h) produced by frogfs' external
 *    Python packing tool, committed as a binary fixture. Regeneration is
 *    triggered by a frogfs format MAJOR bump, not by an LVGL bump - frogfs.c
 *    validates ver_major only. The format's CRC32 footer is never checked at
 *    load time, so a fixture does not have to produce a correct one.
 *
 *    Deliberately kept rather than deleted: lv_xml_unload() is this module's
 *    ONLY teardown path - a pack loaded through lv_xml_load_all_from_path()
 *    cannot be un-registered at all - and the blob route is upstream LVGL's
 *    documented way to ship XML to a target with no writable filesystem. The
 *    code costs nothing while LV_USE_FS_FROGFS is 0. Upstream shipped these
 *    untested too.
 *
 *  - NULL into lv_xml_load_all_from_path(). It does `path[0]` and
 *    `lv_strlen(path)` with no guard, then hands the pointer to
 *    lv_fs_dir_open(). No guard exists, so there is no behaviour to pin.
 *
 *  - An unreadable (chmod 000) file inside a pack. Permissions are not a
 *    property a committed fixture can carry, and a test that chmod()s a file in
 *    the source tree would leave the tree dirty if it failed mid-way.
 * ---------------------------------------------------------------------------
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include <lvgl.h>
#if LV_USE_TRANSLATION
    #include <others/translation/lv_translation.h>
#endif

/* Log capture: the scan reports every per-file problem through LV_LOG_WARN and
 * keeps returning LV_RESULT_OK, so the log is the only channel that
 * distinguishes "skipped a bad file" from "never saw it". */
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
 * Fixture paths
 *
 * HELIX_TEST_ASSET_DIR is injected by CMake as an absolute path, so every path
 * below is absolute and none of these tests care about the process cwd.
 * "A:" is LV_FS_STDIO_LETTER from tests/lv_conf.h.
 *--------------------------------------------------------------------------*/

#define ASSET_PATH(sub) "A:" HELIX_TEST_ASSET_DIR sub

#define PACK_GOOD    ASSET_PATH("/pack")
#define PACK_BAD     ASSET_PATH("/pack_with_bad")
#define PACK_UNKNOWN ASSET_PATH("/pack_unknown")
#define PACK_MISSING ASSET_PATH("/pack_does_not_exist")
#define FILE_NOT_DIR ASSET_PATH("/pack/load_card.xml")
#define FILE_MISSING ASSET_PATH("/pack/no_such_file.xml")

/*---------------------------------------------------------------------------
 * Helpers
 *--------------------------------------------------------------------------*/

/** Assert a component name is present in the registry. */
static void assert_registered(const char * name)
{
    TEST_ASSERT_NOT_NULL_MESSAGE(
        lv_xml_component_get_scope(name),
        helix_xml_assert_msgf("component \"%s\" did not register from the pack", name));
}

/** Assert a component name is absent from the registry. */
static void assert_not_registered(const char * name)
{
    TEST_ASSERT_NULL_MESSAGE(
        lv_xml_component_get_scope(name),
        helix_xml_assert_msgf("component \"%s\" registered but should not have", name));
}

/**
 * A scope to hang probe images off. lv_xml_register_image() falls back to the
 * "globals" scope when passed NULL, and there is no globals component in these
 * tests, so every asset-path probe needs a real scope of its own.
 */
static lv_xml_component_scope_t * make_probe_scope(const char * name)
{
    ASSERT_XML_REGISTERS(name, "<component><view extends=\"lv_obj\"/></component>");
    lv_xml_component_scope_t * scope = lv_xml_component_get_scope(name);
    TEST_ASSERT_NOT_NULL(scope);
    return scope;
}

/*===========================================================================
 * lv_xml_load_all_from_path - the happy path
 *==========================================================================*/

/**
 * Every component file in the directory tree must end up in the registry,
 * under its filename minus the extension, and must actually be creatable.
 *
 * The pack deliberately contains a mixture: two plain components, a <screen>,
 * a <translations> file and a .txt. Only the first three are components.
 */
static void test_every_component_in_a_directory_registers(void)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_RESULT_OK, (int)lv_xml_load_all_from_path(PACK_GOOD),
                                  "scanning a directory of valid XML reported failure");

    assert_registered("load_card");
    assert_registered("load_badge");
    assert_registered("load_screen_main");

    /* Registered is not the same as usable - build one and read a value the
     * file declared. */
    lv_obj_t * card = XML_CREATE(helix_test_env_screen(), "load_card", NULL);
    ASSERT_LABEL_TEXT(ASSERT_NAMED(card, "load_card_title"), "Card from disk");

    lv_obj_t * badge = XML_CREATE(helix_test_env_screen(), "load_badge", NULL);
    ASSERT_LABEL_TEXT(ASSERT_NAMED(badge, "load_badge_text"), "Badge from disk");
}

/** load_all_recursive() must descend into subdirectories, not just list one level. */
static void test_the_scan_recurses_into_subdirectories(void)
{
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_load_all_from_path(PACK_GOOD));

    assert_registered("load_nested");

    lv_obj_t * nested = XML_CREATE(helix_test_env_screen(), "load_nested", NULL);
    ASSERT_LABEL_TEXT(ASSERT_NAMED(nested, "load_nested_text"), "Nested from disk");
}

/** A .txt sitting in the pack must be ignored, not parsed. */
static void test_non_xml_files_are_skipped(void)
{
    log_capture_start();
    lv_result_t res = lv_xml_load_all_from_path(PACK_GOOD);
    log_capture_stop();

    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)res);

    /* It must not have become a component ... */
    assert_not_registered("load_notes");

    /* ... and it must not have been fed to expat either. Every other file in
     * this pack is valid, and load_notes.txt is not well-formed XML, so if the
     * extension check regressed the scan would have logged a parse error (or,
     * had it somehow parsed, an unknown root tag). A completely silent scan is
     * the proof. */
    TEST_ASSERT_FALSE_MESSAGE(log_contains("XML parsing error"),
                              "a non-.xml file in the pack was parsed instead of skipped");
    TEST_ASSERT_FALSE_MESSAGE(log_contains("Unknown XML type"),
                              "a non-.xml file in the pack reached the root-tag classifier");
}

/**
 * The root TAG decides what a file becomes. load_strings.xml is <translations>,
 * so it must land in the translation packs and must NOT become a component
 * called "load_strings" just because of its filename.
 */
#if LV_USE_TRANSLATION
static void test_a_translations_file_in_a_pack_becomes_translations_not_a_component(void)
{
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_load_all_from_path(PACK_GOOD));

    assert_not_registered("load_strings");

    lv_translation_set_language("en");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Hello from pack", lv_translation_get("pack_greeting"),
                                     "the <translations> file in the pack was not registered");

    lv_translation_set_language("de");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Hallo vom Pack", lv_translation_get("pack_greeting"),
                                     "the second language from the pack's translations is missing");
}
#endif

/*===========================================================================
 * The case that matters: one bad file must not take the pack down
 *==========================================================================*/

/**
 * THE test for this module.
 *
 * pack_with_bad/ holds two good components and two broken files, one broken at
 * the root tag and one broken only in its body. After the scan:
 *   - the call still reports success (load_from_path() returns void; only
 *     directory-level errors can fail a scan)
 *   - both good components are registered AND creatable
 *   - neither broken file is registered - a half-registered component would be
 *     worse than none
 *   - both failures were reported
 */
static void test_a_malformed_file_does_not_abort_the_directory_scan(void)
{
    log_capture_start();
    lv_result_t res = lv_xml_load_all_from_path(PACK_BAD);
    log_capture_stop();

    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_RESULT_OK, (int)res,
                                  "one malformed file failed the whole directory scan");

    assert_registered("ok_alpha");
    assert_registered("ok_beta");

    lv_obj_t * alpha = XML_CREATE(helix_test_env_screen(), "ok_alpha", NULL);
    ASSERT_LABEL_TEXT(ASSERT_NAMED(alpha, "ok_alpha_text"), "Alpha survived");

    lv_obj_t * beta = XML_CREATE(helix_test_env_screen(), "ok_beta", NULL);
    ASSERT_LABEL_TEXT(ASSERT_NAMED(beta, "ok_beta_text"), "Beta survived");

    assert_not_registered("broken_syntax");
    assert_not_registered("broken_body");

    /* The two rejections are distinguishable in the log, which is the only
     * evidence that both paths ran: load_from_path() punctuates its message
     * with a colon, lv_xml_register_component_from_data() with a semicolon. */
    TEST_ASSERT_TRUE_MESSAGE(log_contains("XML parsing error:"),
                             "the file broken at its root tag was skipped silently");
    TEST_ASSERT_TRUE_MESSAGE(log_contains("XML parsing error;"),
                             "the file with a valid root tag but a broken body was not reported");
}

/** Well-formed XML with an unrecognised root tag is skipped, with a warning. */
static void test_an_unknown_root_tag_is_skipped_with_a_warning(void)
{
    log_capture_start();
    lv_result_t res = lv_xml_load_all_from_path(PACK_UNKNOWN);
    log_capture_stop();

    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)res);
    assert_not_registered("mystery_root");
    TEST_ASSERT_TRUE_MESSAGE(log_contains("Unknown XML type found in pack"),
                             "an unrecognised root tag was skipped without a warning");
}

/*===========================================================================
 * Paths that cannot be scanned
 *==========================================================================*/

/** A directory that is not there fails, loudly, and registers nothing. */
static void test_scanning_a_path_that_does_not_exist_fails(void)
{
    log_capture_start();
    lv_result_t res = lv_xml_load_all_from_path(PACK_MISSING);
    log_capture_stop();

    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_RESULT_INVALID, (int)res,
                                  "scanning a non-existent directory reported success");
    TEST_ASSERT_TRUE_MESSAGE(log_contains("Couldn't open directory"),
                             "a non-existent directory was rejected without a warning");

    /* Nothing from any pack may have leaked in. */
    assert_not_registered("load_card");
    assert_not_registered("ok_alpha");
}

/**
 * A path to an existing FILE is not a directory. lv_fs_dir_open() must reject
 * it rather than the scan somehow loading the single file.
 */
static void test_scanning_a_file_instead_of_a_directory_fails(void)
{
    log_capture_start();
    lv_result_t res = lv_xml_load_all_from_path(FILE_NOT_DIR);
    log_capture_stop();

    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_RESULT_INVALID, (int)res,
                                  "a file path was accepted as a directory to scan");
    TEST_ASSERT_TRUE(log_contains("Couldn't open directory"));

    /* And in particular it did NOT get loaded as a one-file pack. */
    assert_not_registered("load_card");
}

/** A file that does not exist is not a directory either. */
static void test_scanning_a_file_that_does_not_exist_fails(void)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_RESULT_INVALID, (int)lv_xml_load_all_from_path(FILE_MISSING),
                                  "a non-existent file path was accepted as a directory to scan");
}

/**
 * Paths reach lv_fs, so they need a driver letter. A bare POSIX path - the
 * mistake every integrator makes once - must fail rather than silently scan
 * nothing and report success.
 */
static void test_scanning_a_path_without_a_driver_letter_fails(void)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_RESULT_INVALID,
                                  (int)lv_xml_load_all_from_path(HELIX_TEST_ASSET_DIR "/pack"),
                                  "a path with no lv_fs driver letter was accepted");
    assert_not_registered("load_card");
}

/*===========================================================================
 * lv_xml_set_default_asset_path
 *==========================================================================*/

/**
 * The asset path is a plain prefix glued onto file-based image sources at
 * registration time. This is what makes a pack's XML able to say
 * src="img/logo.png" and still resolve wherever the pack was installed.
 */
static void test_the_default_asset_path_prefixes_file_image_sources(void)
{
    lv_xml_component_scope_t * scope = make_probe_scope("asset_probe");

    lv_xml_set_default_asset_path("A:/opt/ui/");
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_register_image(scope, "logo", "img/logo.png"));

    TEST_ASSERT_EQUAL_STRING_MESSAGE("A:/opt/ui/img/logo.png",
                                     (const char *)lv_xml_get_image(scope, "logo"),
                                     "the default asset path was not prepended to a file image source");
}

/** Changing the prefix changes what later registrations resolve to. */
static void test_changing_the_default_asset_path_changes_later_resolutions(void)
{
    lv_xml_component_scope_t * scope = make_probe_scope("asset_probe");

    lv_xml_set_default_asset_path("A:/first/");
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_register_image(scope, "one", "a.png"));

    lv_xml_set_default_asset_path("A:/second/");
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_register_image(scope, "two", "b.png"));

    /* The first registration keeps the prefix that was live when it was made -
     * the prefix is baked in at registration, not resolved lazily. */
    TEST_ASSERT_EQUAL_STRING("A:/first/a.png", (const char *)lv_xml_get_image(scope, "one"));
    TEST_ASSERT_EQUAL_STRING("A:/second/b.png", (const char *)lv_xml_get_image(scope, "two"));
}

/** NULL is guarded and means "no prefix" - lv_xml_set_default_asset_path() checks. */
static void test_a_null_default_asset_path_means_no_prefix(void)
{
    lv_xml_component_scope_t * scope = make_probe_scope("asset_probe");

    lv_xml_set_default_asset_path("A:/somewhere/");
    lv_xml_set_default_asset_path(NULL);

    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_register_image(scope, "bare", "c.png"));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("c.png", (const char *)lv_xml_get_image(scope, "bare"),
                                     "NULL asset path did not reset the prefix to empty");
}

/* A compiled-in descriptor in STATIC storage - the shape an embedded app
 * actually ships, and the one that used to be unusable here because scope
 * teardown lv_free()'d every image src unconditionally. Zeroed by definition,
 * so its first byte is < 0x20 and lv_image_src_get_type() classifies it as
 * LV_IMAGE_SRC_VARIABLE rather than a filename. */
static const lv_image_dsc_t STATIC_IMAGE_DSC;

/**
 * Only FILE sources get the prefix. An in-memory lv_image_dsc_t is stored
 * verbatim - prefixing a pointer would corrupt it - and, because the engine
 * never copied it, teardown must not free it either.
 *
 * The descriptor below is deliberately `static const`. That is the whole point:
 * lv_xml_register_image() only lv_strdup()s FILE sources, so freeing every
 * image src at scope teardown handed rodata to lv_free() and segfaulted in
 * tlsf's block_link_next(). Getting through tearDown() is half the assertion.
 */
static void test_the_default_asset_path_does_not_touch_in_memory_image_sources(void)
{
    lv_xml_component_scope_t * scope = make_probe_scope("asset_probe");

    lv_xml_set_default_asset_path("A:/opt/ui/");
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK,
                          (int)lv_xml_register_image(scope, "mem", &STATIC_IMAGE_DSC));

    TEST_ASSERT_EQUAL_PTR_MESSAGE(&STATIC_IMAGE_DSC, lv_xml_get_image(scope, "mem"),
                                  "an in-memory image source was rewritten by the asset path");
}

/**
 * A successful scan points the asset path at the directory it scanned, adding
 * the trailing '/' if the caller left it off. That is how relative src= paths
 * inside a pack resolve against the pack.
 */
static void test_a_successful_scan_points_the_asset_path_at_the_scanned_directory(void)
{
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_load_all_from_path(PACK_GOOD));

    lv_xml_component_scope_t * scope = make_probe_scope("asset_probe");
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_register_image(scope, "logo", "img/logo.png"));

    /* PACK_GOOD has no trailing separator, so the scan must have supplied one. */
    TEST_ASSERT_EQUAL_STRING_MESSAGE(PACK_GOOD "/img/logo.png",
                                     (const char *)lv_xml_get_image(scope, "logo"),
                                     "the scan did not set the asset path to the scanned directory");
}

/** A path that already ends in '/' must not gain a second one. */
static void test_a_scanned_path_that_already_ends_in_a_separator_is_not_doubled(void)
{
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_load_all_from_path(PACK_GOOD "/"));

    lv_xml_component_scope_t * scope = make_probe_scope("asset_probe");
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_register_image(scope, "logo", "img/logo.png"));

    TEST_ASSERT_EQUAL_STRING_MESSAGE(PACK_GOOD "/img/logo.png",
                                     (const char *)lv_xml_get_image(scope, "logo"),
                                     "a trailing separator on the scanned path was doubled");
}

/**
 * A scan that fails outright must leave the default asset path alone.
 *
 * lv_xml_load_all_from_path() used to call lv_xml_set_default_asset_path()
 * BEFORE opening the directory, so a failed scan still repointed the prefix at
 * a directory that does not exist - an app probing a list of candidate pack
 * locations silently resolved every later image against the last candidate it
 * tried. The directory is now probed first and the prefix is written only once
 * the scan is known to be able to start.
 */
static void test_a_failed_scan_leaves_the_default_asset_path_alone(void)
{
    lv_xml_set_default_asset_path("A:/known/good/");

    TEST_ASSERT_EQUAL_INT(LV_RESULT_INVALID, (int)lv_xml_load_all_from_path(PACK_MISSING));

    lv_xml_component_scope_t * scope = make_probe_scope("asset_probe");
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_register_image(scope, "logo", "logo.png"));

    TEST_ASSERT_EQUAL_STRING_MESSAGE("A:/known/good/logo.png",
                                     (const char *)lv_xml_get_image(scope, "logo"),
                                     "a failed scan clobbered the previously established asset path");
}

/**
 * The same guarantee for the other two ways a scan can fail to start: a path
 * that names a plain file, and a path with no driver letter. Neither reaches
 * the point of loading anything, so neither may move the prefix.
 */
static void test_a_scan_of_a_file_or_a_letterless_path_leaves_the_asset_path_alone(void)
{
    lv_xml_set_default_asset_path("A:/known/good/");

    TEST_ASSERT_EQUAL_INT(LV_RESULT_INVALID, (int)lv_xml_load_all_from_path(FILE_NOT_DIR));

    lv_xml_component_scope_t * scope = make_probe_scope("asset_probe");
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_register_image(scope, "logo", "logo.png"));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("A:/known/good/logo.png",
                                     (const char *)lv_xml_get_image(scope, "logo"),
                                     "scanning a file instead of a directory moved the asset path");

    TEST_ASSERT_EQUAL_INT(LV_RESULT_INVALID, (int)lv_xml_load_all_from_path("/no/driver/letter"));

    lv_xml_component_scope_t * scope2 = make_probe_scope("asset_probe2");
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_register_image(scope2, "logo", "logo.png"));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("A:/known/good/logo.png",
                                     (const char *)lv_xml_get_image(scope2, "logo"),
                                     "scanning a letterless path moved the asset path");
}

/*---------------------------------------------------------------------------
 * main
 *--------------------------------------------------------------------------*/

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_every_component_in_a_directory_registers);
    RUN_TEST(test_the_scan_recurses_into_subdirectories);
    RUN_TEST(test_non_xml_files_are_skipped);
#if LV_USE_TRANSLATION
    RUN_TEST(test_a_translations_file_in_a_pack_becomes_translations_not_a_component);
#endif

    RUN_TEST(test_a_malformed_file_does_not_abort_the_directory_scan);
    RUN_TEST(test_an_unknown_root_tag_is_skipped_with_a_warning);

    RUN_TEST(test_scanning_a_path_that_does_not_exist_fails);
    RUN_TEST(test_scanning_a_file_instead_of_a_directory_fails);
    RUN_TEST(test_scanning_a_file_that_does_not_exist_fails);
    RUN_TEST(test_scanning_a_path_without_a_driver_letter_fails);

    RUN_TEST(test_the_default_asset_path_prefixes_file_image_sources);
    RUN_TEST(test_changing_the_default_asset_path_changes_later_resolutions);
    RUN_TEST(test_a_null_default_asset_path_means_no_prefix);
    RUN_TEST(test_the_default_asset_path_does_not_touch_in_memory_image_sources);
    RUN_TEST(test_a_successful_scan_points_the_asset_path_at_the_scanned_directory);
    RUN_TEST(test_a_scanned_path_that_already_ends_in_a_separator_is_not_doubled);
    RUN_TEST(test_a_failed_scan_leaves_the_default_asset_path_alone);
    RUN_TEST(test_a_scan_of_a_file_or_a_letterless_path_leaves_the_asset_path_alone);

    return UNITY_END();
}
