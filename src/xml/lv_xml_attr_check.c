/**
 * @file lv_xml_attr_check.c
 *
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 356C LLC
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_xml_attr_check.h"
#if LV_USE_XML

#include "lv_xml_private.h"
#include <stdlib/lv_string.h>

/*********************
 *      DEFINES
 *********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static int32_t attr_index_of(const lv_xml_parser_state_t * state, const char * name);
static bool attr_is_framework(const char * name);

/**********************
 *  STATIC VARIABLES
 **********************/

/** Attributes the FRAMEWORK consumes before any widget processor sees the
 *  array. They are widget-agnostic, so they belong here rather than in some
 *  arbitrary parser's chain.
 *
 *  - `name`: read by lv_xml.c at component instantiation, and applied by
 *    lv_xml_obj_apply() in a standalone `if` that sits OUTSIDE its else-if
 *    chain - so the chain falls through to its terminating else and records a
 *    miss for an attribute it in fact handled.
 *  - `extends`: consumed by view_start_element_handler() to pick the processor
 *    in the first place; it is gone by the time an apply_cb could match it. */
static const char * const framework_attrs[] = {
    "name",
    "extends",
};

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_xml_attr_check_begin(lv_xml_parser_state_t * state, const char ** attrs, const char * widget_name)
{
    state->attr_check_attrs = NULL;
    state->attr_check_widget = NULL;
    state->attr_check_count = 0;
    state->attr_check_participants = 0;

    if(attrs == NULL || widget_name == NULL) return;

    /* Count the slots. Over the ceiling the check simply does not run: a
     * truncated view of the array could mis-index a miss onto an innocent
     * attribute, and a wrong warning is worse than none. */
    uint32_t cnt = 0;
    while(attrs[cnt] != NULL) {
        cnt++;
        if(cnt > LV_XML_ATTR_CHECK_MAX) return;
    }

    state->attr_check_attrs = attrs;
    state->attr_check_widget = widget_name;
    state->attr_check_count = (uint8_t)cnt;
    lv_memzero(state->attr_check_miss, sizeof(state->attr_check_miss));
    lv_memzero(state->attr_check_handled, sizeof(state->attr_check_handled));
}

void lv_xml_attr_check_enter(lv_xml_parser_state_t * state)
{
    if(state == NULL || state->attr_check_attrs == NULL) return;
    /* Saturate rather than wrap: a wrapped count would read as "fewer
     * participants than misses" and warn on valid attributes. */
    if(state->attr_check_participants < 255) state->attr_check_participants++;
}

void lv_xml_attr_check_miss(lv_xml_parser_state_t * state, const char * name)
{
    if(state == NULL || state->attr_check_attrs == NULL) return;

    int32_t idx = attr_index_of(state, name);
    if(idx < 0) return;
    if(state->attr_check_miss[idx] < 255) state->attr_check_miss[idx]++;
}

void lv_xml_attr_check_consume(lv_xml_parser_state_t * state, const char * name)
{
    if(state == NULL || state->attr_check_attrs == NULL) return;

    int32_t idx = attr_index_of(state, name);
    if(idx < 0) return;
    state->attr_check_handled[idx] = 1;
}

void lv_xml_attr_check_end(lv_xml_parser_state_t * state)
{
    if(state == NULL || state->attr_check_attrs == NULL) return;

    const char ** attrs = state->attr_check_attrs;
    uint8_t participants = state->attr_check_participants;

    /* No chain reported its misses, so there is no evidence either way. This is
     * the exemption for processors that read everything through
     * lv_xml_get_value_of() - <lv_obj-style>, <bind_flag_if_eq>, the *_event
     * elements - and it costs nothing to maintain. */
    if(participants > 0) {
        for(uint8_t i = 0; i + 1 < state->attr_check_count; i += 2) {
            if(state->attr_check_handled[i]) continue;
            if(state->attr_check_miss[i] < participants) continue;
            /* An empty NAME is resolve_params() erasing the slot in place - the
             * `$prop` it held could not be resolved, which it has already warned
             * about by name. The slot is not an attribute anyone wrote. */
            if(attrs[i] == NULL || attrs[i][0] == '\0') continue;
            if(attr_is_framework(attrs[i])) continue;

            LV_LOG_WARN("Unknown attribute \"%s\" on <%s>; it is ignored (typo? "
                        "attribute of a different widget?)",
                        attrs[i], state->attr_check_widget);
        }
    }

    state->attr_check_attrs = NULL;
    state->attr_check_widget = NULL;
    state->attr_check_count = 0;
    state->attr_check_participants = 0;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/**
 * Slot of @p name in the tracked array, or -1.
 *
 * Only EVEN slots are searched. Odd slots hold values, and a value equal to an
 * attribute name ("state" as both) would otherwise be marked instead.
 * Callers pass attrs[i] straight through, so the pointer normally matches
 * outright; the string compare covers lv_xml_attr_check_consume(), which is
 * called with a literal.
 */
static int32_t attr_index_of(const lv_xml_parser_state_t * state, const char * name)
{
    if(name == NULL) return -1;

    const char ** attrs = state->attr_check_attrs;
    for(uint8_t i = 0; i + 1 < state->attr_check_count; i += 2) {
        if(attrs[i] == name) return (int32_t)i;
    }
    for(uint8_t i = 0; i + 1 < state->attr_check_count; i += 2) {
        if(attrs[i] && lv_streq(attrs[i], name)) return (int32_t)i;
    }
    return -1;
}

static bool attr_is_framework(const char * name)
{
    if(name == NULL) return true;
    for(size_t i = 0; i < sizeof(framework_attrs) / sizeof(framework_attrs[0]); i++) {
        if(lv_streq(framework_attrs[i], name)) return true;
    }
    return false;
}

#endif /* LV_USE_XML */
