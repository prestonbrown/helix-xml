/**
 * @file lv_xml_roller_parser.c
 *
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2025 LVGL Kft
 * SPDX-FileCopyrightText: 2026 356C LLC
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_xml_roller_parser.h"
#if LV_USE_XML && LV_USE_ROLLER

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
static lv_roller_mode_t mode_text_to_enum_value(const char * txt);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void * lv_xml_roller_create(lv_xml_parser_state_t * state, const char ** attrs)
{
    LV_UNUSED(attrs);

    void * item = lv_roller_create(lv_xml_state_get_parent(state));
    return item;
}

void lv_xml_roller_apply(lv_xml_parser_state_t * state, const char ** attrs)
{
    void * item = lv_xml_state_get_item(state);
    lv_xml_obj_apply(state, attrs); /*Apply the common properties, e.g. width, height, styles flags etc*/

    lv_xml_attr_check_enter(state);
    /* Modifiers read out of band from inside another attribute's branch, so no
     * if/else-if arm ever matches their name. */
    lv_xml_attr_check_consume(state, "value-animated");
    lv_xml_attr_check_consume(state, "options-mode");

    for(int i = 0; attrs[i]; i += 2) {
        const char * name = attrs[i];
        const char * value = attrs[i + 1];

        if(lv_streq("selected", name)) {
            int32_t v = lv_xml_atoi(value);
            const char * anim_str = lv_xml_get_value_of(attrs, "value-animated");
            bool anim = anim_str ? lv_xml_to_bool(anim_str) : false;
            lv_roller_set_selected(item, v, anim);
        }
        else if(lv_streq("visible_row_count", name)) {
            lv_roller_set_visible_row_count(item, lv_xml_atoi(value));
        }

        else if(lv_streq("options", name)) {
            const char * mode_str = lv_xml_get_value_of(attrs, "options-mode");
            lv_roller_mode_t mode = mode_str ? mode_text_to_enum_value(mode_str) : LV_ROLLER_MODE_NORMAL;
            lv_roller_set_options(item, value, mode);
        }
        else if(lv_streq("bind_value", name)) {
            lv_subject_t * subject = lv_xml_get_subject(&state->scope, value);
            if(subject) {
                lv_roller_bind_value(item, subject);
            }
            else {
                LV_LOG_WARN("Subject \"%s\" doesn't exist in roller bind_value", value);
            }
        }
        else lv_xml_attr_check_miss(state, name);
    }
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static lv_roller_mode_t mode_text_to_enum_value(const char * txt)
{
    if(lv_streq("normal", txt)) return LV_ROLLER_MODE_NORMAL;
    if(lv_streq("infinite", txt)) return LV_ROLLER_MODE_INFINITE;

    LV_LOG_WARN("%s is an unknown value for roller's mode", txt);
    return 0; /*Return 0 in lack of a better option. */
}

#endif /* LV_USE_XML */
