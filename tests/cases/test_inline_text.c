/**
 * @file test_inline_text.c
 *
 * Inline element text: `<lv_label>Foo</lv_label>` must behave like
 * `<lv_label text="Foo" translation_tag="Foo"/>`.
 *
 * The feature lives in three pieces of src/xml/lv_xml.c:
 *   - view_character_data_handler(), which accumulates PCDATA onto a per-element
 *     entry in state->pcdata_ll (expat hands chardata over in arbitrary chunks,
 *     so accumulation is not optional),
 *   - collapse_whitespace(), HTML PCDATA semantics: trim the ends, collapse every
 *     internal run of space/tab/CR/LF to one space,
 *   - apply_pending_inline_text(), called from the end-element handler, which
 *     synthesizes {text, translation_tag} and pushes it through the element's
 *     normal apply_cb.
 *
 * ---------------------------------------------------------------------------
 * WHAT IS ASSERTED, AND WHAT IS NOT
 *
 * Every test here asserts on lv_label_get_text() - the CONTENT the engine put on
 * the widget. Nothing asserts on how wide that text renders, per the rule at the
 * top of helpers/xml_assert.h.
 *
 * The upstream HelixScreen suite this was migrated from drove almost every case
 * through `<text_muted>`, one of that application's semantic typography widgets.
 * Those widgets do not exist in helix-xml and are not the thing under test: the
 * engine has no idea `text_muted` is special, it just finds a widget processor
 * and calls its apply_cb. `lv_label` exercises exactly the same path, so it is
 * what these tests use.
 *
 * TRANSLATION-TAG SYNTHESIS is the half that lv_label alone cannot show without
 * help: with no pack registered, lv_tr() falls back to the tag string, so a test
 * that only checks the visible text cannot tell "text= was applied" apart from
 * "translation_tag= was applied and fell back". The two tests at the bottom
 * register a real pack and switch languages under a built tree, which is the
 * only way to prove the tag was stored rather than the literal.
 * ---------------------------------------------------------------------------
 *
 * SPDX-License-Identifier: MIT
 */

#include <lvgl.h>

#include "helpers/helix_log_capture.h"
#include "helpers/helix_test_env.h"
#include "helpers/helix_test_pump.h"
#include "helpers/xml_assert.h"

#if LV_USE_TRANSLATION
#include <others/translation/lv_translation.h>
#include "xml/lv_xml_translation.h"
#endif

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
 * Register an inline component, create it on the screen, pump, evaluate to the
 * root. A macro rather than a function so a failure inside it is reported
 * against the line in the test that used it.
 */
#define BUILD_ATTRS(comp, xml, attrs)                                                    \
    __extension__({                                                                      \
        ASSERT_XML_REGISTERS((comp), (xml));                                             \
        lv_obj_t * hx_built_ = XML_CREATE(helix_test_env_screen(), (comp), (attrs));     \
        helix_test_pump(30);                                                             \
        hx_built_;                                                                       \
    })

#define BUILD(comp, xml) BUILD_ATTRS((comp), (xml), NULL)

/** Wrap a body fragment in the boilerplate every case below shares. */
#define COMPONENT(body) "<component><view extends=\"lv_obj\">" body "</view></component>"

/*===========================================================================
 * The basic contract
 *==========================================================================*/

/** Text between a label's tags becomes that label's text. Everything else here
 *  is a qualification of this one sentence. */
static void test_inline_element_text_becomes_the_label_text(void)
{
    lv_obj_t * root = BUILD("inl_basic",
                            COMPONENT("<lv_label name=\"msg\">Hello world</lv_label>"));

    ASSERT_LABEL_TEXT(ASSERT_NAMED(root, "msg"), "Hello world");
}

/**
 * Inline text on a COMPONENT instance, not a built-in widget.
 * apply_pending_inline_text() cannot find a widget processor for a component
 * name, so it falls back to the processor of whatever the component `extends`.
 * That fallback branch has no other coverage.
 */
static void test_inline_text_on_a_component_instance_applies_through_what_it_extends(void)
{
    ASSERT_XML_REGISTERS("inl_leaf", "<component><view extends=\"lv_label\"/></component>");

    lv_obj_t * root = BUILD("inl_host",
                            COMPONENT("<inl_leaf name=\"msg\">From a component</inl_leaf>"));

    ASSERT_LABEL_TEXT(ASSERT_NAMED(root, "msg"), "From a component");
}

/*===========================================================================
 * Whitespace collapsing (HTML PCDATA semantics)
 *==========================================================================*/

/** Pretty-printed XML puts padding around the text. It must not survive. */
static void test_leading_and_trailing_whitespace_is_trimmed(void)
{
    lv_obj_t * root = BUILD("inl_trim",
                            COMPONENT("<lv_label name=\"msg\">   Hello world   </lv_label>"));

    ASSERT_LABEL_TEXT(ASSERT_NAMED(root, "msg"), "Hello world");
}

/** An internal run of spaces collapses to exactly one. */
static void test_internal_whitespace_runs_collapse_to_a_single_space(void)
{
    lv_obj_t * root = BUILD("inl_runs",
                            COMPONENT("<lv_label name=\"msg\">Hello     world</lv_label>"));

    ASSERT_LABEL_TEXT(ASSERT_NAMED(root, "msg"), "Hello world");
}

/** Newlines, tabs and CRs are whitespace too - the collapse is not space-only. */
static void test_newlines_tabs_and_carriage_returns_collapse_like_spaces(void)
{
    lv_obj_t * root = BUILD("inl_ws_chars",
                            "<component>\n"
                            "  <view extends=\"lv_obj\">\n"
                            "    <lv_label name=\"msg\">\n"
                            "      Tabs\there\tand\rthere\n"
                            "    </lv_label>\n"
                            "  </view>\n"
                            "</component>");

    ASSERT_LABEL_TEXT(ASSERT_NAMED(root, "msg"), "Tabs here and there");
}

/**
 * A newline written as a numeric character reference. expat decodes it before
 * the chardata handler sees it, so it must collapse exactly like a literal one -
 * i.e. `&#10;` is not a way to smuggle a hard line break into inline text.
 */
static void test_a_numeric_newline_reference_collapses_to_a_space(void)
{
    lv_obj_t * root = BUILD("inl_nl_ref",
                            COMPONENT("<lv_label name=\"msg\">Hello&#10;world</lv_label>"));

    ASSERT_LABEL_TEXT(ASSERT_NAMED(root, "msg"), "Hello world");
}

/*===========================================================================
 * Entity decoding
 *==========================================================================*/

/** The characters XML reserves have to be reachable from inline text. */
static void test_xml_entities_in_inline_text_are_decoded(void)
{
    lv_obj_t * root = BUILD("inl_entities",
                            COMPONENT("<lv_label name=\"msg\">Fish &amp; chips &lt;3</lv_label>"));

    ASSERT_LABEL_TEXT(ASSERT_NAMED(root, "msg"), "Fish & chips <3");
}

/*===========================================================================
 * The empty cases
 *==========================================================================*/

/**
 * The single most important negative case: this is literally what a formatter
 * produces for `<lv_label text="kept">\n    </lv_label>`. If whitespace-only
 * content counted as inline text, reformatting a file would blank every label
 * in it.
 */
static void test_whitespace_only_content_is_not_applied_as_text(void)
{
    lv_obj_t * root = BUILD("inl_ws_only",
                            "<component>\n"
                            "  <view extends=\"lv_obj\">\n"
                            "    <lv_label name=\"msg\">\n"
                            "    </lv_label>\n"
                            "  </view>\n"
                            "</component>");

    /* Nothing was applied, so the label still carries what its constructor set. */
    ASSERT_LABEL_TEXT(ASSERT_NAMED(root, "msg"), LV_LABEL_DEFAULT_TEXT);
}

/**
 * Same shape, but with a text= attribute present. Two things are pinned:
 * the attribute survives, and NO conflict warning is emitted - the empty check
 * runs before the conflict check, so pretty-printing a file must not fill the
 * log with warnings about text that was never really there.
 */
static void test_whitespace_only_content_leaves_the_text_attribute_alone_and_silent(void)
{
    log_capture_start();
    lv_obj_t * root = BUILD("inl_ws_attr",
                            "<component>\n"
                            "  <view extends=\"lv_obj\">\n"
                            "    <lv_label name=\"msg\" text=\"kept\">\n"
                            "    </lv_label>\n"
                            "  </view>\n"
                            "</component>");
    log_capture_stop();

    ASSERT_LABEL_TEXT(ASSERT_NAMED(root, "msg"), "kept");
    TEST_ASSERT_FALSE_MESSAGE(log_contains("Inline text ignored"),
                              "whitespace-only content warned about a conflict that does not exist");
}

/*===========================================================================
 * Mixed content and non-text targets
 *==========================================================================*/

/**
 * Text on either side of a child element belongs to the OWNING element, not to
 * the child, and the two runs concatenate. The child is built normally.
 *
 * This is the case that proves the PCDATA stack is a stack: the chardata handler
 * appends to whichever element is currently open, so "after" must land back on
 * the label once </lv_obj> has popped the child's entry.
 */
static void test_text_before_and_after_a_child_lands_on_the_owning_element(void)
{
    lv_obj_t * root = BUILD("inl_mixed",
                            COMPONENT("<lv_label name=\"owner\">Before "
                                      "<lv_obj name=\"kid\" width=\"10\" height=\"10\"/>"
                                      " after</lv_label>"));

    lv_obj_t * owner = ASSERT_NAMED(root, "owner");
    ASSERT_LABEL_TEXT(owner, "Before after");

    /* The child was still created, and did not inherit any of that text. */
    lv_obj_t * kid = ASSERT_NAMED(root, "kid");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(owner, lv_obj_get_parent(kid),
                                  "the child element was not parented to the element that owned the text");
    ASSERT_CHILD_COUNT(kid, 0);
}

/**
 * A widget whose apply_cb has no `text` property must ignore the synthesized
 * attributes rather than crash or invent a child label for them.
 */
static void test_a_widget_with_no_text_property_ignores_inline_text(void)
{
    lv_obj_t * root = BUILD("inl_no_text",
                            COMPONENT("<lv_obj name=\"box\" width=\"100\" height=\"100\">"
                                      "stray text</lv_obj>"));

    lv_obj_t * box = ASSERT_NAMED(root, "box");
    ASSERT_CHILD_COUNT(box, 0);
}

/**
 * PINS CURRENT BEHAVIOUR - suspected bug: inline text written directly inside
 * the root `<view>` is silently discarded.
 *
 * expat hands the end-element handler the literal tag name, which for the root
 * is always "view" - never the resolved `extends` target. apply_pending_inline_text()
 * looks the processor up by that raw name (lv_xml_widget_get_processor("view"),
 * then lv_xml_component_get_scope("view")); both miss, so the collapsed text is
 * freed without ever being applied. No warning is emitted.
 *
 * The test extends lv_label deliberately. A view extending lv_obj would swallow
 * the text anyway, so it could not tell "dropped before dispatch" apart from
 * "dispatched to a widget that ignores it". lv_label WOULD render the text if it
 * arrived, so the constructor's placeholder still being there is proof it never
 * did.
 */
static void test_inline_text_on_the_root_view_element_is_dropped(void)
{
    lv_obj_t * root = BUILD("inl_root_view",
                            "<component>"
                            "  <view extends=\"lv_label\">Root inline text</view>"
                            "</component>");

    ASSERT_LABEL_TEXT(root, LV_LABEL_DEFAULT_TEXT);
}

/*===========================================================================
 * Attribute precedence: an explicit attribute always wins
 *==========================================================================*/

/** text= beats inline text, and says so in the log. */
static void test_a_text_attribute_beats_inline_element_text(void)
{
    log_capture_start();
    lv_obj_t * root = BUILD("inl_vs_text",
                            COMPONENT("<lv_label name=\"msg\" text=\"attribute wins\">"
                                      "inline loses</lv_label>"));
    log_capture_stop();

    ASSERT_LABEL_TEXT(ASSERT_NAMED(root, "msg"), "attribute wins");
    TEST_ASSERT_TRUE_MESSAGE(log_contains("Inline text ignored"),
                             "inline text was discarded silently instead of warning about the conflict");
}

#if LV_USE_TRANSLATION
/**
 * translation_tag= beats inline text. With no pack registered the tag falls back
 * to itself, which is what makes the visible text observable at all here.
 */
static void test_a_translation_tag_attribute_beats_inline_element_text(void)
{
    lv_obj_t * root = BUILD("inl_vs_tag",
                            COMPONENT("<lv_label name=\"msg\" translation_tag=\"Existing Key\">"
                                      "inline loses</lv_label>"));

    ASSERT_LABEL_TEXT(ASSERT_NAMED(root, "msg"), "Existing Key");
}
#endif /* LV_USE_TRANSLATION */

/** bind_text= beats inline text: the subject's value is what shows. */
static void test_a_bind_text_attribute_beats_inline_element_text(void)
{
    lv_obj_t * root = BUILD("inl_vs_bind",
                            "<component>"
                            "  <subjects>"
                            "    <subject name=\"inl_bound\" type=\"string\" value=\"bound value\"/>"
                            "  </subjects>"
                            "  <view extends=\"lv_obj\">"
                            "    <lv_label name=\"msg\" bind_text=\"inl_bound\">inline loses</lv_label>"
                            "  </view>"
                            "</component>");

    ASSERT_LABEL_TEXT(ASSERT_NAMED(root, "msg"), "bound value");
}

/*===========================================================================
 * $prop and #const resolution
 *==========================================================================*/

/** Inline text goes through the same $prop substitution as an attribute value. */
static void test_a_prop_reference_in_inline_text_is_substituted(void)
{
    const char * attrs[] = {"title", "Passed title", NULL, NULL};
    lv_obj_t * root = BUILD_ATTRS("inl_prop",
                                  "<component>"
                                  "  <api><prop name=\"title\" type=\"string\" default=\"Default title\"/></api>"
                                  "  <view extends=\"lv_obj\">"
                                  "    <lv_label name=\"msg\">$title</lv_label>"
                                  "  </view>"
                                  "</component>",
                                  attrs);

    ASSERT_LABEL_TEXT(ASSERT_NAMED(root, "msg"), "Passed title");
}

/** ...including the prop's declared default when the caller passes nothing. */
static void test_a_prop_default_applies_to_inline_text_when_the_attribute_is_absent(void)
{
    lv_obj_t * root = BUILD("inl_prop_default",
                            "<component>"
                            "  <api><prop name=\"title\" type=\"string\" default=\"Default title\"/></api>"
                            "  <view extends=\"lv_obj\">"
                            "    <lv_label name=\"msg\">$title</lv_label>"
                            "  </view>"
                            "</component>");

    ASSERT_LABEL_TEXT(ASSERT_NAMED(root, "msg"), "Default title");
}

/** And the same for #const lookups. */
static void test_a_const_reference_in_inline_text_is_resolved(void)
{
    lv_obj_t * root = BUILD("inl_const",
                            "<component>"
                            "  <consts><str name=\"inl_greeting\" value=\"Const hello\"/></consts>"
                            "  <view extends=\"lv_obj\">"
                            "    <lv_label name=\"msg\">#inl_greeting</lv_label>"
                            "  </view>"
                            "</component>");

    ASSERT_LABEL_TEXT(ASSERT_NAMED(root, "msg"), "Const hello");
}

/**
 * An unresolvable #const drops the synthesized pair, so nothing is applied at
 * all - the widget keeps its default rather than being given the literal
 * "#inl_no_such_const" as its text.
 */
static void test_an_unknown_const_in_inline_text_applies_nothing(void)
{
    log_capture_start();
    lv_obj_t * root = BUILD("inl_const_unknown",
                            COMPONENT("<lv_label name=\"msg\">#inl_no_such_const</lv_label>"));
    log_capture_stop();

    ASSERT_LABEL_TEXT(ASSERT_NAMED(root, "msg"), LV_LABEL_DEFAULT_TEXT);
    TEST_ASSERT_TRUE_MESSAGE(log_contains("Unknown const"),
                             "an unresolvable const in inline text was dropped silently");
}

/*===========================================================================
 * Translation-tag synthesis
 *
 * The half that only a real pack can demonstrate: inline text is applied as
 * BOTH text= and translation_tag=, so a label built from inline text has to
 * follow the active language exactly like one that declared a tag by hand.
 *==========================================================================*/

#if LV_USE_TRANSLATION

/**
 * en/de/fr, all complete. `cat` exists for the two-tag checkbox cases; `bird`
 * only so the dropdown cases below have a three-option list, which is the
 * shortest one where a selection can be non-zero AND not the last index.
 */
static const char * INLINE_PACK =
    "<translations languages=\"en de fr\">"
    "  <translation tag=\"dog\" en=\"Dog\" de=\"Hund\" fr=\"Chien\"/>"
    "  <translation tag=\"cat\" en=\"Cat\" de=\"Katze\" fr=\"Chat\"/>"
    "  <translation tag=\"bird\" en=\"Bird\" de=\"Vogel\" fr=\"Oiseau\"/>"
    "</translations>";

static void register_inline_pack(void)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_RESULT_OK,
                                  (int)lv_xml_register_translation_from_data(INLINE_PACK),
                                  "the test translation pack failed to register");
}

/**
 * The proof that a tag is synthesized, not just a literal copied into text=.
 * The inline text is "dog"; if only text= had been applied the label would read
 * "dog". It reads "Hund", so translation_tag= was applied too and it won.
 */
static void test_inline_text_is_applied_as_a_translation_tag(void)
{
    register_inline_pack();
    lv_translation_set_language("de");

    lv_obj_t * root = BUILD("inl_i18n", COMPONENT("<lv_label name=\"msg\">dog</lv_label>"));

    ASSERT_LABEL_TEXT(ASSERT_NAMED(root, "msg"), "Hund");
}

/**
 * And the tag is STORED on the widget, not merely consulted once: changing the
 * language after the tree is built has to re-resolve it in place.
 */
static void test_inline_text_re_resolves_on_a_language_change(void)
{
    register_inline_pack();
    lv_translation_set_language("en");

    lv_obj_t * root = BUILD("inl_i18n_switch", COMPONENT("<lv_label name=\"msg\">dog</lv_label>"));
    lv_obj_t * msg = ASSERT_NAMED(root, "msg");
    ASSERT_LABEL_TEXT(msg, "Dog");

    lv_translation_set_language("de");
    helix_test_pump(30);
    ASSERT_LABEL_TEXT(msg, "Hund");

    lv_translation_set_language("fr");
    helix_test_pump(30);
    ASSERT_LABEL_TEXT(msg, "Chien");

    /* Back again, so this cannot pass by resolving to whatever was set last. */
    lv_translation_set_language("en");
    helix_test_pump(30);
    ASSERT_LABEL_TEXT(msg, "Dog");
}

/**
 * The collapsed string is the translation key, not the raw source text. A tag
 * declared as "dog" must still be hit by inline text written across several
 * lines with padding - otherwise every reformat would break every translation.
 */
static void test_the_collapsed_text_is_what_becomes_the_translation_key(void)
{
    register_inline_pack();
    lv_translation_set_language("de");

    lv_obj_t * root = BUILD("inl_i18n_ws",
                            "<component>\n"
                            "  <view extends=\"lv_obj\">\n"
                            "    <lv_label name=\"msg\">\n"
                            "      dog\n"
                            "    </lv_label>\n"
                            "  </view>\n"
                            "</component>");

    ASSERT_LABEL_TEXT(ASSERT_NAMED(root, "msg"), "Hund");
}

/*===========================================================================
 * translation_tag= on <lv_checkbox>
 *
 * A checkbox carries a user-visible string in exactly the sense a label does,
 * but lv_checkbox has no translation_tag field and no
 * LV_EVENT_TRANSLATION_LANGUAGE_CHANGED arm of its own - it stores a bare
 * `char * txt`. The parser therefore owns the tag on an event descriptor and
 * re-resolves from the same tree-wide event lv_translation_set_language() sends.
 *
 * Every case below is written against lv_label as the reference: the checkbox
 * must not merely "work", it must agree with a label given the same attributes.
 *==========================================================================*/

/** Assert @p obj is a checkbox whose text equals @p s. */
#define ASSERT_CHECKBOX_TEXT(obj, s)                                                     \
    do {                                                                                 \
        lv_obj_t * hx_cb_ = (lv_obj_t *)(obj);                                           \
        TEST_ASSERT_NOT_NULL_MESSAGE(hx_cb_, "ASSERT_CHECKBOX_TEXT on a NULL object");   \
        TEST_ASSERT_TRUE_MESSAGE(lv_obj_check_type(hx_cb_, &lv_checkbox_class),          \
                                 "object is not a checkbox, so it has no checkbox text");\
        TEST_ASSERT_EQUAL_STRING_MESSAGE((s), lv_checkbox_get_text(hx_cb_),              \
                                         "wrong text on the checkbox");                  \
    } while(0)

/** The bug this fixes: the tag was parsed and dropped, so the label read "dog". */
static void test_a_translation_tag_on_a_checkbox_resolves_to_the_translated_string(void)
{
    register_inline_pack();
    lv_translation_set_language("de");

    lv_obj_t * root = BUILD("cb_i18n",
                            COMPONENT("<lv_checkbox name=\"opt\" translation_tag=\"dog\"/>"));

    ASSERT_CHECKBOX_TEXT(ASSERT_NAMED(root, "opt"), "Hund");
}

/**
 * The half a parse-time lv_tr() would silently fail: the tag has to be STORED,
 * so a language switch under a built tree updates the checkbox in place. The
 * label in the same component is the control - if only the label moves, the
 * checkbox never joined the language-changed broadcast.
 */
static void test_a_checkbox_translation_tag_re_resolves_on_a_language_change(void)
{
    register_inline_pack();
    lv_translation_set_language("en");

    lv_obj_t * root = BUILD("cb_i18n_switch",
                            COMPONENT("<lv_checkbox name=\"opt\" translation_tag=\"dog\"/>"
                                      "<lv_label name=\"ref\" translation_tag=\"dog\"/>"));
    lv_obj_t * opt = ASSERT_NAMED(root, "opt");
    lv_obj_t * ref = ASSERT_NAMED(root, "ref");
    ASSERT_CHECKBOX_TEXT(opt, "Dog");

    lv_translation_set_language("de");
    helix_test_pump(30);
    ASSERT_LABEL_TEXT(ref, "Hund");
    ASSERT_CHECKBOX_TEXT(opt, "Hund");

    lv_translation_set_language("fr");
    helix_test_pump(30);
    ASSERT_CHECKBOX_TEXT(opt, "Chien");

    /* Back again, so this cannot pass by resolving to whatever was set last. */
    lv_translation_set_language("en");
    helix_test_pump(30);
    ASSERT_LABEL_TEXT(ref, "Dog");
    ASSERT_CHECKBOX_TEXT(opt, "Dog");
}

/** An undeclared tag falls back to the tag string - the same as a label's. */
static void test_an_unknown_translation_tag_on_a_checkbox_shows_the_tag(void)
{
    register_inline_pack();
    lv_translation_set_language("de");

    lv_obj_t * root = BUILD("cb_i18n_unknown",
                            COMPONENT("<lv_checkbox name=\"opt\" translation_tag=\"no_such_tag\"/>"
                                      "<lv_label name=\"ref\" translation_tag=\"no_such_tag\"/>"));

    ASSERT_LABEL_TEXT(ASSERT_NAMED(root, "ref"), "no_such_tag");
    ASSERT_CHECKBOX_TEXT(ASSERT_NAMED(root, "opt"), "no_such_tag");
}

/**
 * PRECEDENCE, pinned against the label rather than invented.
 *
 * lv_label has no fixed rule: lv_label_set_text() clears the stored tag and
 * lv_label_set_translation_tag() overwrites the text, so the winner is whichever
 * attribute the apply loop reaches LAST - i.e. document order. Both orders are
 * asserted, and each is asserted equal to what a label does with the same pair.
 */
static void test_checkbox_text_and_translation_tag_precedence_matches_the_label(void)
{
    register_inline_pack();
    lv_translation_set_language("de");

    lv_obj_t * root = BUILD("cb_i18n_order",
                            COMPONENT("<lv_checkbox name=\"cb_tag_last\" text=\"literal\" translation_tag=\"dog\"/>"
                                      "<lv_label    name=\"lb_tag_last\" text=\"literal\" translation_tag=\"dog\"/>"
                                      "<lv_checkbox name=\"cb_text_last\" translation_tag=\"dog\" text=\"literal\"/>"
                                      "<lv_label    name=\"lb_text_last\" translation_tag=\"dog\" text=\"literal\"/>"));

    /* Whatever the label does, the checkbox does. */
    const char * tag_last = lv_label_get_text(ASSERT_NAMED(root, "lb_tag_last"));
    const char * text_last = lv_label_get_text(ASSERT_NAMED(root, "lb_text_last"));
    ASSERT_CHECKBOX_TEXT(ASSERT_NAMED(root, "cb_tag_last"), tag_last);
    ASSERT_CHECKBOX_TEXT(ASSERT_NAMED(root, "cb_text_last"), text_last);

    /* And that behaviour is order-sensitive, not a constant: if these were equal
     * the two assertions above would pass no matter which attribute won. */
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Hund", tag_last,
                                     "a trailing translation_tag= no longer beats text= on a label");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("literal", text_last,
                                     "a trailing text= no longer beats translation_tag= on a label");
}

/**
 * text= must also STOP the widget following the language, the way
 * lv_label_set_text()'s remove_translation_tag() does. A checkbox that kept the
 * handler armed would snap back to "Hund" on the next switch and silently
 * overwrite the literal.
 */
static void test_a_trailing_text_attribute_stops_the_checkbox_following_the_language(void)
{
    register_inline_pack();
    lv_translation_set_language("en");

    lv_obj_t * root = BUILD("cb_i18n_text_wins",
                            COMPONENT("<lv_checkbox name=\"opt\" translation_tag=\"dog\" text=\"literal\"/>"
                                      "<lv_label name=\"ref\" translation_tag=\"dog\" text=\"literal\"/>"));
    lv_obj_t * opt = ASSERT_NAMED(root, "opt");

    lv_translation_set_language("de");
    helix_test_pump(30);

    ASSERT_LABEL_TEXT(ASSERT_NAMED(root, "ref"), "literal");
    ASSERT_CHECKBOX_TEXT(opt, "literal");
}

/**
 * An apply chain can run twice over one widget - the component's own view, then
 * the attributes of the instance that used it. The second tag has to REPLACE the
 * first, not stack another handler beside it: two armed handlers would race on
 * every language change and the loser's copy would leak.
 */
static void test_a_second_translation_tag_replaces_the_first_rather_than_stacking(void)
{
    register_inline_pack();
    lv_translation_set_language("en");

    ASSERT_XML_REGISTERS("cb_i18n_leaf",
                         "<component><view extends=\"lv_checkbox\" translation_tag=\"dog\"/></component>");

    lv_obj_t * root = BUILD("cb_i18n_host",
                            COMPONENT("<cb_i18n_leaf name=\"plain\"/>"
                                      "<cb_i18n_leaf name=\"opt\" translation_tag=\"cat\"/>"));
    lv_obj_t * plain = ASSERT_NAMED(root, "plain");
    lv_obj_t * opt = ASSERT_NAMED(root, "opt");

    /* Premise check: the view's own tag really does reach the widget, so `opt`
     * genuinely has a tag applied twice. Without this the rest is vacuous. */
    ASSERT_CHECKBOX_TEXT(plain, "Dog");
    ASSERT_CHECKBOX_TEXT(opt, "Cat");

    /* The structural half. Text alone cannot see stacking: the two handlers fire
     * in registration order, so the later tag wins the visible string either way.
     * `plain` carries exactly one registration, so `opt` carrying more means the
     * first tag was never taken down - and its copy was never freed. */
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(lv_obj_get_event_count(plain), lv_obj_get_event_count(opt),
                                     "the overriding translation_tag= stacked another handler "
                                     "instead of replacing the one already there");

    lv_translation_set_language("de");
    helix_test_pump(30);
    ASSERT_CHECKBOX_TEXT(opt, "Katze");
}

/**
 * The attribute is now HANDLED, so the unknown-attribute check must stop
 * reporting it. Kept next to the behaviour it belongs to rather than in
 * test_malformed.c, because it is only true while the branch above exists.
 */
static void test_a_checkbox_translation_tag_is_not_reported_as_an_unknown_attribute(void)
{
    register_inline_pack();

    log_capture_start();
    lv_obj_t * root = BUILD("cb_i18n_quiet",
                            COMPONENT("<lv_checkbox name=\"opt\" translation_tag=\"dog\"/>"));
    log_capture_stop();

    ASSERT_NAMED(root, "opt");
    TEST_ASSERT_FALSE_MESSAGE(log_contains("Unknown attribute"),
                              "translation_tag= on a checkbox still warns as an unknown attribute");
}

/*===========================================================================
 * options_tag= on <lv_dropdown>
 *
 * The dropdown's whole option list is one translatable unit: options_tag= is a
 * newline-separated list of tags, re-resolved as a block on every language
 * change. That block rewrite goes through lv_dropdown_set_options(), which
 * resets sel_opt_id to 0 - so the interesting property is not "the strings
 * translated" but "the strings translated AND the selected index survived".
 *
 * Every case here asserts the index explicitly and uses a non-zero, non-last
 * selection, because a reset-to-0 and a clamp-to-last are both invisible to an
 * assertion that only reads the selected string.
 *==========================================================================*/

/** The `&#10;` is how a newline is written inside an XML attribute value. */
#define DD_TAGS "dog&#10;cat&#10;bird"

/** Assert @p obj is a dropdown selecting index @p idx, whose text is @p s. */
#define ASSERT_DROPDOWN_SELECTED(obj, idx, s)                                            \
    do {                                                                                 \
        lv_obj_t * hx_dd_ = (lv_obj_t *)(obj);                                           \
        TEST_ASSERT_NOT_NULL_MESSAGE(hx_dd_, "ASSERT_DROPDOWN_SELECTED on a NULL object");\
        TEST_ASSERT_TRUE_MESSAGE(lv_obj_check_type(hx_dd_, &lv_dropdown_class),          \
                                 "object is not a dropdown");                            \
        TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)(idx), lv_dropdown_get_selected(hx_dd_), \
                                         "wrong selected INDEX on the dropdown");        \
        char hx_buf_[64];                                                                \
        lv_dropdown_get_selected_str(hx_dd_, hx_buf_, sizeof(hx_buf_));                  \
        TEST_ASSERT_EQUAL_STRING_MESSAGE((s), hx_buf_,                                   \
                                         "wrong selected TEXT on the dropdown");         \
    } while(0)

/** Baseline: the tag list resolves through the active pack at parse time. */
static void test_a_dropdown_options_tag_resolves_to_the_translated_options(void)
{
    register_inline_pack();
    lv_translation_set_language("de");

    lv_obj_t * root = BUILD("dd_i18n",
                            COMPONENT("<lv_dropdown name=\"pick\" options_tag=\"" DD_TAGS "\"/>"));
    lv_obj_t * pick = ASSERT_NAMED(root, "pick");

    TEST_ASSERT_EQUAL_STRING_MESSAGE("Hund\nKatze\nVogel", lv_dropdown_get_options(pick),
                                     "options_tag= did not resolve the whole list");
}

/**
 * THE BUG. lv_dropdown_set_options() resets sel_opt_id to 0 (lv_dropdown.c:208),
 * so re-translating on a language change used to throw away the user's choice
 * and silently jump the widget to its first entry.
 *
 * Index 1 is deliberate: it is neither the value a reset produces (0) nor the
 * value a clamp-to-last would produce (2), so both failure modes are visible.
 * The option strings are asserted too, so the test cannot pass by re-translating
 * nothing at all.
 */
static void test_a_dropdown_options_tag_keeps_the_selection_across_a_language_change(void)
{
    register_inline_pack();
    lv_translation_set_language("en");

    lv_obj_t * root = BUILD("dd_i18n_keep",
                            COMPONENT("<lv_dropdown name=\"pick\" options_tag=\"" DD_TAGS "\"/>"));
    lv_obj_t * pick = ASSERT_NAMED(root, "pick");

    /* Stand in for the user opening the list and picking the middle entry. */
    lv_dropdown_set_selected(pick, 1);
    ASSERT_DROPDOWN_SELECTED(pick, 1, "Cat");

    lv_translation_set_language("de");
    helix_test_pump(30);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Hund\nKatze\nVogel", lv_dropdown_get_options(pick),
                                     "the options did not re-resolve on the language change");
    ASSERT_DROPDOWN_SELECTED(pick, 1, "Katze");

    /* Again, so this cannot pass by coincidentally landing on 1 once. */
    lv_translation_set_language("fr");
    helix_test_pump(30);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Chien\nChat\nOiseau", lv_dropdown_get_options(pick),
                                     "the options did not re-resolve on the second change");
    ASSERT_DROPDOWN_SELECTED(pick, 1, "Chat");

    /* And a selection made under one language survives into another. */
    lv_dropdown_set_selected(pick, 2);
    lv_translation_set_language("en");
    helix_test_pump(30);
    ASSERT_DROPDOWN_SELECTED(pick, 2, "Bird");
}

/**
 * The same reset, reached through the apply loop instead of an event: attributes
 * are applied in document order, so a selected= written BEFORE options_tag= was
 * being wiped by the parse-time translate before the widget was ever shown.
 */
static void test_a_selected_attribute_before_options_tag_is_not_wiped_by_it(void)
{
    register_inline_pack();
    lv_translation_set_language("de");

    lv_obj_t * root = BUILD("dd_i18n_order",
                            COMPONENT("<lv_dropdown name=\"pick\" selected=\"1\""
                                      " options_tag=\"" DD_TAGS "\"/>"));

    ASSERT_DROPDOWN_SELECTED(ASSERT_NAMED(root, "pick"), 1, "Katze");
}

/*===========================================================================
 * placeholder_tag= on <lv_textarea>
 *
 * Same shape as the checkbox: lv_textarea has no tag field and no
 * LV_EVENT_TRANSLATION_LANGUAGE_CHANGED arm, so the parser owns the tag on an
 * event descriptor and re-resolves from the tree-wide language event.
 *
 * This replaced a parse-time lv_tr() whose comment claimed re-resolution was
 * unnecessary "because inputs live in modals/wizards recreated on each open".
 * Nothing enforced that, and it was false for three long-lived panels in the
 * application. The tests below therefore switch the language under an ALREADY
 * BUILT tree, which is exactly the case the old code got wrong.
 *==========================================================================*/

/** Assert @p obj is a textarea whose placeholder equals @p s. */
#define ASSERT_PLACEHOLDER(obj, s)                                                       \
    do {                                                                                 \
        lv_obj_t * hx_ta_ = (lv_obj_t *)(obj);                                           \
        TEST_ASSERT_NOT_NULL_MESSAGE(hx_ta_, "ASSERT_PLACEHOLDER on a NULL object");     \
        TEST_ASSERT_TRUE_MESSAGE(lv_obj_check_type(hx_ta_, &lv_textarea_class),          \
                                 "object is not a textarea, so it has no placeholder");  \
        TEST_ASSERT_EQUAL_STRING_MESSAGE((s), lv_textarea_get_placeholder_text(hx_ta_),  \
                                         "wrong placeholder on the textarea");           \
    } while(0)

/** Baseline: the tag resolves through the active pack at parse time. */
static void test_a_placeholder_tag_resolves_to_the_translated_string(void)
{
    register_inline_pack();
    lv_translation_set_language("de");

    lv_obj_t * root = BUILD("ta_i18n",
                            COMPONENT("<lv_textarea name=\"inp\" placeholder_tag=\"dog\"/>"));

    ASSERT_PLACEHOLDER(ASSERT_NAMED(root, "inp"), "Hund");
}

/**
 * THE BUG. The tag has to be STORED, not consulted once, so a language switch
 * under a built tree updates the placeholder in place. The label in the same
 * component is the control: if only the label moves, the textarea never joined
 * the language-changed broadcast.
 */
static void test_a_placeholder_tag_re_resolves_on_a_language_change(void)
{
    register_inline_pack();
    lv_translation_set_language("en");

    lv_obj_t * root = BUILD("ta_i18n_switch",
                            COMPONENT("<lv_textarea name=\"inp\" placeholder_tag=\"dog\"/>"
                                      "<lv_label name=\"ref\" translation_tag=\"dog\"/>"));
    lv_obj_t * inp = ASSERT_NAMED(root, "inp");
    lv_obj_t * ref = ASSERT_NAMED(root, "ref");
    ASSERT_PLACEHOLDER(inp, "Dog");

    lv_translation_set_language("de");
    helix_test_pump(30);
    ASSERT_LABEL_TEXT(ref, "Hund");
    ASSERT_PLACEHOLDER(inp, "Hund");

    lv_translation_set_language("fr");
    helix_test_pump(30);
    ASSERT_PLACEHOLDER(inp, "Chien");

    /* Back again, so this cannot pass by resolving to whatever was set last. */
    lv_translation_set_language("en");
    helix_test_pump(30);
    ASSERT_LABEL_TEXT(ref, "Dog");
    ASSERT_PLACEHOLDER(inp, "Dog");
}

/**
 * A trailing placeholder_text= must win AND stop the widget following the
 * language, the way lv_label_set_text() clears a label's stored tag. A textarea
 * that kept the handler armed would snap back to "Hund" on the next switch and
 * silently overwrite the literal.
 */
static void test_a_trailing_placeholder_text_stops_the_textarea_following_the_language(void)
{
    register_inline_pack();
    lv_translation_set_language("en");

    lv_obj_t * root = BUILD("ta_i18n_text_wins",
                            COMPONENT("<lv_textarea name=\"inp\" placeholder_tag=\"dog\""
                                      " placeholder_text=\"literal\"/>"));
    lv_obj_t * inp = ASSERT_NAMED(root, "inp");
    ASSERT_PLACEHOLDER(inp, "literal");

    lv_translation_set_language("de");
    helix_test_pump(30);
    ASSERT_PLACEHOLDER(inp, "literal");
}

/** And the other order still lets the tag win, so precedence is document order. */
static void test_a_trailing_placeholder_tag_beats_an_earlier_placeholder_text(void)
{
    register_inline_pack();
    lv_translation_set_language("de");

    lv_obj_t * root = BUILD("ta_i18n_tag_wins",
                            COMPONENT("<lv_textarea name=\"inp\" placeholder_text=\"literal\""
                                      " placeholder_tag=\"dog\"/>"));

    ASSERT_PLACEHOLDER(ASSERT_NAMED(root, "inp"), "Hund");
}

/**
 * An apply chain can run twice over one widget - the component's own view, then
 * the attributes of the instance that used it. (The application's <text_input>
 * wrapper forwards the whole attribute array into this parser, so this is not
 * hypothetical.) The second tag has to REPLACE the first: two armed handlers
 * would race on every language change and the loser's copy would leak - which
 * LeakSanitizer would then fail the build over.
 */
static void test_a_second_placeholder_tag_replaces_the_first_rather_than_stacking(void)
{
    register_inline_pack();
    lv_translation_set_language("en");

    ASSERT_XML_REGISTERS("ta_i18n_leaf",
                         "<component><view extends=\"lv_textarea\" placeholder_tag=\"dog\"/></component>");

    lv_obj_t * root = BUILD("ta_i18n_host",
                            COMPONENT("<ta_i18n_leaf name=\"plain\"/>"
                                      "<ta_i18n_leaf name=\"inp\" placeholder_tag=\"cat\"/>"));
    lv_obj_t * plain = ASSERT_NAMED(root, "plain");
    lv_obj_t * inp = ASSERT_NAMED(root, "inp");

    /* Premise check: the view's own tag really does reach the widget, so `inp`
     * genuinely has a tag applied twice. Without this the rest is vacuous. */
    ASSERT_PLACEHOLDER(plain, "Dog");
    ASSERT_PLACEHOLDER(inp, "Cat");

    /* The structural half. Text alone cannot see stacking: the two handlers fire
     * in registration order, so the later tag wins the visible string either way.
     * `plain` carries exactly one registration, so `inp` carrying more means the
     * first tag was never taken down - and its copy was never freed. */
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(lv_obj_get_event_count(plain), lv_obj_get_event_count(inp),
                                     "the overriding placeholder_tag= stacked another handler "
                                     "instead of replacing the one already there");

    lv_translation_set_language("de");
    helix_test_pump(30);
    ASSERT_PLACEHOLDER(inp, "Katze");
}

/* lv_mem_monitor() only reports anything under the BUILTIN allocator; an
 * LV_STDLIB_CLIB build answers zero to everything, and a heap assertion there
 * would fail on the configuration rather than on the behaviour. Gate the
 * accounting so the CLIB/ASAN run still executes the create-destroy cycle -
 * that is the run where the sanitizer, not this counter, is the adjudicator. */
#if LV_USE_STDLIB_MALLOC == LV_STDLIB_BUILTIN
    #define ASSERT_TAG_HEAP(cond, msg) TEST_ASSERT_TRUE_MESSAGE((cond), (msg))
#else
    #define ASSERT_TAG_HEAP(cond, msg) do { (void)(msg); } while(0)
#endif

/**
 * The tag is a lv_strdup() the parser owns, handed to LVGL only as event user
 * data - so nothing frees it except the LV_EVENT_DELETE arm registered beside
 * the language handler. Drop that arm and every textarea built with a
 * placeholder_tag leaks its tag for the life of the process.
 *
 * CI's sanitize job would catch this with LeakSanitizer, but LSan does not exist
 * on every host the suite runs on (macOS/arm64 refuses detect_leaks outright), so
 * the invariant is pinned here in the allocator LVGL can account for itself.
 * Measured as a delta between whole build-and-destroy cycles rather than against
 * a cold baseline, so one-time allocations elsewhere cannot read as a leak: a
 * per-cycle leak shows up as free_size falling from one cycle to the next.
 */
static void test_a_placeholder_tag_is_freed_when_the_textarea_is_deleted(void)
{
    register_inline_pack();
    lv_translation_set_language("en");

    ASSERT_XML_REGISTERS("ta_i18n_leak",
                         COMPONENT("<lv_textarea name=\"inp\" placeholder_tag=\"dog\"/>"));

    size_t after_cycle[3];
    for(uint32_t c = 0; c < 3; c++) {
        lv_obj_t * inst = XML_CREATE(helix_test_env_screen(), "ta_i18n_leak", NULL);
        helix_test_pump(30);
        ASSERT_PLACEHOLDER(ASSERT_NAMED(inst, "inp"), "Dog");

        lv_obj_delete(inst);
        helix_test_pump(30);

        lv_mem_monitor_t mon;
        lv_mem_monitor(&mon);
        after_cycle[c] = mon.free_size;
    }

    /* Cycle 0 is a warm-up and its figure is discarded - the first pass costs
     * one-time state that later passes do not. Cycles 1 and 2 are like for like. */
    ASSERT_TAG_HEAP(after_cycle[2] >= after_cycle[1],
                    "free_size fell across identical build-and-destroy cycles - the "
                    "placeholder tag copy is not being freed on LV_EVENT_DELETE");
}

/*===========================================================================
 * translation_tag= on <lv_tabview-tab>
 *
 * The odd one out. A tab's title is fixed when the tab is built, so the parser
 * cannot handle the attribute in an apply chain the way the checkbox and the
 * textarea do - it has to choose between lv_tabview_add_tab() and
 * lv_tabview_set_tab_translation_tag() at CREATE time.
 *
 * The payoff is that re-resolution comes for free: the tag lands on the tab
 * button's real lv_label, so the label's own language handler does the work and
 * the parser stores nothing. The cases below still assert the re-resolution,
 * because "LVGL handles it" is exactly the kind of claim that stops being true
 * silently.
 *==========================================================================*/

/** The visible title of tab @p idx: the label LVGL puts inside the tab button. */
static const char * tab_title(lv_obj_t * tv, int32_t idx)
{
    lv_obj_t * btn = lv_tabview_get_tab_button(tv, idx);
    TEST_ASSERT_NOT_NULL_MESSAGE(btn, "the tabview has no button at that index");
    lv_obj_t * lbl = lv_obj_get_child_by_type(btn, 0, &lv_label_class);
    TEST_ASSERT_NOT_NULL_MESSAGE(lbl, "the tab button has no label");
    return lv_label_get_text(lbl);
}

/** Baseline: the tag resolves through the active pack when the tab is built. */
static void test_a_tab_translation_tag_resolves_to_the_translated_title(void)
{
    register_inline_pack();
    lv_translation_set_language("de");

    lv_obj_t * root = BUILD("tv_i18n",
                            COMPONENT("<lv_tabview name=\"tv\">"
                                      "  <lv_tabview-tab translation_tag=\"dog\"/>"
                                      "  <lv_tabview-tab text=\"literal\"/>"
                                      "</lv_tabview>"));
    lv_obj_t * tv = ASSERT_NAMED(root, "tv");

    TEST_ASSERT_EQUAL_STRING_MESSAGE("Hund", tab_title(tv, 0),
                                     "translation_tag= on a tab was not resolved");
    /* The control: a plain text= tab in the same tabview is untouched, so the
     * assertion above cannot be satisfied by some tabview-wide translation. */
    TEST_ASSERT_EQUAL_STRING_MESSAGE("literal", tab_title(tv, 1),
                                     "text= on a tab stopped working");
}

/**
 * The half that distinguishes a stored tag from a one-shot lv_tr(). This works
 * only because lv_tabview_set_tab_translation_tag() delegates to
 * lv_label_set_translation_tag() on the button's label - if that delegation ever
 * changes to a plain lv_label_set_text(), this is what catches it.
 */
static void test_a_tab_translation_tag_re_resolves_on_a_language_change(void)
{
    register_inline_pack();
    lv_translation_set_language("en");

    lv_obj_t * root = BUILD("tv_i18n_switch",
                            COMPONENT("<lv_tabview name=\"tv\">"
                                      "  <lv_tabview-tab translation_tag=\"dog\"/>"
                                      "  <lv_tabview-tab text=\"literal\"/>"
                                      "</lv_tabview>"));
    lv_obj_t * tv = ASSERT_NAMED(root, "tv");
    TEST_ASSERT_EQUAL_STRING("Dog", tab_title(tv, 0));

    lv_translation_set_language("de");
    helix_test_pump(30);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Hund", tab_title(tv, 0),
                                     "the tab title did not follow the language change");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("literal", tab_title(tv, 1),
                                     "a literal tab title was overwritten by the language change");

    /* Back again, so this cannot pass by resolving to whatever was set last. */
    lv_translation_set_language("en");
    helix_test_pump(30);
    TEST_ASSERT_EQUAL_STRING("Dog", tab_title(tv, 0));
}

/**
 * PRECEDENCE, and the reason it is worth its own test: it does NOT match the
 * checkbox's. Both attributes are read from the same array before the tab
 * exists, so there is no apply order to observe and translation_tag= wins in
 * either writing order. Both orders are asserted so the rule cannot quietly
 * become order-sensitive.
 */
static void test_a_tab_translation_tag_beats_text_in_either_order(void)
{
    register_inline_pack();
    lv_translation_set_language("de");

    lv_obj_t * root = BUILD("tv_i18n_order",
                            COMPONENT("<lv_tabview name=\"tv\">"
                                      "  <lv_tabview-tab text=\"literal\" translation_tag=\"dog\"/>"
                                      "  <lv_tabview-tab translation_tag=\"dog\" text=\"literal\"/>"
                                      "</lv_tabview>"));
    lv_obj_t * tv = ASSERT_NAMED(root, "tv");

    TEST_ASSERT_EQUAL_STRING_MESSAGE("Hund", tab_title(tv, 0),
                                     "translation_tag= lost to an earlier text= on a tab");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Hund", tab_title(tv, 1),
                                     "translation_tag= lost to a later text= on a tab");
}

/** An empty tag is treated as absent, so text= still applies rather than blanking. */
static void test_an_empty_tab_translation_tag_falls_through_to_text(void)
{
    register_inline_pack();
    lv_translation_set_language("de");

    lv_obj_t * root = BUILD("tv_i18n_empty",
                            COMPONENT("<lv_tabview name=\"tv\">"
                                      "  <lv_tabview-tab text=\"literal\" translation_tag=\"\"/>"
                                      "</lv_tabview>"));

    TEST_ASSERT_EQUAL_STRING_MESSAGE("literal", tab_title(ASSERT_NAMED(root, "tv"), 0),
                                     "an empty translation_tag= blanked the tab title");
}

/**
 * The attribute is consumed at create time, so the unknown-attribute check has
 * to be told about it explicitly - nothing in the apply chain ever sees it.
 * Without the lv_xml_attr_check_consume() call this warns on every tagged tab.
 */
static void test_a_tab_translation_tag_is_not_reported_as_an_unknown_attribute(void)
{
    register_inline_pack();

    log_capture_start();
    lv_obj_t * root = BUILD("tv_i18n_quiet",
                            COMPONENT("<lv_tabview name=\"tv\">"
                                      "  <lv_tabview-tab translation_tag=\"dog\"/>"
                                      "</lv_tabview>"));
    log_capture_stop();

    ASSERT_NAMED(root, "tv");
    TEST_ASSERT_FALSE_MESSAGE(log_contains("Unknown attribute"),
                              "translation_tag= on a tab still warns as an unknown attribute");
}

#endif /* LV_USE_TRANSLATION */

/*---------------------------------------------------------------------------
 * main
 *--------------------------------------------------------------------------*/

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_inline_element_text_becomes_the_label_text);
    RUN_TEST(test_inline_text_on_a_component_instance_applies_through_what_it_extends);

    RUN_TEST(test_leading_and_trailing_whitespace_is_trimmed);
    RUN_TEST(test_internal_whitespace_runs_collapse_to_a_single_space);
    RUN_TEST(test_newlines_tabs_and_carriage_returns_collapse_like_spaces);
    RUN_TEST(test_a_numeric_newline_reference_collapses_to_a_space);

    RUN_TEST(test_xml_entities_in_inline_text_are_decoded);

    RUN_TEST(test_whitespace_only_content_is_not_applied_as_text);
    RUN_TEST(test_whitespace_only_content_leaves_the_text_attribute_alone_and_silent);

    RUN_TEST(test_text_before_and_after_a_child_lands_on_the_owning_element);
    RUN_TEST(test_a_widget_with_no_text_property_ignores_inline_text);
    RUN_TEST(test_inline_text_on_the_root_view_element_is_dropped);

    RUN_TEST(test_a_text_attribute_beats_inline_element_text);
#if LV_USE_TRANSLATION
    RUN_TEST(test_a_translation_tag_attribute_beats_inline_element_text);
#endif
    RUN_TEST(test_a_bind_text_attribute_beats_inline_element_text);

    RUN_TEST(test_a_prop_reference_in_inline_text_is_substituted);
    RUN_TEST(test_a_prop_default_applies_to_inline_text_when_the_attribute_is_absent);
    RUN_TEST(test_a_const_reference_in_inline_text_is_resolved);
    RUN_TEST(test_an_unknown_const_in_inline_text_applies_nothing);

#if LV_USE_TRANSLATION
    RUN_TEST(test_inline_text_is_applied_as_a_translation_tag);
    RUN_TEST(test_inline_text_re_resolves_on_a_language_change);
    RUN_TEST(test_the_collapsed_text_is_what_becomes_the_translation_key);

    RUN_TEST(test_a_translation_tag_on_a_checkbox_resolves_to_the_translated_string);
    RUN_TEST(test_a_checkbox_translation_tag_re_resolves_on_a_language_change);
    RUN_TEST(test_an_unknown_translation_tag_on_a_checkbox_shows_the_tag);
    RUN_TEST(test_checkbox_text_and_translation_tag_precedence_matches_the_label);
    RUN_TEST(test_a_trailing_text_attribute_stops_the_checkbox_following_the_language);
    RUN_TEST(test_a_second_translation_tag_replaces_the_first_rather_than_stacking);
    RUN_TEST(test_a_checkbox_translation_tag_is_not_reported_as_an_unknown_attribute);

    RUN_TEST(test_a_dropdown_options_tag_resolves_to_the_translated_options);
    RUN_TEST(test_a_dropdown_options_tag_keeps_the_selection_across_a_language_change);
    RUN_TEST(test_a_selected_attribute_before_options_tag_is_not_wiped_by_it);

    RUN_TEST(test_a_placeholder_tag_resolves_to_the_translated_string);
    RUN_TEST(test_a_placeholder_tag_re_resolves_on_a_language_change);
    RUN_TEST(test_a_trailing_placeholder_text_stops_the_textarea_following_the_language);
    RUN_TEST(test_a_trailing_placeholder_tag_beats_an_earlier_placeholder_text);
    RUN_TEST(test_a_second_placeholder_tag_replaces_the_first_rather_than_stacking);
    RUN_TEST(test_a_placeholder_tag_is_freed_when_the_textarea_is_deleted);

    RUN_TEST(test_a_tab_translation_tag_resolves_to_the_translated_title);
    RUN_TEST(test_a_tab_translation_tag_re_resolves_on_a_language_change);
    RUN_TEST(test_a_tab_translation_tag_beats_text_in_either_order);
    RUN_TEST(test_an_empty_tab_translation_tag_falls_through_to_text);
    RUN_TEST(test_a_tab_translation_tag_is_not_reported_as_an_unknown_attribute);
#endif

    return UNITY_END();
}
