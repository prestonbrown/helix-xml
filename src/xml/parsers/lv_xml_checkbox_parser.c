/**
 * @file lv_xml_checkbox_parser.c
 *
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2025 LVGL Kft
 * SPDX-FileCopyrightText: 2026 356C LLC
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_xml_checkbox_parser.h"
#if LV_USE_XML

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
static void checkbox_on_language_changed(lv_event_t * e);
static void checkbox_clear_translation_tag(lv_obj_t * obj);
static void checkbox_set_translation_tag(lv_obj_t * obj, const char * tag);
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

void * lv_xml_checkbox_create(lv_xml_parser_state_t * state, const char ** attrs)
{
    LV_UNUSED(attrs);

    void * item = lv_checkbox_create(lv_xml_state_get_parent(state));
    return item;
}

void lv_xml_checkbox_apply(lv_xml_parser_state_t * state, const char ** attrs)
{
    void * item = lv_xml_state_get_item(state);
    lv_xml_obj_apply(state, attrs); /*Apply the common properties, e.g. width, height, styles flags etc*/

    lv_xml_attr_check_enter(state);

    for(int i = 0; attrs[i]; i += 2) {
        const char * name = attrs[i];
        const char * value = attrs[i + 1];

        if(lv_streq("text", name)) {
#if LV_USE_TRANSLATION
            /* A literal text= drops any tag set earlier on this element, exactly
             * as lv_label_set_text() calls the label's remove_translation_tag().
             * Precedence is therefore attribute ORDER: whichever of text= and
             * translation_tag= is written last wins, and text= also stops the
             * widget following further language changes. */
            checkbox_clear_translation_tag(item);
#endif
            lv_checkbox_set_text(item, value);
        }
#if LV_USE_TRANSLATION
        else if(lv_streq("translation_tag", name)) checkbox_set_translation_tag(item, value);
#endif
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
 * lv_label owns its tag inside the widget and re-resolves from the label's own
 * LV_EVENT_TRANSLATION_LANGUAGE_CHANGED arm. lv_checkbox has no tag field and no
 * such arm - it stores only `char * txt` - so the tag has to live on an event
 * descriptor instead. lv_translation_set_language() walks the whole object tree
 * sending the event to every object, so a plain lv_obj_add_event_cb() puts the
 * checkbox on exactly the same notification as the label.
 */
static void checkbox_on_language_changed(lv_event_t * e)
{
    lv_obj_t * obj = lv_event_get_target(e);
    const char * tag = lv_event_get_user_data(e);
    if(obj && tag) lv_checkbox_set_text(obj, lv_tr(tag));
}

/**
 * Drop any tag this parser previously installed on @p obj, freeing the copy.
 *
 * Needed because an apply chain can run more than once over the same widget (a
 * component's own view, then the attributes of the instance that used it).
 * Without this a second translation_tag= would leave the first handler armed on
 * a leaked string, and the two would fight over the text on every switch.
 */
static void checkbox_clear_translation_tag(lv_obj_t * obj)
{
    char * tag = NULL;

    uint32_t i = 0;
    while(i < lv_obj_get_event_count(obj)) {
        lv_event_dsc_t * dsc = lv_obj_get_event_dsc(obj, i);
        if(dsc && lv_event_dsc_get_cb(dsc) == checkbox_on_language_changed) {
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
static void checkbox_set_translation_tag(lv_obj_t * obj, const char * tag)
{
    if(tag == NULL || tag[0] == '\0') return;

    checkbox_clear_translation_tag(obj);

    char * copy = lv_strdup(tag);
    LV_ASSERT_MALLOC(copy);
    if(copy == NULL) {
        LV_LOG_WARN("Couldn't allocate the translation tag \"%s\" for the checkbox", tag);
        return;
    }

    lv_obj_add_event_cb(obj, checkbox_on_language_changed, LV_EVENT_TRANSLATION_LANGUAGE_CHANGED, copy);
    lv_obj_add_event_cb(obj, lv_event_free_user_data_cb, LV_EVENT_DELETE, copy);

    lv_checkbox_set_text(obj, lv_tr(tag));
}

#endif /*LV_USE_TRANSLATION*/


#endif /* LV_USE_XML */
