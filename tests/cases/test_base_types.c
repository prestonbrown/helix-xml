/**
 * @file test_base_types.c
 *
 * Every string-to-enum converter in src/xml/lv_xml_base_types.h.
 *
 * These functions are the entire vocabulary of the XML dialect: if
 * `align="center"` stops meaning LV_ALIGN_CENTER, every layout in every
 * consuming app silently changes shape. Almost none of them can report a
 * failure - unknown input maps onto whatever enumerator happens to be 0, which
 * for most of them is also a legitimate accepted string. So the accepted-string
 * table IS the contract, and it is spelled out exhaustively below.
 *
 * Shape: one table per converter listing EVERY accepted string, looped in one
 * test function, plus an explicit assertion for the documented fallback value.
 * Where the fallback is indistinguishable from a valid parse, the fallback test
 * also captures the log to prove the warning is (or is not) emitted - that is
 * the only observable difference.
 *
 * ---------------------------------------------------------------------------
 * NOT TESTED, DELIBERATELY
 *
 *  - NULL into any converter except lv_xml_to_size and
 *    lv_xml_style_selector_text_to_enum. The rest pass the pointer straight to
 *    lv_strcmp, which dereferences unconditionally. The two that DO guard are
 *    tested.
 *    (lv_xml_to_size(" ") and other all-whitespace input IS safe now that
 *    lv_xml_atoi no longer reads past the NUL - see tests/cases/test_utils.c.)
 * ---------------------------------------------------------------------------
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <string.h>

/* Log capture: for most converters the fallback value is also a legal parse
 * result, so the LV_LOG_WARN is the only way to tell a rejected string from
 * an accepted one. */
#include "helpers/helix_log_capture.h"
#include "helpers/helix_test_env.h"
#include "helpers/xml_assert.h"

#include "xml/lv_xml_base_types.h"

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
 * Table driver
 *--------------------------------------------------------------------------*/

typedef struct {
    const char * in;
    int32_t expect;
} enum_case_t;

/**
 * Run one converter over its complete accepted-string table.
 *
 * A macro rather than a function because the converters have nine different
 * return types and casting between incompatible function-pointer types to
 * unify them would be undefined behaviour.
 */
#define RUN_ENUM_TABLE(fn, table)                                                        \
    do {                                                                                 \
        for(size_t i_ = 0; i_ < sizeof(table) / sizeof((table)[0]); i_++) {               \
            TEST_ASSERT_EQUAL_INT32_MESSAGE(                                             \
                (table)[i_].expect, (int32_t)fn((table)[i_].in),                          \
                helix_xml_assert_msgf(#fn "(\"%s\") mapped to the wrong enum",            \
                                      (table)[i_].in));                                  \
        }                                                                                \
    } while(0)

/*===========================================================================
 * lv_xml_state_to_enum
 *==========================================================================*/

static const enum_case_t STATE_CASES[] = {
    {"default", LV_STATE_DEFAULT},   {"pressed", LV_STATE_PRESSED},
    {"checked", LV_STATE_CHECKED},   {"hovered", LV_STATE_HOVERED},
    {"scrolled", LV_STATE_SCROLLED}, {"disabled", LV_STATE_DISABLED},
    {"focused", LV_STATE_FOCUSED},   {"focus_key", LV_STATE_FOCUS_KEY},
    {"edited", LV_STATE_EDITED},     {"user_1", LV_STATE_USER_1},
    {"user_2", LV_STATE_USER_2},     {"user_3", LV_STATE_USER_3},
    {"user_4", LV_STATE_USER_4},
};

static void test_state_to_enum_accepts_every_state_name(void)
{
    RUN_ENUM_TABLE(lv_xml_state_to_enum, STATE_CASES);
}

static void test_state_to_enum_warns_and_returns_zero_for_unknown(void)
{
    log_capture_start();
    /* The fallback collides with "default", so the warning is the only signal. */
    TEST_ASSERT_EQUAL_INT32(0, (int32_t)lv_xml_state_to_enum("bogus_state"));
    /* Neither LV_STATE_ALT nor LV_STATE_ANY is part of the accepted set, and
     * there is no '|' splitting: a combined string is simply unknown. */
    TEST_ASSERT_EQUAL_INT32(0, (int32_t)lv_xml_state_to_enum("pressed|checked"));
    TEST_ASSERT_EQUAL_INT32(0, (int32_t)lv_xml_state_to_enum("Pressed"));
    TEST_ASSERT_EQUAL_INT32(0, (int32_t)lv_xml_state_to_enum(""));
    log_capture_stop();

    TEST_ASSERT_TRUE_MESSAGE(log_contains("bogus_state is an unknown value for state"),
                             "an unrecognised state must be reported - the return value cannot be");
    TEST_ASSERT_TRUE(log_contains("pressed|checked is an unknown value for state"));
}

/*===========================================================================
 * lv_xml_to_size
 *==========================================================================*/

static void test_to_size_parses_pixels_percentages_and_content(void)
{
    TEST_ASSERT_EQUAL_INT32(32, lv_xml_to_size("32"));
    TEST_ASSERT_EQUAL_INT32(-10, lv_xml_to_size("-10"));
    /* atoi stops at 'p'; the final 'x' is not '%', so it is a plain pixel value. */
    TEST_ASSERT_EQUAL_INT32(32, lv_xml_to_size("32px"));
    TEST_ASSERT_EQUAL_INT32(LV_SIZE_CONTENT, lv_xml_to_size("content"));
    TEST_ASSERT_EQUAL_INT32(lv_pct(25), lv_xml_to_size("25%"));
    TEST_ASSERT_EQUAL_INT32(lv_pct(-25), lv_xml_to_size("-25%"));
}

/** lv_pct(0) is a spec-encoded coordinate, NOT the integer 0. */
static void test_to_size_zero_percent_is_not_zero(void)
{
    TEST_ASSERT_EQUAL_INT32(lv_pct(0), lv_xml_to_size("0%"));
    TEST_ASSERT_TRUE_MESSAGE(lv_xml_to_size("0%") != 0,
                             "\"0%\" must encode as lv_pct(0), which is distinct from a plain 0");
    TEST_ASSERT_EQUAL_INT32(0, lv_xml_to_size("0"));
}

/** One of only two NULL-guarded converters in the file (see the #1121 comment). */
static void test_to_size_guards_null_and_empty(void)
{
    TEST_ASSERT_EQUAL_INT32_MESSAGE(0, lv_xml_to_size(NULL),
                                    "lv_xml_to_size() must guard NULL");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(0, lv_xml_to_size(""),
                                    "an empty value (what a failed ${expr} splices in) must be 0");
}

/**
 * The empty-string guard exists because the percent-suffix check was written as
 * `txt[lv_strlen(txt) - 1]`, which for "" indexes txt[-1] - one byte BEFORE the
 * buffer. Whatever happened to sit there decided the answer: a stray '%' turned
 * a plain 0 into lv_pct(0).
 *
 * test_to_size_guards_null_and_empty above passes "" as a string literal, whose
 * predecessor byte is whatever the linker put there - so without the guard it
 * passes or fails on the rodata layout. This one CONTROLS the preceding byte, so
 * removing the guard fails it every time, and fails it identically in a solo run
 * and a full-suite run.
 */
static void test_to_size_empty_string_does_not_read_the_byte_before_it(void)
{
    /* Two empty strings differing only in their out-of-bounds predecessor. An
     * in-bounds parser cannot tell them apart. */
    char poisoned[4] = {'%', '\0', '\0', '\0'};
    char benign[4] = {'x', '\0', '\0', '\0'};

    int32_t after_pct = lv_xml_to_size(poisoned + 1);
    int32_t after_x = lv_xml_to_size(benign + 1);

    TEST_ASSERT_EQUAL_INT32_MESSAGE(after_x, after_pct,
                                    "lv_xml_to_size(\"\") read the byte before the string - the "
                                    "result changed with a '%' sitting in front of the NUL");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(0, after_pct,
                                    "an absent size is a plain 0, never lv_pct(0)");
}

static void test_to_size_returns_zero_for_unparseable_input(void)
{
    TEST_ASSERT_EQUAL_INT32(0, lv_xml_to_size("abc"));
    /* A lone '%' still takes the percentage branch, on a parsed value of 0. */
    TEST_ASSERT_EQUAL_INT32(lv_pct(0), lv_xml_to_size("%"));
}

/*===========================================================================
 * lv_xml_align_to_enum
 *==========================================================================*/

static const enum_case_t ALIGN_CASES[] = {
    {"top_left", LV_ALIGN_TOP_LEFT},         {"top_mid", LV_ALIGN_TOP_MID},
    {"top_right", LV_ALIGN_TOP_RIGHT},       {"bottom_left", LV_ALIGN_BOTTOM_LEFT},
    {"bottom_mid", LV_ALIGN_BOTTOM_MID},     {"bottom_right", LV_ALIGN_BOTTOM_RIGHT},
    {"left_mid", LV_ALIGN_LEFT_MID},         {"right_mid", LV_ALIGN_RIGHT_MID},
    {"center", LV_ALIGN_CENTER},
};

static void test_align_to_enum_accepts_every_align_name(void)
{
    RUN_ENUM_TABLE(lv_xml_align_to_enum, ALIGN_CASES);
}

static void test_align_to_enum_warns_and_returns_default_for_unknown(void)
{
    log_capture_start();
    TEST_ASSERT_EQUAL_INT32(LV_ALIGN_DEFAULT, (int32_t)lv_xml_align_to_enum("bogus_align"));
    /* None of the twelve LV_ALIGN_OUT_* values are part of the dialect. */
    TEST_ASSERT_EQUAL_INT32(LV_ALIGN_DEFAULT, (int32_t)lv_xml_align_to_enum("out_top_left"));
    TEST_ASSERT_EQUAL_INT32(LV_ALIGN_DEFAULT, (int32_t)lv_xml_align_to_enum(""));
    log_capture_stop();

    TEST_ASSERT_TRUE(log_contains("bogus_align is an unknown value for align"));
    TEST_ASSERT_TRUE(log_contains("out_top_left is an unknown value for align"));
}

/*===========================================================================
 * lv_xml_dir_to_enum
 *==========================================================================*/

static const enum_case_t DIR_CASES[] = {
    {"none", LV_DIR_NONE}, {"top", LV_DIR_TOP},     {"bottom", LV_DIR_BOTTOM},
    {"left", LV_DIR_LEFT}, {"right", LV_DIR_RIGHT}, {"hor", LV_DIR_HOR},
    {"ver", LV_DIR_VER},   {"all", LV_DIR_ALL},
};

static void test_dir_to_enum_accepts_every_direction_name(void)
{
    RUN_ENUM_TABLE(lv_xml_dir_to_enum, DIR_CASES);
}

static void test_dir_to_enum_warns_and_returns_none_for_unknown(void)
{
    log_capture_start();
    TEST_ASSERT_EQUAL_INT32(LV_DIR_NONE, (int32_t)lv_xml_dir_to_enum("bogus_dir"));
    /* Unlike border_side, dir does NOT split on '|'. */
    TEST_ASSERT_EQUAL_INT32(LV_DIR_NONE, (int32_t)lv_xml_dir_to_enum("top|left"));
    TEST_ASSERT_EQUAL_INT32(LV_DIR_NONE, (int32_t)lv_xml_dir_to_enum(""));
    log_capture_stop();

    TEST_ASSERT_TRUE(log_contains("bogus_dir is an unknown value for dir"));
    TEST_ASSERT_TRUE_MESSAGE(log_contains("top|left is an unknown value for dir"),
                             "lv_xml_dir_to_enum() must NOT accept OR'd tokens");
}

/*===========================================================================
 * lv_xml_border_side_to_enum  - the only multi-token converter in the file
 *==========================================================================*/

static const enum_case_t BORDER_SIDE_CASES[] = {
    {"none", LV_BORDER_SIDE_NONE},     {"top", LV_BORDER_SIDE_TOP},
    {"bottom", LV_BORDER_SIDE_BOTTOM}, {"left", LV_BORDER_SIDE_LEFT},
    {"right", LV_BORDER_SIDE_RIGHT},   {"full", LV_BORDER_SIDE_FULL},
};

static void test_border_side_to_enum_accepts_every_side_name(void)
{
    RUN_ENUM_TABLE(lv_xml_border_side_to_enum, BORDER_SIDE_CASES);
}

static const enum_case_t BORDER_SIDE_MULTI_CASES[] = {
    {"left|bottom", LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_BOTTOM},
    {"left,right", LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT},
    {"left right", LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT},
    {"left\tright", LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT},
    {"  left | | right  ", LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT},
    {"top|bottom|left|right", LV_BORDER_SIDE_FULL},
    /* "none" contributes 0 without failing the parse. */
    {"none|left", LV_BORDER_SIDE_LEFT},
    /* OR is idempotent. */
    {"left|left", LV_BORDER_SIDE_LEFT},
    {"full|top", LV_BORDER_SIDE_FULL},
};

static void test_border_side_to_enum_ors_multiple_tokens(void)
{
    RUN_ENUM_TABLE(lv_xml_border_side_to_enum, BORDER_SIDE_MULTI_CASES);
}

static void test_border_side_to_enum_warns_and_returns_none_for_empty_input(void)
{
    log_capture_start();
    /* No token at all: `any` stays false, so this is an error, not NONE. */
    TEST_ASSERT_EQUAL_INT32(0, (int32_t)lv_xml_border_side_to_enum(""));
    TEST_ASSERT_EQUAL_INT32(0, (int32_t)lv_xml_border_side_to_enum("   "));
    TEST_ASSERT_EQUAL_INT32(0, (int32_t)lv_xml_border_side_to_enum("|||"));
    /* LV_BORDER_SIDE_INTERNAL is not part of the dialect. */
    TEST_ASSERT_EQUAL_INT32(0, (int32_t)lv_xml_border_side_to_enum("internal"));
    log_capture_stop();

    TEST_ASSERT_TRUE(log_contains("is an unknown value for border_side"));
    TEST_ASSERT_TRUE(log_contains("internal is an unknown value for border_side"));
}

/**
 * A bad token is skipped, not fatal. Discarding the sides that already parsed
 * turned one typo into "no border at all", which reads as a layout bug rather
 * than a typo - and the warning named the whole attribute value, so it did not
 * say which token to go and fix.
 */
static void test_border_side_to_enum_keeps_valid_tokens_and_names_the_bad_one(void)
{
    log_capture_start();
    TEST_ASSERT_EQUAL_INT32_MESSAGE(LV_BORDER_SIDE_LEFT,
                                    (int32_t)lv_xml_border_side_to_enum("left|bogus"),
                                    "a later bad token must not throw away the earlier valid ones");
    log_capture_stop();
    TEST_ASSERT_TRUE_MESSAGE(log_contains("bogus is an unknown value for border_side"),
                             "the warning must name the offending TOKEN");
    TEST_ASSERT_FALSE_MESSAGE(log_contains("left|bogus is an unknown value"),
                              "the warning must not print the whole attribute value");

    /* The bad token may be anywhere, and there may be several. */
    log_capture_start();
    TEST_ASSERT_EQUAL_INT32(LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT,
                            (int32_t)lv_xml_border_side_to_enum("bogus|left|nonsense|right"));
    log_capture_stop();
    TEST_ASSERT_TRUE(log_contains("bogus is an unknown value for border_side"));
    TEST_ASSERT_TRUE(log_contains("nonsense is an unknown value for border_side"));

    /* When every token is bad there is nothing left to keep, so it is still 0. */
    log_capture_start();
    TEST_ASSERT_EQUAL_INT32(0, (int32_t)lv_xml_border_side_to_enum("bogus|nonsense"));
    log_capture_stop();
    TEST_ASSERT_TRUE(log_contains("bogus is an unknown value for border_side"));
}

/*===========================================================================
 * lv_xml_grad_dir_to_enum
 *==========================================================================*/

static const enum_case_t GRAD_DIR_CASES[] = {
    {"none", LV_GRAD_DIR_NONE}, {"hor", LV_GRAD_DIR_HOR}, {"ver", LV_GRAD_DIR_VER},
};

static void test_grad_dir_to_enum_accepts_none_hor_and_ver(void)
{
    RUN_ENUM_TABLE(lv_xml_grad_dir_to_enum, GRAD_DIR_CASES);

    /* The LVGL enum orders VER before HOR, so "hor" is 2 and "ver" is 1 - the
     * opposite of the order they are written in the source. A test that assumed
     * source order would pass on a coincidence. */
    TEST_ASSERT_EQUAL_INT32(2, (int32_t)lv_xml_grad_dir_to_enum("hor"));
    TEST_ASSERT_EQUAL_INT32(1, (int32_t)lv_xml_grad_dir_to_enum("ver"));
}

static void test_grad_dir_to_enum_warns_and_returns_none_for_unknown(void)
{
    log_capture_start();
    TEST_ASSERT_EQUAL_INT32(LV_GRAD_DIR_NONE, (int32_t)lv_xml_grad_dir_to_enum("bogus_grad"));
    /* LINEAR / RADIAL / CONICAL exist in LVGL but not in the XML dialect. */
    TEST_ASSERT_EQUAL_INT32(LV_GRAD_DIR_NONE, (int32_t)lv_xml_grad_dir_to_enum("radial"));
    log_capture_stop();

    TEST_ASSERT_TRUE(log_contains("bogus_grad is an unknown value for grad_dir"));
    TEST_ASSERT_TRUE(log_contains("radial is an unknown value for grad_dir"));
}

/*===========================================================================
 * lv_xml_base_dir_to_enum
 *==========================================================================*/

static const enum_case_t BASE_DIR_CASES[] = {
    {"auto", LV_BASE_DIR_AUTO}, {"ltr", LV_BASE_DIR_LTR}, {"rtl", LV_BASE_DIR_RTL},
};

static void test_base_dir_to_enum_accepts_auto_ltr_and_rtl(void)
{
    RUN_ENUM_TABLE(lv_xml_base_dir_to_enum, BASE_DIR_CASES);
}

/**
 * The unknown-value fallback is AUTO, deliberately NOT the file's usual
 * `return 0` - 0 is LV_BASE_DIR_LTR, so a typo'd base_dir used to silently
 * FORCE left-to-right and break every RTL locale. AUTO leaves the direction
 * inherited/detected, which is what an absent attribute does.
 */
static void test_base_dir_to_enum_falls_back_to_auto_not_ltr(void)
{
    log_capture_start();
    lv_base_dir_t got = lv_xml_base_dir_to_enum("bogus_dir");
    log_capture_stop();

    TEST_ASSERT_EQUAL_INT32_MESSAGE(LV_BASE_DIR_AUTO, (int32_t)got,
                                    "an unknown base_dir must fall back to AUTO, never to LTR");
    TEST_ASSERT_TRUE_MESSAGE(LV_BASE_DIR_LTR != LV_BASE_DIR_AUTO,
                             "if these ever become equal this test stops meaning anything");
    TEST_ASSERT_TRUE(log_contains("bogus_dir is an unknown value for base_dir"));

    /* Not just the one string: nothing outside auto/ltr/rtl may yield LTR. */
    log_capture_start();
    TEST_ASSERT_EQUAL_INT32(LV_BASE_DIR_AUTO, (int32_t)lv_xml_base_dir_to_enum(""));
    TEST_ASSERT_EQUAL_INT32(LV_BASE_DIR_AUTO, (int32_t)lv_xml_base_dir_to_enum("LTR"));
    log_capture_stop();
}

/*===========================================================================
 * lv_xml_text_align_to_enum
 *==========================================================================*/

static const enum_case_t TEXT_ALIGN_CASES[] = {
    {"auto", LV_TEXT_ALIGN_AUTO},     {"left", LV_TEXT_ALIGN_LEFT},
    {"right", LV_TEXT_ALIGN_RIGHT},   {"center", LV_TEXT_ALIGN_CENTER},
};

static void test_text_align_to_enum_accepts_every_alignment(void)
{
    RUN_ENUM_TABLE(lv_xml_text_align_to_enum, TEXT_ALIGN_CASES);
}

static void test_text_align_to_enum_warns_and_returns_auto_for_unknown(void)
{
    log_capture_start();
    TEST_ASSERT_EQUAL_INT32(LV_TEXT_ALIGN_AUTO, (int32_t)lv_xml_text_align_to_enum("justify"));
    log_capture_stop();

    TEST_ASSERT_TRUE(log_contains("justify is an unknown value for text_align"));
}

/*===========================================================================
 * lv_xml_text_decor_to_enum
 *==========================================================================*/

static const enum_case_t TEXT_DECOR_CASES[] = {
    {"none", LV_TEXT_DECOR_NONE},
    {"underline", LV_TEXT_DECOR_UNDERLINE},
    {"strikethrough", LV_TEXT_DECOR_STRIKETHROUGH},
};

static void test_text_decor_to_enum_accepts_every_decoration(void)
{
    RUN_ENUM_TABLE(lv_xml_text_decor_to_enum, TEXT_DECOR_CASES);
}

static void test_text_decor_to_enum_warns_and_returns_none_for_unknown(void)
{
    log_capture_start();
    TEST_ASSERT_EQUAL_INT32(LV_TEXT_DECOR_NONE, (int32_t)lv_xml_text_decor_to_enum("bogus_decor"));
    /* LV_TEXT_DECOR_* are bit flags and LVGL documents them as OR-able, but
     * this parser does not split, so the combined form is rejected. */
    TEST_ASSERT_EQUAL_INT32(LV_TEXT_DECOR_NONE,
                            (int32_t)lv_xml_text_decor_to_enum("underline|strikethrough"));
    log_capture_stop();

    TEST_ASSERT_TRUE(log_contains("bogus_decor is an unknown value for text_decor"));
    TEST_ASSERT_TRUE(log_contains("underline|strikethrough is an unknown value for text_decor"));
}

/*===========================================================================
 * lv_xml_scroll_snap_to_enum
 *==========================================================================*/

static const enum_case_t SCROLL_SNAP_CASES[] = {
    {"none", LV_SCROLL_SNAP_NONE},   {"start", LV_SCROLL_SNAP_START},
    {"end", LV_SCROLL_SNAP_END},     {"center", LV_SCROLL_SNAP_CENTER},
};

static void test_scroll_snap_to_enum_accepts_every_snap_mode(void)
{
    RUN_ENUM_TABLE(lv_xml_scroll_snap_to_enum, SCROLL_SNAP_CASES);
}

static void test_scroll_snap_to_enum_warns_and_returns_none_for_unknown(void)
{
    log_capture_start();
    TEST_ASSERT_EQUAL_INT32(LV_SCROLL_SNAP_NONE, (int32_t)lv_xml_scroll_snap_to_enum("middle"));
    log_capture_stop();

    TEST_ASSERT_TRUE(log_contains("middle is an unknown value for scroll_snap"));
}

/*===========================================================================
 * lv_xml_scrollbar_mode_to_enum
 *==========================================================================*/

static const enum_case_t SCROLLBAR_MODE_CASES[] = {
    {"off", LV_SCROLLBAR_MODE_OFF},       {"on", LV_SCROLLBAR_MODE_ON},
    {"active", LV_SCROLLBAR_MODE_ACTIVE}, {"auto", LV_SCROLLBAR_MODE_AUTO},
};

static void test_scrollbar_mode_to_enum_accepts_every_mode(void)
{
    RUN_ENUM_TABLE(lv_xml_scrollbar_mode_to_enum, SCROLLBAR_MODE_CASES);
}

/**
 * The fallback (0) is LV_SCROLLBAR_MODE_OFF, so without a warning a typo'd
 * scrollbar_mode is completely undetectable - the scrollbar just disappears.
 * This was the only converter in the warn-group missing the line.
 */
static void test_scrollbar_mode_to_enum_warns_and_returns_off_for_unknown(void)
{
    log_capture_start();
    TEST_ASSERT_EQUAL_INT32(LV_SCROLLBAR_MODE_OFF,
                            (int32_t)lv_xml_scrollbar_mode_to_enum("bogus_scrollbar_mode"));
    TEST_ASSERT_EQUAL_INT32(LV_SCROLLBAR_MODE_OFF, (int32_t)lv_xml_scrollbar_mode_to_enum(""));
    log_capture_stop();

    TEST_ASSERT_TRUE_MESSAGE(log_contains("bogus_scrollbar_mode is an unknown value for scrollbar_mode"),
                             "the warning must match the phrasing every sibling converter uses");
}

/*===========================================================================
 * lv_xml_flex_flow_to_enum
 *==========================================================================*/

static const enum_case_t FLEX_FLOW_CASES[] = {
    {"row", LV_FLEX_FLOW_ROW},
    {"row_reverse", LV_FLEX_FLOW_ROW_REVERSE},
    {"row_wrap", LV_FLEX_FLOW_ROW_WRAP},
    {"row_wrap_reverse", LV_FLEX_FLOW_ROW_WRAP_REVERSE},
    {"column", LV_FLEX_FLOW_COLUMN},
    {"column_reverse", LV_FLEX_FLOW_COLUMN_REVERSE},
    {"column_wrap", LV_FLEX_FLOW_COLUMN_WRAP},
    {"column_wrap_reverse", LV_FLEX_FLOW_COLUMN_WRAP_REVERSE},
};

static void test_flex_flow_to_enum_accepts_every_flow(void)
{
    RUN_ENUM_TABLE(lv_xml_flex_flow_to_enum, FLEX_FLOW_CASES);
}

static void test_flex_flow_to_enum_warns_and_returns_row_for_unknown(void)
{
    log_capture_start();
    TEST_ASSERT_EQUAL_INT32(LV_FLEX_FLOW_ROW, (int32_t)lv_xml_flex_flow_to_enum("diagonal"));
    log_capture_stop();

    TEST_ASSERT_TRUE(log_contains("diagonal is an unknown value for flex_flow"));
}

/*===========================================================================
 * lv_xml_flex_align_to_enum
 *==========================================================================*/

static const enum_case_t FLEX_ALIGN_CASES[] = {
    {"start", LV_FLEX_ALIGN_START},
    {"end", LV_FLEX_ALIGN_END},
    {"center", LV_FLEX_ALIGN_CENTER},
    {"space_evenly", LV_FLEX_ALIGN_SPACE_EVENLY},
    {"space_around", LV_FLEX_ALIGN_SPACE_AROUND},
    {"space_between", LV_FLEX_ALIGN_SPACE_BETWEEN},
};

static void test_flex_align_to_enum_accepts_every_alignment(void)
{
    RUN_ENUM_TABLE(lv_xml_flex_align_to_enum, FLEX_ALIGN_CASES);
}

static void test_flex_align_to_enum_warns_and_returns_start_for_unknown(void)
{
    log_capture_start();
    TEST_ASSERT_EQUAL_INT32(LV_FLEX_ALIGN_START, (int32_t)lv_xml_flex_align_to_enum("stretch"));
    log_capture_stop();

    TEST_ASSERT_TRUE_MESSAGE(log_contains("stretch is an unknown value for flex_align"),
                             "\"stretch\" is a grid_align value; flex_align must reject it");
}

/*===========================================================================
 * lv_xml_grid_align_to_enum
 *==========================================================================*/

static const enum_case_t GRID_ALIGN_CASES[] = {
    {"start", LV_GRID_ALIGN_START},
    {"center", LV_GRID_ALIGN_CENTER},
    {"end", LV_GRID_ALIGN_END},
    {"stretch", LV_GRID_ALIGN_STRETCH},
    {"space_evenly", LV_GRID_ALIGN_SPACE_EVENLY},
    {"space_around", LV_GRID_ALIGN_SPACE_AROUND},
    {"space_between", LV_GRID_ALIGN_SPACE_BETWEEN},
};

static void test_grid_align_to_enum_accepts_every_alignment(void)
{
    RUN_ENUM_TABLE(lv_xml_grid_align_to_enum, GRID_ALIGN_CASES);
}

/**
 * grid and flex share five spelling-identical strings but number them
 * differently, so the two tables can never be merged.
 */
static void test_grid_and_flex_align_number_the_same_names_differently(void)
{
    TEST_ASSERT_TRUE_MESSAGE(
        (int32_t)lv_xml_grid_align_to_enum("center") != (int32_t)lv_xml_flex_align_to_enum("center"),
        "grid and flex \"center\" must keep their own numbering");
    TEST_ASSERT_TRUE_MESSAGE(
        (int32_t)lv_xml_grid_align_to_enum("end") != (int32_t)lv_xml_flex_align_to_enum("end"),
        "grid and flex \"end\" must keep their own numbering");
    /* "start" is 0 in both - the one place they agree. */
    TEST_ASSERT_EQUAL_INT32((int32_t)lv_xml_flex_align_to_enum("start"),
                            (int32_t)lv_xml_grid_align_to_enum("start"));
}

static void test_grid_align_to_enum_warns_and_returns_start_for_unknown(void)
{
    log_capture_start();
    TEST_ASSERT_EQUAL_INT32(LV_GRID_ALIGN_START, (int32_t)lv_xml_grid_align_to_enum("bogus_grid"));
    log_capture_stop();

    TEST_ASSERT_TRUE(log_contains("bogus_grid is an unknown value for grid_align"));
}

/*===========================================================================
 * lv_xml_layout_to_enum
 *==========================================================================*/

static const enum_case_t LAYOUT_CASES[] = {
    {"none", LV_LAYOUT_NONE}, {"flex", LV_LAYOUT_FLEX}, {"grid", LV_LAYOUT_GRID},
};

static void test_layout_to_enum_accepts_none_flex_and_grid(void)
{
    RUN_ENUM_TABLE(lv_xml_layout_to_enum, LAYOUT_CASES);
}

static void test_layout_to_enum_warns_and_returns_none_for_unknown(void)
{
    log_capture_start();
    TEST_ASSERT_EQUAL_INT32(LV_LAYOUT_NONE, (int32_t)lv_xml_layout_to_enum("absolute"));
    log_capture_stop();

    TEST_ASSERT_TRUE(log_contains("absolute is an unknown value for layout"));
}

/*===========================================================================
 * lv_xml_blend_mode_to_enum
 *==========================================================================*/

static const enum_case_t BLEND_MODE_CASES[] = {
    {"normal", LV_BLEND_MODE_NORMAL},           {"additive", LV_BLEND_MODE_ADDITIVE},
    {"subtractive", LV_BLEND_MODE_SUBTRACTIVE}, {"multiply", LV_BLEND_MODE_MULTIPLY},
    {"difference", LV_BLEND_MODE_DIFFERENCE},
};

static void test_blend_mode_to_enum_accepts_every_mode(void)
{
    RUN_ENUM_TABLE(lv_xml_blend_mode_to_enum, BLEND_MODE_CASES);
}

static void test_blend_mode_to_enum_warns_and_returns_normal_for_unknown(void)
{
    log_capture_start();
    TEST_ASSERT_EQUAL_INT32(LV_BLEND_MODE_NORMAL, (int32_t)lv_xml_blend_mode_to_enum("screen"));
    log_capture_stop();

    TEST_ASSERT_TRUE(log_contains("screen is an unknown value for blend_mode"));
}

/*===========================================================================
 * lv_xml_blur_quality_to_enum
 *==========================================================================*/

static const enum_case_t BLUR_QUALITY_CASES[] = {
    {"auto", LV_BLUR_QUALITY_AUTO},
    {"speed", LV_BLUR_QUALITY_SPEED},
    {"precision", LV_BLUR_QUALITY_PRECISION},
};

static void test_blur_quality_to_enum_accepts_every_quality(void)
{
    RUN_ENUM_TABLE(lv_xml_blur_quality_to_enum, BLUR_QUALITY_CASES);
}

static void test_blur_quality_to_enum_warns_and_returns_auto_for_unknown(void)
{
    log_capture_start();
    TEST_ASSERT_EQUAL_INT32(LV_BLUR_QUALITY_AUTO, (int32_t)lv_xml_blur_quality_to_enum("fastest"));
    log_capture_stop();

    TEST_ASSERT_TRUE(log_contains("fastest is an unknown value for blur_quality"));
}

/*===========================================================================
 * lv_xml_trigger_text_to_enum_value  - 68 accepted strings
 *==========================================================================*/

static const enum_case_t TRIGGER_CASES[] = {
    {"all", LV_EVENT_ALL},
    {"pressed", LV_EVENT_PRESSED},
    {"pressing", LV_EVENT_PRESSING},
    {"press_lost", LV_EVENT_PRESS_LOST},
    {"short_clicked", LV_EVENT_SHORT_CLICKED},
    {"single_clicked", LV_EVENT_SINGLE_CLICKED},
    {"double_clicked", LV_EVENT_DOUBLE_CLICKED},
    {"triple_clicked", LV_EVENT_TRIPLE_CLICKED},
    {"long_pressed", LV_EVENT_LONG_PRESSED},
    {"long_pressed_repeat", LV_EVENT_LONG_PRESSED_REPEAT},
    {"clicked", LV_EVENT_CLICKED},
    {"released", LV_EVENT_RELEASED},
    {"scroll_begin", LV_EVENT_SCROLL_BEGIN},
    {"scroll_throw_begin", LV_EVENT_SCROLL_THROW_BEGIN},
    {"scroll_end", LV_EVENT_SCROLL_END},
    {"scroll", LV_EVENT_SCROLL},
    {"gesture", LV_EVENT_GESTURE},
    {"key", LV_EVENT_KEY},
    {"rotary", LV_EVENT_ROTARY},
    {"focused", LV_EVENT_FOCUSED},
    {"defocused", LV_EVENT_DEFOCUSED},
    {"leave", LV_EVENT_LEAVE},
    {"hit_test", LV_EVENT_HIT_TEST},
    {"indev_reset", LV_EVENT_INDEV_RESET},
    {"hover_over", LV_EVENT_HOVER_OVER},
    {"hover_leave", LV_EVENT_HOVER_LEAVE},
    {"cover_check", LV_EVENT_COVER_CHECK},
    {"refr_ext_draw_size", LV_EVENT_REFR_EXT_DRAW_SIZE},
    {"draw_main_begin", LV_EVENT_DRAW_MAIN_BEGIN},
    {"draw_main", LV_EVENT_DRAW_MAIN},
    {"draw_main_end", LV_EVENT_DRAW_MAIN_END},
    {"draw_post_begin", LV_EVENT_DRAW_POST_BEGIN},
    {"draw_post", LV_EVENT_DRAW_POST},
    {"draw_post_end", LV_EVENT_DRAW_POST_END},
    {"draw_task_added", LV_EVENT_DRAW_TASK_ADDED},
    {"value_changed", LV_EVENT_VALUE_CHANGED},
    {"insert", LV_EVENT_INSERT},
    {"refresh", LV_EVENT_REFRESH},
    {"ready", LV_EVENT_READY},
    {"cancel", LV_EVENT_CANCEL},
    {"create", LV_EVENT_CREATE},
    {"delete", LV_EVENT_DELETE},
    {"child_changed", LV_EVENT_CHILD_CHANGED},
    {"child_created", LV_EVENT_CHILD_CREATED},
    {"child_deleted", LV_EVENT_CHILD_DELETED},
    {"state_changed", LV_EVENT_STATE_CHANGED},
    {"screen_unload_start", LV_EVENT_SCREEN_UNLOAD_START},
    {"screen_load_start", LV_EVENT_SCREEN_LOAD_START},
    {"screen_loaded", LV_EVENT_SCREEN_LOADED},
    {"screen_unloaded", LV_EVENT_SCREEN_UNLOADED},
    {"size_changed", LV_EVENT_SIZE_CHANGED},
    {"style_changed", LV_EVENT_STYLE_CHANGED},
    {"layout_changed", LV_EVENT_LAYOUT_CHANGED},
    {"get_self_size", LV_EVENT_GET_SELF_SIZE},
    {"invalidate_area", LV_EVENT_INVALIDATE_AREA},
    {"resolution_changed", LV_EVENT_RESOLUTION_CHANGED},
    {"color_format_changed", LV_EVENT_COLOR_FORMAT_CHANGED},
    {"refr_request", LV_EVENT_REFR_REQUEST},
    {"refr_start", LV_EVENT_REFR_START},
    {"refr_ready", LV_EVENT_REFR_READY},
    {"render_start", LV_EVENT_RENDER_START},
    {"render_ready", LV_EVENT_RENDER_READY},
    {"flush_start", LV_EVENT_FLUSH_START},
    {"flush_finish", LV_EVENT_FLUSH_FINISH},
    {"flush_wait_start", LV_EVENT_FLUSH_WAIT_START},
    {"flush_wait_finish", LV_EVENT_FLUSH_WAIT_FINISH},
    {"vsync", LV_EVENT_VSYNC},
};

static void test_trigger_to_enum_accepts_every_event_name(void)
{
    TEST_ASSERT_EQUAL_size_t_MESSAGE(67, sizeof(TRIGGER_CASES) / sizeof(TRIGGER_CASES[0]),
                                     "the trigger table must stay exhaustive - 67 accepted strings");
    RUN_ENUM_TABLE(lv_xml_trigger_text_to_enum_value, TRIGGER_CASES);
}

/**
 * The cleanest error contract in the file: LV_EVENT_LAST is not a real event,
 * so unlike every other converter here a caller CAN detect the failure.
 */
static void test_trigger_to_enum_returns_a_distinguishable_sentinel_for_unknown(void)
{
    log_capture_start();
    TEST_ASSERT_EQUAL_INT32(LV_EVENT_LAST, (int32_t)lv_xml_trigger_text_to_enum_value("bogus_event"));
    /* The 0x8000 "preprocess" flag form is not accepted. */
    TEST_ASSERT_EQUAL_INT32(LV_EVENT_LAST, (int32_t)lv_xml_trigger_text_to_enum_value("preprocess"));
    TEST_ASSERT_EQUAL_INT32(LV_EVENT_LAST, (int32_t)lv_xml_trigger_text_to_enum_value(""));
    log_capture_stop();

    TEST_ASSERT_TRUE_MESSAGE((int32_t)LV_EVENT_LAST != (int32_t)LV_EVENT_ALL,
                             "the sentinel must not collide with the accepted string \"all\"");
    TEST_ASSERT_TRUE(log_contains("bogus_event is an unknown value for event's trigger"));
}

/*===========================================================================
 * lv_xml_screen_load_anim_text_to_enum_value
 *==========================================================================*/

static const enum_case_t SCREEN_LOAD_ANIM_CASES[] = {
    {"none", LV_SCREEN_LOAD_ANIM_NONE},
    {"over_left", LV_SCREEN_LOAD_ANIM_OVER_LEFT},
    {"over_right", LV_SCREEN_LOAD_ANIM_OVER_RIGHT},
    {"over_top", LV_SCREEN_LOAD_ANIM_OVER_TOP},
    {"over_bottom", LV_SCREEN_LOAD_ANIM_OVER_BOTTOM},
    {"move_left", LV_SCREEN_LOAD_ANIM_MOVE_LEFT},
    {"move_right", LV_SCREEN_LOAD_ANIM_MOVE_RIGHT},
    {"move_top", LV_SCREEN_LOAD_ANIM_MOVE_TOP},
    {"move_bottom", LV_SCREEN_LOAD_ANIM_MOVE_BOTTOM},
    {"fade_in", LV_SCREEN_LOAD_ANIM_FADE_IN},
    {"fade_on", LV_SCREEN_LOAD_ANIM_FADE_ON},
    {"fade_out", LV_SCREEN_LOAD_ANIM_FADE_OUT},
    {"out_left", LV_SCREEN_LOAD_ANIM_OUT_LEFT},
    {"out_right", LV_SCREEN_LOAD_ANIM_OUT_RIGHT},
    {"out_top", LV_SCREEN_LOAD_ANIM_OUT_TOP},
    {"out_bottom", LV_SCREEN_LOAD_ANIM_OUT_BOTTOM},
};

static void test_screen_load_anim_to_enum_accepts_every_animation(void)
{
    RUN_ENUM_TABLE(lv_xml_screen_load_anim_text_to_enum_value, SCREEN_LOAD_ANIM_CASES);

    /* "fade_on" is LVGL's back-compat alias for "fade_in" and is numerically
     * identical - two spellings, one animation. */
    TEST_ASSERT_EQUAL_INT32_MESSAGE(
        (int32_t)lv_xml_screen_load_anim_text_to_enum_value("fade_in"),
        (int32_t)lv_xml_screen_load_anim_text_to_enum_value("fade_on"),
        "fade_in and fade_on are the same LVGL enumerator");
}

static void test_screen_load_anim_to_enum_warns_and_returns_none_for_unknown(void)
{
    log_capture_start();
    TEST_ASSERT_EQUAL_INT32(LV_SCREEN_LOAD_ANIM_NONE,
                            (int32_t)lv_xml_screen_load_anim_text_to_enum_value("spin"));
    log_capture_stop();

    TEST_ASSERT_TRUE(log_contains("spin is an unknown value for screen_load_anim"));
}

/*===========================================================================
 * lv_xml_style_prop_to_enum  - 110 accepted strings
 *==========================================================================*/

static const enum_case_t STYLE_PROP_CASES[] = {
    /* size / position */
    {"width", LV_STYLE_WIDTH},
    {"min_width", LV_STYLE_MIN_WIDTH},
    {"max_width", LV_STYLE_MAX_WIDTH},
    {"height", LV_STYLE_HEIGHT},
    {"min_height", LV_STYLE_MIN_HEIGHT},
    {"max_height", LV_STYLE_MAX_HEIGHT},
    {"length", LV_STYLE_LENGTH},
    {"radius", LV_STYLE_RADIUS},
    {"radial_offset", LV_STYLE_RADIAL_OFFSET},
    {"align", LV_STYLE_ALIGN},
    /* padding */
    {"pad_left", LV_STYLE_PAD_LEFT},
    {"pad_right", LV_STYLE_PAD_RIGHT},
    {"pad_top", LV_STYLE_PAD_TOP},
    {"pad_bottom", LV_STYLE_PAD_BOTTOM},
    {"pad_row", LV_STYLE_PAD_ROW},
    {"pad_column", LV_STYLE_PAD_COLUMN},
    {"pad_radial", LV_STYLE_PAD_RADIAL},
    /* margin */
    {"margin_left", LV_STYLE_MARGIN_LEFT},
    {"margin_right", LV_STYLE_MARGIN_RIGHT},
    {"margin_top", LV_STYLE_MARGIN_TOP},
    {"margin_bottom", LV_STYLE_MARGIN_BOTTOM},
    /* misc */
    {"base_dir", LV_STYLE_BASE_DIR},
    {"clip_corner", LV_STYLE_CLIP_CORNER},
    /* background */
    {"bg_opa", LV_STYLE_BG_OPA},
    {"bg_color", LV_STYLE_BG_COLOR},
    {"bg_grad_dir", LV_STYLE_BG_GRAD_DIR},
    {"bg_grad_color", LV_STYLE_BG_GRAD_COLOR},
    {"bg_main_stop", LV_STYLE_BG_MAIN_STOP},
    {"bg_grad_stop", LV_STYLE_BG_GRAD_STOP},
    {"bg_grad", LV_STYLE_BG_GRAD},
    /* background image */
    {"bg_image_src", LV_STYLE_BG_IMAGE_SRC},
    {"bg_image_tiled", LV_STYLE_BG_IMAGE_TILED},
    {"bg_image_recolor", LV_STYLE_BG_IMAGE_RECOLOR},
    {"bg_image_recolor_opa", LV_STYLE_BG_IMAGE_RECOLOR_OPA},
    /* border */
    {"border_color", LV_STYLE_BORDER_COLOR},
    {"border_width", LV_STYLE_BORDER_WIDTH},
    {"border_opa", LV_STYLE_BORDER_OPA},
    {"border_side", LV_STYLE_BORDER_SIDE},
    {"border_post", LV_STYLE_BORDER_POST},
    /* outline */
    {"outline_color", LV_STYLE_OUTLINE_COLOR},
    {"outline_width", LV_STYLE_OUTLINE_WIDTH},
    {"outline_opa", LV_STYLE_OUTLINE_OPA},
    {"outline_pad", LV_STYLE_OUTLINE_PAD},
    /* shadow */
    {"shadow_width", LV_STYLE_SHADOW_WIDTH},
    {"shadow_color", LV_STYLE_SHADOW_COLOR},
    {"shadow_offset_x", LV_STYLE_SHADOW_OFFSET_X},
    {"shadow_offset_y", LV_STYLE_SHADOW_OFFSET_Y},
    {"shadow_spread", LV_STYLE_SHADOW_SPREAD},
    {"shadow_opa", LV_STYLE_SHADOW_OPA},
    /* text */
    {"text_color", LV_STYLE_TEXT_COLOR},
    {"text_font", LV_STYLE_TEXT_FONT},
    {"text_opa", LV_STYLE_TEXT_OPA},
    {"text_align", LV_STYLE_TEXT_ALIGN},
    {"text_letter_space", LV_STYLE_TEXT_LETTER_SPACE},
    {"text_line_space", LV_STYLE_TEXT_LINE_SPACE},
    {"text_decor", LV_STYLE_TEXT_DECOR},
    /* image */
    {"image_opa", LV_STYLE_IMAGE_OPA},
    {"image_recolor", LV_STYLE_IMAGE_RECOLOR},
    {"image_recolor_opa", LV_STYLE_IMAGE_RECOLOR_OPA},
    /* line */
    {"line_color", LV_STYLE_LINE_COLOR},
    {"line_opa", LV_STYLE_LINE_OPA},
    {"line_width", LV_STYLE_LINE_WIDTH},
    {"line_dash_width", LV_STYLE_LINE_DASH_WIDTH},
    {"line_dash_gap", LV_STYLE_LINE_DASH_GAP},
    {"line_rounded", LV_STYLE_LINE_ROUNDED},
    /* arc */
    {"arc_color", LV_STYLE_ARC_COLOR},
    {"arc_opa", LV_STYLE_ARC_OPA},
    {"arc_width", LV_STYLE_ARC_WIDTH},
    {"arc_rounded", LV_STYLE_ARC_ROUNDED},
    {"arc_image_src", LV_STYLE_ARC_IMAGE_SRC},
    /* transform / other */
    {"opa", LV_STYLE_OPA},
    {"opa_layered", LV_STYLE_OPA_LAYERED},
    {"color_filter_opa", LV_STYLE_COLOR_FILTER_OPA},
    {"anim_duration", LV_STYLE_ANIM_DURATION},
    {"blend_mode", LV_STYLE_BLEND_MODE},
    {"transform_width", LV_STYLE_TRANSFORM_WIDTH},
    {"transform_height", LV_STYLE_TRANSFORM_HEIGHT},
    {"translate_x", LV_STYLE_TRANSLATE_X},
    {"translate_y", LV_STYLE_TRANSLATE_Y},
    {"translate_radial", LV_STYLE_TRANSLATE_RADIAL},
    {"transform_scale_x", LV_STYLE_TRANSFORM_SCALE_X},
    {"transform_scale_y", LV_STYLE_TRANSFORM_SCALE_Y},
    {"transform_rotation", LV_STYLE_TRANSFORM_ROTATION},
    {"transform_pivot_x", LV_STYLE_TRANSFORM_PIVOT_X},
    {"transform_pivot_y", LV_STYLE_TRANSFORM_PIVOT_Y},
    {"transform_skew_x", LV_STYLE_TRANSFORM_SKEW_X},
    {"transform_skew_y", LV_STYLE_TRANSFORM_SKEW_Y},
    {"bitmap_mask_src", LV_STYLE_BITMAP_MASK_SRC},
    {"rotary_sensitivity", LV_STYLE_ROTARY_SENSITIVITY},
    {"recolor", LV_STYLE_RECOLOR},
    {"recolor_opa", LV_STYLE_RECOLOR_OPA},
    /* layout */
    {"layout", LV_STYLE_LAYOUT},
    /* flex */
    {"flex_flow", LV_STYLE_FLEX_FLOW},
    {"flex_grow", LV_STYLE_FLEX_GROW},
    {"flex_main_place", LV_STYLE_FLEX_MAIN_PLACE},
    {"flex_cross_place", LV_STYLE_FLEX_CROSS_PLACE},
    {"flex_track_place", LV_STYLE_FLEX_TRACK_PLACE},
    /* grid */
    {"grid_column_align", LV_STYLE_GRID_COLUMN_ALIGN},
    {"grid_row_align", LV_STYLE_GRID_ROW_ALIGN},
    {"grid_cell_column_pos", LV_STYLE_GRID_CELL_COLUMN_POS},
    {"grid_cell_column_span", LV_STYLE_GRID_CELL_COLUMN_SPAN},
    {"grid_cell_x_align", LV_STYLE_GRID_CELL_X_ALIGN},
    {"grid_cell_row_pos", LV_STYLE_GRID_CELL_ROW_POS},
    {"grid_cell_row_span", LV_STYLE_GRID_CELL_ROW_SPAN},
    {"grid_cell_y_align", LV_STYLE_GRID_CELL_Y_ALIGN},
};

static void test_style_prop_to_enum_accepts_every_property_name(void)
{
    TEST_ASSERT_EQUAL_size_t_MESSAGE(105, sizeof(STYLE_PROP_CASES) / sizeof(STYLE_PROP_CASES[0]),
                                     "the style-prop table must stay exhaustive - 105 accepted strings");
    RUN_ENUM_TABLE(lv_xml_style_prop_to_enum, STYLE_PROP_CASES);
}

/**
 * LV_STYLE_PROP_INV is a real sentinel: it is not a usable property, so a
 * caller can tell a typo from a valid parse. Only this converter and
 * lv_xml_trigger_text_to_enum_value manage that.
 */
static void test_style_prop_to_enum_returns_inv_without_warning_for_unknown(void)
{
    log_capture_start();
    TEST_ASSERT_EQUAL_INT32(LV_STYLE_PROP_INV, (int32_t)lv_xml_style_prop_to_enum("bogus_prop"));
    TEST_ASSERT_EQUAL_INT32(LV_STYLE_PROP_INV, (int32_t)lv_xml_style_prop_to_enum(""));
    /* Properties LVGL has but the XML dialect does not expose. */
    TEST_ASSERT_EQUAL_INT32(LV_STYLE_PROP_INV, (int32_t)lv_xml_style_prop_to_enum("x"));
    TEST_ASSERT_EQUAL_INT32(LV_STYLE_PROP_INV, (int32_t)lv_xml_style_prop_to_enum("y"));
    TEST_ASSERT_EQUAL_INT32(LV_STYLE_PROP_INV, (int32_t)lv_xml_style_prop_to_enum("transition"));
    /* Shorthands are expanded elsewhere, not here. */
    TEST_ASSERT_EQUAL_INT32(LV_STYLE_PROP_INV, (int32_t)lv_xml_style_prop_to_enum("pad_all"));
    TEST_ASSERT_EQUAL_INT32(LV_STYLE_PROP_INV, (int32_t)lv_xml_style_prop_to_enum("pad_hor"));
    TEST_ASSERT_EQUAL_INT32(LV_STYLE_PROP_INV, (int32_t)lv_xml_style_prop_to_enum("pad_ver"));
    log_capture_stop();

    TEST_ASSERT_FALSE_MESSAGE(log_contains("bogus_prop"),
                              "lv_xml_style_prop_to_enum reports through its return value, not the log");
}

/*===========================================================================
 * lv_xml_style_state_to_enum
 *==========================================================================*/

static const enum_case_t STYLE_STATE_CASES[] = {
    {"default", LV_STATE_DEFAULT},   {"pressed", LV_STATE_PRESSED},
    {"checked", LV_STATE_CHECKED},   {"scrolled", LV_STATE_SCROLLED},
    {"focused", LV_STATE_FOCUSED},   {"focus_key", LV_STATE_FOCUS_KEY},
    {"edited", LV_STATE_EDITED},     {"hovered", LV_STATE_HOVERED},
    {"disabled", LV_STATE_DISABLED}, {"user_1", LV_STATE_USER_1},
    {"user_2", LV_STATE_USER_2},     {"user_3", LV_STATE_USER_3},
    {"user_4", LV_STATE_USER_4},
};

static void test_style_state_to_enum_accepts_every_state_name(void)
{
    RUN_ENUM_TABLE(lv_xml_style_state_to_enum, STYLE_STATE_CASES);
}

/**
 * Same table as lv_xml_state_to_enum but a different failure contract: this
 * one is silent. A typo in a style selector produces LV_STATE_DEFAULT with no
 * diagnostic anywhere.
 */
static void test_style_state_to_enum_falls_back_to_default_without_warning(void)
{
    log_capture_start();
    TEST_ASSERT_EQUAL_INT32(LV_STATE_DEFAULT, (int32_t)lv_xml_style_state_to_enum("bogus_state"));
    TEST_ASSERT_EQUAL_INT32(LV_STATE_DEFAULT, (int32_t)lv_xml_style_state_to_enum(""));
    log_capture_stop();

    TEST_ASSERT_FALSE_MESSAGE(log_contains("bogus_state"),
                              "lv_xml_style_state_to_enum is the silent twin of lv_xml_state_to_enum");
}

/*===========================================================================
 * lv_xml_style_part_to_enum
 *==========================================================================*/

static const enum_case_t STYLE_PART_CASES[] = {
    {"main", LV_PART_MAIN},         {"scrollbar", LV_PART_SCROLLBAR},
    {"indicator", LV_PART_INDICATOR}, {"knob", LV_PART_KNOB},
    {"selected", LV_PART_SELECTED}, {"items", LV_PART_ITEMS},
    {"cursor", LV_PART_CURSOR},
};

static void test_style_part_to_enum_accepts_every_part_name(void)
{
    RUN_ENUM_TABLE(lv_xml_style_part_to_enum, STYLE_PART_CASES);
}

static void test_style_part_to_enum_falls_back_to_main_without_warning(void)
{
    log_capture_start();
    TEST_ASSERT_EQUAL_INT32(LV_PART_MAIN, (int32_t)lv_xml_style_part_to_enum("bogus_part"));
    /* LV_PART_CUSTOM_FIRST and LV_PART_ANY are not part of the dialect. */
    TEST_ASSERT_EQUAL_INT32(LV_PART_MAIN, (int32_t)lv_xml_style_part_to_enum("any"));
    TEST_ASSERT_EQUAL_INT32(LV_PART_MAIN, (int32_t)lv_xml_style_part_to_enum(""));
    log_capture_stop();

    TEST_ASSERT_FALSE_MESSAGE(log_contains("bogus_part"),
                              "lv_xml_style_part_to_enum never warns");
}

/*===========================================================================
 * lv_xml_style_selector_text_to_enum
 *==========================================================================*/

static void test_style_selector_ors_state_and_part(void)
{
    TEST_ASSERT_EQUAL_UINT32(LV_PART_KNOB, lv_xml_style_selector_text_to_enum("knob"));
    TEST_ASSERT_EQUAL_UINT32(LV_STATE_PRESSED, lv_xml_style_selector_text_to_enum("pressed"));
    TEST_ASSERT_EQUAL_UINT32(LV_PART_KNOB | LV_STATE_PRESSED,
                             lv_xml_style_selector_text_to_enum("knob|pressed"));
    /* Token order does not matter - both sides are OR'd unconditionally. */
    TEST_ASSERT_EQUAL_UINT32(LV_PART_KNOB | LV_STATE_PRESSED,
                             lv_xml_style_selector_text_to_enum("pressed|knob"));
    TEST_ASSERT_EQUAL_UINT32(LV_PART_INDICATOR | LV_STATE_CHECKED | LV_STATE_DISABLED,
                             lv_xml_style_selector_text_to_enum("indicator|checked|disabled"));
    /* Empty tokens are dropped by lv_xml_split_str. */
    TEST_ASSERT_EQUAL_UINT32(LV_PART_KNOB | LV_STATE_PRESSED,
                             lv_xml_style_selector_text_to_enum("knob||pressed"));
}

/** The second of the two NULL-guarded converters. */
static void test_style_selector_guards_null_and_empty(void)
{
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, lv_xml_style_selector_text_to_enum(NULL),
                                     "lv_xml_style_selector_text_to_enum() must guard NULL");
    TEST_ASSERT_EQUAL_UINT32(0, lv_xml_style_selector_text_to_enum(""));
    TEST_ASSERT_EQUAL_UINT32(0, lv_xml_style_selector_text_to_enum("|||"));
    /* "main|default" is two legitimate tokens that are both 0. */
    TEST_ASSERT_EQUAL_UINT32(0, lv_xml_style_selector_text_to_enum("main|default"));
}

/**
 * A bad token is now REPORTED, but still dropped rather than failing the parse.
 * The lenient return is deliberate and must not change: consuming XML may
 * already carry tokens this parser does not know, and a hard failure would be a
 * behaviour change rather than a bug fix. So: warning only.
 *
 * The remaining sharp edges below are unchanged and stay pinned - '|' is the
 * only separator (a stray space makes a valid token unrecognisable), and two
 * PART tokens OR into a value that is neither of them.
 */
static void test_style_selector_warns_but_still_drops_bad_tokens(void)
{
    log_capture_start();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, lv_xml_style_selector_text_to_enum("bogus"),
                                     "an unknown token must still contribute nothing");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(LV_PART_KNOB,
                                     lv_xml_style_selector_text_to_enum("knob|bogus"),
                                     "a bad token must be dropped, NOT fail the whole selector");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, lv_xml_style_selector_text_to_enum(" knob"),
                                     "there is no whitespace trimming - only '|' separates tokens");

    log_capture_stop();
    TEST_ASSERT_TRUE_MESSAGE(log_contains("bogus is an unknown token in style selector"),
                             "the offending token must be named in the log");
    TEST_ASSERT_TRUE_MESSAGE(log_contains(" knob is an unknown token in style selector"),
                             "an untrimmed token is unrecognised and must say so");

    /* Two parts OR'd is nonsense, not an error: SCROLLBAR|KNOB happens to be
     * bit-identical to KNOB. Both tokens are known, so nothing is logged. */
    log_capture_start();
    TEST_ASSERT_EQUAL_UINT32(LV_PART_SCROLLBAR | LV_PART_KNOB,
                             lv_xml_style_selector_text_to_enum("knob|scrollbar"));
    TEST_ASSERT_EQUAL_UINT32(LV_PART_KNOB, lv_xml_style_selector_text_to_enum("knob|scrollbar"));
    log_capture_stop();
    TEST_ASSERT_FALSE_MESSAGE(log_contains("unknown token"),
                              "two known PART tokens are legal input, however useless the result");
}

/**
 * "main" and "default" are recognised names whose value happens to be 0. They
 * must NOT warn - the warning distinguishes "unknown token" from "known token
 * worth zero", which the enum values alone cannot.
 */
static void test_style_selector_does_not_warn_for_known_zero_valued_tokens(void)
{
    log_capture_start();
    TEST_ASSERT_EQUAL_UINT32(0, lv_xml_style_selector_text_to_enum("main|default"));
    TEST_ASSERT_EQUAL_UINT32(0, lv_xml_style_selector_text_to_enum("main"));
    TEST_ASSERT_EQUAL_UINT32(0, lv_xml_style_selector_text_to_enum("default"));
    /* No tokens at all is not a bad token. */
    TEST_ASSERT_EQUAL_UINT32(0, lv_xml_style_selector_text_to_enum("|||"));
    TEST_ASSERT_EQUAL_UINT32(0, lv_xml_style_selector_text_to_enum(""));
    log_capture_stop();

    TEST_ASSERT_FALSE_MESSAGE(log_contains("unknown token"),
                              "a recognised token that evaluates to 0 must not be reported as unknown");
}

/**
 * A selector of 256 characters or more.
 *
 * The function copies into `char buf[256]` with lv_strncpy(), which mirrors
 * strncpy(): when the source is at least dst_size bytes it fills the buffer and
 * writes NO terminator. The split loop then ran off the end of that stack
 * buffer, reading whatever the frame happened to hold.
 *
 * The input is built so the over-read is observable rather than merely
 * undefined: the first 255 bytes are padding tokens, so an in-bounds parse
 * yields nothing but "unknown token" warnings and a selector of 0. Only a read
 * past the buffer can pick up anything else - and under ASAN it is a hard
 * stack-buffer-overflow.
 */
static void test_style_selector_longer_than_the_buffer_is_truncated_not_overread(void)
{
    /* 300 'a's: one token, far longer than the 256-byte copy buffer. */
    char oversized[301];
    lv_memset(oversized, 'a', sizeof(oversized) - 1);
    oversized[sizeof(oversized) - 1] = '\0';

    log_capture_start();
    lv_style_selector_t sel = lv_xml_style_selector_text_to_enum(oversized);
    log_capture_stop();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, sel,
                                     "an oversized selector names no state or part, so it is 0");
    TEST_ASSERT_TRUE_MESSAGE(log_contains("truncated"),
                             "an over-length selector must say it was truncated, not silently "
                             "read past the copy buffer");

    /* Exactly 256 bytes of content is the boundary: lv_strncpy() fills the
     * buffer completely and leaves no room for the terminator. */
    char exact[257];
    lv_memset(exact, 'b', sizeof(exact) - 1);
    exact[sizeof(exact) - 1] = '\0';

    log_capture_start();
    lv_style_selector_t sel_exact = lv_xml_style_selector_text_to_enum(exact);
    log_capture_stop();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, sel_exact, "a 256-byte selector still resolves to 0");
    TEST_ASSERT_TRUE_MESSAGE(log_contains("truncated"),
                             "256 bytes is already over the limit - lv_strncpy() writes no "
                             "terminator at exactly dst_size");

    /* 255 bytes fits with room for the terminator: no truncation. */
    char fits[256];
    lv_memset(fits, 'c', sizeof(fits) - 1);
    fits[sizeof(fits) - 1] = '\0';

    log_capture_start();
    (void)lv_xml_style_selector_text_to_enum(fits);
    log_capture_stop();

    TEST_ASSERT_FALSE_MESSAGE(log_contains("truncated"),
                              "255 characters fit in the buffer and must not be reported truncated");
}

/** The caller's string must survive - the destructive split works on a copy. */
static void test_style_selector_does_not_mutate_the_caller_string(void)
{
    char input[] = "knob|pressed";

    TEST_ASSERT_EQUAL_UINT32(LV_PART_KNOB | LV_STATE_PRESSED,
                             lv_xml_style_selector_text_to_enum(input));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("knob|pressed", input,
                                     "the selector parser must copy before splitting - "
                                     "lv_xml_split_str writes NULs into its input");
}

/*---------------------------------------------------------------------------
 * main
 *--------------------------------------------------------------------------*/

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_state_to_enum_accepts_every_state_name);
    RUN_TEST(test_state_to_enum_warns_and_returns_zero_for_unknown);

    RUN_TEST(test_to_size_parses_pixels_percentages_and_content);
    RUN_TEST(test_to_size_zero_percent_is_not_zero);
    RUN_TEST(test_to_size_guards_null_and_empty);
    RUN_TEST(test_to_size_empty_string_does_not_read_the_byte_before_it);
    RUN_TEST(test_to_size_returns_zero_for_unparseable_input);

    RUN_TEST(test_align_to_enum_accepts_every_align_name);
    RUN_TEST(test_align_to_enum_warns_and_returns_default_for_unknown);

    RUN_TEST(test_dir_to_enum_accepts_every_direction_name);
    RUN_TEST(test_dir_to_enum_warns_and_returns_none_for_unknown);

    RUN_TEST(test_border_side_to_enum_accepts_every_side_name);
    RUN_TEST(test_border_side_to_enum_ors_multiple_tokens);
    RUN_TEST(test_border_side_to_enum_warns_and_returns_none_for_empty_input);
    RUN_TEST(test_border_side_to_enum_keeps_valid_tokens_and_names_the_bad_one);

    RUN_TEST(test_grad_dir_to_enum_accepts_none_hor_and_ver);
    RUN_TEST(test_grad_dir_to_enum_warns_and_returns_none_for_unknown);

    RUN_TEST(test_base_dir_to_enum_accepts_auto_ltr_and_rtl);
    RUN_TEST(test_base_dir_to_enum_falls_back_to_auto_not_ltr);

    RUN_TEST(test_text_align_to_enum_accepts_every_alignment);
    RUN_TEST(test_text_align_to_enum_warns_and_returns_auto_for_unknown);

    RUN_TEST(test_text_decor_to_enum_accepts_every_decoration);
    RUN_TEST(test_text_decor_to_enum_warns_and_returns_none_for_unknown);

    RUN_TEST(test_scroll_snap_to_enum_accepts_every_snap_mode);
    RUN_TEST(test_scroll_snap_to_enum_warns_and_returns_none_for_unknown);

    RUN_TEST(test_scrollbar_mode_to_enum_accepts_every_mode);
    RUN_TEST(test_scrollbar_mode_to_enum_warns_and_returns_off_for_unknown);

    RUN_TEST(test_flex_flow_to_enum_accepts_every_flow);
    RUN_TEST(test_flex_flow_to_enum_warns_and_returns_row_for_unknown);

    RUN_TEST(test_flex_align_to_enum_accepts_every_alignment);
    RUN_TEST(test_flex_align_to_enum_warns_and_returns_start_for_unknown);

    RUN_TEST(test_grid_align_to_enum_accepts_every_alignment);
    RUN_TEST(test_grid_and_flex_align_number_the_same_names_differently);
    RUN_TEST(test_grid_align_to_enum_warns_and_returns_start_for_unknown);

    RUN_TEST(test_layout_to_enum_accepts_none_flex_and_grid);
    RUN_TEST(test_layout_to_enum_warns_and_returns_none_for_unknown);

    RUN_TEST(test_blend_mode_to_enum_accepts_every_mode);
    RUN_TEST(test_blend_mode_to_enum_warns_and_returns_normal_for_unknown);

    RUN_TEST(test_blur_quality_to_enum_accepts_every_quality);
    RUN_TEST(test_blur_quality_to_enum_warns_and_returns_auto_for_unknown);

    RUN_TEST(test_trigger_to_enum_accepts_every_event_name);
    RUN_TEST(test_trigger_to_enum_returns_a_distinguishable_sentinel_for_unknown);

    RUN_TEST(test_screen_load_anim_to_enum_accepts_every_animation);
    RUN_TEST(test_screen_load_anim_to_enum_warns_and_returns_none_for_unknown);

    RUN_TEST(test_style_prop_to_enum_accepts_every_property_name);
    RUN_TEST(test_style_prop_to_enum_returns_inv_without_warning_for_unknown);

    RUN_TEST(test_style_state_to_enum_accepts_every_state_name);
    RUN_TEST(test_style_state_to_enum_falls_back_to_default_without_warning);

    RUN_TEST(test_style_part_to_enum_accepts_every_part_name);
    RUN_TEST(test_style_part_to_enum_falls_back_to_main_without_warning);

    RUN_TEST(test_style_selector_ors_state_and_part);
    RUN_TEST(test_style_selector_guards_null_and_empty);
    RUN_TEST(test_style_selector_warns_but_still_drops_bad_tokens);
    RUN_TEST(test_style_selector_does_not_warn_for_known_zero_valued_tokens);
    RUN_TEST(test_style_selector_longer_than_the_buffer_is_truncated_not_overread);
    RUN_TEST(test_style_selector_does_not_mutate_the_caller_string);

    return UNITY_END();
}
