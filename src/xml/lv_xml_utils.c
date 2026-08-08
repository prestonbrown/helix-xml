/**
 * @file lv_xml_utils.c
 *
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2025 LVGL Kft
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_xml_utils.h"
#include <stdlib/lv_string.h>
#include <misc/lv_log.h>
#if LV_USE_XML

#if LV_USE_STDLIB_STRING == LV_STDLIB_CLIB
    #include <stdlib.h>
#endif

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static bool is_digit(char c, int base);
static bool hex_digit_value(char c, uint32_t * out);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/


const char * lv_xml_get_value_of(const char ** attrs, const char * name)
{
    if(attrs == NULL) return NULL;
    if(name == NULL) return NULL;

    for(int i = 0; attrs[i]; i += 2) {
        if(lv_streq(attrs[i], name)) return attrs[i + 1];
    }

    return NULL;
}

/**
 * Parse a colour literal.
 *
 * Accepted: an optional `#` or `0x`/`0X` prefix followed by EXACTLY 3, 6 or 8
 * hexadecimal digits and nothing else.
 *   - 3 digits  -> lv_color_hex3(), each nibble doubled ("f00" == "ff0000")
 *   - 6 digits  -> RRGGBB
 *   - 8 digits  -> AARRGGBB; the alpha byte is DISCARDED (lv_color_t has no
 *                  alpha channel). Opacity is a separate `*_opa` attribute.
 *
 * Anything else - a 4- or 5-digit literal, a CSS colour name, trailing junk,
 * an empty string - warns and returns the documented fallback, BLACK
 * (lv_color_hex(0x000000)). Black is the fallback rather than magenta because
 * it is what the previous length-only implementation already produced for
 * every unparseable value, so no existing markup changes appearance.
 *
 * The digits are accumulated here rather than via lv_xml_strtol() for two
 * reasons: strtol silently SKIPS non-hex characters (which is exactly why this
 * function never validated anything), and it saturates at INT32_MAX, which
 * would mangle any 8-digit value with the high bit set.
 */
lv_color_t lv_xml_to_color(const char * str)
{
    if(str == NULL) {
        LV_LOG_WARN("NULL is not a valid color");
        return lv_color_hex(0x000000);
    }

    const char * p = str;
    if(p[0] == '#') p++;
    else if(p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;

    uint32_t value = 0;
    size_t digits = 0;
    uint32_t nibble;
    while(hex_digit_value(p[digits], &nibble)) {
        value = (value << 4) | nibble;
        digits++;
        if(digits > 8) break; /*Already too long; stop before value wraps.*/
    }

    if(p[digits] != '\0' || (digits != 3 && digits != 6 && digits != 8)) {
        LV_LOG_WARN("%s is not a valid color - expected 3, 6 or 8 hex digits, "
                    "optionally prefixed with '#' or '0x'", str);
        return lv_color_hex(0x000000);
    }

    if(digits == 3) return lv_color_hex3(value);
    return lv_color_hex(value); /*6 digits as-is; 8 drops the alpha byte.*/
}

lv_opa_t lv_xml_to_opa(const char * str)
{
    /*An empty value is what a failed ${expr} splices in. Without this guard the
     *percent check below evaluates str[lv_strlen(str) - 1], i.e. str[-1] -- one
     *byte BEFORE the buffer -- so a stray '%' sitting in front of the string
     *scaled the result by 255/100. Same hardening lv_xml_to_size() already has
     *(#1121).*/
    if(str == NULL || str[0] == '\0') return 0;

    int32_t v = lv_xml_atoi(str);
    size_t len = lv_strlen(str);
    if(str[len - 1] == '%') {
        v = v * 255 / 100;
    }

    v = LV_CLAMP(0, v, 255);
    return (lv_opa_t)v;
}

bool lv_xml_to_bool(const char * str)
{
    return lv_streq(str, "false") ? false : true;
}

int32_t lv_xml_atoi_split(const char ** str, char delimiter)
{
    const char * s = *str;
    int32_t result = 0;
    int sign = 1;

    /* Skip leading whitespace and repeated delimiters.
     *
     * The `*s != '\0'` term is load-bearing: lv_xml_atoi() calls in with
     * delimiter == '\0', so without it the NUL terminator itself matched
     * `*s == delimiter` and the loop walked PAST the end of the buffer, reading
     * whatever followed until it hit a byte that was none of NUL/space/tab.
     * lv_xml_atoi("") and lv_xml_atoi("   ") both did this. For a real
     * delimiter the term changes nothing - '\0' never equalled it anyway. */
    while(*s != '\0' && (*s == delimiter || *s == ' ' || *s == '\t')) s++;

    /* Handle optional sign */
    if(*s == '-') {
        sign = -1;
        s++;
    }
    else if(*s == '+') {
        s++;
    }

    /* Convert the string*/
    while(*s != delimiter) {
        if(*s >= '0' && *s <= '9') {
            int32_t digit = *s - '0';

            result = result * 10 + digit;
            s++;
        }
        else {
            break; /* Non-digit character */
        }
    }

    result = result * sign;
    while(*s != delimiter && *s != '\0') s++; /*Make sure to find the delimiter*/

    if(*s != '\0') s++; /*Skip the delimiter*/
    *str = s;
    return result;
}

int32_t lv_xml_atoi(const char * str)
{
    return lv_xml_atoi_split(&str, '\0');
}

#if LV_USE_FLOAT
float lv_xml_atof_split(const char ** str, char delimiter)
{
    const char * s = *str;
    float result = 0.0f;
    int sign = 1;

    /* Skip leading whitespace and repeated delimiters. The `*s != '\0'` term
     * guards the delimiter == '\0' entry point (lv_xml_atof) against walking
     * past the terminator - see the note in lv_xml_atoi_split(). */
    while(*s != '\0' && (*s == delimiter || *s == ' ' || *s == '\t')) s++;

    /* Handle optional sign */
    if(*s == '-') {
        sign = -1;
        s++;
    }
    else if(*s == '+') {
        s++;
    }

    /* Convert the integer part */
    while(*s != delimiter && *s != '.' && *s != '\0') {
        if(*s >= '0' && *s <= '9') {
            float digit = *s - '0';
            result = result * 10.0f + digit;
            s++;
        }
        else {
            break; /* Non-digit character */
        }
    }

    /* Convert the fractional part */
    if(*s == '.') {
        s++; /* Skip the decimal point */
        float fraction = 0.0f;
        float divisor = 10.0f;

        while(*s != delimiter && *s != '\0') {
            if(*s >= '0' && *s <= '9') {
                float digit = *s - '0';
                fraction += digit / divisor;
                divisor *= 10.0f;
                s++;
            }
            else {
                break; /* Non-digit character */
            }
        }
        result += fraction;
    }

    result = result * sign;
    while(*s != delimiter && *s != '\0') s++; /*Make sure to find the delimiter*/

    if(*s != '\0') s++; /*Skip the delimiter*/
    *str = s;
    return result;
}


float lv_xml_atof(const char * str)
{
    return lv_xml_atof_split(&str, '\0');
}
#endif

int32_t lv_xml_strtol(const char * str, char ** endptr, int32_t base)
{
    const char * s = str;
    int32_t result = 0;
    int32_t sign = 1;

    /* Only 2..16 (and 0 = auto-detect) can be decoded: the digit table below
     * stops at 'f'. is_digit() used to ADMIT 17+, so the first 'g' fell through
     * to the defensive `break` and silently truncated the number instead of
     * reporting anything. Reject the base up front, consume nothing, return 0. */
    if(base != 0 && (base < 2 || base > 16)) {
        LV_LOG_WARN("%d is an unsupported base for lv_xml_strtol (expected 2..16, or 0 to auto-detect)",
                    (int)base);
        if(endptr) *endptr = (char *)str;
        return 0;
    }

    /* Skip leading whitespace */
    while(*s == ' ' || *s == '\t') s++;

    /* Handle optional sign*/
    if(*s == '-') {
        sign = -1;
        s++;
    }
    else if(*s == '+') {
        s++;
    }

    /* Determine base if 0 is passed as base*/
    if(base == 0) {
        if(*s == '0') {
            if(*(s + 1) == 'x' || *(s + 1) == 'X') {
                base = 16;
                s += 2;
            }
            else {
                base = 8;
                s++;
            }
        }
        else {
            base = 10;
        }
    }

    /* Convert the string*/
    while(*s) {
        int32_t digit;

        if(is_digit(*s, base)) {
            if(*s >= '0' && *s <= '9') {
                digit = *s - '0';
            }
            else if(*s >= 'a' && *s <= 'f') {
                digit = *s - 'a' + 10;
            }
            else if(*s >= 'A' && *s <= 'F') {
                digit = *s - 'A' + 10;
            }
            else {
                /* This should not happen due to is_digit check*/
                break;
            }

            /* Check for overflow */
            if(result > (INT32_MAX - digit) / base) {
                result = (sign == 1) ? INT32_MAX : INT32_MIN;
                if(endptr) *endptr = (char *)s;
                return result;
            }

            result = result * base + digit;
        }
        s++;
    }

    /* Set end pointer to the last character processed*/
    if(endptr) {
        *endptr = (char *)s;
    }

    return result * sign;
}

char * lv_xml_split_str(char ** src, char delimiter)
{
    /*Skip multiple delimiters*/
    while(*src[0] == delimiter) {
        (*src)++;
    }

    if(*src[0] == '\0') return NULL;

    char * src_first = *src;
    char * src_next = *src;

    /*Find the delimiter*/
    while(*src_next != '\0') {
        if(*src_next == delimiter) {
            *src_next = '\0';       /*Close the string on the delimiter*/
            *src = src_next + 1;    /*Change the source continue after the found delimiter*/
            return src_first;
        }
        src_next++;
    }

    /*No delimiter found, return the string as it is*/
    *src = src_next;    /*Move the source point to the end*/

    return src_first;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/** Decode one hexadecimal digit. @return false if @p c is not one. */
static bool hex_digit_value(char c, uint32_t * out)
{
    if(c >= '0' && c <= '9') *out = (uint32_t)(c - '0');
    else if(c >= 'a' && c <= 'f') *out = (uint32_t)(c - 'a' + 10);
    else if(c >= 'A' && c <= 'F') *out = (uint32_t)(c - 'A' + 10);
    else return false;

    return true;
}

static bool is_digit(char c, int base)
{
    if(base <= 10) {
        return (c >= '0' && c < '0' + base);
    }
    else {
        return (c >= '0' && c <= '9') || (c >= 'a' && c < 'a' + (base - 10)) || (c >= 'A' && c < 'A' + (base - 10));
    }
}


#endif /* LV_USE_XML */
