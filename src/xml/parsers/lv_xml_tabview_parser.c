/**
 * @file lv_xml_tabview_parser.c
 *
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2025 LVGL Kft
 * SPDX-FileCopyrightText: 2026 356C LLC
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_xml_tabview_parser.h"
#if LV_USE_XML && LV_USE_TABVIEW

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

void * lv_xml_tabview_create(lv_xml_parser_state_t * state, const char ** attrs)
{
    LV_UNUSED(attrs);
    void * item = lv_tabview_create(lv_xml_state_get_parent(state));

    return item;
}


void lv_xml_tabview_apply(lv_xml_parser_state_t * state, const char ** attrs)
{
    void * item = lv_xml_state_get_item(state);

    lv_xml_obj_apply(state, attrs); /*Apply the common properties, e.g. width, height, styles flags etc*/

    lv_xml_attr_check_enter(state);

    for(int i = 0; attrs[i]; i += 2) {
        const char * name = attrs[i];
        const char * value = attrs[i + 1];

        if(lv_streq("active", name)) lv_tabview_set_active(item, lv_xml_atoi(value), 0);
        else if(lv_streq("tab_bar_position", name)) lv_tabview_set_tab_bar_position(item, lv_xml_dir_to_enum(value));
        else lv_xml_attr_check_miss(state, name);
    }
}

void * lv_xml_tabview_tab_bar_create(lv_xml_parser_state_t * state, const char ** attrs)
{
    LV_UNUSED(attrs);
    void * item = lv_tabview_get_tab_bar(lv_xml_state_get_parent(state));
    return item;
}


void lv_xml_tabview_tab_bar_apply(lv_xml_parser_state_t * state, const char ** attrs)
{
    /*Apply the common properties, e.g. width, height, styles flags etc*/
    lv_xml_obj_apply(state, attrs);
}

/**
 * A tab's title is fixed at creation - lv_tabview_add_tab() builds the button and
 * its label in one go - so unlike every other translatable attribute in this
 * engine, translation_tag= cannot be handled in the apply chain. It has to pick
 * WHICH LVGL call creates the tab.
 *
 * lv_tabview_set_tab_translation_tag() is that alternative creator: it adds the
 * tab with a NULL title and calls lv_label_set_translation_tag() on the button's
 * label, returning the same page pointer lv_tabview_add_tab() would. Because the
 * tag ends up on a real lv_label, re-resolution is the label's own
 * LV_EVENT_TRANSLATION_LANGUAGE_CHANGED arm - this parser stores nothing, owns
 * no allocation, and needs no delete hook.
 *
 * PRECEDENCE differs from <lv_checkbox> and <lv_textarea> deliberately. Those
 * resolve text= against translation_tag= by attribute ORDER, because their apply
 * loop reaches the attributes in sequence and each setter clears the other. Here
 * both attributes are read from the same array before anything exists, so there
 * is no order to observe: translation_tag= simply wins whenever it is present
 * and non-empty. An empty tag falls through to text=, so translation_tag=""
 * behaves like the attribute being absent rather than blanking the title.
 */
void * lv_xml_tabview_tab_create(lv_xml_parser_state_t * state, const char ** attrs)
{
    lv_obj_t * parent = lv_xml_state_get_parent(state);

#if LV_USE_TRANSLATION
    const char * tag = lv_xml_get_value_of(attrs, "translation_tag");
    if(tag && tag[0] != '\0') return lv_tabview_set_tab_translation_tag(parent, tag);
#endif

    const char * text = lv_xml_get_value_of(attrs, "text");
    return lv_tabview_add_tab(parent, text);
}

void lv_xml_tabview_tab_apply(lv_xml_parser_state_t * state, const char ** attrs)
{
    /* Consumed by lv_xml_tabview_tab_create() - the tab's title has to be known
     * before the tab exists, so it never reaches an apply chain. */
    lv_xml_attr_check_consume(state, "text");
#if LV_USE_TRANSLATION
    /* Same reason: consumed at create time, so the unknown-attribute check must
     * not report it. Consumed unconditionally - including when it lost to text=
     * by being empty - because it WAS read, and warning about it would send the
     * author looking for a typo that is not there. */
    lv_xml_attr_check_consume(state, "translation_tag");
#endif

    /*Apply the common properties, e.g. width, height, styles flags etc*/
    lv_xml_obj_apply(state, attrs);
}

void * lv_xml_tabview_tab_button_create(lv_xml_parser_state_t * state, const char ** attrs)
{
    lv_obj_t * tv = lv_xml_state_get_parent(state);
    int32_t btn_cnt = lv_tabview_get_tab_count(tv);
    if(btn_cnt == 0) {
        LV_LOG_WARN("There are no buttons on the tab view. Get tab buttons when the tabs are already created");
        return NULL;
    }

    const char * index_str = lv_xml_get_value_of(attrs, "index");
    int32_t index_int = index_str ? lv_xml_atoi(index_str) : 0;

    void * item = lv_tabview_get_tab_button(tv, index_int);

    if(item == NULL) {
        LV_LOG_WARN("tabindex is out of range, using the first tab instead");
        item = lv_tabview_get_tab_button(tv, 0);
    }

    return item;
}

void lv_xml_tabview_tab_button_apply(lv_xml_parser_state_t * state, const char ** attrs)
{
    /* Consumed by lv_xml_tabview_tab_button_create(), which uses it to pick
     * WHICH existing button this element addresses. */
    lv_xml_attr_check_consume(state, "index");

    /*Apply the common properties, e.g. width, height, styles flags etc*/
    lv_xml_obj_apply(state, attrs);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

#endif /* LV_USE_XML */
