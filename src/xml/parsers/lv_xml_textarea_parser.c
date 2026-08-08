/**
 * @file lv_xml_textarea_parser.c
 *
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2025 LVGL Kft
 * SPDX-FileCopyrightText: 2026 356C LLC
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_xml_textarea_parser.h"
#if LV_USE_XML && LV_USE_TEXTAREA

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

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void * lv_xml_textarea_create(lv_xml_parser_state_t * state, const char ** attrs)
{
    LV_UNUSED(attrs);
    void * item = lv_textarea_create(lv_xml_state_get_parent(state));

    return item;
}

void lv_xml_textarea_apply(lv_xml_parser_state_t * state, const char ** attrs)
{
    void * item = lv_xml_state_get_item(state);

    lv_xml_obj_apply(state, attrs); /*Apply the common properties, e.g. width, height, styles flags etc*/

    lv_xml_attr_check_enter(state);

    for(int i = 0; attrs[i]; i += 2) {
        const char * name = attrs[i];
        const char * value = attrs[i + 1];


        if(lv_streq("text", name)) lv_textarea_set_text(item, value);
        else if(lv_streq("placeholder_text", name)) lv_textarea_set_placeholder_text(item, value);
#if LV_USE_TRANSLATION
        /* placeholder_tag: translate the placeholder through the active language
         * pack (mirrors lv_label's translation_tag). Place it after placeholder_text
         * in the XML so it wins when both are present. Resolved at parse time, which
         * is sufficient because inputs live in modals/wizards recreated on each open. */
        else if(lv_streq("placeholder_tag", name)) lv_textarea_set_placeholder_text(item, lv_tr(value));
#endif
        else if(lv_streq("one_line", name)) lv_textarea_set_one_line(item, lv_xml_to_bool(value));
        else if(lv_streq("password_mode", name)) lv_textarea_set_password_mode(item, lv_xml_to_bool(value));
        else if(lv_streq("password_show_time", name)) lv_textarea_set_password_show_time(item, lv_xml_atoi(value));
        else if(lv_streq("text_selection", name)) lv_textarea_set_text_selection(item, lv_xml_to_bool(value));
        else if(lv_streq("cursor_pos", name)) lv_textarea_set_cursor_pos(item, lv_xml_atoi(value));
        else lv_xml_attr_check_miss(state, name);
    }
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

#endif /* LV_USE_XML */
