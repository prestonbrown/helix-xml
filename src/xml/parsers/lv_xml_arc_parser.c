/**
 * @file lv_xml_arc_parser.c
 *
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2025 LVGL Kft
 * SPDX-FileCopyrightText: 2026 356C LLC
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_xml_arc_parser.h"
#if LV_USE_XML && LV_USE_ARC

#include <lvgl.h>
#include <lvgl_private.h>
#include "../lv_xml_private.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static lv_arc_mode_t mode_text_to_enum_value(const char * txt);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void * lv_xml_arc_create(lv_xml_parser_state_t * state, const char ** attrs)
{
    LV_UNUSED(attrs);

    void * item = lv_arc_create(lv_xml_state_get_parent(state));
    return item;
}

void lv_xml_arc_apply(lv_xml_parser_state_t * state, const char ** attrs)
{
    void * item = lv_xml_state_get_item(state);
    lv_xml_obj_apply(state, attrs); /*Apply the common properties, e.g. width, height, styles flags etc*/

    for(int i = 0; attrs[i]; i += 2) {
        const char * name = attrs[i];
        const char * value = attrs[i + 1];

        if(lv_streq("start_angle", name)) lv_arc_set_start_angle(item, lv_xml_atoi(value));
        else if(lv_streq("end_angle", name)) lv_arc_set_end_angle(item, lv_xml_atoi(value));
        else if(lv_streq("bg_start_angle", name)) lv_arc_set_bg_start_angle(item, lv_xml_atoi(value));
        else if(lv_streq("bg_end_angle", name)) lv_arc_set_bg_end_angle(item, lv_xml_atoi(value));
        else if(lv_streq("rotation", name)) lv_arc_set_rotation(item, lv_xml_atoi(value));
        else if(lv_streq("value", name)) lv_arc_set_value(item, lv_xml_atoi(value));
        else if(lv_streq("min_value", name)) lv_arc_set_min_value(item, lv_xml_atoi(value));
        else if(lv_streq("max_value", name)) lv_arc_set_max_value(item, lv_xml_atoi(value));
        else if(lv_streq("mode", name)) lv_arc_set_mode(item, mode_text_to_enum_value(value));
        else if(lv_streq("bind_value", name)) {
            /* Empty/missing bind_value means "unset" — a component prop that
             * defaults to "". The host app registers a no-op subject under the
             * "" name to silence warnings, but lv_arc_bind_value is a two-way
             * binding: every arc that bound to that shared subject would mirror
             * each other's value (drag one fan dial, all of them follow). Skip
             * the bind entirely when no real subject name was given. */
            if(value && value[0] != '\0') {
                lv_subject_t * subject = lv_xml_get_subject(&state->scope, value);
                if(subject) {
                    lv_arc_bind_value(item, subject);
                }
                else {
                    LV_LOG_WARN("Subject \"%s\" doesn't exist in arc bind_value", value);
                }
            }
        }
    }
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static lv_arc_mode_t mode_text_to_enum_value(const char * txt)
{
    if(lv_streq("normal", txt)) return LV_ARC_MODE_NORMAL;
    if(lv_streq("symmetrical", txt)) return LV_ARC_MODE_SYMMETRICAL;
    if(lv_streq("reverse", txt)) return LV_ARC_MODE_REVERSE;

    LV_LOG_WARN("%s is an unknown value for bar's mode", txt);
    return 0; /*Return 0 in lack of a better option. */
}
#endif /* LV_USE_XML */
