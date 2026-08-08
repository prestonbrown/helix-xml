/**
 * @file lv_xml_attr_check.h
 *
 * Detect XML attributes that no processor recognised, so a typo is reported
 * instead of silently dropped.
 *
 * ---------------------------------------------------------------------------
 * WHY THIS IS NOT A TABLE OF KNOWN ATTRIBUTE NAMES
 *
 * Attribute dispatch is COMPOSED, not flat. `<lv_label text="x" width="100">`
 * is handled by two different if/else-if chains: lv_xml_label_apply() knows
 * `text` and nothing about `width`, and lv_xml_obj_apply() - which every widget
 * parser calls first - knows `width` and nothing about `text`. Neither chain
 * alone can tell a valid attribute from a typo, so a per-parser `else { warn }`
 * would fire on every correctly spelled attribute belonging to the other chain.
 *
 * A central table of legal names per widget would work but duplicates every
 * chain, and the failure mode of drift is a warning on CORRECT input - the one
 * outcome that must never happen, because it trains people to ignore the log.
 *
 * So the chains stay the single source of truth and simply report their misses:
 *
 *   - each participating apply chain calls lv_xml_attr_check_enter() once and
 *     ends in `else lv_xml_attr_check_miss(state, name);`
 *   - an attribute is unknown only when EVERY participant missed it
 *     (miss count == participant count)
 *
 * Adding an attribute to a chain therefore updates the checker automatically:
 * the new branch fires, the else does not, the miss count drops below the
 * participant count, no warning. There is nothing to keep in sync.
 * ---------------------------------------------------------------------------
 *
 * OPT-IN, SO DOWNSTREAM WIDGETS ARE NEVER PUNISHED
 *
 * The check is armed by the CALLER (lv_xml.c), and only for widgets the engine
 * itself registered (lv_widget_processor_t::builtin). An application widget
 * registered through lv_xml_register_widget() has an apply_cb this library
 * cannot see inside: it may legitimately ignore attributes, consume them by
 * hand, or handle them in its create_cb. Those never arm the check, so they
 * cannot produce a warning - even though they call lv_xml_obj_apply() and thus
 * run code that would otherwise record misses.
 *
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 356C LLC
 */

#ifndef LV_XML_ATTR_CHECK_H
#define LV_XML_ATTR_CHECK_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include "lv_xml.h"
#if LV_USE_XML

#include "lv_xml_parser.h"

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Arm the unknown-attribute check for one element.
 *
 * @param state         parser state; holds the whole check
 * @param attrs         the NULL-terminated name/value array about to be applied
 * @param widget_name   name the warning should blame, e.g. "lv_label"
 *
 * Silently declines (leaving the check disarmed) when @p attrs is NULL, carries
 * more than LV_XML_ATTR_CHECK_MAX attributes, or a check is already armed.
 * Declining costs a missed diagnostic, never a false one.
 */
void lv_xml_attr_check_begin(lv_xml_parser_state_t * state, const char ** attrs, const char * widget_name);

/**
 * Announce that the calling apply chain terminates in lv_xml_attr_check_miss().
 * Call once at the top of every such chain, before the attribute loop. No-op
 * unless a check is armed.
 *
 * A chain that does NOT call this must not call lv_xml_attr_check_miss()
 * either: the report compares miss count against participant count, and a
 * participant that never reports its misses would push every attribute below
 * the threshold and suppress the whole element's diagnostics.
 */
void lv_xml_attr_check_enter(lv_xml_parser_state_t * state);

/**
 * Record that the calling chain did not recognise @p name. Call from the
 * terminating `else` of an attribute loop. No-op unless a check is armed.
 */
void lv_xml_attr_check_miss(lv_xml_parser_state_t * state, const char * name);

/**
 * Mark @p name as legitimately consumed even though no chain matched it.
 *
 * For attributes read out of band with lv_xml_get_value_of() - either by a
 * create_cb (`<lv_tabview-tab text="...">`) or as a modifier read from inside
 * another attribute's branch (`bind_text-fmt`, `value-animated`). Those never
 * reach an if/else-if arm, so without this they would be reported as unknown.
 * No-op unless a check is armed.
 */
void lv_xml_attr_check_consume(lv_xml_parser_state_t * state, const char * name);

/**
 * Report every attribute that all participants missed, then disarm.
 * Reports nothing when no chain participated - which is how the element-style
 * processors (`<lv_obj-style>`, `<bind_flag_if_eq>`, ...) that read everything
 * through lv_xml_get_value_of() are exempted without listing them anywhere.
 */
void lv_xml_attr_check_end(lv_xml_parser_state_t * state);

#endif /* LV_USE_XML */

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LV_XML_ATTR_CHECK_H*/
