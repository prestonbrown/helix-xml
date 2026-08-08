/**
 * @file test_malformed.c
 *
 * Malformed-input robustness for the helix-xml engine.
 *
 * ---------------------------------------------------------------------------
 * THE CONTRACT EVERY TEST IN THIS FILE ASSERTS
 *
 *   The engine WARNS and SURVIVES. It never crashes, never hangs, and - the
 *   part that actually matters - it is never left wedged for the NEXT
 *   document. A parser that shrugs off one bad file but then rejects every
 *   good file after it is still broken.
 *
 * So no test here stops at "did not crash": that passes trivially and proves
 * nothing. Every row asserts three things:
 *
 *   1. the exact registration/creation OUTCOME (accepted or rejected),
 *   2. that the engine LOGGED about it - or, where it deliberately says
 *      nothing, that it stayed exactly silent,
 *   3. that a KNOWN-GOOD component registered under THE SAME NAME immediately
 *      afterwards still parses, still builds, and still carries its text.
 *
 * (3) is not decoration. It is the hot-reload scenario this whole file exists
 * for: HELIX_HOT_RELOAD polls the ui_xml directory on a background thread and will
 * happily read a file the editor is halfway through writing. The engine sees a
 * truncated document, then ~500 ms later sees the finished one under the same
 * component name. If the first parse leaves a half-registered scope, a live
 * `<repeat>` capture, or a dangling parent stack behind, the second parse is
 * where the user's UI dies.
 * ---------------------------------------------------------------------------
 *
 * SHAPE: malformed inputs are table rows - {desc, xml, expect_register,
 * expect_log} - fed through one driver, run_malformed_case(), which applies
 * the survive-and-still-works check uniformly. Hand-written tests are reserved
 * for the cases whose interesting part is the resulting TREE rather than the
 * parse verdict.
 *
 * ---------------------------------------------------------------------------
 * DELIBERATELY NOT TESTED
 *
 *  - NULL into lv_xml_register_component_from_data(): it opens with
 *    lv_streq(name, "globals") and hands xml_def straight to lv_strlen. Both
 *    dereference unconditionally. Malformed STRINGS are the subject here, not
 *    NULL pointers.
 *  - Element nesting deep enough to exhaust LVGL's heap. There is no
 *    engine-imposed depth limit (see
 *    test_deeply_nested_elements_have_no_engine_limit_and_stay_usable), so the
 *    ceiling is LV_MEM_SIZE, and hitting it does NOT produce a clean error: at
 *    ~8192 levels lv_obj_allocate_spec_attr trips LV_USE_ASSERT_MALLOC and the
 *    process takes SIGABRT. That is an allocator assert under this file's
 *    4 MB test heap, not a parser defect, and encoding it would pin a number
 *    that moves with lv_conf.h. The depths exercised below stay 4x clear of it.
 *  - Style selectors of 256+ characters. Now bounded and covered directly at
 *    the converter in tests/cases/test_base_types.c
 *    (test_style_selector_longer_than_the_buffer_is_truncated_not_overread);
 *    routing one through a whole document adds no coverage.
 * ---------------------------------------------------------------------------
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <string.h>

/* Log capture: a single malformed input can produce several messages, one of
 * which quotes a 300-character tag name back at us - see the buffer size note
 * in the header. */
/* The custom-widget test registers a processor of its own, which means reaching
 * the parser state and lv_obj's shared apply the same way a real application
 * widget does. */
#include "xml/lv_xml_parser.h"
#include "xml/parsers/lv_xml_obj_parser.h"

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

/** The name every malformed input is registered under. Reused on purpose: the
 *  survive check re-registers THIS name with good XML, which is exactly what a
 *  hot reload does after a half-written save. */
#define SUBJECT_NAME "hot_reloaded_card"

/** The known-good document. Nothing in it depends on geometry or fonts. */
static const char * GOOD_XML =
    "<component>"
    "  <view extends=\"lv_obj\" name=\"good_root\">"
    "    <lv_label name=\"good_label\" text=\"OK\"/>"
    "  </view>"
    "</component>";

/*---------------------------------------------------------------------------
 * The survive-and-still-works check
 *--------------------------------------------------------------------------*/

/**
 * Prove the engine is still fully functional after whatever just happened.
 *
 * Three independent paths, because they wedge independently:
 *   - the COMPONENT path, under the same name the malformed input used (a
 *     failed registration must not leave the name claimed or half-built)
 *   - the resulting widget TREE (right parent, right child, right text)
 *   - the plain WIDGET path, lv_xml_create() straight onto a registered
 *     widget name, which does not go through a component scope at all
 */
static void assert_engine_still_works(const char * desc)
{
    lv_obj_t * screen = helix_test_env_screen();

    /* Clean while the XML engine is still up, then release the name - the
     * order tests/helpers/helix_test_env.h calls load-bearing. */
    lv_obj_clean(screen);
    lv_xml_component_unregister(SUBJECT_NAME);

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        LV_RESULT_OK,
        (int)lv_xml_register_component_from_data(SUBJECT_NAME, GOOD_XML),
        helix_xml_assert_msgf("after \"%s\": a KNOWN-GOOD component no longer registers - "
                              "the malformed input wedged the parser", desc));

    lv_obj_t * good = lv_xml_create(screen, SUBJECT_NAME, NULL);
    TEST_ASSERT_NOT_NULL_MESSAGE(
        good,
        helix_xml_assert_msgf("after \"%s\": a KNOWN-GOOD component registered but no longer "
                              "builds", desc));
    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        screen, lv_obj_get_parent(good),
        helix_xml_assert_msgf("after \"%s\": the good component was not parented to the screen - "
                              "the parent stack is corrupt", desc));

    lv_obj_t * label = lv_obj_find_by_name(good, "good_label");
    TEST_ASSERT_NOT_NULL_MESSAGE(
        label,
        helix_xml_assert_msgf("after \"%s\": the good component built but lost its named child",
                              desc));
    TEST_ASSERT_TRUE_MESSAGE(
        lv_obj_check_type(label, &lv_label_class),
        helix_xml_assert_msgf("after \"%s\": good_label is not a label", desc));
    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        "OK", lv_label_get_text(label),
        helix_xml_assert_msgf("after \"%s\": the good component built but lost its text", desc));

    /* The widget-processor registry is a separate list from the component
     * registry and can be wedged on its own, so exercise it too. */
    const char * attrs[] = {"name", "plain_label", "text", "still here", NULL, NULL};
    lv_obj_t * plain = lv_xml_create(screen, "lv_label", attrs);
    TEST_ASSERT_NOT_NULL_MESSAGE(
        plain,
        helix_xml_assert_msgf("after \"%s\": the plain widget path no longer creates", desc));
    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        "still here", lv_label_get_text(plain),
        helix_xml_assert_msgf("after \"%s\": the plain widget path lost its attributes", desc));

    helix_test_pump(20);

    lv_obj_clean(screen);
    lv_xml_component_unregister(SUBJECT_NAME);
}

/*---------------------------------------------------------------------------
 * Table driver
 *--------------------------------------------------------------------------*/

typedef struct {
    const char * desc;
    const char * xml;
    /** true if lv_xml_register_component_from_data() is expected to ACCEPT it. */
    bool expect_register_ok;
    /** substring the log must contain; NULL means "the engine must stay silent". */
    const char * expect_log;
} malformed_case_t;

/**
 * Feed one malformed document through registration (and creation, if it
 * registered), assert the outcome and the logging, then prove the engine still
 * works. Every row in this file goes through here.
 */
static void run_malformed_case(const malformed_case_t * c)
{
    lv_obj_t * screen = helix_test_env_screen();

    lv_obj_clean(screen);
    lv_xml_component_unregister(SUBJECT_NAME);

    log_capture_start();
    lv_result_t res = lv_xml_register_component_from_data(SUBJECT_NAME, c->xml);
    if(res == LV_RESULT_OK) {
        /* Creation is where a bad view_def actually detonates, so always take
         * this step when registration let the document through. */
        lv_xml_create(screen, SUBJECT_NAME, NULL);
    }
    helix_test_pump(20);
    log_capture_stop();

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        c->expect_register_ok ? (int)LV_RESULT_OK : (int)LV_RESULT_INVALID, (int)res,
        helix_xml_assert_msgf("\"%s\": wrong registration verdict", c->desc));

    if(c->expect_log) {
        TEST_ASSERT_TRUE_MESSAGE(
            log_contains(c->expect_log),
            helix_xml_assert_msgf("\"%s\": expected a log containing \"%s\", got: %.400s",
                                  c->desc, c->expect_log, g_log_buf));
    }
    else {
        TEST_ASSERT_EQUAL_STRING_MESSAGE(
            "", g_log_buf,
            helix_xml_assert_msgf("\"%s\": the engine was expected to stay silent", c->desc));
    }

    assert_engine_still_works(c->desc);
}

static void run_malformed_table(const malformed_case_t * table, size_t n)
{
    for(size_t i = 0; i < n; i++) run_malformed_case(&table[i]);
}

#define RUN_TABLE(t) run_malformed_table((t), sizeof(t) / sizeof((t)[0]))

/*---------------------------------------------------------------------------
 * Dynamic document builders
 *--------------------------------------------------------------------------*/

/** prefix + `n` copies of `fill` + suffix. Caller frees. */
static char * build_padded(const char * prefix, size_t n, char fill, const char * suffix)
{
    size_t pl = strlen(prefix), sl = strlen(suffix);
    char * s = malloc(pl + n + sl + 1);
    TEST_ASSERT_NOT_NULL_MESSAGE(s, "test harness out of memory building a padded document");
    memcpy(s, prefix, pl);
    memset(s + pl, fill, n);
    memcpy(s + pl + n, suffix, sl + 1);
    return s;
}

/** A component whose view nests `depth` lv_obj levels around a named label. */
static char * build_nested(int depth)
{
    size_t cap = (size_t)depth * 20 + 256;
    char * s = malloc(cap);
    TEST_ASSERT_NOT_NULL_MESSAGE(s, "test harness out of memory building a nested document");
    size_t at = 0;
    at += (size_t)snprintf(s + at, cap - at, "<component><view extends=\"lv_obj\" name=\"deep_root\">");
    for(int i = 0; i < depth; i++) at += (size_t)snprintf(s + at, cap - at, "<lv_obj>");
    at += (size_t)snprintf(s + at, cap - at, "<lv_label name=\"deep_leaf\" text=\"Deep\"/>");
    for(int i = 0; i < depth; i++) at += (size_t)snprintf(s + at, cap - at, "</lv_obj>");
    snprintf(s + at, cap - at, "</view></component>");
    return s;
}

/*---------------------------------------------------------------------------
 * Unclosed and mismatched tags
 *--------------------------------------------------------------------------*/

/**
 * An element left open, or closed by the wrong name, must be rejected at
 * registration with a parse error - never half-applied.
 *
 * Note the second row: expat reports `<lv_obj></lv_label>` as "mismatched tag",
 * not as two separate problems, so the engine cannot and does not try to
 * recover the intended tree. Rejection is the whole behaviour.
 */
static void test_unclosed_and_mismatched_tags_warn_and_leave_the_parser_usable(void)
{
    static const malformed_case_t table[] = {
        {
            "unclosed child element",
            "<component><view extends=\"lv_obj\"><lv_obj></view></component>",
            false, "XML parsing error"
        },
        {
            "closing tag names a different element",
            "<component><view extends=\"lv_obj\"><lv_obj></lv_label></view></component>",
            false, "mismatched tag"
        },
        {
            "self-closed element followed by its closing tag",
            "<component><view extends=\"lv_obj\" name=\"r\"><lv_obj/></lv_obj></view></component>",
            false, "mismatched tag"
        },
        {
            "closing tag before any opening tag",
            "</view><component><view extends=\"lv_obj\" name=\"r\"/></component>",
            false, "XML parsing error"
        },
        {
            "unclosed <view>, document ends",
            "<component><view extends=\"lv_obj\" name=\"r\">",
            false, "XML parsing error"
        },
        {
            "unclosed <repeat> body",
            "<component><view extends=\"lv_obj\" name=\"r\"><repeat count=\"3\">"
            "<lv_label name=\"x\" text=\"X\"/></view></component>",
            false, "mismatched tag"
        },
        {
            "unclosed comment swallows the rest of the document",
            "<component><view extends=\"lv_obj\" name=\"r\"><!-- nope </view></component>",
            false, "XML parsing error"
        },
        {
            "unterminated CDATA section",
            "<component><view extends=\"lv_obj\" name=\"r\"><lv_label name=\"a\">"
            "<![CDATA[oops</lv_label></view></component>",
            false, "unclosed CDATA section"
        },
        {
            "junk after the document element",
            "<component><view extends=\"lv_obj\" name=\"r\"/></component>trailing",
            false, "junk after document element"
        },
    };
    RUN_TABLE(table);
}

/*---------------------------------------------------------------------------
 * Truncation - the hot-reload case
 *--------------------------------------------------------------------------*/

/**
 * THE reason this file exists.
 *
 * A hot-reload poll can read a component file at any byte offset the editor
 * happens to have flushed. So do not pick a few cuts by hand: cut ONE valid
 * document at EVERY offset and require identical behaviour at all of them -
 * rejected, logged, and the parser still usable afterwards.
 *
 * "Logged" is asserted per offset. A silent rejection would be nearly as bad as
 * a crash: hot reload would keep serving the stale component with nothing in
 * the log to explain why the edit had no effect.
 */
static void test_truncated_document_warns_and_leaves_the_parser_usable(void)
{
    static const char * FULL =
        "<component><view extends=\"lv_obj\" name=\"card_root\">"
        "<lv_label name=\"card_title\" text=\"Hello\"/></view></component>";

    lv_obj_t * screen = helix_test_env_screen();
    const size_t full_len = strlen(FULL);
    char prefix[256];
    TEST_ASSERT_TRUE_MESSAGE(full_len < sizeof(prefix), "fixture outgrew the prefix buffer");

    /* Every proper prefix: mid-element, mid-attribute name, mid-attribute
     * value, mid-closing-tag, and every byte in between. */
    for(size_t cut = 1; cut < full_len; cut++) {
        memcpy(prefix, FULL, cut);
        prefix[cut] = '\0';

        lv_obj_clean(screen);
        lv_xml_component_unregister(SUBJECT_NAME);

        log_capture_start();
        lv_result_t res = lv_xml_register_component_from_data(SUBJECT_NAME, prefix);
        log_capture_stop();

        TEST_ASSERT_EQUAL_INT_MESSAGE(
            (int)LV_RESULT_INVALID, (int)res,
            helix_xml_assert_msgf("truncation at byte %u was accepted as a valid component: [%s]",
                                  (unsigned)cut, prefix));
        TEST_ASSERT_TRUE_MESSAGE(
            log_contains("XML parsing error"),
            helix_xml_assert_msgf("truncation at byte %u was rejected SILENTLY - hot reload would "
                                  "serve the stale component with nothing in the log: [%s]",
                                  (unsigned)cut, prefix));
        TEST_ASSERT_NULL_MESSAGE(
            lv_xml_component_get_scope(SUBJECT_NAME),
            helix_xml_assert_msgf("truncation at byte %u left a half-registered scope behind "
                                  "under the component name", (unsigned)cut));
    }

    /* And the finished file, arriving under the same name, must still work. */
    assert_engine_still_works("truncation sweep");

    /* The realistic loop: bad save, good save, over and over. Nothing may
     * accumulate across cycles. */
    for(int i = 0; i < 5; i++) {
        memcpy(prefix, FULL, 40);
        prefix[40] = '\0';

        lv_obj_clean(screen);
        lv_xml_component_unregister(SUBJECT_NAME);

        TEST_ASSERT_EQUAL_INT_MESSAGE(
            (int)LV_RESULT_INVALID,
            (int)lv_xml_register_component_from_data(SUBJECT_NAME, prefix),
            "a truncated save was accepted mid hot-reload cycle");
        assert_engine_still_works("hot-reload bad/good cycle");
    }
}

/*---------------------------------------------------------------------------
 * Malformed attribute syntax
 *--------------------------------------------------------------------------*/

/**
 * Broken attribute syntax is rejected at registration.
 *
 * The `>` row is the odd one out and is here deliberately: a bare `>` inside an
 * attribute value is LEGAL XML (only `<` and `&` are forbidden there), so the
 * engine must ACCEPT it silently and keep the character. See
 * test_stray_angle_brackets_in_attribute_values for the tree-level assertion.
 */
static void test_malformed_attribute_syntax_warns_and_leaves_the_parser_usable(void)
{
    static const malformed_case_t table[] = {
        {
            "attribute value missing its quotes",
            "<component><view extends=lv_obj></view></component>",
            false, "XML parsing error"
        },
        {
            "attribute missing its '='",
            "<component><view extends=\"lv_obj\"><lv_label \"x\"/></view></component>",
            false, "XML parsing error"
        },
        {
            "unterminated attribute value",
            "<component><view extends=\"lv_obj\"><lv_label text=\"He",
            false, "XML parsing error"
        },
        {
            "stray '<' inside an attribute value",
            "<component><view extends=\"lv_obj\"><lv_label text=\"a<b\"/></view></component>",
            false, "XML parsing error"
        },
        {
            "bare '&' inside an attribute value",
            "<component><view extends=\"lv_obj\" name=\"r\">"
            "<lv_label name=\"a\" text=\"a &amp b\"/></view></component>",
            false, "XML parsing error"
        },
        {
            "the same attribute given twice",
            "<component><view extends=\"lv_obj\" name=\"r\">"
            "<lv_label name=\"a\" text=\"first\" text=\"second\"/></view></component>",
            false, "duplicate attribute"
        },
    };
    RUN_TABLE(table);
}

/**
 * `<` is rejected, `>` is accepted and preserved verbatim.
 *
 * Worth its own test because the two look identical to a human writing XML by
 * hand, and the difference is only visible in the built tree.
 */
static void test_stray_angle_brackets_in_attribute_values(void)
{
    lv_obj_t * screen = helix_test_env_screen();

    log_capture_start();
    lv_result_t res = lv_xml_register_component_from_data(
                          SUBJECT_NAME,
                          "<component><view extends=\"lv_obj\" name=\"r\">"
                          "<lv_label name=\"gt\" text=\"a>b\"/></view></component>");
    lv_obj_t * root = (res == LV_RESULT_OK) ? lv_xml_create(screen, SUBJECT_NAME, NULL) : NULL;
    helix_test_pump(20);
    log_capture_stop();

    TEST_ASSERT_EQUAL_INT_MESSAGE((int)LV_RESULT_OK, (int)res,
                                  "'>' in an attribute value is legal XML but was rejected");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("", g_log_buf,
                                     "'>' in an attribute value is legal XML but was warned about");
    TEST_ASSERT_NOT_NULL_MESSAGE(root, "a component with '>' in an attribute value did not build");
    ASSERT_LABEL_TEXT(ASSERT_NAMED(root, "gt"), "a>b");

    assert_engine_still_works("'>' in an attribute value");
}

/*---------------------------------------------------------------------------
 * Unknown names, attributes and enum values
 *--------------------------------------------------------------------------*/

/**
 * An unknown widget name registers fine (nothing looks at tag names until
 * creation), logs loudly at creation, produces no object of its own - and
 * leaves everything AFTER it exactly where it was written.
 *
 * That last part is the whole point. The unknown start tag creates nothing, but
 * its close tag is still delivered and the end handler pops unconditionally, so
 * the start has to push a balancing frame. Without it the stack lost a level per
 * unknown element and the following sibling landed on the SCREEN instead of
 * inside the component - a quiet, nasty failure mode in a real UI (widgets
 * bleeding across panels at 0,0) from nothing worse than a stale binary.
 */
static void test_unknown_widget_name_warns_but_keeps_following_siblings_in_place(void)
{
    lv_obj_t * screen = helix_test_env_screen();

    log_capture_start();
    lv_result_t res = lv_xml_register_component_from_data(
                          SUBJECT_NAME,
                          "<component><view extends=\"lv_obj\" name=\"r\">"
                          "<lv_nope name=\"ghost\"/>"
                          "<lv_label name=\"after_the_ghost\" text=\"A\"/>"
                          "</view></component>");
    lv_obj_t * root = (res == LV_RESULT_OK) ? lv_xml_create(screen, SUBJECT_NAME, NULL) : NULL;
    helix_test_pump(20);
    log_capture_stop();

    TEST_ASSERT_EQUAL_INT_MESSAGE((int)LV_RESULT_OK, (int)res,
                                  "an unknown tag name should not block registration - "
                                  "tag names are only resolved at creation");
    TEST_ASSERT_NOT_NULL_MESSAGE(root, "the component did not build at all");
    TEST_ASSERT_TRUE_MESSAGE(
        log_contains("is not a known widget/element/component/slot"),
        helix_xml_assert_msgf("unknown widget was not reported; log: %.400s", g_log_buf));

    /* The unknown element itself produced nothing. */
    ASSERT_NO_NAMED(root, "ghost");

    /* ...and the sibling written after it is inside the component, not adrift on
     * the screen. */
    lv_obj_t * after = ASSERT_NAMED(root, "after_the_ghost");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(root, lv_obj_get_parent(after),
                                  "the sibling after the unknown tag is mis-parented - the "
                                  "unknown tag's open/close are unbalanced again");
    ASSERT_LABEL_TEXT(after, "A");
    ASSERT_CHILD_COUNT(root, 1);
    ASSERT_CHILD_COUNT(screen, 1);

    assert_engine_still_works("unknown widget name");
}

/**
 * An unknown ATTRIBUTE on a KNOWN widget warns, naming both the attribute and
 * the widget, and the widget's real attributes still apply.
 *
 * The failure mode being caught is an ordinary typo. Attribute dispatch is
 * composed - lv_xml_label_apply() handles `text`, lv_xml_obj_apply() handles
 * `width`, and neither one alone can tell a valid name from a misspelled one -
 * so for a long time a typo produced no diagnostic at any log level and the
 * widget just came out wrong. The warning is the only observable difference
 * between "you misspelled it" and "you never wrote it", which is why it is
 * asserted per row.
 *
 * The message must name the WIDGET as well as the attribute: "unknown attribute
 * txet" alone does not tell you which of the six elements on the line to look
 * at, and `text` is valid on lv_label but not on lv_button.
 */
static void test_unknown_attribute_on_a_known_widget_warns(void)
{
    static const malformed_case_t table[] = {
        {
            "made-up attribute name on lv_label",
            "<component><view extends=\"lv_obj\" name=\"r\">"
            "<lv_label name=\"a\" text=\"Kept\" bogus_attr=\"7\"/></view></component>",
            true, "Unknown attribute \"bogus_attr\" on <lv_label>"
        },
        {
            "misspelled real attribute (txet instead of text)",
            "<component><view extends=\"lv_obj\" name=\"r\">"
            "<lv_label name=\"a\" txet=\"typo\"/></view></component>",
            true, "Unknown attribute \"txet\" on <lv_label>"
        },
        {
            "typo'd lv_obj attribute, on a widget whose own chain also misses it",
            "<component><view extends=\"lv_obj\" name=\"r\">"
            "<lv_label name=\"a\" text=\"Kept\" widht=\"100\"/></view></component>",
            true, "Unknown attribute \"widht\" on <lv_label>"
        },
        {
            "attribute of a DIFFERENT widget (lv_label's text on an lv_slider)",
            "<component><view extends=\"lv_obj\" name=\"r\">"
            "<lv_slider name=\"a\" text=\"nope\"/></view></component>",
            true, "Unknown attribute \"text\" on <lv_slider>"
        },
        {
            "typo on the component's own <view> root",
            "<component><view extends=\"lv_obj\" name=\"r\" scrollabel=\"false\">"
            "<lv_label name=\"a\" text=\"Kept\"/></view></component>",
            true, "Unknown attribute \"scrollabel\" on <lv_obj>"
        },
    };
    RUN_TABLE(table);

    /* And the surviving attributes on the same element still applied - the
     * check reports, it must not change what gets applied. */
    lv_obj_t * screen = helix_test_env_screen();
    lv_xml_component_unregister(SUBJECT_NAME);
    ASSERT_XML_REGISTERS(SUBJECT_NAME,
                         "<component><view extends=\"lv_obj\" name=\"r\">"
                         "<lv_label name=\"a\" text=\"Kept\" bogus_attr=\"7\"/></view></component>");
    lv_obj_t * root = XML_CREATE(screen, SUBJECT_NAME, NULL);
    ASSERT_LABEL_TEXT(ASSERT_NAMED(root, "a"), "Kept");

    assert_engine_still_works("unknown attribute");
}

/**
 * THE ONE THAT MATTERS: a wholly CORRECT document, using the full spread of the
 * dialect, must produce no unknown-attribute warning at all.
 *
 * A warning that fires on correct XML is worse than the silence it replaced,
 * because it teaches everyone to ignore the log. Every category below reaches
 * the widget's apply chain by a DIFFERENT route, and each one was capable of
 * producing a flood on its own:
 *
 *   - widget-specific attributes (`text`, `long_mode`, `value`, `options`) -
 *     unknown to lv_xml_obj_apply(), which runs first on every element
 *   - shared lv_obj attributes (`width`, `align`, `flex_flow`, `hidden`,
 *     `clickable`, `checked`) - unknown to every widget chain
 *   - `style_*`, matched by prefix rather than by name
 *   - `bind_*` attributes, and the framework's `name`
 *   - `$prop` values substituted at the instance site
 *   - out-of-band modifiers (`bind_text-fmt`, `value-animated`) that no
 *     if/else-if arm ever matches because they are read from inside another
 *     attribute's branch
 *   - a widget whose chain is EMPTY (`lv_button`), where lv_obj is the only
 *     handler there is
 *
 * The assertion is on the whole log rather than a row-by-row substring: any hit
 * anywhere in the document is a false positive.
 */
static const char * WIDE_GOOD_XML =
    "<component>"
    "  <api><prop name=\"caption\" type=\"string\" default=\"Hi\"/></api>"
    "  <subjects>"
    "    <subject name=\"lvl\" type=\"int\" value=\"3\"/>"
    "    <subject name=\"on\" type=\"int\" value=\"1\"/>"
    "  </subjects>"
    "  <view extends=\"lv_obj\" name=\"wide_root\" width=\"200\" height=\"content\""
    "        flex_flow=\"column\" scrollable=\"false\" style_pad_all=\"4\">"
    "    <lv_label name=\"lbl\" text=\"$caption\" long_mode=\"wrap\" align=\"center\""
    "              style_text_align=\"left\" style_pad_left=\"2\" hidden=\"false\"/>"
    "    <lv_label name=\"bound\" bind_text=\"lvl\" bind_text-fmt=\"%d mm\"/>"
    "    <lv_slider name=\"sld\" value=\"40\" value-animated=\"true\" min_value=\"0\""
    "               max_value=\"100\" width=\"120\" clickable=\"true\"/>"
    "    <lv_bar name=\"bar\" bind_value=\"lvl\" style_bg_opa=\"128\" flex_grow=\"1\"/>"
    "    <lv_button name=\"btn\" width=\"60\" height=\"30\" checked=\"true\">"
    "      <bind_flag_if_eq subject=\"on\" flag=\"hidden\" ref_value=\"0\"/>"
    "    </lv_button>"
    "    <lv_dropdown name=\"dd\" options=\"a\nb\" selected=\"1\" width=\"80\"/>"
    "  </view>"
    "</component>";

static void test_a_correct_document_produces_no_unknown_attribute_warning(void)
{
    lv_obj_t * screen = helix_test_env_screen();
    lv_xml_component_unregister(SUBJECT_NAME);

    log_capture_start();
    lv_result_t res = lv_xml_register_component_from_data(SUBJECT_NAME, WIDE_GOOD_XML);
    const char * inst[] = {"caption", "Hello", "name", "instance_root", NULL, NULL};
    lv_obj_t * root = res == LV_RESULT_OK ? lv_xml_create(screen, SUBJECT_NAME, inst) : NULL;
    helix_test_pump(20);
    log_capture_stop();

    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_RESULT_OK, (int)res, "the wide-spread document did not register");
    TEST_ASSERT_NOT_NULL_MESSAGE(root, "the wide-spread document did not build");

    TEST_ASSERT_FALSE_MESSAGE(
        log_contains("Unknown attribute"),
        helix_xml_assert_msgf("a CORRECT document produced an unknown-attribute warning - this is "
                              "the false-positive flood the check exists to avoid; log: %.700s",
                              g_log_buf));

    /* Prove the document really was applied, so a future refactor cannot make
     * this test pass by quietly failing to parse anything. */
    ASSERT_LABEL_TEXT(ASSERT_NAMED(root, "lbl"), "Hello");
    TEST_ASSERT_EQUAL_INT_MESSAGE(40, lv_slider_get_value(ASSERT_NAMED(root, "sld")),
                                  "the slider's value attribute was not applied");
    TEST_ASSERT_TRUE_MESSAGE(lv_obj_has_state(ASSERT_NAMED(root, "btn"), LV_STATE_CHECKED),
                             "the button's checked attribute was not applied");

    assert_engine_still_works("correct wide-spread document");
}

/**
 * A widget registered by the APPLICATION is never checked, however many
 * attributes it declines to handle.
 *
 * Downstream widgets (HelixScreen registers ~30) have apply_cbs this library
 * cannot see inside. They routinely accept attributes their create_cb consumed,
 * or ignore some on purpose. Checking them would mean a warning on every one of
 * those, so registration through lv_xml_register_widget() deliberately opts out
 * - and it has to keep doing so even though such a widget calls
 * lv_xml_obj_apply(), which does record misses.
 */
static lv_obj_t * g_custom_widget_last;

static void * custom_widget_create(lv_xml_parser_state_t * state, const char ** attrs)
{
    LV_UNUSED(attrs);
    g_custom_widget_last = lv_obj_create(lv_xml_state_get_parent(state));
    return g_custom_widget_last;
}

static void custom_widget_apply(lv_xml_parser_state_t * state, const char ** attrs)
{
    /* Exactly what a downstream widget does: hand the array to lv_obj and keep
     * its own attributes to itself. */
    lv_xml_obj_apply(state, attrs);
}

static void test_an_application_registered_widget_is_never_reported(void)
{
    lv_obj_t * screen = helix_test_env_screen();
    lv_xml_component_unregister(SUBJECT_NAME);

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        LV_RESULT_OK,
        (int)lv_xml_register_widget("app_widget", custom_widget_create, custom_widget_apply),
        "the custom widget did not register");

    log_capture_start();
    lv_result_t res = lv_xml_register_component_from_data(
                          SUBJECT_NAME,
                          "<component><view extends=\"lv_obj\" name=\"r\">"
                          "<app_widget name=\"aw\" width=\"50\" my_own_attr=\"7\" another=\"x\"/>"
                          "</view></component>");
    lv_obj_t * root = res == LV_RESULT_OK ? lv_xml_create(screen, SUBJECT_NAME, NULL) : NULL;
    helix_test_pump(20);
    log_capture_stop();

    TEST_ASSERT_NOT_NULL_MESSAGE(root, "the document with the custom widget did not build");
    TEST_ASSERT_NOT_NULL_MESSAGE(lv_obj_find_by_name(root, "aw"),
                                 "the custom widget was not created, so nothing was checked");
    TEST_ASSERT_FALSE_MESSAGE(
        log_contains("Unknown attribute"),
        helix_xml_assert_msgf("an application-registered widget was reported on - every downstream "
                              "consumer would drown in this; log: %.500s", g_log_buf));

    assert_engine_still_works("application-registered widget");
}

/**
 * An unknown value for a KNOWN enum-valued attribute warns, falls back, and
 * leaves the rest of the element intact.
 *
 * The fallback is usually a legal enumerator (whatever happens to be 0), so the
 * warning is the only observable difference between "you typed nonsense" and
 * "you asked for the default" - which is exactly why it is asserted per row.
 */
static void test_unknown_enum_value_warns_and_the_widget_is_still_built(void)
{
    static const malformed_case_t table[] = {
        {
            "align=\"nonsense\"",
            "<component><view extends=\"lv_obj\" name=\"r\">"
            "<lv_label name=\"a\" text=\"Kept\" align=\"nonsense\"/></view></component>",
            true, "unknown value for align"
        },
        {
            "style_flex_flow=\"sideways\"",
            "<component><view extends=\"lv_obj\" name=\"r\" style_flex_flow=\"sideways\">"
            "<lv_label name=\"a\" text=\"Kept\"/></view></component>",
            true, "unknown value for flex_flow"
        },
        {
            "style_text_align=\"diagonal\"",
            "<component><view extends=\"lv_obj\" name=\"r\">"
            "<lv_label name=\"a\" text=\"Kept\" style_text_align=\"diagonal\"/></view></component>",
            true, "unknown value for text_align"
        },
        {
            "style_base_dir=\"sideways\"",
            "<component><view extends=\"lv_obj\" name=\"r\">"
            "<lv_label name=\"a\" text=\"Kept\" style_base_dir=\"sideways\"/></view></component>",
            true, "unknown value for base_dir"
        },
    };
    RUN_TABLE(table);

    /* The bad enum must not take the rest of the element down with it. */
    lv_obj_t * screen = helix_test_env_screen();
    lv_xml_component_unregister(SUBJECT_NAME);
    ASSERT_XML_REGISTERS(SUBJECT_NAME,
                         "<component><view extends=\"lv_obj\" name=\"r\">"
                         "<lv_label name=\"a\" text=\"Kept\" align=\"nonsense\"/></view></component>");
    lv_obj_t * root = XML_CREATE(screen, SUBJECT_NAME, NULL);
    ASSERT_LABEL_TEXT(ASSERT_NAMED(root, "a"), "Kept");

    assert_engine_still_works("unknown enum value");
}

/*---------------------------------------------------------------------------
 * Documents with no content
 *--------------------------------------------------------------------------*/

/**
 * Empty, whitespace-only, declaration-only and text-only documents are all
 * rejected with a parse error.
 *
 * The empty string is the realistic one: a hot-reload poll that catches a file
 * between truncate and write sees exactly this.
 */
static void test_contentless_documents_warn_and_leave_the_parser_usable(void)
{
    static const malformed_case_t table[] = {
        {"empty document",            "",                                       false, "XML parsing error"},
        {"whitespace-only document",  "   \n\t  ",                              false, "XML parsing error"},
        {"XML declaration only",      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>", false, "XML parsing error"},
        {"plain text, no markup",     "just some text",                         false, "XML parsing error"},
    };
    RUN_TABLE(table);
}

/*---------------------------------------------------------------------------
 * A <component> with no <view>
 *--------------------------------------------------------------------------*/

/**
 * A well-formed `<component>` that never declares a `<view>` is rejected.
 *
 * This one does NOT go through expat - the document parses fine. It is
 * extract_view_content() in lv_xml_component.c failing to find `<view`, after
 * which the partly-built scope is unregistered again. Assert that: the name
 * must be free afterwards, not left claimed by a viewless scope.
 */
static void test_component_without_a_view_warns_and_leaves_the_parser_usable(void)
{
    static const malformed_case_t table[] = {
        {
            "component with only <consts>",
            "<component><consts><px name=\"c\" value=\"1\"/></consts></component>",
            false, "Failed to extract view content"
        },
        {
            "completely empty component",
            "<component></component>",
            false, "Failed to extract view content"
        },
    };
    RUN_TABLE(table);

    lv_xml_component_unregister(SUBJECT_NAME);
    lv_result_t res = lv_xml_register_component_from_data(
                          SUBJECT_NAME, "<component><consts><px name=\"c\" value=\"1\"/></consts></component>");
    TEST_ASSERT_EQUAL_INT((int)LV_RESULT_INVALID, (int)res);
    TEST_ASSERT_NULL_MESSAGE(lv_xml_component_get_scope(SUBJECT_NAME),
                             "a viewless component left its name claimed in the registry");
    TEST_ASSERT_NULL_MESSAGE(lv_xml_create(helix_test_env_screen(), SUBJECT_NAME, NULL),
                             "a viewless component was still creatable");

    assert_engine_still_works("component without a view");
}

/*---------------------------------------------------------------------------
 * Bytes that are not valid XML text
 *--------------------------------------------------------------------------*/

/**
 * Invalid UTF-8 sequences and raw control bytes are rejected at registration.
 *
 * Realistic input: a file read with the wrong encoding, or a hot-reload read
 * that lands in the middle of a multi-byte character (the last row - a lone
 * UTF-8 lead byte with its continuation byte not yet written).
 */
static void test_invalid_utf8_and_control_bytes_warn_and_leave_the_parser_usable(void)
{
    static const malformed_case_t table[] = {
        {
            "invalid UTF-8 sequence in text content",
            "<component><view extends=\"lv_obj\" name=\"r\">"
            "<lv_label name=\"a\">\xC3\x28 bad</lv_label></view></component>",
            false, "XML parsing error"
        },
        {
            "raw control bytes in text content",
            "<component><view extends=\"lv_obj\" name=\"r\">"
            "<lv_label name=\"a\">a\x01\x02z</lv_label></view></component>",
            false, "XML parsing error"
        },
        {
            "UTF-8 lead byte truncated mid-character",
            "<component><view extends=\"lv_obj\" name=\"r\">"
            "<lv_label name=\"a\" text=\"caf\xC3\"/></view></component>",
            false, "XML parsing error"
        },
    };
    RUN_TABLE(table);
}

/*---------------------------------------------------------------------------
 * Entities and character references
 *--------------------------------------------------------------------------*/

/**
 * Undefined entities, unterminated entities, and character references naming
 * characters that cannot appear in XML are all rejected with a parse error.
 *
 * There is no DTD anywhere in this dialect, so `&nope;` can never resolve -
 * and there is no entity-expansion path to attack, which is why the
 * billion-laughs family is absent from this table.
 */
static void test_bad_entities_warn_and_leave_the_parser_usable(void)
{
    static const malformed_case_t table[] = {
        {
            "undefined entity",
            "<component><view extends=\"lv_obj\" name=\"r\">"
            "<lv_label name=\"a\" text=\"&nope;\"/></view></component>",
            false, "undefined entity"
        },
        {
            "unterminated entity (missing ';')",
            "<component><view extends=\"lv_obj\" name=\"r\">"
            "<lv_label name=\"a\" text=\"&amp\"/></view></component>",
            false, "XML parsing error"
        },
        {
            "malformed hex character reference",
            "<component><view extends=\"lv_obj\" name=\"r\">"
            "<lv_label name=\"a\" text=\"&#xZZ;\"/></view></component>",
            false, "XML parsing error"
        },
        {
            "character reference to NUL",
            "<component><view extends=\"lv_obj\" name=\"r\">"
            "<lv_label name=\"a\" text=\"&#0;\"/></view></component>",
            false, "reference to invalid character number"
        },
        {
            "character reference to a lone surrogate",
            "<component><view extends=\"lv_obj\" name=\"r\">"
            "<lv_label name=\"a\" text=\"&#xD800;\"/></view></component>",
            false, "reference to invalid character number"
        },
    };
    RUN_TABLE(table);
}

/*---------------------------------------------------------------------------
 * Deep nesting
 *--------------------------------------------------------------------------*/

/**
 * The engine imposes NO element-nesting limit, and must not.
 *
 * Nothing in src/xml/ counts depth: expat keeps its open-element stack as a
 * heap-allocated list, and the engine mirrors it in state->parent_ll, another
 * heap list. So the only ceiling is LVGL's heap, and the documented behaviour
 * at any depth that fits is "it just works, silently, with the innermost
 * element reachable by name".
 *
 * That is what is asserted here, across three magnitudes. See the DELIBERATELY
 * NOT TESTED note at the top of this file for what happens past the heap - it
 * is an allocator assert, not a parser behaviour, and it is not pinned.
 */
static void test_deeply_nested_elements_have_no_engine_limit_and_stay_usable(void)
{
    static const int depths[] = {8, 64, 256, 1024};

    lv_obj_t * screen = helix_test_env_screen();

    for(size_t i = 0; i < sizeof(depths) / sizeof(depths[0]); i++) {
        char * xml = build_nested(depths[i]);

        lv_obj_clean(screen);
        lv_xml_component_unregister(SUBJECT_NAME);

        log_capture_start();
        lv_result_t res = lv_xml_register_component_from_data(SUBJECT_NAME, xml);
        lv_obj_t * root = (res == LV_RESULT_OK) ? lv_xml_create(screen, SUBJECT_NAME, NULL) : NULL;
        helix_test_pump(20);
        log_capture_stop();

        TEST_ASSERT_EQUAL_INT_MESSAGE(
            (int)LV_RESULT_OK, (int)res,
            helix_xml_assert_msgf("%d levels of nesting was rejected at registration", depths[i]));
        TEST_ASSERT_NOT_NULL_MESSAGE(
            root, helix_xml_assert_msgf("%d levels of nesting did not build", depths[i]));
        TEST_ASSERT_EQUAL_STRING_MESSAGE(
            "", g_log_buf,
            helix_xml_assert_msgf("%d levels of nesting produced a diagnostic; there is no depth "
                                  "limit, so there should be nothing to say: %.300s",
                                  depths[i], g_log_buf));

        /* The innermost element really was built at the bottom of the chain -
         * a depth cap that silently stopped descending would still leave a
         * plausible-looking root. */
        lv_obj_t * leaf = ASSERT_NAMED(root, "deep_leaf");
        ASSERT_LABEL_TEXT(leaf, "Deep");
        ASSERT_CHILD_COUNT(root, 1);

        int walked = 0;
        for(lv_obj_t * p = lv_obj_get_parent(leaf); p && p != root; p = lv_obj_get_parent(p)) walked++;
        TEST_ASSERT_EQUAL_INT_MESSAGE(
            depths[i], walked,
            helix_xml_assert_msgf("the leaf sits %d levels under the root, not %d - the engine "
                                  "flattened or truncated the nesting", walked, depths[i]));

        free(xml);
        assert_engine_still_works("deep nesting");
    }
}

/*---------------------------------------------------------------------------
 * Duplicate names
 *--------------------------------------------------------------------------*/

/**
 * Two siblings sharing one `name=` are both created, nothing is logged, and
 * lv_obj_find_by_name() returns the FIRST of them.
 *
 * PINS CURRENT BEHAVIOUR - suspected bug: HelixScreen runs a lint gate against
 * duplicate names precisely because the engine has no opinion here. Every
 * lookup silently resolves to one arbitrary widget while the other is
 * unreachable by name, and nothing anywhere says so. If the engine ever starts
 * warning, this test should be updated to require the warning, not deleted.
 */
static void test_duplicate_sibling_names_are_accepted_and_the_first_one_wins(void)
{
    lv_obj_t * screen = helix_test_env_screen();

    log_capture_start();
    lv_result_t res = lv_xml_register_component_from_data(
                          SUBJECT_NAME,
                          "<component><view extends=\"lv_obj\" name=\"r\">"
                          "<lv_label name=\"twin\" text=\"first\"/>"
                          "<lv_label name=\"twin\" text=\"second\"/>"
                          "</view></component>");
    lv_obj_t * root = (res == LV_RESULT_OK) ? lv_xml_create(screen, SUBJECT_NAME, NULL) : NULL;
    helix_test_pump(20);
    log_capture_stop();

    TEST_ASSERT_EQUAL_INT_MESSAGE((int)LV_RESULT_OK, (int)res,
                                  "duplicate sibling names were rejected at registration");
    TEST_ASSERT_NOT_NULL_MESSAGE(root, "a component with duplicate sibling names did not build");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("", g_log_buf,
                                     "the engine now diagnoses duplicate names - update this test "
                                     "to require the warning");

    /* Both widgets exist... */
    ASSERT_CHILD_COUNT(root, 2);
    /* ...but only the first is reachable by name. */
    ASSERT_LABEL_TEXT(ASSERT_NAMED(root, "twin"), "first");

    assert_engine_still_works("duplicate sibling names");
}

/*---------------------------------------------------------------------------
 * Oversized values and identifiers
 *--------------------------------------------------------------------------*/

/**
 * Very long attribute values and identifiers must not overflow anything.
 *
 * Two outcomes, both pinned:
 *   - a 4000-character attribute value and a 300-character `name=` are handled
 *     silently and correctly (both are heap-copied)
 *   - a 300-character TAG name hits the 128-byte slot-parsing buffer in
 *     view_start_element_handler, which declines rather than truncating, and
 *     says so twice before the element is dropped
 */
static void test_oversized_values_and_identifiers_stay_usable(void)
{
    lv_obj_t * screen = helix_test_env_screen();

    /* --- 4000-character attribute value: accepted verbatim, silently. --- */
    {
        char * xml = build_padded("<component><view extends=\"lv_obj\" name=\"r\">"
                                  "<lv_label name=\"a\" text=\"", 4000, 'x',
                                  "\"/></view></component>");
        lv_obj_clean(screen);
        lv_xml_component_unregister(SUBJECT_NAME);

        log_capture_start();
        lv_result_t res = lv_xml_register_component_from_data(SUBJECT_NAME, xml);
        lv_obj_t * root = (res == LV_RESULT_OK) ? lv_xml_create(screen, SUBJECT_NAME, NULL) : NULL;
        helix_test_pump(20);
        log_capture_stop();

        TEST_ASSERT_EQUAL_INT_MESSAGE((int)LV_RESULT_OK, (int)res,
                                      "a 4000-character attribute value was rejected");
        TEST_ASSERT_NOT_NULL_MESSAGE(root, "a 4000-character attribute value did not build");
        TEST_ASSERT_EQUAL_STRING_MESSAGE("", g_log_buf,
                                         "a 4000-character attribute value produced a diagnostic");

        lv_obj_t * label = ASSERT_NAMED(root, "a");
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(
            4000u, (uint32_t)strlen(lv_label_get_text(label)),
            "the long attribute value was truncated on its way to the widget");

        free(xml);
        assert_engine_still_works("4000-character attribute value");
    }

    /* --- Over-long name=: accepted and stored in full, but not findable. ---
     *
     * PINS CURRENT BEHAVIOUR - suspected bug: lv_obj_find_by_name() resolves
     * each candidate's name into a 128-byte stack buffer
     * (LV_OBJ_NAME_MAX_LEN in lv_obj_tree.c) before comparing, so a name of
     * 128 characters or more is silently truncated AT LOOKUP TIME and can
     * never match. The widget is created, `name=` really is applied, and
     * lv_obj_get_name() hands the whole string back - yet every lookup misses.
     * A long generated name in a layout file therefore produces a widget that
     * exists, renders, and is unreachable, with nothing logged anywhere.
     *
     * The boundary is asserted from both sides so this pins the real cutoff
     * rather than "long names are broken". */
    {
        static const struct { size_t len; bool findable; } name_lens[] = {
            {127, true},   /* fits the resolve buffer with its NUL */
            {128, false},  /* first length that does not */
            {300, false},
        };

        for(size_t k = 0; k < sizeof(name_lens) / sizeof(name_lens[0]); k++) {
            const size_t nlen = name_lens[k].len;
            char * xml = build_padded("<component><view extends=\"lv_obj\" name=\"r\">"
                                      "<lv_label name=\"", nlen, 'n',
                                      "\" text=\"Kept\"/></view></component>");
            char * long_name = malloc(nlen + 1);
            TEST_ASSERT_NOT_NULL(long_name);
            memset(long_name, 'n', nlen);
            long_name[nlen] = '\0';

            lv_obj_clean(screen);
            lv_xml_component_unregister(SUBJECT_NAME);

            log_capture_start();
            lv_result_t res = lv_xml_register_component_from_data(SUBJECT_NAME, xml);
            lv_obj_t * root = (res == LV_RESULT_OK) ? lv_xml_create(screen, SUBJECT_NAME, NULL) : NULL;
            helix_test_pump(20);
            log_capture_stop();

            TEST_ASSERT_EQUAL_INT_MESSAGE(
                (int)LV_RESULT_OK, (int)res,
                helix_xml_assert_msgf("a %u-character name= was rejected", (unsigned)nlen));
            TEST_ASSERT_NOT_NULL_MESSAGE(
                root, helix_xml_assert_msgf("a %u-character name= did not build", (unsigned)nlen));
            TEST_ASSERT_EQUAL_STRING_MESSAGE(
                "", g_log_buf,
                helix_xml_assert_msgf("a %u-character name= produced a diagnostic - the engine may "
                                      "have started reporting these; re-read this test",
                                      (unsigned)nlen));

            /* Reach the label WITHOUT a name lookup, so the assertions below are
             * about findability rather than about whether it was built. */
            ASSERT_CHILD_COUNT(root, 1);
            lv_obj_t * label = lv_obj_get_child(root, 0);
            ASSERT_LABEL_TEXT(label, "Kept");
            TEST_ASSERT_EQUAL_STRING_MESSAGE(
                long_name, lv_obj_get_name(label),
                helix_xml_assert_msgf("the %u-character name= was not stored verbatim on the widget",
                                      (unsigned)nlen));

            if(name_lens[k].findable) {
                TEST_ASSERT_EQUAL_PTR_MESSAGE(
                    label, lv_obj_find_by_name(root, long_name),
                    helix_xml_assert_msgf("a %u-character name should still resolve - it fits the "
                                          "128-byte name-resolve buffer", (unsigned)nlen));
            }
            else {
                TEST_ASSERT_NULL_MESSAGE(
                    lv_obj_find_by_name(root, long_name),
                    helix_xml_assert_msgf("a %u-character name now resolves - the 128-byte "
                                          "name-resolve buffer may have grown; re-read this test",
                                          (unsigned)nlen));
            }

            free(long_name);
            free(xml);
            assert_engine_still_works("over-long name attribute");
        }
    }

    /* --- Over-long TAG name: declined twice, and the element is dropped. ---
     *
     * view_start_element_handler copies the tag into a 128-byte buffer to try
     * slot parsing (`my_button-icon`). Over that it declines outright instead
     * of truncating into a wrong-but-plausible slot name, then falls through to
     * the unknown-tag path. Both messages are required: dropping either would
     * leave a silently missing widget.
     *
     * 150 characters is used for the message assertions rather than 300,
     * because LVGL's log formatter (lv_log.c) renders into a 512-byte buffer
     * and quotes the whole tag name - at 300 the message is cut off before its
     * own explanatory tail, so only the leading text survives to be matched. */
    {
        static const size_t tag_lens[] = {150, 300};

        for(size_t k = 0; k < sizeof(tag_lens) / sizeof(tag_lens[0]); k++) {
            char * xml = build_padded("<component><view extends=\"lv_obj\" name=\"r\"><",
                                      tag_lens[k], 'w', " name=\"a\"/></view></component>");
            lv_obj_clean(screen);
            lv_xml_component_unregister(SUBJECT_NAME);

            log_capture_start();
            lv_result_t res = lv_xml_register_component_from_data(SUBJECT_NAME, xml);
            lv_obj_t * root = (res == LV_RESULT_OK) ? lv_xml_create(screen, SUBJECT_NAME, NULL) : NULL;
            helix_test_pump(20);
            log_capture_stop();

            TEST_ASSERT_EQUAL_INT_MESSAGE(
                (int)LV_RESULT_OK, (int)res,
                helix_xml_assert_msgf("a %u-character tag name was rejected at registration - "
                                      "tag names are only resolved at creation", (unsigned)tag_lens[k]));
            TEST_ASSERT_NOT_NULL_MESSAGE(
                root, helix_xml_assert_msgf("the component around a %u-character tag did not build",
                                            (unsigned)tag_lens[k]));

            /* Present at every length: the two distinct diagnostics. */
            TEST_ASSERT_TRUE_MESSAGE(
                log_contains("Component/slot name '"),
                helix_xml_assert_msgf("a %u-character tag was not reported as too long for slot "
                                      "parsing; log: %.200s", (unsigned)tag_lens[k], g_log_buf));
            TEST_ASSERT_TRUE_MESSAGE(
                log_contains("XML tag '"),
                helix_xml_assert_msgf("a %u-character tag was not reported as unknown; log: %.200s",
                                      (unsigned)tag_lens[k], g_log_buf));

            if(tag_lens[k] == 150) {
                /* Short enough that the whole message survives the log buffer. */
                TEST_ASSERT_TRUE_MESSAGE(
                    log_contains("is too long (max 127 chars)"),
                    helix_xml_assert_msgf("the too-long diagnostic lost its explanation; log: %.200s",
                                          g_log_buf));
                TEST_ASSERT_TRUE_MESSAGE(
                    log_contains("is not a known widget/element/component/slot"),
                    helix_xml_assert_msgf("the unknown-tag diagnostic lost its explanation; log: %.200s",
                                          g_log_buf));
            }

            /* The element itself produced nothing at all. */
            ASSERT_NO_NAMED(root, "a");
            ASSERT_CHILD_COUNT(root, 0);

            free(xml);
            assert_engine_still_works("over-long tag name");
        }
    }
}

/*---------------------------------------------------------------------------
 * The strstr-based <view> extraction
 *--------------------------------------------------------------------------*/

/**
 * A literal `</view>` inside a comment, a CDATA section or a processing
 * instruction is NOT the closing tag, and the view body must not be cut there.
 *
 * extract_view_content() used to locate the body with strstr("</view>"), which
 * is not XML-aware. The document is well-formed - expat accepts it, so
 * registration returned OK - but the substring stored as `scope->view_def` was
 * cut at the first literal `</view>` anywhere in the file, comments included.
 * The stored fragment was then unparseable, so creation warned and returned NULL
 * every single time, forever. Commenting out a block of a layout file is an
 * entirely ordinary thing to do.
 */
static void test_a_view_close_inside_a_comment_or_cdata_is_not_the_closing_tag(void)
{
    static const struct {
        const char * desc;
        const char * xml;
    } rows[] = {
        {
            "</view> inside a comment",
            "<component><view extends=\"lv_obj\" name=\"r\">"
            "<lv_label name=\"before\" text=\"B\"/>"
            "<!-- </view> -->"
            "<lv_label name=\"after\" text=\"A\"/>"
            "</view></component>"
        },
        {
            "a whole commented-out block containing <view> and </view>",
            "<component>"
            "<!-- <view extends=\"lv_obj\" name=\"old\"><lv_label name=\"dead\"/></view> -->"
            "<view extends=\"lv_obj\" name=\"r\">"
            "<lv_label name=\"before\" text=\"B\"/>"
            "<lv_label name=\"after\" text=\"A\"/>"
            "</view></component>"
        },
        {
            "</view> inside a CDATA section",
            "<component><view extends=\"lv_obj\" name=\"r\">"
            "<lv_label name=\"before\" text=\"B\"/>"
            "<lv_obj name=\"holder\"><![CDATA[</view>]]></lv_obj>"
            "<lv_label name=\"after\" text=\"A\"/>"
            "</view></component>"
        },
        {
            "</view> inside a processing instruction",
            "<component><view extends=\"lv_obj\" name=\"r\">"
            "<lv_label name=\"before\" text=\"B\"/>"
            "<?helix </view> ?>"
            "<lv_label name=\"after\" text=\"A\"/>"
            "</view></component>"
        },
    };

    lv_obj_t * screen = helix_test_env_screen();

    for(size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        lv_obj_clean(screen);
        lv_xml_component_unregister(SUBJECT_NAME);

        log_capture_start();
        lv_result_t res = lv_xml_register_component_from_data(SUBJECT_NAME, rows[i].xml);
        lv_obj_t * root = (res == LV_RESULT_OK) ? lv_xml_create(screen, SUBJECT_NAME, NULL) : NULL;
        helix_test_pump(20);
        log_capture_stop();

        TEST_ASSERT_EQUAL_INT_MESSAGE(
            (int)LV_RESULT_OK, (int)res,
            helix_xml_assert_msgf("\"%s\": the document is well-formed XML but registration "
                                  "rejected it", rows[i].desc));
        TEST_ASSERT_NOT_NULL_MESSAGE(
            root,
            helix_xml_assert_msgf("\"%s\": creation failed - the view body was cut at a "
                                  "</view> that is not the closing tag; log: %.400s",
                                  rows[i].desc, g_log_buf));

        /* The whole body survived: everything written after the decoy is there. */
        TEST_ASSERT_NOT_NULL_MESSAGE(
            lv_obj_find_by_name(root, "before"),
            helix_xml_assert_msgf("\"%s\": the element before the decoy is missing", rows[i].desc));
        TEST_ASSERT_NOT_NULL_MESSAGE(
            lv_obj_find_by_name(root, "after"),
            helix_xml_assert_msgf("\"%s\": the element AFTER the decoy is missing - the view "
                                  "body was truncated there", rows[i].desc));
        /* A commented-out view is not the view. */
        ASSERT_NO_NAMED(root, "dead");

        assert_engine_still_works(rows[i].desc);
    }
}

/**
 * The failed-create cleanup, which the truncated-view case above used to be the
 * only route to. A view body that parses at registration but not on its own -
 * here through an entity declared in the document's internal DTD subset, which
 * the stored `view_def` fragment does not carry - dies partway through
 * lv_xml_create with widgets already built onto the caller's parent. The caller
 * gets NULL back and has no handle to clean them up, so the error path must
 * destroy exactly what it built and nothing the caller already had. A failing
 * hot reload used to accumulate orphaned subtrees on the live screen.
 */
static void test_a_view_that_fails_to_parse_leaves_no_orphans_on_the_caller(void)
{
    lv_obj_t * screen = helix_test_env_screen();
    lv_obj_clean(screen);
    lv_xml_component_unregister(SUBJECT_NAME);

    /* A pre-existing child of the caller's parent. The failure path has to tell
     * "what this parse built" from "what the caller already had", so this must
     * still be standing afterwards. */
    lv_obj_t * bystander = lv_obj_create(screen);
    lv_obj_set_name(bystander, "bystander");

    log_capture_start();
    lv_result_t res = lv_xml_register_component_from_data(
                          SUBJECT_NAME,
                          "<!DOCTYPE component [<!ENTITY greeting \"Hello\">]>"
                          "<component><view extends=\"lv_obj\" name=\"r\">"
                          "<lv_label name=\"before\" text=\"B\"/>"
                          "<lv_label name=\"boom\" text=\"&greeting;\"/>"
                          "</view></component>");
    lv_obj_t * root = (res == LV_RESULT_OK) ? lv_xml_create(screen, SUBJECT_NAME, NULL) : NULL;
    helix_test_pump(20);
    log_capture_stop();

    TEST_ASSERT_EQUAL_INT_MESSAGE((int)LV_RESULT_OK, (int)res,
                                  "the document is well-formed XML but registration rejected it");
    TEST_ASSERT_NULL_MESSAGE(root, "the view fragment parsed cleanly on its own - this test no "
                             "longer reaches the failed-create path, find another trigger");
    TEST_ASSERT_TRUE_MESSAGE(
        log_contains("Couldn't create component"),
        helix_xml_assert_msgf("the failed create was not reported to the caller's log; got: %.400s",
                              g_log_buf));

    /* No orphan: the partially built subtree must be gone. */
    TEST_ASSERT_NULL_MESSAGE(
        lv_obj_find_by_name(screen, "before"),
        "the partially built subtree is still orphaned onto the caller's parent");
    /* ...and the caller's own child is untouched: exactly the bystander is left. */
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        1u, lv_obj_get_child_count(screen),
        helix_xml_assert_msgf("after a failed create the screen holds %" LV_PRIu32
                              " object(s); expected only the bystander",
                              lv_obj_get_child_count(screen)));
    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        bystander, lv_obj_get_child(screen, 0),
        "the failed-create cleanup destroyed a child the caller already had");

    assert_engine_still_works("view fragment that fails to parse");
}

/*---------------------------------------------------------------------------
 * Structural elements missing their required attributes
 *--------------------------------------------------------------------------*/

/**
 * `<repeat>` without `count`, `<if>` without `cond`, and `<else>` outside any
 * `<if>` each warn and degrade to doing nothing, rather than aborting the parse
 * or leaving a live fragment capture behind.
 *
 * The capture is the interesting part: a `<repeat>`/`<if>` allocates an
 * xml_frag_capture_t and hangs it off state->context for the duration of the
 * body. If a malformed body could strand one, the create would leak it and the
 * next parse would start on top of it - which is why every row here is followed
 * by the full survive check like all the others.
 */
static void test_structural_elements_missing_attributes_warn_and_expand_to_nothing(void)
{
    static const malformed_case_t table[] = {
        {
            "<repeat> with no count",
            "<component><view extends=\"lv_obj\" name=\"r\"><repeat>"
            "<lv_label name=\"x\" text=\"X\"/></repeat></view></component>",
            true, "missing the required 'count' attribute"
        },
        {
            "<if> with no cond",
            "<component><view extends=\"lv_obj\" name=\"r\"><if>"
            "<lv_label name=\"x\" text=\"X\"/></if></view></component>",
            true, "missing the required 'cond' attribute"
        },
        {
            "<else> with no enclosing <if>",
            "<component><view extends=\"lv_obj\" name=\"r\"><else/>"
            "<lv_label name=\"x\" text=\"X\"/></view></component>",
            true, "<else> outside <if>"
        },
    };
    RUN_TABLE(table);

    /* The bodies really did expand to nothing rather than partially. */
    lv_obj_t * screen = helix_test_env_screen();
    lv_xml_component_unregister(SUBJECT_NAME);
    ASSERT_XML_REGISTERS(SUBJECT_NAME,
                         "<component><view extends=\"lv_obj\" name=\"r\"><if>"
                         "<lv_label name=\"x\" text=\"X\"/></if></view></component>");
    lv_obj_t * root = XML_CREATE(screen, SUBJECT_NAME, NULL);
    ASSERT_NO_NAMED(root, "x");
    ASSERT_CHILD_COUNT(root, 0);

    assert_engine_still_works("structural element missing its attribute");
}

/*---------------------------------------------------------------------------
 * main
 *--------------------------------------------------------------------------*/

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_unclosed_and_mismatched_tags_warn_and_leave_the_parser_usable);
    RUN_TEST(test_truncated_document_warns_and_leaves_the_parser_usable);
    RUN_TEST(test_malformed_attribute_syntax_warns_and_leaves_the_parser_usable);
    RUN_TEST(test_stray_angle_brackets_in_attribute_values);
    RUN_TEST(test_unknown_widget_name_warns_but_keeps_following_siblings_in_place);
    RUN_TEST(test_unknown_attribute_on_a_known_widget_warns);
    RUN_TEST(test_a_correct_document_produces_no_unknown_attribute_warning);
    RUN_TEST(test_an_application_registered_widget_is_never_reported);
    RUN_TEST(test_unknown_enum_value_warns_and_the_widget_is_still_built);
    RUN_TEST(test_contentless_documents_warn_and_leave_the_parser_usable);
    RUN_TEST(test_component_without_a_view_warns_and_leaves_the_parser_usable);
    RUN_TEST(test_invalid_utf8_and_control_bytes_warn_and_leave_the_parser_usable);
    RUN_TEST(test_bad_entities_warn_and_leave_the_parser_usable);
    RUN_TEST(test_deeply_nested_elements_have_no_engine_limit_and_stay_usable);
    RUN_TEST(test_duplicate_sibling_names_are_accepted_and_the_first_one_wins);
    RUN_TEST(test_oversized_values_and_identifiers_stay_usable);
    RUN_TEST(test_a_view_close_inside_a_comment_or_cdata_is_not_the_closing_tag);
    RUN_TEST(test_a_view_that_fails_to_parse_leaves_no_orphans_on_the_caller);
    RUN_TEST(test_structural_elements_missing_attributes_warn_and_expand_to_nothing);

    return UNITY_END();
}
