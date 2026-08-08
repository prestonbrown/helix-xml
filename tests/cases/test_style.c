/**
 * @file test_style.c
 *
 * src/xml/lv_xml_style.c, exercised through XML rather than through the
 * converters.
 *
 * The converter layer (`lv_xml_style_prop_to_enum`, `lv_xml_style_selector_text_to_enum`,
 * ...) already has exhaustive coverage in tests/cases/test_base_types.c. What is
 * NOT covered anywhere is the file that stitches those converters into the
 * dialect: registering a `<style>` into a component scope, resolving it back by
 * name (with and without a `component.` prefix, with and without a fallback to
 * `globals`), and getting the resulting `lv_style_t` onto the right PART and the
 * right STATE of a real widget. Every assertion below reads a style property the
 * XML under test declared - never a measured size, never a font metric.
 *
 * ---------------------------------------------------------------------------
 * THE CASCADE, which is the trap this file exists for
 *
 * An inline `style_*` attribute (`style_radius="9"`) becomes an LVGL *local*
 * style, and lv_obj_add_style()/lv_obj_bind_style() insert normal styles AFTER
 * every local one in obj->styles[]. The property lookup returns the first hit,
 * so the inline attribute always wins over a `<style>` or a `<bind_style>` that
 * sets the same property - regardless of which one the XML mentions first. Two
 * tests below pin that in both directions, because the usual way people try to
 * "fix" a style that isn't taking effect is to add an inline attribute, and the
 * usual way people are then surprised is by trying to override that attribute
 * from a style later.
 * ---------------------------------------------------------------------------
 *
 * NOT TESTED, DELIBERATELY
 *
 *  - lv_xml_component_get_grad(NULL, name). It dereferences `scope` with no
 *    guard (LV_LL_READ(&scope->gradient_ll, ...)), so a NULL scope is a crash,
 *    not a return value. lv_xml_get_style_by_name() DOES guard, and that guard
 *    is tested.
 *  - a `style` reference whose component-name prefix is >= 256 characters. The
 *    over-long path is a plain early return, but the test would have to hold a
 *    260-byte literal for no additional information.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <string.h>

#include "helpers/helix_test_env.h"
#include "helpers/helix_test_pump.h"
#include "helpers/xml_assert.h"

#include "xml/lv_xml_base_types.h"
#include "xml/lv_xml_component_private.h"
#include "xml/lv_xml_style.h"

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
 * Log capture
 *
 * Same shape as the helper in tests/cases/test_base_types.c. It cannot be
 * shared: every file under cases/ is its own executable and the helper is file-static
 * there, so the alternative would be adding a header to tests/helpers/ that
 * only two files use.
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
 * Local assertions
 *--------------------------------------------------------------------------*/

/** ASSERT_STYLE_INT's colour sibling. `expected` is 0xRRGGBB; alpha is masked. */
#define ASSERT_STYLE_COLOR(obj, prop, part, expected)                                    \
    do {                                                                                 \
        lv_obj_t * hx_o_ = (lv_obj_t *)(obj);                                            \
        TEST_ASSERT_NOT_NULL_MESSAGE(hx_o_, "ASSERT_STYLE_COLOR on a NULL object");      \
        lv_style_value_t hx_v_ = lv_obj_get_style_prop(hx_o_, (part), (prop));           \
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(                                                 \
            (uint32_t)(expected), lv_color_to_u32(hx_v_.color) & 0x00FFFFFFu,            \
            helix_xml_assert_msgf("wrong %s on \"%s\" (part %s)",                        \
                                  #prop, helix_xml_assert_name_of(hx_o_), #part));       \
    } while(0)

/** Assert an integer style property is NOT the given value. */
#define ASSERT_STYLE_INT_NOT(obj, prop, part, unexpected)                                \
    do {                                                                                 \
        lv_obj_t * hx_o_ = (lv_obj_t *)(obj);                                            \
        TEST_ASSERT_NOT_NULL_MESSAGE(hx_o_, "ASSERT_STYLE_INT_NOT on a NULL object");    \
        lv_style_value_t hx_v_ = lv_obj_get_style_prop(hx_o_, (part), (prop));           \
        TEST_ASSERT_TRUE_MESSAGE(                                                        \
            (int32_t)(unexpected) != (int32_t)hx_v_.num,                                 \
            helix_xml_assert_msgf("%s on \"%s\" (part %s) leaked the value %d",          \
                                  #prop, helix_xml_assert_name_of(hx_o_), #part,         \
                                  (int)(int32_t)(unexpected)));                          \
    } while(0)

/** Read an integer property straight out of a registered lv_xml_style_t. */
static int32_t style_prop_num(const lv_xml_style_t * xs, lv_style_prop_t prop)
{
    lv_style_value_t v;
    lv_style_res_t res = lv_style_get_prop((lv_style_t *)&xs->style, prop, &v);
    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_STYLE_RES_FOUND, (int)res,
                                  "the property is not present in the registered style at all");
    return v.num;
}

static bool style_has_prop(const lv_xml_style_t * xs, lv_style_prop_t prop)
{
    lv_style_value_t v;
    return lv_style_get_prop((lv_style_t *)&xs->style, prop, &v) == LV_STYLE_RES_FOUND;
}

/*---------------------------------------------------------------------------
 * Fixtures
 *
 * The numbers are deliberately odd (37, 41, 23, 11, 19...) so that a value the
 * default theme happens to supply can never be mistaken for a value the XML set.
 *--------------------------------------------------------------------------*/

/* A style declared in a <styles> block, applied by a <style name=".."/> child. */
static const char * STYLE_BASIC_XML =
    "<component>"
    "  <styles>"
    "    <style name=\"boxy\" radius=\"37\" pad_all=\"11\" border_width=\"5\"/>"
    "  </styles>"
    "  <view extends=\"lv_obj\" name=\"basic_root\">"
    "    <lv_obj name=\"styled\">"
    "      <style name=\"boxy\"/>"
    "    </lv_obj>"
    "    <lv_obj name=\"bare\"/>"
    "  </view>"
    "</component>";

/*===========================================================================
 * <styles> -> <style name="..."/> -> the object
 *==========================================================================*/

/**
 * The whole point of the file in one test: a style declared inside `<styles>`
 * must reach the object that references it, with the values the XML wrote, and
 * must NOT reach a sibling that does not reference it.
 */
static void test_style_from_a_styles_block_lands_on_the_referencing_widget(void)
{
    ASSERT_XML_REGISTERS("style_basic", STYLE_BASIC_XML);

    lv_obj_t * root = XML_CREATE(helix_test_env_screen(), "style_basic", NULL);
    helix_test_pump(30);

    lv_obj_t * styled = ASSERT_NAMED(root, "styled");
    lv_obj_t * bare   = ASSERT_NAMED(root, "bare");

    ASSERT_STYLE_INT(styled, LV_STYLE_RADIUS, LV_PART_MAIN, 37);
    ASSERT_STYLE_INT(styled, LV_STYLE_BORDER_WIDTH, LV_PART_MAIN, 5);
    /* pad_all is one attribute that fans out to four properties. */
    ASSERT_STYLE_INT(styled, LV_STYLE_PAD_LEFT, LV_PART_MAIN, 11);
    ASSERT_STYLE_INT(styled, LV_STYLE_PAD_RIGHT, LV_PART_MAIN, 11);
    ASSERT_STYLE_INT(styled, LV_STYLE_PAD_TOP, LV_PART_MAIN, 11);
    ASSERT_STYLE_INT(styled, LV_STYLE_PAD_BOTTOM, LV_PART_MAIN, 11);

    /* A sibling that never named the style must be untouched by it. */
    ASSERT_STYLE_INT_NOT(bare, LV_STYLE_RADIUS, LV_PART_MAIN, 37);
    ASSERT_STYLE_INT_NOT(bare, LV_STYLE_BORDER_WIDTH, LV_PART_MAIN, 5);
    ASSERT_STYLE_INT_NOT(bare, LV_STYLE_PAD_LEFT, LV_PART_MAIN, 11);
}

/*===========================================================================
 * lv_xml_get_style_by_name
 *==========================================================================*/

static void test_get_style_by_name_resolves_plain_and_component_qualified_names(void)
{
    ASSERT_XML_REGISTERS("style_basic", STYLE_BASIC_XML);

    lv_xml_component_scope_t * scope = lv_xml_component_get_scope("style_basic");
    TEST_ASSERT_NOT_NULL(scope);

    lv_xml_style_t * s = lv_xml_get_style_by_name(scope, "boxy");
    TEST_ASSERT_NOT_NULL_MESSAGE(s, "a style registered in this scope must resolve by its plain name");
    TEST_ASSERT_EQUAL_STRING("boxy", s->name);
    /* long_name is "<scope>.<style>" and is what makes cross-component
     * references addressable at all. */
    TEST_ASSERT_EQUAL_STRING("style_basic.boxy", s->long_name);
    TEST_ASSERT_EQUAL_INT32(37, style_prop_num(s, LV_STYLE_RADIUS));
    TEST_ASSERT_EQUAL_INT32(5, style_prop_num(s, LV_STYLE_BORDER_WIDTH));

    /* The dotted form must reach the same record from an UNRELATED scope -
     * that is the only way one component can borrow another's style. */
    ASSERT_XML_REGISTERS("style_other",
                         "<component><view extends=\"lv_obj\" name=\"other_root\"/></component>");
    lv_xml_component_scope_t * other = lv_xml_component_get_scope("style_other");
    TEST_ASSERT_NOT_NULL(other);

    TEST_ASSERT_NULL_MESSAGE(lv_xml_get_style_by_name(other, "boxy"),
                             "an unqualified name must NOT leak across component scopes");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(s, lv_xml_get_style_by_name(other, "style_basic.boxy"),
                                  "the `component.style` form must resolve to the same record");
}

static void test_get_style_by_name_warns_and_returns_null_for_unknown_names(void)
{
    ASSERT_XML_REGISTERS("style_basic", STYLE_BASIC_XML);
    lv_xml_component_scope_t * scope = lv_xml_component_get_scope("style_basic");
    TEST_ASSERT_NOT_NULL(scope);

    log_capture_start();
    TEST_ASSERT_NULL(lv_xml_get_style_by_name(scope, "no_such_style"));
    log_capture_stop();
    TEST_ASSERT_TRUE_MESSAGE(log_contains("No style found with no_such_style name"),
                             "an unresolvable style name must be reported - NULL alone is silent");

    /* An unknown component prefix is a different failure and gets its own line
     * before the generic one, because "the component is missing" and "the
     * component exists but has no such style" need different fixes. */
    log_capture_start();
    TEST_ASSERT_NULL(lv_xml_get_style_by_name(scope, "no_such_component.boxy"));
    log_capture_stop();
    TEST_ASSERT_TRUE(log_contains("'no_such_component' component or widget is not found"));
    TEST_ASSERT_TRUE(log_contains("No style found with no_such_component.boxy name"));

    /* NULL scope is guarded: it means "look in globals", which here is empty. */
    log_capture_start();
    TEST_ASSERT_NULL_MESSAGE(lv_xml_get_style_by_name(NULL, "boxy"),
                             "a NULL scope must fall back to globals, not to the last-used scope");
    log_capture_stop();
    TEST_ASSERT_TRUE(log_contains("No style found with boxy name"));
}

/**
 * A style that is not in the component's own scope must still be found if
 * `globals` has one by that name. This is how a shared design-token style is
 * reachable from every component without a prefix.
 */
static void test_get_style_by_name_falls_back_to_the_globals_scope(void)
{
    ASSERT_XML_REGISTERS("globals",
                         "<globals>"
                         "  <styles>"
                         "    <style name=\"shared\" radius=\"29\"/>"
                         "  </styles>"
                         "</globals>");

    ASSERT_XML_REGISTERS("style_consumer",
                         "<component>"
                         "  <view extends=\"lv_obj\" name=\"consumer_root\">"
                         "    <lv_obj name=\"borrower\">"
                         "      <style name=\"shared\"/>"
                         "    </lv_obj>"
                         "  </view>"
                         "</component>");

    lv_xml_component_scope_t * consumer = lv_xml_component_get_scope("style_consumer");
    TEST_ASSERT_NOT_NULL(consumer);

    lv_xml_style_t * s = lv_xml_get_style_by_name(consumer, "shared");
    TEST_ASSERT_NOT_NULL_MESSAGE(s, "a globals style must be reachable from a component scope");
    TEST_ASSERT_EQUAL_STRING("globals.shared", s->long_name);
    TEST_ASSERT_EQUAL_INT32(29, style_prop_num(s, LV_STYLE_RADIUS));

    /* And it must actually be applied when the component is instantiated. */
    lv_obj_t * root = XML_CREATE(helix_test_env_screen(), "style_consumer", NULL);
    helix_test_pump(30);
    ASSERT_STYLE_INT(ASSERT_NAMED(root, "borrower"), LV_STYLE_RADIUS, LV_PART_MAIN, 29);
}

/*===========================================================================
 * lv_xml_style_string_process
 *==========================================================================*/

/**
 * The splitter behind every inline `style_*-state-part` attribute name. It
 * mutates its input in place, so every case needs its own writable array.
 */
static void test_style_string_process_splits_the_name_from_state_and_part_tokens(void)
{
    lv_style_selector_t sel = 0xDEAD;

    char plain[] = "style_radius";
    TEST_ASSERT_EQUAL_STRING("style_radius", lv_xml_style_string_process(plain, &sel));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, sel, "no '-' token means the default state and the main part");

    char state_only[] = "style_bg_color-pressed";
    sel = 0xDEAD;
    TEST_ASSERT_EQUAL_STRING("style_bg_color", lv_xml_style_string_process(state_only, &sel));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)LV_STATE_PRESSED, (uint32_t)sel);

    char part_only[] = "style_bg_color-knob";
    sel = 0xDEAD;
    TEST_ASSERT_EQUAL_STRING("style_bg_color", lv_xml_style_string_process(part_only, &sel));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)LV_PART_KNOB, (uint32_t)sel);

    /* States and parts share one token stream and are OR'd together, in any
     * order and in any number. */
    char both[] = "style_bg_color-pressed-knob";
    sel = 0;
    TEST_ASSERT_EQUAL_STRING("style_bg_color", lv_xml_style_string_process(both, &sel));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(LV_STATE_PRESSED | LV_PART_KNOB), (uint32_t)sel);

    char reversed[] = "style_bg_color-knob-pressed";
    sel = 0;
    TEST_ASSERT_EQUAL_STRING("style_bg_color", lv_xml_style_string_process(reversed, &sel));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(LV_STATE_PRESSED | LV_PART_KNOB), (uint32_t)sel);

    char two_states[] = "style_radius-checked-disabled";
    sel = 0;
    TEST_ASSERT_EQUAL_STRING("style_radius", lv_xml_style_string_process(two_states, &sel));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(LV_STATE_CHECKED | LV_STATE_DISABLED), (uint32_t)sel);

    /* The selector out-param is always written, even when nothing is parsed -
     * callers pass an uninitialised local. */
    char trailing[] = "style_radius-";
    sel = 0xDEAD;
    TEST_ASSERT_EQUAL_STRING("style_radius", lv_xml_style_string_process(trailing, &sel));
    TEST_ASSERT_EQUAL_UINT32(0, sel);
}

/**
 * PINS CURRENT BEHAVIOUR - suspected bug: a misspelled state/part token in an
 * inline style attribute name (`style_radius-presed`) is silently discarded.
 * lv_xml_style_string_process() ORs lv_xml_style_state_to_enum() and
 * lv_xml_style_part_to_enum() together, and BOTH return 0 for an unrecognised
 * string without logging - so the selector collapses to
 * LV_STATE_DEFAULT|LV_PART_MAIN and the property is applied unconditionally
 * instead of only in the intended state. The other selector path,
 * lv_xml_style_selector_text_to_enum() (the `selector="..."` attribute), warns
 * on exactly the same input; see the sibling test below. The two paths must
 * eventually agree.
 */
static void test_style_string_process_silently_swallows_an_unknown_selector_token(void)
{
    lv_style_selector_t sel = 0xDEAD;

    log_capture_start();
    char typo[] = "style_radius-presed";
    TEST_ASSERT_EQUAL_STRING("style_radius", lv_xml_style_string_process(typo, &sel));
    log_capture_stop();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, sel, "an unknown token currently collapses to the default state / main part");
    TEST_ASSERT_FALSE_MESSAGE(log_contains("presed"),
                              "nothing is logged today - if this starts failing the bug was fixed");

    /* The `selector=` path DOES report it. Same typo, different answer. */
    log_capture_start();
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)lv_xml_style_selector_text_to_enum("presed"));
    log_capture_stop();
    TEST_ASSERT_TRUE_MESSAGE(log_contains("presed is an unknown token in style selector"),
                             "the selector= attribute path must keep reporting unknown tokens");
}

/*===========================================================================
 * Selectors on real widgets: parts, states, the selector= attribute
 *==========================================================================*/

/**
 * `parts="main,indicator,knob"` on a single element must apply the style to
 * every named part and to no other. `<style>` itself has no `parts` support
 * (see the pin below), so the multi-part form lives on `bind_style`.
 */
static const char * STYLE_PARTS_XML =
    "<component>"
    "  <subjects>"
    "    <subject name=\"parts_on\" type=\"int\" value=\"1\"/>"
    "  </subjects>"
    "  <styles>"
    "    <style name=\"tri\" pad_top=\"23\"/>"
    "  </styles>"
    "  <view extends=\"lv_obj\" name=\"parts_root\">"
    "    <lv_slider name=\"sl\">"
    "      <bind_style name=\"tri\" subject=\"parts_on\" ref_value=\"1\""
    "                  parts=\"main,indicator,knob\"/>"
    "    </lv_slider>"
    "  </view>"
    "</component>";

static void test_parts_attribute_applies_one_style_to_every_named_part(void)
{
    ASSERT_XML_REGISTERS("style_parts", STYLE_PARTS_XML);

    lv_obj_t * root = XML_CREATE(helix_test_env_screen(), "style_parts", NULL);
    helix_test_pump(30);

    lv_obj_t * sl = ASSERT_NAMED(root, "sl");

    /* Each part is asserted independently: a bug that applied the style once
     * (to whichever part happened to be last) would pass a single assertion. */
    ASSERT_STYLE_INT(sl, LV_STYLE_PAD_TOP, LV_PART_MAIN, 23);
    ASSERT_STYLE_INT(sl, LV_STYLE_PAD_TOP, LV_PART_INDICATOR, 23);
    ASSERT_STYLE_INT(sl, LV_STYLE_PAD_TOP, LV_PART_KNOB, 23);

    /* pad_top is not inheritable, so an unnamed part must not pick it up from
     * LV_PART_MAIN - this is what proves the list was honoured rather than the
     * style being blanket-applied. */
    ASSERT_STYLE_INT_NOT(sl, LV_STYLE_PAD_TOP, LV_PART_SCROLLBAR, 23);
}

/**
 * PINS CURRENT BEHAVIOUR - suspected bug: `parts="..."` is honoured by
 * <bind_style>, <bind_style_if_*> and <bind_style_if>, but NOT by the plain
 * <style> element. lv_obj_xml_style_apply() only reads `selector`, so
 * `<style name="x" parts="indicator,knob"/>` silently applies x to LV_PART_MAIN
 * and to nothing else. The attribute looks supported because three of its four
 * siblings support it.
 */
static void test_plain_style_element_silently_ignores_the_parts_attribute(void)
{
    ASSERT_XML_REGISTERS("style_parts_ignored",
                         "<component>"
                         "  <styles>"
                         "    <style name=\"tri\" pad_top=\"23\"/>"
                         "  </styles>"
                         "  <view extends=\"lv_obj\" name=\"pi_root\">"
                         "    <lv_slider name=\"sl\">"
                         "      <style name=\"tri\" parts=\"indicator,knob\"/>"
                         "    </lv_slider>"
                         "  </view>"
                         "</component>");

    lv_obj_t * root = XML_CREATE(helix_test_env_screen(), "style_parts_ignored", NULL);
    helix_test_pump(30);

    lv_obj_t * sl = ASSERT_NAMED(root, "sl");

    /* The parts named in the XML do NOT get the style ... */
    ASSERT_STYLE_INT_NOT(sl, LV_STYLE_PAD_TOP, LV_PART_INDICATOR, 23);
    ASSERT_STYLE_INT_NOT(sl, LV_STYLE_PAD_TOP, LV_PART_KNOB, 23);
    /* ... and LV_PART_MAIN, which the XML did not name, gets it instead. */
    ASSERT_STYLE_INT(sl, LV_STYLE_PAD_TOP, LV_PART_MAIN, 23);
}

/** The `selector=` attribute form, targeting a part. */
static void test_selector_attribute_scopes_a_style_to_a_single_part(void)
{
    ASSERT_XML_REGISTERS("style_selector_part",
                         "<component>"
                         "  <styles>"
                         "    <style name=\"knobby\" pad_top=\"17\"/>"
                         "  </styles>"
                         "  <view extends=\"lv_obj\" name=\"sp_root\">"
                         "    <lv_slider name=\"sl\">"
                         "      <style name=\"knobby\" selector=\"knob\"/>"
                         "    </lv_slider>"
                         "    <lv_slider name=\"sl_plain\"/>"
                         "  </view>"
                         "</component>");

    lv_obj_t * root = XML_CREATE(helix_test_env_screen(), "style_selector_part", NULL);
    helix_test_pump(30);

    lv_obj_t * sl = ASSERT_NAMED(root, "sl");
    ASSERT_STYLE_INT(sl, LV_STYLE_PAD_TOP, LV_PART_KNOB, 17);
    ASSERT_STYLE_INT_NOT(sl, LV_STYLE_PAD_TOP, LV_PART_MAIN, 17);
    ASSERT_STYLE_INT_NOT(sl, LV_STYLE_PAD_TOP, LV_PART_INDICATOR, 17);

    ASSERT_STYLE_INT_NOT(ASSERT_NAMED(root, "sl_plain"), LV_STYLE_PAD_TOP, LV_PART_KNOB, 17);
}

/**
 * A state-qualified selector must be inert until the object enters that state.
 * Two objects share one style: one declares the state in XML, one does not.
 */
static const char * STYLE_STATE_XML =
    "<component>"
    "  <styles>"
    "    <style name=\"hot\" radius=\"41\"/>"
    "    <style name=\"greyed\" border_width=\"13\"/>"
    "  </styles>"
    "  <view extends=\"lv_obj\" name=\"state_root\">"
    "    <lv_obj name=\"unchecked\">"
    "      <style name=\"hot\" selector=\"checked\"/>"
    "    </lv_obj>"
    "    <lv_obj name=\"checked_one\" checked=\"true\">"
    "      <style name=\"hot\" selector=\"checked\"/>"
    "    </lv_obj>"
    "    <lv_obj name=\"enabled_one\">"
    "      <style name=\"greyed\" selector=\"disabled\"/>"
    "    </lv_obj>"
    "    <lv_obj name=\"disabled_one\" disabled=\"true\">"
    "      <style name=\"greyed\" selector=\"disabled\"/>"
    "    </lv_obj>"
    "  </view>"
    "</component>";

static void test_state_qualified_style_applies_only_in_that_state(void)
{
    ASSERT_XML_REGISTERS("style_state", STYLE_STATE_XML);

    lv_obj_t * root = XML_CREATE(helix_test_env_screen(), "style_state", NULL);
    helix_test_pump(30);

    lv_obj_t * unchecked   = ASSERT_NAMED(root, "unchecked");
    lv_obj_t * checked_one = ASSERT_NAMED(root, "checked_one");

    ASSERT_STATE(checked_one, LV_STATE_CHECKED);
    TEST_ASSERT_FALSE_MESSAGE(lv_obj_has_state(unchecked, LV_STATE_CHECKED),
                              "the control object must not be checked, or the test proves nothing");

    ASSERT_STYLE_INT(checked_one, LV_STYLE_RADIUS, LV_PART_MAIN, 41);
    ASSERT_STYLE_INT_NOT(unchecked, LV_STYLE_RADIUS, LV_PART_MAIN, 41);

    /* Entering the state at runtime must switch the same object over - the
     * style really is state-conditional, not just "applied at build time to
     * whichever object happened to be checked". */
    lv_obj_add_state(unchecked, LV_STATE_CHECKED);
    helix_test_pump(30);
    ASSERT_STYLE_INT(unchecked, LV_STYLE_RADIUS, LV_PART_MAIN, 41);

    lv_obj_remove_state(unchecked, LV_STATE_CHECKED);
    helix_test_pump(30);
    ASSERT_STYLE_INT_NOT(unchecked, LV_STYLE_RADIUS, LV_PART_MAIN, 41);

    /* Same again for `disabled`, which is a different bit and a different
     * XML attribute path. */
    ASSERT_STYLE_INT(ASSERT_NAMED(root, "disabled_one"), LV_STYLE_BORDER_WIDTH, LV_PART_MAIN, 13);
    ASSERT_STYLE_INT_NOT(ASSERT_NAMED(root, "enabled_one"), LV_STYLE_BORDER_WIDTH, LV_PART_MAIN, 13);
}

/** The inline `style_<prop>-<state>` attribute form, end to end. */
static void test_inline_style_attribute_with_a_state_suffix_applies_only_in_that_state(void)
{
    ASSERT_XML_REGISTERS("style_inline_state",
                         "<component>"
                         "  <view extends=\"lv_obj\" name=\"is_root\">"
                         "    <lv_obj name=\"tgt\" style_radius=\"7\" style_radius-checked=\"43\"/>"
                         "  </view>"
                         "</component>");

    lv_obj_t * root = XML_CREATE(helix_test_env_screen(), "style_inline_state", NULL);
    helix_test_pump(30);

    lv_obj_t * tgt = ASSERT_NAMED(root, "tgt");
    ASSERT_STYLE_INT(tgt, LV_STYLE_RADIUS, LV_PART_MAIN, 7);

    lv_obj_add_state(tgt, LV_STATE_CHECKED);
    helix_test_pump(30);
    ASSERT_STYLE_INT(tgt, LV_STYLE_RADIUS, LV_PART_MAIN, 43);

    lv_obj_remove_state(tgt, LV_STATE_CHECKED);
    helix_test_pump(30);
    ASSERT_STYLE_INT(tgt, LV_STYLE_RADIUS, LV_PART_MAIN, 7);
}

/*===========================================================================
 * The cascade
 *==========================================================================*/

static const char * STYLE_CASCADE_XML =
    "<component>"
    "  <styles>"
    "    <style name=\"loud\" radius=\"41\" bg_color=\"0x00FF00\" pad_top=\"19\"/>"
    "  </styles>"
    "  <view extends=\"lv_obj\" name=\"cascade_root\">"
    "    <lv_obj name=\"inline_wins\" style_radius=\"9\" style_bg_color=\"0xFF0000\">"
    "      <style name=\"loud\"/>"
    "    </lv_obj>"
    "    <lv_obj name=\"style_only\">"
    "      <style name=\"loud\"/>"
    "    </lv_obj>"
    "  </view>"
    "</component>";

/**
 * The documented precedence rule: an inline `style_*` attribute becomes a LOCAL
 * style, and LVGL keeps local styles ahead of every added style in the lookup
 * order, so the attribute wins for the properties it names - and only those.
 */
static void test_inline_style_attribute_outranks_a_style_element(void)
{
    ASSERT_XML_REGISTERS("style_cascade", STYLE_CASCADE_XML);

    lv_obj_t * root = XML_CREATE(helix_test_env_screen(), "style_cascade", NULL);
    helix_test_pump(30);

    lv_obj_t * inline_wins = ASSERT_NAMED(root, "inline_wins");
    lv_obj_t * style_only  = ASSERT_NAMED(root, "style_only");

    /* Control: with no inline attribute the <style> supplies everything. */
    ASSERT_STYLE_INT(style_only, LV_STYLE_RADIUS, LV_PART_MAIN, 41);
    ASSERT_STYLE_COLOR(style_only, LV_STYLE_BG_COLOR, LV_PART_MAIN, 0x00FF00);
    ASSERT_STYLE_INT(style_only, LV_STYLE_PAD_TOP, LV_PART_MAIN, 19);

    /* The contested properties go to the inline attribute, even though the
     * <style> child element is processed LATER in document order. */
    ASSERT_STYLE_INT(inline_wins, LV_STYLE_RADIUS, LV_PART_MAIN, 9);
    ASSERT_STYLE_COLOR(inline_wins, LV_STYLE_BG_COLOR, LV_PART_MAIN, 0xFF0000);

    /* An uncontested property still comes from the style: the inline attribute
     * overrides per-property, it does not shadow the whole style. */
    ASSERT_STYLE_INT(inline_wins, LV_STYLE_PAD_TOP, LV_PART_MAIN, 19);
}

static const char * STYLE_BIND_CASCADE_XML =
    "<component>"
    "  <subjects>"
    "    <subject name=\"bind_on\" type=\"int\" value=\"1\"/>"
    "  </subjects>"
    "  <styles>"
    "    <style name=\"loud\" radius=\"41\" pad_top=\"19\"/>"
    "  </styles>"
    "  <view extends=\"lv_obj\" name=\"bind_cascade_root\">"
    "    <lv_obj name=\"bound_inline\" style_radius=\"9\">"
    "      <bind_style name=\"loud\" subject=\"bind_on\" ref_value=\"1\"/>"
    "    </lv_obj>"
    "    <lv_obj name=\"bound_only\">"
    "      <bind_style name=\"loud\" subject=\"bind_on\" ref_value=\"1\"/>"
    "    </lv_obj>"
    "  </view>"
    "</component>";

/**
 * Same rule against `<bind_style>`, which is the form people actually reach for
 * and the one where the surprise is worst: the subject says the style is ON,
 * the style names the property, and the inline attribute still wins.
 */
static void test_inline_style_attribute_outranks_a_bound_style(void)
{
    ASSERT_XML_REGISTERS("style_bind_cascade", STYLE_BIND_CASCADE_XML);

    lv_obj_t * root = XML_CREATE(helix_test_env_screen(), "style_bind_cascade", NULL);
    helix_test_pump(30);

    lv_obj_t * bound_inline = ASSERT_NAMED(root, "bound_inline");
    lv_obj_t * bound_only   = ASSERT_NAMED(root, "bound_only");

    /* Control: the bind is live (subject value == ref_value), so the style
     * is enabled and its radius shows through. */
    ASSERT_STYLE_INT(bound_only, LV_STYLE_RADIUS, LV_PART_MAIN, 41);

    ASSERT_STYLE_INT(bound_inline, LV_STYLE_RADIUS, LV_PART_MAIN, 9);
    ASSERT_STYLE_INT(bound_inline, LV_STYLE_PAD_TOP, LV_PART_MAIN, 19);
}

/*===========================================================================
 * Gradients
 *==========================================================================*/

static const char * STYLE_GRAD_XML =
    "<component>"
    "  <gradients>"
    "    <horizontal name=\"warm\">"
    "      <stop color=\"0xFF0000\" offset=\"0\"/>"
    "      <stop color=\"0x0000FF\" offset=\"255\"/>"
    "    </horizontal>"
    "  </gradients>"
    "  <styles>"
    "    <style name=\"grad_style\" bg_grad=\"warm\"/>"
    "  </styles>"
    "  <view extends=\"lv_obj\" name=\"grad_root\">"
    "    <lv_obj name=\"via_style\">"
    "      <style name=\"grad_style\"/>"
    "    </lv_obj>"
    "    <lv_obj name=\"via_inline\" style_bg_grad=\"warm\"/>"
    "  </view>"
    "</component>";

static void test_gradient_declared_in_a_component_scope_is_resolvable_by_name(void)
{
    ASSERT_XML_REGISTERS("style_grad", STYLE_GRAD_XML);

    lv_xml_component_scope_t * scope = lv_xml_component_get_scope("style_grad");
    TEST_ASSERT_NOT_NULL(scope);

    lv_grad_dsc_t * g = lv_xml_component_get_grad(scope, "warm");
    TEST_ASSERT_NOT_NULL_MESSAGE(g, "a <gradient> declared in this scope must resolve by name");

    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_GRAD_DIR_HOR, (int)g->dir,
                                  "<horizontal> must map to LV_GRAD_DIR_HOR");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(2, g->stops_count, "both <stop> elements must be recorded");
    TEST_ASSERT_EQUAL_HEX32(0xFF0000u, lv_color_to_u32(g->stops[0].color) & 0x00FFFFFFu);
    TEST_ASSERT_EQUAL_HEX32(0x0000FFu, lv_color_to_u32(g->stops[1].color) & 0x00FFFFFFu);
    TEST_ASSERT_EQUAL_UINT8(0, g->stops[0].frac);
    TEST_ASSERT_EQUAL_UINT8(255, g->stops[1].frac);

    /* A name that was never declared must not resolve to a neighbouring
     * gradient - the lookup is by name, not by position. */
    TEST_ASSERT_NULL(lv_xml_component_get_grad(scope, "cold"));
}

static void test_gradient_reference_reaches_the_widget_from_a_style_and_from_an_attribute(void)
{
    ASSERT_XML_REGISTERS("style_grad", STYLE_GRAD_XML);

    lv_xml_component_scope_t * scope = lv_xml_component_get_scope("style_grad");
    TEST_ASSERT_NOT_NULL(scope);
    lv_grad_dsc_t * g = lv_xml_component_get_grad(scope, "warm");
    TEST_ASSERT_NOT_NULL(g);

    lv_obj_t * root = XML_CREATE(helix_test_env_screen(), "style_grad", NULL);
    helix_test_pump(30);

    /* Both spellings must land on the very same descriptor: the engine stores a
     * pointer into the scope, it does not copy the stops. */
    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        g, lv_obj_get_style_bg_grad(ASSERT_NAMED(root, "via_style"), LV_PART_MAIN),
        "`bg_grad=\"warm\"` inside a <style> must resolve to the scope's descriptor");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        g, lv_obj_get_style_bg_grad(ASSERT_NAMED(root, "via_inline"), LV_PART_MAIN),
        "inline `style_bg_grad=\"warm\"` must resolve to the same descriptor");
}

/*===========================================================================
 * Failure paths
 *==========================================================================*/

/**
 * A `<style>` naming something that was never registered must warn and leave
 * the widget alone. Silently building a bare widget is how a typo'd style name
 * turns into "the theme looks wrong on one screen" three weeks later.
 */
static void test_style_reference_to_an_unregistered_name_warns_and_leaves_the_widget_bare(void)
{
    ASSERT_XML_REGISTERS("style_missing",
                         "<component>"
                         "  <styles>"
                         "    <style name=\"exists\" radius=\"37\"/>"
                         "  </styles>"
                         "  <view extends=\"lv_obj\" name=\"missing_root\">"
                         "    <lv_obj name=\"tgt\">"
                         "      <style name=\"never_registered\"/>"
                         "    </lv_obj>"
                         "  </view>"
                         "</component>");

    log_capture_start();
    lv_obj_t * root = XML_CREATE(helix_test_env_screen(), "style_missing", NULL);
    helix_test_pump(30);
    log_capture_stop();

    /* Two distinct messages: the lookup reports "not found", the applier
     * reports which reference could not be satisfied. */
    TEST_ASSERT_TRUE_MESSAGE(log_contains("No style found with never_registered name"),
                             "lv_xml_get_style_by_name() must report the miss");
    TEST_ASSERT_TRUE_MESSAGE(log_contains("`never_registered` style is not found"),
                             "lv_obj_xml_style_apply() must report the unsatisfied reference");

    /* The widget still exists and simply has no style applied. */
    lv_obj_t * tgt = ASSERT_NAMED(root, "tgt");
    ASSERT_STYLE_INT_NOT(tgt, LV_STYLE_RADIUS, LV_PART_MAIN, 37);
}

/**
 * Two `<style>` entries with the same name in one scope are ONE style that
 * accumulates properties, not two competing ones. A second entry may also
 * overwrite a property the first set.
 */
static void test_registering_the_same_style_name_twice_extends_the_first(void)
{
    ASSERT_XML_REGISTERS("style_extend",
                         "<component>"
                         "  <styles>"
                         "    <style name=\"acc\" radius=\"37\" pad_top=\"11\"/>"
                         "    <style name=\"acc\" pad_top=\"19\" border_width=\"5\"/>"
                         "  </styles>"
                         "  <view extends=\"lv_obj\" name=\"extend_root\">"
                         "    <lv_obj name=\"tgt\">"
                         "      <style name=\"acc\"/>"
                         "    </lv_obj>"
                         "  </view>"
                         "</component>");

    lv_xml_component_scope_t * scope = lv_xml_component_get_scope("style_extend");
    TEST_ASSERT_NOT_NULL(scope);

    /* Exactly one record, not two. */
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, lv_ll_get_len(&scope->style_ll),
                                     "a repeated style name must extend the existing record");

    lv_obj_t * root = XML_CREATE(helix_test_env_screen(), "style_extend", NULL);
    helix_test_pump(30);
    lv_obj_t * tgt = ASSERT_NAMED(root, "tgt");

    ASSERT_STYLE_INT(tgt, LV_STYLE_RADIUS, LV_PART_MAIN, 37);       /* only the first set it */
    ASSERT_STYLE_INT(tgt, LV_STYLE_BORDER_WIDTH, LV_PART_MAIN, 5);  /* only the second set it */
    ASSERT_STYLE_INT(tgt, LV_STYLE_PAD_TOP, LV_PART_MAIN, 19);      /* the second overwrote it */
}

static void test_style_without_a_name_is_rejected_with_a_warning(void)
{
    log_capture_start();
    ASSERT_XML_REGISTERS("style_noname",
                         "<component>"
                         "  <styles>"
                         "    <style radius=\"37\"/>"
                         "  </styles>"
                         "  <view extends=\"lv_obj\" name=\"noname_root\"/>"
                         "</component>");
    log_capture_stop();

    TEST_ASSERT_TRUE(log_contains("'name' is missing from a style"));

    lv_xml_component_scope_t * scope = lv_xml_component_get_scope("style_noname");
    TEST_ASSERT_NOT_NULL(scope);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, lv_ll_get_len(&scope->style_ll),
                                     "a nameless <style> must not create a record");
}

static void test_unsupported_style_property_warns_and_the_rest_of_the_style_survives(void)
{
    log_capture_start();
    ASSERT_XML_REGISTERS("style_badprop",
                         "<component>"
                         "  <styles>"
                         "    <style name=\"mixed\" radius=\"37\" not_a_property=\"1\""
                         "           border_width=\"5\"/>"
                         "  </styles>"
                         "  <view extends=\"lv_obj\" name=\"badprop_root\"/>"
                         "</component>");
    log_capture_stop();

    TEST_ASSERT_TRUE(log_contains("not_a_property style property is not supported"));

    lv_xml_component_scope_t * scope = lv_xml_component_get_scope("style_badprop");
    TEST_ASSERT_NOT_NULL(scope);
    lv_xml_style_t * s = lv_xml_get_style_by_name(scope, "mixed");
    TEST_ASSERT_NOT_NULL(s);

    /* An unknown property is skipped, not fatal: the attributes on both sides
     * of it still made it into the style. */
    TEST_ASSERT_EQUAL_INT32(37, style_prop_num(s, LV_STYLE_RADIUS));
    TEST_ASSERT_EQUAL_INT32(5, style_prop_num(s, LV_STYLE_BORDER_WIDTH));

    /* `help` and `figma_node_id` are accepted-and-ignored, not "unsupported". */
    log_capture_start();
    ASSERT_XML_REGISTERS("style_meta",
                         "<component>"
                         "  <styles>"
                         "    <style name=\"m\" help=\"a note\" figma_node_id=\"1:2\" radius=\"37\"/>"
                         "  </styles>"
                         "  <view extends=\"lv_obj\" name=\"meta_root\"/>"
                         "</component>");
    log_capture_stop();
    TEST_ASSERT_FALSE_MESSAGE(log_contains("style property is not supported"),
                              "`help` and `figma_node_id` are documentation attributes, not properties");
}

/**
 * The literal value `remove` deletes the property from the style rather than
 * setting it, so a later `<style>` entry with the same name can subtract from
 * an earlier one. `pad_all="remove"` has to clear all four pad properties.
 */
static void test_remove_value_deletes_the_property_from_the_style(void)
{
    ASSERT_XML_REGISTERS("style_remove",
                         "<component>"
                         "  <styles>"
                         "    <style name=\"r\" radius=\"37\" pad_all=\"11\" border_width=\"5\"/>"
                         "    <style name=\"r\" pad_all=\"remove\" radius=\"remove\"/>"
                         "  </styles>"
                         "  <view extends=\"lv_obj\" name=\"remove_root\"/>"
                         "</component>");

    lv_xml_component_scope_t * scope = lv_xml_component_get_scope("style_remove");
    TEST_ASSERT_NOT_NULL(scope);
    lv_xml_style_t * s = lv_xml_get_style_by_name(scope, "r");
    TEST_ASSERT_NOT_NULL(s);

    TEST_ASSERT_FALSE_MESSAGE(style_has_prop(s, LV_STYLE_RADIUS),
                              "radius=\"remove\" must delete the property, not set it to 0");
    TEST_ASSERT_FALSE_MESSAGE(style_has_prop(s, LV_STYLE_PAD_LEFT),
                              "pad_all=\"remove\" must clear all four pad properties");
    TEST_ASSERT_FALSE(style_has_prop(s, LV_STYLE_PAD_RIGHT));
    TEST_ASSERT_FALSE(style_has_prop(s, LV_STYLE_PAD_TOP));
    TEST_ASSERT_FALSE(style_has_prop(s, LV_STYLE_PAD_BOTTOM));

    /* Untouched properties survive. */
    TEST_ASSERT_EQUAL_INT32(5, style_prop_num(s, LV_STYLE_BORDER_WIDTH));
}

/**
 * `#name` in a style property value is a const reference. It resolves against
 * the component's own consts first, then globals; an unresolvable one is
 * skipped with a warning that names the component AND the property, because
 * neither alone is enough to find it in a tree of any size.
 */
static void test_const_reference_in_a_style_property_resolves_and_reports_misses(void)
{
    log_capture_start();
    ASSERT_XML_REGISTERS("style_const",
                         "<component>"
                         "  <consts>"
                         "    <int name=\"corner\" value=\"37\"/>"
                         "  </consts>"
                         "  <styles>"
                         "    <style name=\"c\" radius=\"#corner\" border_width=\"#no_such_const\""
                         "           pad_top=\"11\"/>"
                         "  </styles>"
                         "  <view extends=\"lv_obj\" name=\"const_root\"/>"
                         "</component>");
    log_capture_stop();

    TEST_ASSERT_TRUE_MESSAGE(log_contains("Unknown const `#no_such_const`"),
                             "an unresolvable const must be reported");
    TEST_ASSERT_TRUE_MESSAGE(log_contains("style_const"),
                             "the warning must name the component it came from");
    TEST_ASSERT_TRUE_MESSAGE(log_contains("border_width"),
                             "the warning must name the property it came from");

    lv_xml_component_scope_t * scope = lv_xml_component_get_scope("style_const");
    TEST_ASSERT_NOT_NULL(scope);
    lv_xml_style_t * s = lv_xml_get_style_by_name(scope, "c");
    TEST_ASSERT_NOT_NULL(s);

    TEST_ASSERT_EQUAL_INT32_MESSAGE(37, style_prop_num(s, LV_STYLE_RADIUS),
                                    "a resolvable const must be substituted before parsing");
    TEST_ASSERT_FALSE_MESSAGE(style_has_prop(s, LV_STYLE_BORDER_WIDTH),
                              "an unresolvable const must skip the property, not set it to 0");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(11, style_prop_num(s, LV_STYLE_PAD_TOP),
                                    "parsing must continue after a skipped property");
}

/*---------------------------------------------------------------------------
 * main
 *--------------------------------------------------------------------------*/

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_style_from_a_styles_block_lands_on_the_referencing_widget);

    RUN_TEST(test_get_style_by_name_resolves_plain_and_component_qualified_names);
    RUN_TEST(test_get_style_by_name_warns_and_returns_null_for_unknown_names);
    RUN_TEST(test_get_style_by_name_falls_back_to_the_globals_scope);

    RUN_TEST(test_style_string_process_splits_the_name_from_state_and_part_tokens);
    RUN_TEST(test_style_string_process_silently_swallows_an_unknown_selector_token);

    RUN_TEST(test_parts_attribute_applies_one_style_to_every_named_part);
    RUN_TEST(test_plain_style_element_silently_ignores_the_parts_attribute);
    RUN_TEST(test_selector_attribute_scopes_a_style_to_a_single_part);
    RUN_TEST(test_state_qualified_style_applies_only_in_that_state);
    RUN_TEST(test_inline_style_attribute_with_a_state_suffix_applies_only_in_that_state);

    RUN_TEST(test_inline_style_attribute_outranks_a_style_element);
    RUN_TEST(test_inline_style_attribute_outranks_a_bound_style);

    RUN_TEST(test_gradient_declared_in_a_component_scope_is_resolvable_by_name);
    RUN_TEST(test_gradient_reference_reaches_the_widget_from_a_style_and_from_an_attribute);

    RUN_TEST(test_style_reference_to_an_unregistered_name_warns_and_leaves_the_widget_bare);
    RUN_TEST(test_registering_the_same_style_name_twice_extends_the_first);
    RUN_TEST(test_style_without_a_name_is_rejected_with_a_warning);
    RUN_TEST(test_unsupported_style_property_warns_and_the_rest_of_the_style_survives);
    RUN_TEST(test_remove_value_deletes_the_property_from_the_style);
    RUN_TEST(test_const_reference_in_a_style_property_resolves_and_reports_misses);

    return UNITY_END();
}
