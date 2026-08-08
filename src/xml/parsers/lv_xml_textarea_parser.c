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
#if LV_USE_TRANSLATION
static void textarea_on_language_changed(lv_event_t * e);
static void textarea_clear_placeholder_tag(lv_obj_t * obj);
static void textarea_set_placeholder_tag(lv_obj_t * obj, const char * tag);
#endif /*LV_USE_TRANSLATION*/

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
        else if(lv_streq("placeholder_text", name)) {
#if LV_USE_TRANSLATION
            /* A literal placeholder_text= drops any tag set earlier on this element,
             * mirroring how lv_label_set_text() clears the label's stored tag.
             * Precedence is therefore attribute ORDER: whichever of placeholder_text=
             * and placeholder_tag= is written last wins, and placeholder_text= also
             * stops the widget following further language changes. */
            textarea_clear_placeholder_tag(item);
#endif
            lv_textarea_set_placeholder_text(item, value);
        }
#if LV_USE_TRANSLATION
        /* placeholder_tag: translate the placeholder through the active language
         * pack (mirrors lv_label's translation_tag), and keep following the language
         * for as long as the widget lives. */
        else if(lv_streq("placeholder_tag", name)) textarea_set_placeholder_tag(item, value);
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

#if LV_USE_TRANSLATION

/**
 * Re-resolve the stored tag under the newly selected language.
 *
 * This used to be a bare parse-time lv_tr(), justified in a comment on the
 * grounds that "inputs live in modals/wizards recreated on each open". That
 * invariant does not hold: in the application this engine was written for,
 * spoolman_panel, history_list_panel and printer_manager_overlay are each built
 * once and kept for the process lifetime, and all three carry a placeholder_tag.
 * Their placeholders froze in whatever language was active when the panel was
 * first opened. Rather than re-assert an invariant nothing enforces, the tag is
 * now stored and re-resolved, which makes the widget's lifetime irrelevant.
 *
 * Mechanism is the checkbox's: lv_textarea has no tag field and no
 * LV_EVENT_TRANSLATION_LANGUAGE_CHANGED arm of its own, so the tag lives on an
 * event descriptor. lv_translation_set_language() sends that event to every
 * object in the tree, which puts the textarea on the same notification a label
 * already gets.
 */
static void textarea_on_language_changed(lv_event_t * e)
{
    lv_obj_t * obj = lv_event_get_target(e);
    const char * tag = lv_event_get_user_data(e);
    if(obj && tag) lv_textarea_set_placeholder_text(obj, lv_tr(tag));
}

/**
 * Drop any tag this parser previously installed on @p obj, freeing the copy.
 *
 * Needed because an apply chain can run more than once over the same widget (a
 * component's own view, then the attributes of the instance that used it), and
 * because <text_input>-style wrappers forward the whole attribute array into
 * this parser before running their own loop. Without this a second
 * placeholder_tag= would leave the first handler armed on a leaked string, and
 * the two would fight over the placeholder on every switch.
 */
static void textarea_clear_placeholder_tag(lv_obj_t * obj)
{
    char * tag = NULL;

    uint32_t i = 0;
    while(i < lv_obj_get_event_count(obj)) {
        lv_event_dsc_t * dsc = lv_obj_get_event_dsc(obj, i);
        if(dsc && lv_event_dsc_get_cb(dsc) == textarea_on_language_changed) {
            tag = lv_event_dsc_get_user_data(dsc);
            lv_obj_remove_event_dsc(obj, dsc);
            continue; /*Removal shifts the remaining indices down*/
        }
        i++;
    }

    if(tag == NULL) return;

    /*Registered with the same pointer, so this un-arms the free without doing it*/
    lv_obj_remove_event_cb_with_user_data(obj, lv_event_free_user_data_cb, tag);
    lv_free(tag);
}

/**
 * Mirrors lv_label_set_translation_tag(): store the tag, then show lv_tr(tag).
 * An empty tag is ignored outright - including leaving an existing tag in place -
 * because that is what the label does with one.
 */
static void textarea_set_placeholder_tag(lv_obj_t * obj, const char * tag)
{
    if(tag == NULL || tag[0] == '\0') return;

    textarea_clear_placeholder_tag(obj);

    char * copy = lv_strdup(tag);
    LV_ASSERT_MALLOC(copy);
    if(copy == NULL) {
        LV_LOG_WARN("Couldn't allocate the placeholder tag \"%s\" for the textarea", tag);
        return;
    }

    lv_obj_add_event_cb(obj, textarea_on_language_changed, LV_EVENT_TRANSLATION_LANGUAGE_CHANGED, copy);
    lv_obj_add_event_cb(obj, lv_event_free_user_data_cb, LV_EVENT_DELETE, copy);

    lv_textarea_set_placeholder_text(obj, lv_tr(tag));
}

#endif /*LV_USE_TRANSLATION*/

#endif /* LV_USE_XML */
