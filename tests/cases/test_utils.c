/**
 * @file test_utils.c
 *
 * Every export of src/xml/lv_xml_utils.h:
 *   lv_xml_get_value_of, lv_xml_atoi, lv_xml_atoi_split, lv_xml_atof,
 *   lv_xml_atof_split, lv_xml_to_color, lv_xml_to_opa, lv_xml_to_bool,
 *   lv_xml_strtol, lv_xml_split_str.
 *
 * These are the string primitives the whole XML engine is built on: every
 * attribute value in every layout goes through one of them. None of them
 * report failure - they return a plausible-looking number instead - so the
 * only way to know what a malformed attribute does is to pin it here.
 *
 * ---------------------------------------------------------------------------
 * WHAT IS DELIBERATELY NOT TESTED, AND WHY
 *
 * Several inputs are undefined behaviour in the current implementation. A test
 * that calls them would "pass" on a plain build and blow up under the ASAN
 * job, so they are documented here instead of exercised:
 *
 *  - lv_xml_atoi(""), lv_xml_atoi("   "), lv_xml_atof(""), lv_xml_atof("  ")
 *    BUG: both delegate to *_split with delimiter '\0'. The leading-skip loop
 *    is `while(*s == delimiter || *s == ' ' || *s == '\t') s++;` so with
 *    delimiter == '\0' it steps PAST the NUL terminator and keeps reading
 *    whatever follows the buffer until it hits a byte that is none of those
 *    three. The result depends on unrelated memory.
 *    Empty/blank input IS covered here through the *_split entry points with a
 *    real delimiter, where the same loop terminates correctly.
 *
 *  - lv_xml_to_opa(""), lv_xml_to_opa("  ")
 *    BUG: reads `str[lv_strlen(str) - 1]`, i.e. str[-1] for the empty string -
 *    one byte BEFORE the buffer - and also inherits the atoi over-read above.
 *    lv_xml_to_size was hardened against exactly this (#1121); to_opa was not.
 *
 *  - lv_xml_atoi / lv_xml_atoi_split on values that do not fit in int32_t.
 *    There is no overflow check at all (`result = result * 10 + digit`), so it
 *    is signed-overflow UB rather than a defined wrap. Overflow IS covered on
 *    lv_xml_strtol, which does check.
 *
 *  - NULL into lv_xml_atoi/atof/to_color/to_opa/to_bool/strtol/split_str.
 *    None of them guard; they dereference immediately. Only
 *    lv_xml_get_value_of guards its arguments, and that IS tested.
 *
 *  - lv_xml_split_str(&p, '\0') on an empty string: the skip loop
 *    `while(**src == '\0') (*src)++;` never terminates.
 * ---------------------------------------------------------------------------
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Log capture: a rejected value returns a plausible-looking colour/number, so
 * the warning is the only thing that distinguishes "rejected" from "parsed". */
#include "helpers/helix_log_capture.h"
#include "helpers/helix_test_env.h"
#include "helpers/xml_assert.h"

#include "xml/lv_xml_utils.h"

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

/** Assert a colour's three channels. Nothing here touches the display. */
static void assert_color_rgb(lv_color_t c, uint8_t r, uint8_t g, uint8_t b, const char * what)
{
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(r, c.red, helix_xml_assert_msgf("%s: wrong red channel", what));
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(g, c.green, helix_xml_assert_msgf("%s: wrong green channel", what));
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(b, c.blue, helix_xml_assert_msgf("%s: wrong blue channel", what));
}

/*===========================================================================
 * lv_xml_get_value_of
 *==========================================================================*/

static void test_get_value_of_finds_a_present_key(void)
{
    const char * attrs[] = {"width", "100", "height", "50", "name", "card", NULL};

    TEST_ASSERT_EQUAL_STRING("100", lv_xml_get_value_of(attrs, "width"));
    TEST_ASSERT_EQUAL_STRING("50", lv_xml_get_value_of(attrs, "height"));
    TEST_ASSERT_EQUAL_STRING("card", lv_xml_get_value_of(attrs, "name"));

    /* The returned pointer is borrowed from the array, not a copy. */
    TEST_ASSERT_EQUAL_PTR_MESSAGE(attrs[1], lv_xml_get_value_of(attrs, "width"),
                                  "lv_xml_get_value_of() must return the pointer stored in attrs, not a copy");
}

static void test_get_value_of_returns_null_for_absent_key(void)
{
    const char * attrs[] = {"width", "100", NULL};

    TEST_ASSERT_NULL(lv_xml_get_value_of(attrs, "height"));
    /* Exact match only: no prefix matching, no case folding, no trimming. */
    TEST_ASSERT_NULL(lv_xml_get_value_of(attrs, "widt"));
    TEST_ASSERT_NULL(lv_xml_get_value_of(attrs, "widths"));
    TEST_ASSERT_NULL(lv_xml_get_value_of(attrs, "Width"));
    TEST_ASSERT_NULL(lv_xml_get_value_of(attrs, " width"));
    TEST_ASSERT_NULL(lv_xml_get_value_of(attrs, "width "));
}

static void test_get_value_of_handles_empty_array(void)
{
    const char * attrs[] = {NULL};
    TEST_ASSERT_NULL(lv_xml_get_value_of(attrs, "anything"));
}

/** The only two guarded NULL arguments in the whole utils file. */
static void test_get_value_of_guards_null_arguments(void)
{
    const char * attrs[] = {"width", "100", NULL};

    TEST_ASSERT_NULL_MESSAGE(lv_xml_get_value_of(NULL, "width"),
                             "lv_xml_get_value_of() must guard a NULL attrs array");
    TEST_ASSERT_NULL_MESSAGE(lv_xml_get_value_of(attrs, NULL),
                             "lv_xml_get_value_of() must guard a NULL name");
    TEST_ASSERT_NULL(lv_xml_get_value_of(NULL, NULL));
}

static void test_get_value_of_returns_the_first_duplicate(void)
{
    const char * attrs[] = {"width", "first", "width", "second", NULL};
    TEST_ASSERT_EQUAL_STRING("first", lv_xml_get_value_of(attrs, "width"));
}

/**
 * A key in the final slot whose value is the array terminator matches, and
 * yields NULL - indistinguishable from "key absent". Callers cannot tell the
 * two apart.
 */
static void test_get_value_of_key_with_null_value_is_indistinguishable_from_absent(void)
{
    const char * attrs[] = {"width", NULL};

    TEST_ASSERT_NULL(lv_xml_get_value_of(attrs, "width"));
    TEST_ASSERT_NULL(lv_xml_get_value_of(attrs, "height"));
}

static void test_get_value_of_matches_empty_string_key_literally(void)
{
    const char * attrs[] = {"", "blank", "width", "100", NULL};

    TEST_ASSERT_EQUAL_STRING("blank", lv_xml_get_value_of(attrs, ""));
    TEST_ASSERT_EQUAL_STRING("100", lv_xml_get_value_of(attrs, "width"));
}

/*===========================================================================
 * lv_xml_atoi / lv_xml_atoi_split
 *==========================================================================*/

static void test_atoi_parses_signed_decimals(void)
{
    TEST_ASSERT_EQUAL_INT32(12, lv_xml_atoi("12"));
    TEST_ASSERT_EQUAL_INT32(0, lv_xml_atoi("0"));
    TEST_ASSERT_EQUAL_INT32(-5, lv_xml_atoi("-5"));
    TEST_ASSERT_EQUAL_INT32(5, lv_xml_atoi("+5"));
    TEST_ASSERT_EQUAL_INT32(2147483647, lv_xml_atoi("2147483647"));
}

static void test_atoi_skips_leading_whitespace(void)
{
    TEST_ASSERT_EQUAL_INT32(12, lv_xml_atoi("  12"));
    TEST_ASSERT_EQUAL_INT32(12, lv_xml_atoi("\t12"));
    TEST_ASSERT_EQUAL_INT32(-12, lv_xml_atoi(" \t -12"));
}

static void test_atoi_stops_at_the_first_non_digit(void)
{
    TEST_ASSERT_EQUAL_INT32(12, lv_xml_atoi("12px"));
    TEST_ASSERT_EQUAL_INT32(12, lv_xml_atoi("12%"));
    TEST_ASSERT_EQUAL_INT32(12, lv_xml_atoi("12 "));
    TEST_ASSERT_EQUAL_INT32(12, lv_xml_atoi("12.75"));
}

static void test_atoi_returns_zero_for_malformed_input(void)
{
    TEST_ASSERT_EQUAL_INT32(0, lv_xml_atoi("abc"));
    TEST_ASSERT_EQUAL_INT32(0, lv_xml_atoi("px12"));
    /* A second sign is a non-digit, so nothing is accumulated. */
    TEST_ASSERT_EQUAL_INT32(0, lv_xml_atoi("--5"));
    /* Whitespace is skipped BEFORE the sign, never after it. */
    TEST_ASSERT_EQUAL_INT32(0, lv_xml_atoi("- 5"));
    /* No hex support: 'x' terminates the digit run. */
    TEST_ASSERT_EQUAL_INT32(0, lv_xml_atoi("0x10"));
}

static void test_atoi_split_advances_past_the_delimiter(void)
{
    const char * s = "12,34,56";

    TEST_ASSERT_EQUAL_INT32(12, lv_xml_atoi_split(&s, ','));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("34,56", s, "cursor was not advanced past the first delimiter");

    TEST_ASSERT_EQUAL_INT32(34, lv_xml_atoi_split(&s, ','));
    TEST_ASSERT_EQUAL_STRING("56", s);

    TEST_ASSERT_EQUAL_INT32(56, lv_xml_atoi_split(&s, ','));
    /* Last field: the cursor lands ON the terminator, not past it. */
    TEST_ASSERT_EQUAL_STRING_MESSAGE("", s, "cursor must stop at the NUL, not step over it");
}

static void test_atoi_split_skips_repeated_delimiters_and_blanks(void)
{
    const char * s = ",,,12";
    TEST_ASSERT_EQUAL_INT32(12, lv_xml_atoi_split(&s, ','));

    const char * t = " \t12,9";
    TEST_ASSERT_EQUAL_INT32(12, lv_xml_atoi_split(&t, ','));
    TEST_ASSERT_EQUAL_STRING("9", t);
}

/**
 * Empty and malformed input, exercised through the split entry point where a
 * real delimiter makes the skip loop terminate. See the file header for why
 * the same cases cannot be run through lv_xml_atoi().
 */
static void test_atoi_split_handles_empty_and_malformed_input(void)
{
    const char * empty = "";
    TEST_ASSERT_EQUAL_INT32(0, lv_xml_atoi_split(&empty, ','));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("", empty, "cursor moved on an empty string");

    const char * blank = "   ";
    TEST_ASSERT_EQUAL_INT32(0, lv_xml_atoi_split(&blank, ','));

    const char * junk = "abc,7";
    TEST_ASSERT_EQUAL_INT32(0, lv_xml_atoi_split(&junk, ','));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("7", junk,
                                     "a malformed field must still consume up to its delimiter");

    const char * partial = "12abc,7";
    TEST_ASSERT_EQUAL_INT32(12, lv_xml_atoi_split(&partial, ','));
    TEST_ASSERT_EQUAL_STRING("7", partial);
}

/*===========================================================================
 * lv_xml_atof / lv_xml_atof_split   (compiled only when LV_USE_FLOAT)
 *==========================================================================*/

static void test_atof_parses_decimal_fractions(void)
{
    TEST_ASSERT_EQUAL_FLOAT(1.5f, lv_xml_atof("1.5"));
    TEST_ASSERT_EQUAL_FLOAT(-1.5f, lv_xml_atof("-1.5"));
    TEST_ASSERT_EQUAL_FLOAT(2.25f, lv_xml_atof("+2.25"));
    TEST_ASSERT_EQUAL_FLOAT(42.0f, lv_xml_atof("42"));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.125f, lv_xml_atof("0.125"));
}

static void test_atof_accepts_a_missing_integer_or_fractional_part(void)
{
    TEST_ASSERT_EQUAL_FLOAT(0.5f, lv_xml_atof(".5"));
    TEST_ASSERT_EQUAL_FLOAT(-0.5f, lv_xml_atof("-.5"));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, lv_xml_atof("1."));
}

static void test_atof_stops_at_the_first_non_digit(void)
{
    TEST_ASSERT_EQUAL_FLOAT(1.5f, lv_xml_atof("1.5abc"));
    /* A second '.' is just a non-digit, so it truncates rather than failing. */
    TEST_ASSERT_EQUAL_FLOAT(1.5f, lv_xml_atof("1.5.5"));
    TEST_ASSERT_EQUAL_FLOAT(3.0f, lv_xml_atof("3px"));
}

static void test_atof_skips_leading_whitespace(void)
{
    TEST_ASSERT_EQUAL_FLOAT(2.5f, lv_xml_atof("  2.5"));
    TEST_ASSERT_EQUAL_FLOAT(2.5f, lv_xml_atof("\t2.5"));
}

static void test_atof_returns_zero_for_malformed_input(void)
{
    TEST_ASSERT_EQUAL_FLOAT(0.0f, lv_xml_atof("abc"));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, lv_xml_atof("--1.5"));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, lv_xml_atof("- 1.5"));
}

static void test_atof_split_advances_past_the_delimiter(void)
{
    const char * s = "1.5,2.25,3";

    TEST_ASSERT_EQUAL_FLOAT(1.5f, lv_xml_atof_split(&s, ','));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("2.25,3", s, "cursor was not advanced past the first delimiter");

    TEST_ASSERT_EQUAL_FLOAT(2.25f, lv_xml_atof_split(&s, ','));
    TEST_ASSERT_EQUAL_STRING("3", s);

    TEST_ASSERT_EQUAL_FLOAT(3.0f, lv_xml_atof_split(&s, ','));
    TEST_ASSERT_EQUAL_STRING("", s);
}

static void test_atof_split_handles_empty_and_malformed_input(void)
{
    const char * empty = "";
    TEST_ASSERT_EQUAL_FLOAT(0.0f, lv_xml_atof_split(&empty, ','));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("", empty, "cursor moved on an empty string");

    const char * junk = "abc,1.5";
    TEST_ASSERT_EQUAL_FLOAT(0.0f, lv_xml_atof_split(&junk, ','));
    TEST_ASSERT_EQUAL_STRING("1.5", junk);
}

/*===========================================================================
 * lv_xml_to_color
 *==========================================================================*/

/**
 * The accepted grammar: an optional "#" or "0x"/"0X", then EXACTLY 3, 6 or 8
 * hex digits, then end of string. Digit count - not string length - is what
 * selects the expander.
 */
static void test_to_color_accepts_every_documented_format(void)
{
    log_capture_start();

    /* 3 digits -> lv_color_hex3(), each nibble doubled. */
    assert_color_rgb(lv_xml_to_color("fff"), 0xFF, 0xFF, 0xFF, "\"fff\"");
    assert_color_rgb(lv_xml_to_color("#fff"), 0xFF, 0xFF, 0xFF, "\"#fff\"");
    assert_color_rgb(lv_xml_to_color("0xfff"), 0xFF, 0xFF, 0xFF, "\"0xfff\"");
    assert_color_rgb(lv_xml_to_color("abc"), 0xAA, 0xBB, 0xCC, "\"abc\"");
    assert_color_rgb(lv_xml_to_color("#f00"), 0xFF, 0x00, 0x00, "\"#f00\"");

    /* 6 digits -> RRGGBB. */
    assert_color_rgb(lv_xml_to_color("ffffff"), 0xFF, 0xFF, 0xFF, "\"ffffff\"");
    assert_color_rgb(lv_xml_to_color("#ffffff"), 0xFF, 0xFF, 0xFF, "\"#ffffff\"");
    assert_color_rgb(lv_xml_to_color("0xffffff"), 0xFF, 0xFF, 0xFF, "\"0xffffff\"");
    assert_color_rgb(lv_xml_to_color("0Xffffff"), 0xFF, 0xFF, 0xFF, "\"0Xffffff\"");
    assert_color_rgb(lv_xml_to_color("#ff0000"), 0xFF, 0x00, 0x00, "\"#ff0000\"");
    assert_color_rgb(lv_xml_to_color("#0080FF"), 0x00, 0x80, 0xFF, "\"#0080FF\"");

    /* 8 digits -> AARRGGBB. lv_color_t has no alpha channel, so the leading
     * byte is dropped by design; opacity is a separate `*_opa` attribute. */
    assert_color_rgb(lv_xml_to_color("#12345678"), 0x34, 0x56, 0x78, "\"#12345678\"");
    /* The high bit set is the case a strtol-based parse would saturate away. */
    assert_color_rgb(lv_xml_to_color("#FF112233"), 0x11, 0x22, 0x33, "\"#FF112233\"");

    log_capture_stop();
    TEST_ASSERT_FALSE_MESSAGE(log_contains("is not a valid color"),
                              "a well-formed literal must not warn");
}

static void test_to_color_treats_unparseable_input_as_black(void)
{
    log_capture_start();
    assert_color_rgb(lv_xml_to_color(""), 0, 0, 0, "empty string");
    assert_color_rgb(lv_xml_to_color("#"), 0, 0, 0, "\"#\"");
    assert_color_rgb(lv_xml_to_color("#zzz"), 0, 0, 0, "\"#zzz\"");
    assert_color_rgb(lv_xml_to_color("zzzzzzz"), 0, 0, 0, "\"zzzzzzz\"");
    log_capture_stop();

    TEST_ASSERT_TRUE_MESSAGE(log_contains("#zzz is not a valid color"),
                             "an unparseable value must name itself in the warning");
}

/**
 * The digit count is validated, so anything that is not 3, 6 or 8 hex digits is
 * REJECTED rather than reinterpreted. Black is the documented fallback: it is
 * what the old length-only implementation already produced for every genuinely
 * unparseable value, so nothing that used to render changes colour.
 *
 * lv_xml_strtol() still skips non-hex characters (that behaviour is depended on
 * elsewhere and is deliberately unchanged); to_color no longer relies on it.
 */
static void test_to_color_rejects_malformed_literals_and_warns(void)
{
    /* A 5-digit literal used to be mangled through the 3-digit expander purely
     * because the string was <= 5 characters long. */
    log_capture_start();
    assert_color_rgb(lv_xml_to_color("12345"), 0, 0, 0, "\"12345\"");
    log_capture_stop();
    TEST_ASSERT_TRUE_MESSAGE(log_contains("12345 is not a valid color"),
                             "a 5-digit literal must be rejected by name, not expanded as 3 digits");

    /* Neighbouring digit counts, to prove 3/6/8 are the only accepted widths. */
    log_capture_start();
    assert_color_rgb(lv_xml_to_color("ff"), 0, 0, 0, "\"ff\"");
    assert_color_rgb(lv_xml_to_color("ffff"), 0, 0, 0, "\"ffff\"");
    assert_color_rgb(lv_xml_to_color("fffffff"), 0, 0, 0, "7 digits");
    assert_color_rgb(lv_xml_to_color("fffffffff"), 0, 0, 0, "9 digits");
    log_capture_stop();
    TEST_ASSERT_TRUE(log_contains("ffff is not a valid color"));

    /* CSS colour names are not supported and must not parse as whatever hex
     * digits happen to appear in them ("red" used to be 0x00EEDD). */
    log_capture_start();
    assert_color_rgb(lv_xml_to_color("red"), 0, 0, 0, "\"red\"");
    assert_color_rgb(lv_xml_to_color("purple"), 0, 0, 0, "\"purple\"");
    assert_color_rgb(lv_xml_to_color("white"), 0, 0, 0, "\"white\"");
    log_capture_stop();
    TEST_ASSERT_TRUE_MESSAGE(log_contains("red is not a valid color"),
                             "a CSS colour name must be reported, not silently reinterpreted");

    /* Trailing junk after a valid run of digits is rejected too. */
    log_capture_start();
    assert_color_rgb(lv_xml_to_color("#ff0000 "), 0, 0, 0, "trailing space");
    assert_color_rgb(lv_xml_to_color("#ff0000;"), 0, 0, 0, "trailing semicolon");
    assert_color_rgb(lv_xml_to_color("rgb(255,0,0)"), 0, 0, 0, "rgb() syntax");
    log_capture_stop();
    TEST_ASSERT_TRUE(log_contains("rgb(255,0,0) is not a valid color"));

    /* NULL is guarded rather than dereferenced. */
    log_capture_start();
    assert_color_rgb(lv_xml_to_color(NULL), 0, 0, 0, "NULL");
    log_capture_stop();
    TEST_ASSERT_TRUE(log_contains("NULL is not a valid color"));
}

/*===========================================================================
 * lv_xml_to_opa
 *==========================================================================*/

static void test_to_opa_accepts_plain_numbers(void)
{
    TEST_ASSERT_EQUAL_UINT8(0, lv_xml_to_opa("0"));
    TEST_ASSERT_EQUAL_UINT8(180, lv_xml_to_opa("180"));
    TEST_ASSERT_EQUAL_UINT8(255, lv_xml_to_opa("255"));
}

static void test_to_opa_accepts_percentages(void)
{
    TEST_ASSERT_EQUAL_UINT8(0, lv_xml_to_opa("0%"));
    /* 50 * 255 / 100 truncates to 127, not 128. */
    TEST_ASSERT_EQUAL_UINT8(127, lv_xml_to_opa("50%"));
    TEST_ASSERT_EQUAL_UINT8(178, lv_xml_to_opa("70%"));
    TEST_ASSERT_EQUAL_UINT8(255, lv_xml_to_opa("100%"));
}

static void test_to_opa_clamps_out_of_range_values(void)
{
    TEST_ASSERT_EQUAL_UINT8(255, lv_xml_to_opa("300"));
    TEST_ASSERT_EQUAL_UINT8(255, lv_xml_to_opa("150%"));
    TEST_ASSERT_EQUAL_UINT8(0, lv_xml_to_opa("-5"));
    TEST_ASSERT_EQUAL_UINT8(0, lv_xml_to_opa("-10%"));
}

static void test_to_opa_returns_zero_for_malformed_input(void)
{
    TEST_ASSERT_EQUAL_UINT8(0, lv_xml_to_opa("abc"));
    TEST_ASSERT_EQUAL_UINT8(0, lv_xml_to_opa("%"));
    /* Symbolic opacity names are NOT supported - they parse as 0. */
    TEST_ASSERT_EQUAL_UINT8(0, lv_xml_to_opa("transp"));
    TEST_ASSERT_EQUAL_UINT8(0, lv_xml_to_opa("cover"));
}

/*===========================================================================
 * lv_xml_to_bool
 *==========================================================================*/

/**
 * The accepted-string set is a single entry: "false". Everything else, valid
 * or not, is true. There is no "true" branch at all.
 */
static void test_to_bool_accepts_only_the_exact_string_false(void)
{
    TEST_ASSERT_FALSE_MESSAGE(lv_xml_to_bool("false"),
                              "\"false\" is the one and only false value");
    TEST_ASSERT_TRUE(lv_xml_to_bool("true"));
}

static void test_to_bool_is_case_sensitive_and_untrimmed(void)
{
    static const char * truthy[] = {
        "False", "FALSE", "FaLsE", " false", "false ", "\tfalse",
        "0", "no", "off", "none", "", "1", "yes", "on", "garbage",
    };

    for(size_t i = 0; i < sizeof(truthy) / sizeof(truthy[0]); i++) {
        TEST_ASSERT_TRUE_MESSAGE(lv_xml_to_bool(truthy[i]),
                                 helix_xml_assert_msgf("lv_xml_to_bool(\"%s\") must be true - only the exact "
                                                       "string \"false\" is false", truthy[i]));
    }
}

/*===========================================================================
 * lv_xml_strtol
 *==========================================================================*/

static void test_strtol_parses_signed_values_in_an_explicit_base(void)
{
    TEST_ASSERT_EQUAL_INT32(123, lv_xml_strtol("123", NULL, 10));
    TEST_ASSERT_EQUAL_INT32(-123, lv_xml_strtol("-123", NULL, 10));
    TEST_ASSERT_EQUAL_INT32(123, lv_xml_strtol("+123", NULL, 10));
    TEST_ASSERT_EQUAL_INT32(255, lv_xml_strtol("ff", NULL, 16));
    TEST_ASSERT_EQUAL_INT32(255, lv_xml_strtol("FF", NULL, 16));
    TEST_ASSERT_EQUAL_INT32(15, lv_xml_strtol("17", NULL, 8));
    TEST_ASSERT_EQUAL_INT32(5, lv_xml_strtol("101", NULL, 2));
}

static void test_strtol_skips_leading_space_and_tab(void)
{
    TEST_ASSERT_EQUAL_INT32(42, lv_xml_strtol("  42", NULL, 10));
    TEST_ASSERT_EQUAL_INT32(42, lv_xml_strtol("\t42", NULL, 10));
    TEST_ASSERT_EQUAL_INT32(-42, lv_xml_strtol(" \t-42", NULL, 10));
}

static void test_strtol_auto_detects_base_when_base_is_zero(void)
{
    TEST_ASSERT_EQUAL_INT32(31, lv_xml_strtol("0x1F", NULL, 0));
    TEST_ASSERT_EQUAL_INT32(31, lv_xml_strtol("0X1f", NULL, 0));
    /* A bare leading zero means octal, exactly like the C library. */
    TEST_ASSERT_EQUAL_INT32(15, lv_xml_strtol("017", NULL, 0));
    TEST_ASSERT_EQUAL_INT32(42, lv_xml_strtol("42", NULL, 0));
    TEST_ASSERT_EQUAL_INT32(0, lv_xml_strtol("0", NULL, 0));
    TEST_ASSERT_EQUAL_INT32(-31, lv_xml_strtol("-0x1F", NULL, 0));
}

static void test_strtol_ignores_digits_outside_the_base(void)
{
    /* '9' is not an octal digit, so it contributes nothing. */
    TEST_ASSERT_EQUAL_INT32(0, lv_xml_strtol("09", NULL, 8));
    /* 'f' is not a decimal digit. */
    TEST_ASSERT_EQUAL_INT32(12, lv_xml_strtol("1f2", NULL, 10));
}

/**
 * Only 2..16 can be decoded - the digit table stops at 'f'. A base outside that
 * range (and outside the base == 0 auto-detect case) is REJECTED: warn, consume
 * nothing, return 0. It used to be admitted by the is_digit() range check with
 * no matching decode branch, so the first undecodable letter hit a defensive
 * `break` and silently truncated the number mid-way.
 */
static void test_strtol_rejects_bases_outside_two_to_sixteen(void)
{
    char * end = NULL;
    const char * src = "1g2";

    log_capture_start();
    TEST_ASSERT_EQUAL_INT32_MESSAGE(0, lv_xml_strtol(src, &end, 17),
                                    "base 17 must be rejected outright, not truncate after the '1'");
    log_capture_stop();
    TEST_ASSERT_EQUAL_PTR_MESSAGE(src, end, "a rejected base must consume nothing");
    TEST_ASSERT_TRUE_MESSAGE(log_contains("17 is an unsupported base"),
                             "the rejected base must be named in the warning");

    log_capture_start();
    TEST_ASSERT_EQUAL_INT32(0, lv_xml_strtol("zzz", NULL, 36));
    /* Base 1 and base <= 0 were already 0-valued, but silently so. */
    TEST_ASSERT_EQUAL_INT32(0, lv_xml_strtol("111", NULL, 1));
    TEST_ASSERT_EQUAL_INT32(0, lv_xml_strtol("000", NULL, 1));
    TEST_ASSERT_EQUAL_INT32(0, lv_xml_strtol("123", NULL, -4));
    log_capture_stop();
    TEST_ASSERT_TRUE(log_contains("1 is an unsupported base"));
    TEST_ASSERT_TRUE(log_contains("-4 is an unsupported base"));

    /* The two ends of the supported range, and 0, still work. */
    log_capture_start();
    TEST_ASSERT_EQUAL_INT32(5, lv_xml_strtol("101", NULL, 2));
    TEST_ASSERT_EQUAL_INT32(255, lv_xml_strtol("ff", NULL, 16));
    TEST_ASSERT_EQUAL_INT32(31, lv_xml_strtol("0x1F", NULL, 0));
    log_capture_stop();
    TEST_ASSERT_FALSE_MESSAGE(log_contains("unsupported base"),
                              "bases 2, 16 and the 0 auto-detect must not warn");
}

/* PINS INTENTIONAL BEHAVIOUR: this is deliberately NOT a strtol drop-in. The
 * conversion loop has no `else break`, so invalid characters are skipped rather
 * than terminating the number, and *endptr is therefore always the end of the
 * string on the success path instead of the first invalid character. Callers in
 * the engine rely on the skipping, and tightening it would change the meaning of
 * existing attribute values rather than fix a defect. (lv_xml_to_color used to
 * be the headline dependant - "#ff0000" only worked because the '#' was dropped
 * - but it now validates and decodes its own digits, so it no longer is.) */
static void test_strtol_skips_invalid_characters_by_design(void)
{
    TEST_ASSERT_EQUAL_INT32_MESSAGE(1234, lv_xml_strtol("12x34", NULL, 10),
                                    "invalid characters must be SKIPPED, not treated as terminators");
    TEST_ASSERT_EQUAL_INT32(1234, lv_xml_strtol("12 34", NULL, 10));
    TEST_ASSERT_EQUAL_INT32(12, lv_xml_strtol("1!@#2", NULL, 10));
    TEST_ASSERT_EQUAL_INT32(0xff0000, lv_xml_strtol("#ff0000", NULL, 16));
    TEST_ASSERT_EQUAL_INT32(0, lv_xml_strtol("abc", NULL, 10));
    /* '\n' is not in the leading-whitespace set, but the main loop drops it. */
    TEST_ASSERT_EQUAL_INT32(42, lv_xml_strtol("\n42", NULL, 10));
}

static void test_strtol_endptr_lands_on_the_terminator_on_success(void)
{
    const char * src = "12x34";
    char * end = NULL;

    TEST_ASSERT_EQUAL_INT32(1234, lv_xml_strtol(src, &end, 10));
    TEST_ASSERT_EQUAL_PTR_MESSAGE(src + 5, end,
                                  "endptr must be the terminating NUL, never the first invalid character");
    TEST_ASSERT_EQUAL_CHAR('\0', *end);
}

static void test_strtol_handles_empty_string(void)
{
    const char * src = "";
    char * end = NULL;

    TEST_ASSERT_EQUAL_INT32(0, lv_xml_strtol(src, &end, 10));
    TEST_ASSERT_EQUAL_PTR_MESSAGE(src, end, "endptr must stay at the start for an empty string");
}

static void test_strtol_saturates_on_overflow(void)
{
    const char * pos = "99999999999";
    char * end = NULL;

    TEST_ASSERT_EQUAL_INT32_MESSAGE(INT32_MAX, lv_xml_strtol(pos, &end, 10),
                                    "positive overflow must saturate at INT32_MAX");
    /* Overflow is the one path where endptr is meaningful: it points at the
     * digit that would not fit, i.e. the 10th nine. */
    TEST_ASSERT_EQUAL_PTR_MESSAGE(pos + 9, end, "endptr must point at the offending digit on overflow");

    TEST_ASSERT_EQUAL_INT32_MESSAGE(INT32_MIN, lv_xml_strtol("-99999999999", NULL, 10),
                                    "negative overflow must saturate at INT32_MIN");
    /* One past the largest representable value is enough to trip it. */
    TEST_ASSERT_EQUAL_INT32(INT32_MAX, lv_xml_strtol("2147483648", NULL, 10));
    TEST_ASSERT_EQUAL_INT32(INT32_MAX, lv_xml_strtol("ffffffff", NULL, 16));
}

/*===========================================================================
 * lv_xml_split_str
 *==========================================================================*/

/**
 * Ownership: lv_xml_split_str allocates NOTHING. Each token is a borrowed
 * pointer INTO the caller's buffer, so the caller frees the buffer exactly
 * once and must never free a token. This test proves that by allocating the
 * buffer itself, checking every returned token lies inside it, and freeing
 * only the buffer.
 */
static void test_split_str_returns_borrowed_pointers_into_the_callers_buffer(void)
{
    const char * literal = "alpha|beta|gamma";
    size_t len = strlen(literal);
    char * buf = malloc(len + 1);
    TEST_ASSERT_NOT_NULL(buf);
    memcpy(buf, literal, len + 1);

    char * cursor = buf;

    char * tok = lv_xml_split_str(&cursor, '|');
    TEST_ASSERT_EQUAL_STRING("alpha", tok);
    TEST_ASSERT_EQUAL_PTR_MESSAGE(buf, tok, "the first token must be the buffer itself, not a copy");

    tok = lv_xml_split_str(&cursor, '|');
    TEST_ASSERT_EQUAL_STRING("beta", tok);
    TEST_ASSERT_TRUE_MESSAGE(tok > buf && tok < buf + len,
                             "tokens must point inside the caller's buffer - nothing is allocated");

    tok = lv_xml_split_str(&cursor, '|');
    TEST_ASSERT_EQUAL_STRING("gamma", tok);
    TEST_ASSERT_TRUE(tok > buf && tok < buf + len);

    TEST_ASSERT_NULL_MESSAGE(lv_xml_split_str(&cursor, '|'), "exhausted input must yield NULL");

    /* Freeing the buffer once is the whole of the caller's obligation. */
    free(buf);
}

/** The delimiter is overwritten with a NUL in the caller's buffer. */
static void test_split_str_terminates_tokens_in_place(void)
{
    char buf[] = "a|b";
    char * cursor = buf;

    TEST_ASSERT_EQUAL_STRING("a", lv_xml_split_str(&cursor, '|'));
    TEST_ASSERT_EQUAL_CHAR_MESSAGE('\0', buf[1],
                                   "the delimiter must be overwritten in the caller's buffer");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(buf + 2, cursor, "cursor must resume one past the delimiter");
}

static void test_split_str_skips_leading_and_repeated_delimiters(void)
{
    char buf[] = "||a||b|";
    char * cursor = buf;

    TEST_ASSERT_EQUAL_STRING("a", lv_xml_split_str(&cursor, '|'));
    /* An empty middle field is skipped, never returned as "". */
    TEST_ASSERT_EQUAL_STRING("b", lv_xml_split_str(&cursor, '|'));
    TEST_ASSERT_NULL(lv_xml_split_str(&cursor, '|'));
}

static void test_split_str_returns_null_when_nothing_is_left(void)
{
    char empty[] = "";
    char * cursor = empty;
    TEST_ASSERT_NULL(lv_xml_split_str(&cursor, '|'));
    TEST_ASSERT_EQUAL_PTR(empty, cursor);

    char only_delims[] = "|||";
    cursor = only_delims;
    TEST_ASSERT_NULL(lv_xml_split_str(&cursor, '|'));
    TEST_ASSERT_EQUAL_CHAR_MESSAGE('\0', *cursor, "cursor must end on the terminator");
}

static void test_split_str_returns_the_whole_string_when_no_delimiter_is_present(void)
{
    char buf[] = "solo";
    char * cursor = buf;

    TEST_ASSERT_EQUAL_STRING("solo", lv_xml_split_str(&cursor, '|'));
    TEST_ASSERT_EQUAL_PTR_MESSAGE(buf + 4, cursor, "cursor must be parked on the terminator");
    TEST_ASSERT_NULL(lv_xml_split_str(&cursor, '|'));
}

/*---------------------------------------------------------------------------
 * main
 *--------------------------------------------------------------------------*/

int main(void)
{
    UNITY_BEGIN();

    /* lv_xml_get_value_of */
    RUN_TEST(test_get_value_of_finds_a_present_key);
    RUN_TEST(test_get_value_of_returns_null_for_absent_key);
    RUN_TEST(test_get_value_of_handles_empty_array);
    RUN_TEST(test_get_value_of_guards_null_arguments);
    RUN_TEST(test_get_value_of_returns_the_first_duplicate);
    RUN_TEST(test_get_value_of_key_with_null_value_is_indistinguishable_from_absent);
    RUN_TEST(test_get_value_of_matches_empty_string_key_literally);

    /* lv_xml_atoi / lv_xml_atoi_split */
    RUN_TEST(test_atoi_parses_signed_decimals);
    RUN_TEST(test_atoi_skips_leading_whitespace);
    RUN_TEST(test_atoi_stops_at_the_first_non_digit);
    RUN_TEST(test_atoi_returns_zero_for_malformed_input);
    RUN_TEST(test_atoi_split_advances_past_the_delimiter);
    RUN_TEST(test_atoi_split_skips_repeated_delimiters_and_blanks);
    RUN_TEST(test_atoi_split_handles_empty_and_malformed_input);

    /* lv_xml_atof / lv_xml_atof_split */
    RUN_TEST(test_atof_parses_decimal_fractions);
    RUN_TEST(test_atof_accepts_a_missing_integer_or_fractional_part);
    RUN_TEST(test_atof_stops_at_the_first_non_digit);
    RUN_TEST(test_atof_skips_leading_whitespace);
    RUN_TEST(test_atof_returns_zero_for_malformed_input);
    RUN_TEST(test_atof_split_advances_past_the_delimiter);
    RUN_TEST(test_atof_split_handles_empty_and_malformed_input);

    /* lv_xml_to_color */
    RUN_TEST(test_to_color_accepts_every_documented_format);
    RUN_TEST(test_to_color_treats_unparseable_input_as_black);
    RUN_TEST(test_to_color_rejects_malformed_literals_and_warns);

    /* lv_xml_to_opa */
    RUN_TEST(test_to_opa_accepts_plain_numbers);
    RUN_TEST(test_to_opa_accepts_percentages);
    RUN_TEST(test_to_opa_clamps_out_of_range_values);
    RUN_TEST(test_to_opa_returns_zero_for_malformed_input);

    /* lv_xml_to_bool */
    RUN_TEST(test_to_bool_accepts_only_the_exact_string_false);
    RUN_TEST(test_to_bool_is_case_sensitive_and_untrimmed);

    /* lv_xml_strtol */
    RUN_TEST(test_strtol_parses_signed_values_in_an_explicit_base);
    RUN_TEST(test_strtol_skips_leading_space_and_tab);
    RUN_TEST(test_strtol_auto_detects_base_when_base_is_zero);
    RUN_TEST(test_strtol_ignores_digits_outside_the_base);
    RUN_TEST(test_strtol_rejects_bases_outside_two_to_sixteen);
    RUN_TEST(test_strtol_skips_invalid_characters_by_design);
    RUN_TEST(test_strtol_endptr_lands_on_the_terminator_on_success);
    RUN_TEST(test_strtol_handles_empty_string);
    RUN_TEST(test_strtol_saturates_on_overflow);

    /* lv_xml_split_str */
    RUN_TEST(test_split_str_returns_borrowed_pointers_into_the_callers_buffer);
    RUN_TEST(test_split_str_terminates_tokens_in_place);
    RUN_TEST(test_split_str_skips_leading_and_repeated_delimiters);
    RUN_TEST(test_split_str_returns_null_when_nothing_is_left);
    RUN_TEST(test_split_str_returns_the_whole_string_when_no_delimiter_is_present);

    return UNITY_END();
}
