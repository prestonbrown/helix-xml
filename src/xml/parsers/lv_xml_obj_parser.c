/**
 * @file lv_xml_obj_parser.c
 *
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2025 LVGL Kft
 * SPDX-FileCopyrightText: 2026 356C LLC
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_xml_obj_parser.h"
#if LV_USE_XML

#include <lvgl.h>
#include <lvgl_private.h>
#include "../lv_xml_private.h"
#include "../lv_xml_expr.h"

/*********************
 *      DEFINES
 *********************/
#include "../lv_xml_globals.h"
#define lv_event_xml_store_timeline lv_xml_event_store_timeline

/**********************
 *      TYPEDEFS
 **********************/

/*Duplication from lv_obj.c as lv_obj_add_screen_create_event needs to be
 * reimplemented here slightly differently */
typedef struct {
    lv_screen_load_anim_t anim_type;
    uint32_t duration;
    uint32_t delay;
    const char * screen_name;
} screen_load_anim_dsc_t;

typedef struct {
    const char * timeline_name;
    const char * target_name;
    uint32_t delay;
    bool reverse;
    lv_obj_t * base_obj; /**< Get the objs by name from here (the view) */
} play_anim_dsc_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static lv_obj_flag_t flag_to_enum(const char * txt);
static void apply_styles(lv_xml_parser_state_t * state, lv_obj_t * obj, const char * name, const char * value);
static void screen_create_on_trigger_event_cb(lv_event_t * e);
static void screen_load_on_trigger_event_cb(lv_event_t * e);
static void delete_on_screen_unloaded_event_cb(lv_event_t * e);
static void free_screen_create_user_data_on_delete_event_cb(lv_event_t * e);
static void play_anim_on_trigger_event_cb(lv_event_t * e);
static void free_play_anim_user_data_on_delete_event_cb(lv_event_t * e);
static size_t lv_xml_parse_parts_attr(const char * parts_str, lv_style_selector_t state_bits,
                                      lv_style_selector_t * out, size_t max);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/* Expands to
   if(lv_streq(prop_name, "style_height")) lv_obj_set_style_height(obj, value, selector)
 */
#define SET_STYLE_IF(prop, value) if(lv_streq(prop_name, "style_" #prop)) lv_obj_set_style_##prop(obj, value, selector)

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void * lv_xml_obj_create(lv_xml_parser_state_t * state, const char ** attrs)
{
    LV_UNUSED(attrs);
    void * item = lv_obj_create(lv_xml_state_get_parent(state));

    return item;
}

void lv_xml_obj_apply(lv_xml_parser_state_t * state, const char ** attrs)
{
    void * item = lv_xml_state_get_item(state);

    for(int i = 0; attrs[i]; i += 2) {
        const char * name = attrs[i];
        const char * value = attrs[i + 1];
        size_t name_len = lv_strlen(name);

#if LV_USE_OBJ_NAME
        if(lv_streq("name", name)) {
            lv_obj_set_name(item, value);
        }
#endif
        if(lv_streq("x", name)) lv_obj_set_x(item, lv_xml_to_size(value));
        else if(lv_streq("y", name)) lv_obj_set_y(item, lv_xml_to_size(value));
        else if(lv_streq("width", name)) lv_obj_set_width(item, lv_xml_to_size(value));
        else if(lv_streq("height", name)) lv_obj_set_height(item, lv_xml_to_size(value));
        else if(lv_streq("align", name)) lv_obj_set_align(item, lv_xml_align_to_enum(value));
        else if(lv_streq("flex_flow", name)) lv_obj_set_flex_flow(item, lv_xml_flex_flow_to_enum(value));
        else if(lv_streq("flex_grow", name)) lv_obj_set_flex_grow(item, lv_xml_atoi(value));
        else if(lv_streq("ext_click_area", name)) lv_obj_set_ext_click_area(item, lv_xml_atoi(value));
        else if(lv_streq("scroll_snap_x", name)) lv_obj_set_scroll_snap_x(item, lv_xml_scroll_snap_to_enum(value));
        else if(lv_streq("scroll_snap_y", name)) lv_obj_set_scroll_snap_y(item, lv_xml_scroll_snap_to_enum(value));
        else if(lv_streq("scrollbar_mode", name)) lv_obj_set_scrollbar_mode(item, lv_xml_scrollbar_mode_to_enum(value));
        else if(lv_streq("scroll_dir", name)) lv_obj_set_scroll_dir(item, lv_xml_dir_to_enum(value));

        else if(lv_streq("hidden", name))               lv_obj_set_flag(item, LV_OBJ_FLAG_HIDDEN, lv_xml_to_bool(value));
        else if(lv_streq("hidden_if_prop_eq", name)) {
            /* Format: "resolved_value|ref_value" — hide if resolved_value == ref_value.
             * By this point $prop references are already resolved by resolve_params(),
             * so value contains the final strings separated by pipe. */
            const char * sep = lv_strchr(value, '|');
            if(sep) {
                size_t val_len = (size_t)(sep - value);
                const char * ref = sep + 1;
                size_t ref_len = lv_strlen(ref);
                if(val_len == ref_len && (val_len == 0 || lv_memcmp(value, ref, val_len) == 0)) {
                    lv_obj_add_flag(item, LV_OBJ_FLAG_HIDDEN);
                }
            }
            else {
                LV_LOG_WARN("hidden_if_prop_eq=\"%s\" has no `|` separator so it can never match; "
                            "the expected form is \"$prop|ref_value\"", value);
            }
        }
        else if(lv_streq("hidden_if_prop_not_eq", name)) {
            const char * sep = lv_strchr(value, '|');
            if(sep) {
                size_t val_len = (size_t)(sep - value);
                const char * ref = sep + 1;
                size_t ref_len = lv_strlen(ref);
                bool eq = (val_len == ref_len && (val_len == 0 || lv_memcmp(value, ref, val_len) == 0));
                if(!eq) {
                    lv_obj_add_flag(item, LV_OBJ_FLAG_HIDDEN);
                }
            }
            else {
                LV_LOG_WARN("hidden_if_prop_not_eq=\"%s\" has no `|` separator so it can never match; "
                            "the expected form is \"$prop|ref_value\"", value);
            }
        }
        else if(lv_streq("hidden_if_empty", name)) {
            /* Shortcut for hidden_if_prop_eq="$prop|" — hide if resolved value is empty */
            if(value[0] == '\0') {
                lv_obj_add_flag(item, LV_OBJ_FLAG_HIDDEN);
            }
        }
        else if(lv_streq("clickable", name))            lv_obj_set_flag(item, LV_OBJ_FLAG_CLICKABLE, lv_xml_to_bool(value));
        else if(lv_streq("click_focusable", name))      lv_obj_set_flag(item, LV_OBJ_FLAG_CLICK_FOCUSABLE,
                                                                            lv_xml_to_bool(value));
        else if(lv_streq("checkable", name))            lv_obj_set_flag(item, LV_OBJ_FLAG_CHECKABLE, lv_xml_to_bool(value));
        else if(lv_streq("scrollable", name))           lv_obj_set_flag(item, LV_OBJ_FLAG_SCROLLABLE, lv_xml_to_bool(value));
        else if(lv_streq("scroll_elastic", name))       lv_obj_set_flag(item, LV_OBJ_FLAG_SCROLL_ELASTIC,
                                                                            lv_xml_to_bool(value));
        else if(lv_streq("scroll_momentum", name))      lv_obj_set_flag(item, LV_OBJ_FLAG_SCROLL_MOMENTUM,
                                                                            lv_xml_to_bool(value));
        else if(lv_streq("scroll_one", name))           lv_obj_set_flag(item, LV_OBJ_FLAG_SCROLL_ONE, lv_xml_to_bool(value));
        else if(lv_streq("scroll_chain_hor", name))     lv_obj_set_flag(item, LV_OBJ_FLAG_SCROLL_CHAIN_HOR,
                                                                            lv_xml_to_bool(value));
        else if(lv_streq("scroll_chain_ver", name))     lv_obj_set_flag(item, LV_OBJ_FLAG_SCROLL_CHAIN_VER,
                                                                            lv_xml_to_bool(value));
        else if(lv_streq("scroll_chain", name))         lv_obj_set_flag(item, LV_OBJ_FLAG_SCROLL_CHAIN,
                                                                            lv_xml_to_bool(value));
        else if(lv_streq("scroll_on_focus", name))      lv_obj_set_flag(item, LV_OBJ_FLAG_SCROLL_ON_FOCUS,
                                                                            lv_xml_to_bool(value));
        else if(lv_streq("scroll_with_arrow", name))    lv_obj_set_flag(item, LV_OBJ_FLAG_SCROLL_WITH_ARROW,
                                                                            lv_xml_to_bool(value));
        else if(lv_streq("snappable", name))            lv_obj_set_flag(item, LV_OBJ_FLAG_SNAPPABLE, lv_xml_to_bool(value));
        else if(lv_streq("press_lock", name))           lv_obj_set_flag(item, LV_OBJ_FLAG_PRESS_LOCK, lv_xml_to_bool(value));
        else if(lv_streq("event_bubble", name))         lv_obj_set_flag(item, LV_OBJ_FLAG_EVENT_BUBBLE,
                                                                            lv_xml_to_bool(value));
        else if(lv_streq("event_trickle", name))        lv_obj_set_flag(item, LV_OBJ_FLAG_EVENT_TRICKLE,
                                                                            lv_xml_to_bool(value));
        else if(lv_streq("state_trickle", name))       lv_obj_set_flag(item, LV_OBJ_FLAG_STATE_TRICKLE,
                                                                           lv_xml_to_bool(value));
        else if(lv_streq("gesture_bubble", name))       lv_obj_set_flag(item, LV_OBJ_FLAG_GESTURE_BUBBLE,
                                                                            lv_xml_to_bool(value));
        else if(lv_streq("adv_hittest", name))          lv_obj_set_flag(item, LV_OBJ_FLAG_ADV_HITTEST,
                                                                            lv_xml_to_bool(value));
        else if(lv_streq("ignore_layout", name))        lv_obj_set_flag(item, LV_OBJ_FLAG_IGNORE_LAYOUT,
                                                                            lv_xml_to_bool(value));
        else if(lv_streq("floating", name))             lv_obj_set_flag(item, LV_OBJ_FLAG_FLOATING, lv_xml_to_bool(value));
        else if(lv_streq("send_draw_task_events", name))lv_obj_set_flag(item, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS,
                                                                            lv_xml_to_bool(value));
        else if(lv_streq("overflow_visible", name))     lv_obj_set_flag(item, LV_OBJ_FLAG_OVERFLOW_VISIBLE,
                                                                            lv_xml_to_bool(value));
#ifdef LV_OBJ_FLAG_RADIO_BUTTON
        else if(lv_streq("radio_button", name))     lv_obj_set_flag(item, LV_OBJ_FLAG_RADIO_BUTTON,
                                                                        lv_xml_to_bool(value));
#endif
        else if(lv_streq("flex_in_new_track", name))    lv_obj_set_flag(item, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK,
                                                                            lv_xml_to_bool(value));

        else if(lv_streq("checked", name))  lv_obj_set_state(item, LV_STATE_CHECKED, lv_xml_to_bool(value));
        else if(lv_streq("focused", name))  lv_obj_set_state(item, LV_STATE_FOCUSED, lv_xml_to_bool(value));
        else if(lv_streq("focus_key", name)) lv_obj_set_state(item, LV_STATE_FOCUS_KEY, lv_xml_to_bool(value));
        else if(lv_streq("edited", name))   lv_obj_set_state(item, LV_STATE_EDITED, lv_xml_to_bool(value));
        else if(lv_streq("hovered", name))  lv_obj_set_state(item, LV_STATE_HOVERED, lv_xml_to_bool(value));
        else if(lv_streq("pressed", name))  lv_obj_set_state(item, LV_STATE_PRESSED, lv_xml_to_bool(value));
        else if(lv_streq("scrolled", name)) lv_obj_set_state(item, LV_STATE_SCROLLED, lv_xml_to_bool(value));
        else if(lv_streq("disabled", name)) lv_obj_set_state(item, LV_STATE_DISABLED, lv_xml_to_bool(value));

        else if(lv_streq("bind_checked", name)) {
            lv_subject_t * subject = lv_xml_get_subject(&state->scope, value);
            if(subject) {
                lv_obj_bind_checked(item, subject);
            }
            else {
                LV_LOG_WARN("Subject `%s` doesn't exist in lv_obj bind_checked", value);
            }
        }

        else if(name_len > 6 && lv_memcmp("style_", name, 6) == 0) {
            apply_styles(state, item, name, value);
        }
    }
}

void * lv_obj_xml_style_create(lv_xml_parser_state_t * state, const char ** attrs)
{
    LV_UNUSED(attrs);
    void * item = lv_xml_state_get_parent(state);
    return item;
}

void lv_obj_xml_style_apply(lv_xml_parser_state_t * state, const char ** attrs)
{
    const char * name = lv_xml_get_value_of(attrs, "name");
    if(name == NULL) {
        /*Silently ignore this issue.
         *The name set to NULL if there there was no default value when resolving params*/
        return;
    }
    lv_xml_style_t * xml_style = lv_xml_get_style_by_name(&state->scope, name);
    if(xml_style == NULL) {
        LV_LOG_WARN("`%s` style is not found", name);
        return;
    }

    const char * selector_str = lv_xml_get_value_of(attrs, "selector");
    lv_style_selector_t selector = lv_xml_style_selector_text_to_enum(selector_str);

    void * item = lv_xml_state_get_parent(state);

    /* `parts="main,indicator,knob"` — same attribute <bind_style>,
     * <bind_style_if_*> and <bind_style_if> accept. Plain <style> used to read
     * only `selector`, so a documented `parts=` list silently applied the style
     * to LV_PART_MAIN and to nothing else. State bits from `selector` live in
     * the low 16 and are preserved across each part. */
    const char * parts_str = lv_xml_get_value_of(attrs, "parts");
    lv_style_selector_t parts[8];
    size_t n_parts = lv_xml_parse_parts_attr(parts_str, selector & 0xFFFF, parts, 8);
    if(n_parts > 0) {
        for(size_t i = 0; i < n_parts; i++) {
            lv_obj_add_style(item, &xml_style->style, parts[i]);
        }
    }
    else {
        lv_obj_add_style(item, &xml_style->style, selector);
    }
}

void * lv_obj_xml_remove_style_create(lv_xml_parser_state_t * state, const char ** attrs)
{
    LV_UNUSED(attrs);
    void * item = lv_xml_state_get_parent(state);
    return item;
}

void lv_obj_xml_remove_style_apply(lv_xml_parser_state_t * state, const char ** attrs)
{

    const char * style_str = lv_xml_get_value_of(attrs, "name");
    const char * selector_str = lv_xml_get_value_of(attrs, "selector");

    lv_style_t * style = NULL;
    if(style_str) {
        lv_xml_style_t * xml_style = lv_xml_get_style_by_name(&state->scope, style_str);
        if(xml_style == NULL) {
            LV_LOG_WARN("No style found with name `%s`", style_str);
            return;
        }
        style = &xml_style->style;
    }

    lv_style_selector_t selector = lv_xml_style_selector_text_to_enum(selector_str);

    void * item = lv_xml_state_get_item(state);
    lv_obj_remove_style(item, style, selector);
}

void * lv_obj_xml_remove_style_all_create(lv_xml_parser_state_t * state, const char ** attrs)
{
    LV_UNUSED(attrs);
    void * item = lv_xml_state_get_parent(state);
    return item;
}

void lv_obj_xml_remove_style_all_apply(lv_xml_parser_state_t * state, const char ** attrs)
{
    LV_UNUSED(attrs);
    void * item = lv_xml_state_get_item(state);
    lv_obj_remove_style_all(item);
}

void * lv_obj_xml_event_cb_create(lv_xml_parser_state_t * state, const char ** attrs)
{
    LV_UNUSED(attrs);
    void * item = lv_xml_state_get_parent(state);
    return item;
}

void lv_obj_xml_event_cb_apply(lv_xml_parser_state_t * state, const char ** attrs)
{
    const char * trigger_str = lv_xml_get_value_of(attrs, "trigger");
    lv_event_code_t code = LV_EVENT_CLICKED;
    if(trigger_str) code = lv_xml_trigger_text_to_enum_value(trigger_str);
    if(code == LV_EVENT_LAST)  {
        LV_LOG_WARN("Couldn't add call function event because `%s` trigger is invalid.", trigger_str);
        return;
    }

    const char * cb_str = lv_xml_get_value_of(attrs, "callback");
    if(cb_str == NULL) {
        LV_LOG_WARN("callback is mandatory for event-call_function");
        return;
    }

    lv_obj_t * obj = lv_xml_state_get_parent(state);
    lv_event_cb_t cb = lv_xml_get_event_cb(&state->scope, cb_str);
    if(cb == NULL) {
        LV_LOG_WARN("Couldn't add call function event because `%s` callback is not found.", cb_str);
        return;
    }

    const char * user_data_str = lv_xml_get_value_of(attrs, "user_data");
    char * user_data = NULL;
    if(user_data_str) user_data = lv_strdup(user_data_str);

    lv_obj_add_event_cb(obj, cb, code, user_data);
    if(user_data) lv_obj_add_event_cb(obj, lv_event_free_user_data_cb, LV_EVENT_DELETE, user_data);
}

void * lv_obj_xml_subject_toggle_create(lv_xml_parser_state_t * state, const char ** attrs)
{
    LV_UNUSED(attrs);
    void * item = lv_xml_state_get_parent(state);
    return item;
}

void lv_obj_xml_subject_toggle_apply(lv_xml_parser_state_t * state, const char ** attrs)
{
    /*If the tag_name is */
    const char * subject_str =  lv_xml_get_value_of(attrs, "subject");
    const char * trigger_str =  lv_xml_get_value_of(attrs, "trigger");

    if(subject_str == NULL) {
        LV_LOG_WARN("`subject` is missing in <lv_obj-subject_toggle_event>");
        return;
    }

    lv_event_code_t trigger = LV_EVENT_CLICKED;
    if(trigger_str) trigger = lv_xml_trigger_text_to_enum_value(trigger_str);
    if(trigger == LV_EVENT_LAST)  {
        LV_LOG_WARN("Couldn't apply <lv_obj-subject_toggle_event> because `%s` trigger is invalid.", trigger_str);
        return;
    }

    lv_subject_t * subject = lv_xml_get_subject(&state->scope, subject_str);
    if(subject == NULL) {
        LV_LOG_WARN("Subject `%s` doesn't exist in <lv_obj-subject_toggle>", subject_str);
        return;
    }

    void * item = lv_xml_state_get_item(state);
    lv_obj_add_subject_toggle_event(item, subject, trigger);
}

void * lv_obj_xml_subject_set_create(lv_xml_parser_state_t * state, const char ** attrs)
{
    LV_UNUSED(attrs);
    void * item = lv_xml_state_get_parent(state);
    return item;
}

void lv_obj_xml_subject_set_apply(lv_xml_parser_state_t * state, const char ** attrs)
{

    /*If the tag_name is */
    lv_subject_type_t subject_type = LV_SUBJECT_TYPE_NONE;
    if(lv_streq(state->tag_name, "lv_obj-subject_set_int_event") ||
       lv_streq(state->tag_name, "subject_set_int_event")) {
        subject_type = LV_SUBJECT_TYPE_INT;
    }
#if LV_USE_FLOAT
    else if(lv_streq(state->tag_name, "lv_obj-subject_set_float_event") ||
            lv_streq(state->tag_name, "subject_set_float_event")) {
        subject_type = LV_SUBJECT_TYPE_FLOAT;
    }
#endif
    else if(lv_streq(state->tag_name, "lv_obj-subject_set_string_event") ||
            lv_streq(state->tag_name, "subject_set_string_event")) {
        subject_type = LV_SUBJECT_TYPE_STRING;
    }
    else {
        LV_LOG_WARN("`%s` is not supported in <lv_obj-subject_set_event>", state->tag_name);
        return;
    }

    const char * subject_str =  lv_xml_get_value_of(attrs, "subject");
    const char * trigger_str =  lv_xml_get_value_of(attrs, "trigger");
    const char * value_str =  lv_xml_get_value_of(attrs, "value");

    if(subject_str == NULL) {
        LV_LOG_WARN("`subject` is missing in <lv_obj-subject_set_event>");
        return;
    }

    if(value_str == NULL) {
        LV_LOG_WARN("`value` is missing in <lv_obj-subject_set_event>");
        return;
    }

    lv_event_code_t trigger = LV_EVENT_CLICKED;
    if(trigger_str) trigger = lv_xml_trigger_text_to_enum_value(trigger_str);
    if(trigger == LV_EVENT_LAST)  {
        LV_LOG_WARN("Couldn't apply <subject_set_event> because `%s` trigger is invalid.", trigger_str);
        return;
    }

    lv_subject_t * subject = lv_xml_get_subject(&state->scope, subject_str);
    if(subject == NULL) {
        LV_LOG_WARN("Subject `%s` doesn't exist in <lv_obj-subject_set>", subject_str);
        return;
    }

    if(subject->type != subject_type) {
        LV_LOG_WARN("`%s` subject has incorrect type in <lv_obj-subject_set>", subject_str);
        return;
    }

    void * item = lv_xml_state_get_item(state);
    if(subject_type == LV_SUBJECT_TYPE_INT) {
        lv_obj_add_subject_set_int_event(item, subject, trigger, lv_xml_atoi(value_str));
    }
    else if(subject_type == LV_SUBJECT_TYPE_FLOAT) {
#if LV_USE_FLOAT
        lv_obj_add_subject_set_float_event(item, subject, trigger, lv_xml_atof(value_str));
#else
        LV_LOG_ERROR("Tried to add a subject of type float but LV_USE_FLOAT is not enabled");
#endif
    }
    else if(subject_type == LV_SUBJECT_TYPE_STRING) {
        lv_obj_add_subject_set_string_event(item, subject, trigger, value_str);
    }
}

void * lv_obj_xml_subject_increment_create(lv_xml_parser_state_t * state, const char ** attrs)
{
    LV_UNUSED(attrs);
    void * item = lv_xml_state_get_parent(state);
    return item;
}

void lv_obj_xml_subject_increment_apply(lv_xml_parser_state_t * state, const char ** attrs)
{
    const char * subject_str =  lv_xml_get_value_of(attrs, "subject");
    const char * trigger_str =  lv_xml_get_value_of(attrs, "trigger");
    const char * step_str =  lv_xml_get_value_of(attrs, "step");
    const char * min_value_str =  lv_xml_get_value_of(attrs, "min_value");
    const char * max_value_str =  lv_xml_get_value_of(attrs, "max_value");
    const char * rollover_str =  lv_xml_get_value_of(attrs, "rollover");

    if(subject_str == NULL) {
        LV_LOG_WARN("`subject` is missing in <lv_obj-subject_increment>");
        return;
    }

    if(step_str == NULL) step_str = "1";
    if(rollover_str == NULL) rollover_str = "false";

    lv_event_code_t trigger = LV_EVENT_CLICKED;
    if(trigger_str) trigger = lv_xml_trigger_text_to_enum_value(trigger_str);
    if(trigger == LV_EVENT_LAST)  {
        LV_LOG_WARN("Couldn't apply <lv_obj-subject_increment> because `%s` trigger is invalid.", trigger_str);
        return;
    }

    lv_subject_t * subject = lv_xml_get_subject(&state->scope, subject_str);
    if(subject == NULL) {
        LV_LOG_WARN("Subject `%s` doesn't exist in <lv_obj-subject_increment>", subject_str);
        return;
    }

    if(subject->type != LV_SUBJECT_TYPE_INT && subject->type != LV_SUBJECT_TYPE_FLOAT) {
        LV_LOG_WARN("`%s` subject should have integer type in <lv_obj-subject_increment>", subject_str);
        return;
    }

    void * item = lv_xml_state_get_item(state);

    int32_t step = lv_xml_atoi(step_str);
    lv_subject_increment_dsc_t * dsc = lv_obj_add_subject_increment_event(item, subject, trigger, step);

    if(min_value_str) lv_obj_set_subject_increment_event_min_value(item, dsc, lv_xml_atoi(min_value_str));
    if(max_value_str) lv_obj_set_subject_increment_event_max_value(item, dsc, lv_xml_atoi(max_value_str));
    if(rollover_str) lv_obj_set_subject_increment_event_rollover(item, dsc, lv_xml_to_bool(rollover_str));
}

void * lv_obj_xml_bind_style_create(lv_xml_parser_state_t * state, const char ** attrs)
{
    LV_UNUSED(attrs);
    void * item = lv_xml_state_get_parent(state);
    return item;
}

/* Parse a `parts="main,indicator,knob"` comma-list into up to `max` part
 * selectors. State bits (focused/pressed/etc) parsed from the existing
 * `selector` attr are OR'd into each part. Returns the count parsed; 0
 * means "no parts attr — caller should fall back to single-selector mode". */
static size_t lv_xml_parse_parts_attr(const char * parts_str,
                                      lv_style_selector_t state_bits,
                                      lv_style_selector_t * out,
                                      size_t max)
{
    if(parts_str == NULL || parts_str[0] == '\0') return 0;
    char buf[128];
    lv_strncpy(buf, parts_str, sizeof(buf));
    char * bufp = buf;
    size_t n = 0;
    const char * tok = lv_xml_split_str(&bufp, ',');
    while(tok && n < max) {
        while(*tok == ' ') tok++; /* trim leading whitespace */
        lv_part_t part = lv_xml_style_part_to_enum(tok);
        out[n++] = (lv_style_selector_t)part | state_bits;
        tok = lv_xml_split_str(&bufp, ',');
    }
    return n;
}

void lv_obj_xml_bind_style_apply(lv_xml_parser_state_t * state, const char ** attrs)
{
    const char * name = lv_xml_get_value_of(attrs, "name");
    if(name == NULL) {
        /*Silently ignore this issue.
         *The name set to NULL if there there was no default value when resolving params*/
        return;
    }
    lv_xml_style_t * xml_style = lv_xml_get_style_by_name(&state->scope, name);
    if(xml_style == NULL) {
        LV_LOG_WARN("`%s` style is not found", name);
        return;
    }
    const char * subject_str = lv_xml_get_value_of(attrs, "subject");

    if(subject_str == NULL) {
        LV_LOG_WARN("`subject` is missing in lv_obj bind_style");
        return;
    }

    lv_subject_t * subject = lv_xml_get_subject(&state->scope, subject_str);
    if(subject == NULL) {
        LV_LOG_WARN("Subject `%s` doesn't exist in lv_obj bind_style", subject_str);
        return;
    }

    const char * ref_value_str = lv_xml_get_value_of(attrs, "ref_value");
    if(ref_value_str == NULL) {
        LV_LOG_WARN("`ref_value` is missing in lv_obj bind_style");
        return;
    }

    int32_t ref_value = lv_xml_atoi(ref_value_str);

    const char * selector_str = lv_xml_get_value_of(attrs, "selector");
    lv_style_selector_t selector = lv_xml_style_selector_text_to_enum(selector_str);

    void * item = lv_xml_state_get_parent(state);

    /* `parts="main,indicator"` — apply same style to multiple parts in one
     * element. State bits from the existing `selector` attr are preserved.
     * Halves the line count for multi-part widgets (arc bg+indicator,
     * slider bg+indicator+knob, etc). */
    const char * parts_str = lv_xml_get_value_of(attrs, "parts");
    lv_style_selector_t parts[8];
    /* state bits live in the low 16 — preserve them across each part */
    size_t n_parts = lv_xml_parse_parts_attr(parts_str, selector & 0xFFFF, parts, 8);
    if(n_parts > 0) {
        for(size_t i = 0; i < n_parts; i++) {
            lv_obj_bind_style(item, &xml_style->style, parts[i], subject, ref_value);
        }
    } else {
        lv_obj_bind_style(item, &xml_style->style, selector, subject, ref_value);
    }
}

/* Comparison operators for bind_style_if_* */
typedef enum {
    BIND_STYLE_CMP_EQ,
    BIND_STYLE_CMP_NOT_EQ,
    BIND_STYLE_CMP_GT,
    BIND_STYLE_CMP_GE,
    BIND_STYLE_CMP_LT,
    BIND_STYLE_CMP_LE,
} bind_style_cmp_t;

typedef struct {
    const lv_style_t * style;
    lv_style_selector_t selector;
    int32_t value;
    bind_style_cmp_t cmp;
} bind_style_cmp_data_t;

static void bind_style_cmp_observer_cb(lv_observer_t * observer, lv_subject_t * subject)
{
    bind_style_cmp_data_t * p = observer->user_data;
    int32_t v = lv_subject_get_int(subject);
    bool match = false;
    switch(p->cmp) {
        case BIND_STYLE_CMP_EQ:     match = (v == p->value); break;
        case BIND_STYLE_CMP_NOT_EQ: match = (v != p->value); break;
        case BIND_STYLE_CMP_GT:     match = (v >  p->value); break;
        case BIND_STYLE_CMP_GE:     match = (v >= p->value); break;
        case BIND_STYLE_CMP_LT:     match = (v <  p->value); break;
        case BIND_STYLE_CMP_LE:     match = (v <= p->value); break;
    }
    /* Style is disabled when comparison does NOT match */
    lv_obj_style_set_disabled(observer->target, p->style, p->selector, !match);
}

void * lv_obj_xml_bind_style_cmp_create(lv_xml_parser_state_t * state, const char ** attrs)
{
    LV_UNUSED(attrs);
    void * item = lv_xml_state_get_parent(state);
    return item;
}

void lv_obj_xml_bind_style_cmp_apply(lv_xml_parser_state_t * state, const char ** attrs)
{
    const char * op = state->tag_name;

    /*If starts with "lv_obj-" skip that part*/
    if(op[0] == 'l') op += 7;

    bind_style_cmp_t cmp;
    if(lv_streq(op, "bind_style_if_eq")) cmp = BIND_STYLE_CMP_EQ;
    else if(lv_streq(op, "bind_style_if_not_eq")) cmp = BIND_STYLE_CMP_NOT_EQ;
    else if(lv_streq(op, "bind_style_if_gt")) cmp = BIND_STYLE_CMP_GT;
    else if(lv_streq(op, "bind_style_if_ge")) cmp = BIND_STYLE_CMP_GE;
    else if(lv_streq(op, "bind_style_if_lt")) cmp = BIND_STYLE_CMP_LT;
    else if(lv_streq(op, "bind_style_if_le")) cmp = BIND_STYLE_CMP_LE;
    else {
        LV_LOG_WARN("`%s` is not a known bind_style comparison", op);
        return;
    }

    const char * name = lv_xml_get_value_of(attrs, "name");
    if(name == NULL) {
        /*Silently ignore this issue.
         *The name set to NULL if there there was no default value when resolving params*/
        return;
    }

    lv_xml_style_t * xml_style = lv_xml_get_style_by_name(&state->scope, name);
    if(xml_style == NULL) {
        LV_LOG_WARN("`%s` style is not found", name);
        return;
    }

    const char * subject_str = lv_xml_get_value_of(attrs, "subject");
    if(subject_str == NULL) {
        LV_LOG_WARN("`subject` is missing in bind_style_if_*");
        return;
    }

    lv_subject_t * subject = lv_xml_get_subject(&state->scope, subject_str);
    if(subject == NULL) {
        LV_LOG_WARN("Subject `%s` doesn't exist in bind_style_if_*", subject_str);
        return;
    }

    const char * ref_value_str = lv_xml_get_value_of(attrs, "ref_value");
    if(ref_value_str == NULL) {
        LV_LOG_WARN("`ref_value` is missing in bind_style_if_*");
        return;
    }

    int32_t ref_value = lv_xml_atoi(ref_value_str);

    const char * selector_str = lv_xml_get_value_of(attrs, "selector");
    lv_style_selector_t selector = lv_xml_style_selector_text_to_enum(selector_str);

    void * item = lv_xml_state_get_parent(state);

    /* `parts="main,indicator"` — apply same style + observer to multiple
     * parts in one element. Each part gets its own observer because
     * lv_obj_style_set_disabled operates on (style, selector) pairs. */
    const char * parts_str = lv_xml_get_value_of(attrs, "parts");
    lv_style_selector_t parts[8];
    size_t n_parts = lv_xml_parse_parts_attr(parts_str, selector & 0xFFFF, parts, 8);

    size_t count = (n_parts > 0) ? n_parts : 1;
    for(size_t i = 0; i < count; i++) {
        lv_style_selector_t sel = (n_parts > 0) ? parts[i] : selector;

        /* Add the style (starts enabled, observer will immediately set correct state) */
        lv_obj_add_style(item, &xml_style->style, sel);

        bind_style_cmp_data_t * p = lv_malloc(sizeof(bind_style_cmp_data_t));
        LV_ASSERT_MALLOC(p);
        if(p == NULL) return;

        p->style = &xml_style->style;
        p->selector = sel;
        p->value = ref_value;
        p->cmp = cmp;

        lv_observer_t * obs = lv_subject_add_observer_obj(subject, bind_style_cmp_observer_cb, item, p);
        obs->auto_free_user_data = 1;
    }
}

void * lv_obj_xml_bind_style_prop_create(lv_xml_parser_state_t * state, const char ** attrs)
{
    LV_UNUSED(attrs);
    void * item = lv_xml_state_get_parent(state);
    return item;
}

void lv_obj_xml_bind_style_prop_apply(lv_xml_parser_state_t * state, const char ** attrs)
{
    const char * prop_str = lv_xml_get_value_of(attrs, "prop");
    if(prop_str == NULL) {
        LV_LOG_WARN("`prop` is missing in lv_obj bind_style_prop");
        return;
    }
    lv_style_prop_t prop = lv_xml_style_prop_to_enum(prop_str);

    const char * subject_str = lv_xml_get_value_of(attrs, "subject");

    if(subject_str == NULL) {
        LV_LOG_WARN("`subject` is missing in lv_obj bind_style_prop");
        return;
    }

    lv_subject_t * subject = lv_xml_get_subject(&state->scope, subject_str);
    if(subject == NULL) {
        LV_LOG_WARN("Subject `%s` doesn't exist in lv_obj bind_style_prop", subject_str);
        return;
    }

    const char * selector_str = lv_xml_get_value_of(attrs, "selector");
    lv_style_selector_t selector = lv_xml_style_selector_text_to_enum(selector_str);

    void * item = lv_xml_state_get_parent(state);
    lv_obj_bind_style_prop(item, prop, selector, subject);
}

void * lv_obj_xml_bind_flag_create(lv_xml_parser_state_t * state, const char ** attrs)
{
    LV_UNUSED(attrs);
    void * item = lv_xml_state_get_parent(state);
    return item;
}

void lv_obj_xml_bind_flag_apply(lv_xml_parser_state_t * state, const char ** attrs)
{
    const char * op = state->tag_name;

    /*If starts with "lv_obj-" skip that part*/
    if(op[0] == 'l') op += 7;

    lv_observer_t * (*cb)(lv_obj_t * obj, lv_subject_t * subject, lv_obj_flag_t flag, int32_t ref_value) = NULL;
    if(lv_streq(op, "bind_flag_if_eq")) cb = lv_obj_bind_flag_if_eq;
    else if(lv_streq(op, "bind_flag_if_not_eq")) cb = lv_obj_bind_flag_if_not_eq;
    else if(lv_streq(op, "bind_flag_if_gt")) cb = lv_obj_bind_flag_if_gt;
    else if(lv_streq(op, "bind_flag_if_ge")) cb = lv_obj_bind_flag_if_ge;
    else if(lv_streq(op, "bind_flag_if_lt")) cb = lv_obj_bind_flag_if_lt;
    else if(lv_streq(op, "bind_flag_if_le")) cb = lv_obj_bind_flag_if_le;
    else {
        LV_LOG_WARN("`%s` is not known", op);
        return;
    }

    const char * subject_str =  lv_xml_get_value_of(attrs, "subject");
    const char * flag_str =  lv_xml_get_value_of(attrs, "flag");
    const char * ref_value_str = lv_xml_get_value_of(attrs, "ref_value");

    if(subject_str == NULL) {
        LV_LOG_WARN("`subject` is missing in lv_obj bind_flag");
    }
    else if(subject_str[0] == '\0') {
        /* Empty subject = intentional "no binding" (an optional subject left at
         * its empty default). Skip silently so the binding never installs and
         * clobbers a static flag such as hidden="true". */
    }
    else if(flag_str == NULL) {
        LV_LOG_WARN("`flag` is missing in lv_obj bind_flag");
    }
    else if(ref_value_str == NULL) {
        LV_LOG_WARN("`ref_value` is missing in lv_obj bind_flag");
    }
    else {
        lv_subject_t * subject = lv_xml_get_subject(&state->scope, subject_str);
        if(subject == NULL) {
            LV_LOG_WARN("Subject `%s` doesn't exist in lv_obj bind_flag", subject_str);
        }
        else {
            lv_obj_flag_t flag = flag_to_enum(flag_str);
            int32_t ref_value = lv_xml_atoi(ref_value_str);
            void * item = lv_xml_state_get_item(state);
            cb(item, subject, flag, ref_value);
        }
    }
}

/* Resolver shim: <bind_flag_if cond="..."> looks up subjects referenced by
 * the expression in the enclosing component's scope, same as every other
 * expression-consuming tag (see subject_expr_resolver in lv_xml_component.c). */
static lv_subject_t * cond_flag_scope_resolver(void * ctx, const char * name)
{
    return lv_xml_get_subject((lv_xml_component_scope_t *)ctx, name);
}

typedef struct {
    lv_obj_t * obj;
    lv_obj_flag_t flag;
    bool invert;
} cond_flag_ctx_t;

/* lv_xml_expr_bind callback: fires once immediately at bind and again on any
 * referenced-subject change. `value` is the freshly-evaluated cond result. */
static void cond_flag_cb(void * user_data, int32_t value)
{
    cond_flag_ctx_t * c = (cond_flag_ctx_t *)user_data;
    bool on = value != 0;
    if(c->invert) on = !on;
    if(on) lv_obj_add_flag(c->obj, c->flag);
    else lv_obj_remove_flag(c->obj, c->flag);
}

/* `lv_xml_expr_bind` owns and frees the compiled expression on `owner`
 * delete, but it does not own `user_data` -- free the ctx ourselves on the
 * same LV_EVENT_DELETE. */
static void free_cond_flag_ctx_cb(lv_event_t * e)
{
    cond_flag_ctx_t * c = (cond_flag_ctx_t *)lv_event_get_user_data(e);
    lv_free(c);
}

void * lv_obj_xml_bind_flag_if_create(lv_xml_parser_state_t * state, const char ** attrs)
{
    LV_UNUSED(attrs);
    void * item = lv_xml_state_get_parent(state);
    return item;
}

/**
 * `<bind_flag_if cond="EXPR" flag="FLAGNAME" invert="true|false"/>`: reactive
 * flag binding driven by the expression evaluator instead of a single
 * subject/ref_value comparison. `flag` is added when `cond` is truthy and
 * removed when falsy; `invert="true"` flips that (apply when falsy) -- the
 * common case for `hidden` where the markup wants to express "show when
 * cond" rather than "hide when cond".
 */
void lv_obj_xml_bind_flag_if_apply(lv_xml_parser_state_t * state, const char ** attrs)
{
    const char * cond = lv_xml_get_value_of(attrs, "cond");
    const char * flag_str = lv_xml_get_value_of(attrs, "flag");
    if(cond == NULL) {
        LV_LOG_WARN("`cond` is missing in bind_flag_if");
        return;
    }
    if(flag_str == NULL) {
        LV_LOG_WARN("`flag` is missing in bind_flag_if");
        return;
    }

    lv_obj_flag_t flag = flag_to_enum(flag_str);
    lv_obj_t * item = lv_xml_state_get_parent(state);

    lv_xml_expr_t * expr = lv_xml_expr_compile(cond, cond_flag_scope_resolver, &state->scope);
    if(expr == NULL) {
        LV_LOG_WARN("bind_flag_if: failed to compile cond '%s'", cond);
        return;
    }

    cond_flag_ctx_t * c = lv_malloc(sizeof(cond_flag_ctx_t));
    LV_ASSERT_MALLOC(c);
    if(c == NULL) {
        lv_xml_expr_free(expr);
        return;
    }
    c->obj = item;
    c->flag = flag;
    const char * invert_str = lv_xml_get_value_of(attrs, "invert");
    c->invert = invert_str && (lv_streq(invert_str, "true") || lv_streq(invert_str, "1"));

    lv_xml_expr_bind(expr, item, cond_flag_cb, c);
    lv_obj_add_event_cb(item, free_cond_flag_ctx_cb, LV_EVENT_DELETE, c);
}

void * lv_obj_xml_bind_state_create(lv_xml_parser_state_t * state, const char ** attrs)
{
    LV_UNUSED(attrs);
    void * item = lv_xml_state_get_parent(state);
    return item;
}

void lv_obj_xml_bind_state_apply(lv_xml_parser_state_t * state, const char ** attrs)
{
    const char * op = state->tag_name;

    /*If starts with "lv_obj-" skip that part*/
    if(op[0] == 'l') op += 7;

    lv_observer_t * (*cb)(lv_obj_t * obj, lv_subject_t * subject, lv_state_t flag, int32_t ref_value) = NULL;
    if(lv_streq(op, "bind_state_if_eq")) cb = lv_obj_bind_state_if_eq;
    else if(lv_streq(op, "bind_state_if_not_eq")) cb = lv_obj_bind_state_if_not_eq;
    else if(lv_streq(op, "bind_state_if_gt")) cb = lv_obj_bind_state_if_gt;
    else if(lv_streq(op, "bind_state_if_ge")) cb = lv_obj_bind_state_if_ge;
    else if(lv_streq(op, "bind_state_if_lt")) cb = lv_obj_bind_state_if_lt;
    else if(lv_streq(op, "bind_state_if_le")) cb = lv_obj_bind_state_if_le;
    else {
        LV_LOG_WARN("`%s` is not known", op);
        return;
    }

    const char * subject_str =  lv_xml_get_value_of(attrs, "subject");
    const char * state_str =  lv_xml_get_value_of(attrs, "state");
    const char * ref_value_str = lv_xml_get_value_of(attrs, "ref_value");

    if(subject_str == NULL) {
        LV_LOG_WARN("`subject` is missing in lv_obj state_flag");
    }
    else if(subject_str[0] == '\0') {
        /* Empty subject = intentional "no binding" (an optional subject left at
         * its empty default). Skip silently so the binding never installs and
         * clobbers a static state. */
    }
    else if(state_str == NULL) {
        LV_LOG_WARN("`state` is missing in lv_obj state_flag");
    }
    else if(ref_value_str == NULL) {
        LV_LOG_WARN("`ref_value` is missing in lv_obj state_flag");
    }
    else {
        lv_subject_t * subject = lv_xml_get_subject(&state->scope, subject_str);
        if(subject == NULL) {
            LV_LOG_WARN("Subject `%s` doesn't exist in bind_state", subject_str);
        }
        else {
            lv_state_t s = lv_xml_state_to_enum(state_str);
            int32_t ref_value = lv_xml_atoi(ref_value_str);
            void * item = lv_xml_state_get_item(state);
            cb(item, subject, s, ref_value);
        }
    }
}

typedef struct {
    lv_obj_t * obj;
    lv_state_t state;
    bool invert;
} cond_state_ctx_t;

/* lv_xml_expr_bind callback: fires once immediately at bind and again on any
 * referenced-subject change. `value` is the freshly-evaluated cond result. */
static void cond_state_cb(void * user_data, int32_t value)
{
    cond_state_ctx_t * c = (cond_state_ctx_t *)user_data;
    bool on = value != 0;
    if(c->invert) on = !on;
    if(on) lv_obj_add_state(c->obj, c->state);
    else lv_obj_remove_state(c->obj, c->state);
}

/* `lv_xml_expr_bind` owns and frees the compiled expression on `owner`
 * delete, but it does not own `user_data` -- free the ctx ourselves on the
 * same LV_EVENT_DELETE. */
static void free_cond_state_ctx_cb(lv_event_t * e)
{
    cond_state_ctx_t * c = (cond_state_ctx_t *)lv_event_get_user_data(e);
    lv_free(c);
}

void * lv_obj_xml_bind_state_if_create(lv_xml_parser_state_t * state, const char ** attrs)
{
    LV_UNUSED(attrs);
    void * item = lv_xml_state_get_parent(state);
    return item;
}

/**
 * `<bind_state_if cond="EXPR" state="STATENAME" invert="true|false"/>`:
 * reactive state binding driven by the expression evaluator instead of a
 * single subject/ref_value comparison. `state` is added when `cond` is
 * truthy and removed when falsy; `invert="true"` flips that.
 */
void lv_obj_xml_bind_state_if_apply(lv_xml_parser_state_t * state, const char ** attrs)
{
    const char * cond = lv_xml_get_value_of(attrs, "cond");
    const char * state_str = lv_xml_get_value_of(attrs, "state");
    if(cond == NULL) {
        LV_LOG_WARN("`cond` is missing in bind_state_if");
        return;
    }
    if(state_str == NULL) {
        LV_LOG_WARN("`state` is missing in bind_state_if");
        return;
    }

    lv_state_t st = lv_xml_state_to_enum(state_str);
    lv_obj_t * item = lv_xml_state_get_parent(state);

    lv_xml_expr_t * expr = lv_xml_expr_compile(cond, cond_flag_scope_resolver, &state->scope);
    if(expr == NULL) {
        LV_LOG_WARN("bind_state_if: failed to compile cond '%s'", cond);
        return;
    }

    cond_state_ctx_t * c = lv_malloc(sizeof(cond_state_ctx_t));
    LV_ASSERT_MALLOC(c);
    if(c == NULL) {
        lv_xml_expr_free(expr);
        return;
    }
    c->obj = item;
    c->state = st;
    const char * invert_str = lv_xml_get_value_of(attrs, "invert");
    c->invert = invert_str && (lv_streq(invert_str, "true") || lv_streq(invert_str, "1"));

    lv_xml_expr_bind(expr, item, cond_state_cb, c);
    lv_obj_add_event_cb(item, free_cond_state_ctx_cb, LV_EVENT_DELETE, c);
}

typedef struct {
    lv_obj_t * obj;
    const lv_style_t * style;
    lv_style_selector_t selectors[8];
    size_t n_selectors;
    bool invert;
} cond_style_ctx_t;

/* lv_xml_expr_bind callback: fires once immediately at bind and again on any
 * referenced-subject change. `value` is the freshly-evaluated cond result. */
static void cond_style_cb(void * user_data, int32_t value)
{
    cond_style_ctx_t * c = (cond_style_ctx_t *)user_data;
    bool on = value != 0;
    if(c->invert) on = !on;
    for(size_t i = 0; i < c->n_selectors; i++) {
        lv_obj_style_set_disabled(c->obj, c->style, c->selectors[i], !on);
    }
}

/* `lv_xml_expr_bind` owns and frees the compiled expression on `owner`
 * delete, but it does not own `user_data` -- free the ctx ourselves on the
 * same LV_EVENT_DELETE. */
static void free_cond_style_ctx_cb(lv_event_t * e)
{
    cond_style_ctx_t * c = (cond_style_ctx_t *)lv_event_get_user_data(e);
    lv_free(c);
}

void * lv_obj_xml_bind_style_if_create(lv_xml_parser_state_t * state, const char ** attrs)
{
    LV_UNUSED(attrs);
    void * item = lv_xml_state_get_parent(state);
    return item;
}

/**
 * `<bind_style_if cond="EXPR" name="STYLENAME" selector="..." parts="..."
 * invert="true|false"/>`: reactive style enable/disable driven by the
 * expression evaluator instead of a single subject/ref_value comparison,
 * matching `bind_style_cmp` semantics. The style is added (once) and then
 * enabled when `cond` is truthy, disabled when falsy; `invert="true"` flips
 * that. `parts="main,indicator"` applies the same enable/disable state to
 * multiple parts from a single compiled expression/observer.
 */
void lv_obj_xml_bind_style_if_apply(lv_xml_parser_state_t * state, const char ** attrs)
{
    const char * cond = lv_xml_get_value_of(attrs, "cond");
    if(cond == NULL) {
        LV_LOG_WARN("`cond` is missing in bind_style_if");
        return;
    }

    const char * name = lv_xml_get_value_of(attrs, "name");
    if(name == NULL) {
        /*Silently ignore this issue.
         *The name set to NULL if there there was no default value when resolving params*/
        return;
    }

    lv_xml_style_t * xml_style = lv_xml_get_style_by_name(&state->scope, name);
    if(xml_style == NULL) {
        LV_LOG_WARN("`%s` style is not found", name);
        return;
    }

    const char * selector_str = lv_xml_get_value_of(attrs, "selector");
    lv_style_selector_t selector = lv_xml_style_selector_text_to_enum(selector_str);

    lv_obj_t * item = lv_xml_state_get_parent(state);

    /* `parts="main,indicator"` -- apply the same style + observer to multiple
     * parts in one element, mirroring bind_style_cmp. */
    const char * parts_str = lv_xml_get_value_of(attrs, "parts");
    lv_style_selector_t parts[8];
    size_t n_parts = lv_xml_parse_parts_attr(parts_str, selector & 0xFFFF, parts, 8);

    lv_xml_expr_t * expr = lv_xml_expr_compile(cond, cond_flag_scope_resolver, &state->scope);
    if(expr == NULL) {
        LV_LOG_WARN("bind_style_if: failed to compile cond '%s'", cond);
        return;
    }

    cond_style_ctx_t * c = lv_malloc(sizeof(cond_style_ctx_t));
    LV_ASSERT_MALLOC(c);
    if(c == NULL) {
        lv_xml_expr_free(expr);
        return;
    }
    c->obj = item;
    c->style = &xml_style->style;

    size_t count = (n_parts > 0) ? n_parts : 1;
    for(size_t i = 0; i < count; i++) {
        lv_style_selector_t sel = (n_parts > 0) ? parts[i] : selector;
        c->selectors[i] = sel;
        /* Add the style (starts enabled, observer will immediately set correct state) */
        lv_obj_add_style(item, &xml_style->style, sel);
    }
    c->n_selectors = count;

    const char * invert_str = lv_xml_get_value_of(attrs, "invert");
    c->invert = invert_str && (lv_streq(invert_str, "true") || lv_streq(invert_str, "1"));

    lv_xml_expr_bind(expr, item, cond_style_cb, c);
    lv_obj_add_event_cb(item, free_cond_style_ctx_cb, LV_EVENT_DELETE, c);
}

void * lv_obj_xml_screen_load_event_create(lv_xml_parser_state_t * state, const char ** attrs)
{
    LV_UNUSED(attrs);
    void * item = lv_xml_state_get_parent(state);
    return item;
}

void lv_obj_xml_screen_load_event_apply(lv_xml_parser_state_t * state, const char ** attrs)
{
    const char * screen_str = lv_xml_get_value_of(attrs, "screen");
    const char * duration_str = lv_xml_get_value_of(attrs, "duration");
    const char * delay_str = lv_xml_get_value_of(attrs, "delay");
    const char * anim_type_str = lv_xml_get_value_of(attrs, "anim_type");
    const char * trigger_str = lv_xml_get_value_of(attrs, "trigger");

    if(screen_str == NULL) {
        LV_LOG_WARN("`screen` is missing in <lv_obj-screen_load_event>");
        return;
    }

    if(duration_str == NULL) duration_str = "0";
    if(delay_str == NULL) delay_str = "0";
    if(anim_type_str == NULL) anim_type_str = "none";
    if(trigger_str == NULL) trigger_str = "clicked";

    lv_event_code_t trigger = lv_xml_trigger_text_to_enum_value(trigger_str);
    if(trigger == LV_EVENT_LAST)  {
        LV_LOG_WARN("Couldn't apply <screen_load_event> because `%s` trigger is invalid.", trigger_str);
        return;
    }

    int32_t duration = lv_xml_atoi(duration_str);
    int32_t delay = lv_xml_atoi(delay_str);
    lv_screen_load_anim_t anim_type = lv_xml_screen_load_anim_text_to_enum_value(anim_type_str);

    void * item = lv_xml_state_get_item(state);

    screen_load_anim_dsc_t * dsc = lv_malloc(sizeof(screen_load_anim_dsc_t));
    LV_ASSERT_MALLOC(dsc);
    lv_memzero(dsc, sizeof(screen_load_anim_dsc_t));
    dsc->anim_type = anim_type;
    dsc->duration = duration;
    dsc->delay = delay;
    dsc->screen_name = lv_strdup(screen_str);

    lv_obj_add_event_cb(item, screen_load_on_trigger_event_cb, trigger, dsc);
    lv_obj_add_event_cb(item, free_screen_create_user_data_on_delete_event_cb, LV_EVENT_DELETE, dsc);
}


void * lv_obj_xml_screen_create_event_create(lv_xml_parser_state_t * state, const char ** attrs)
{
    LV_UNUSED(attrs);
    void * item = lv_xml_state_get_parent(state);
    return item;
}

void lv_obj_xml_screen_create_event_apply(lv_xml_parser_state_t * state, const char ** attrs)
{
    const char * screen_str = lv_xml_get_value_of(attrs, "screen");
    const char * duration_str = lv_xml_get_value_of(attrs, "duration");
    const char * delay_str = lv_xml_get_value_of(attrs, "delay");
    const char * anim_type_str = lv_xml_get_value_of(attrs, "anim_type");
    const char * trigger_str = lv_xml_get_value_of(attrs, "trigger");

    if(screen_str == NULL) {
        LV_LOG_WARN("`screen` is missing in <lv_obj-screen_load_event>");
        return;
    }

    if(duration_str == NULL) duration_str = "0";
    if(delay_str == NULL) delay_str = "0";
    if(anim_type_str == NULL) anim_type_str = "none";
    if(trigger_str == NULL) trigger_str = "clicked";

    lv_event_code_t trigger = lv_xml_trigger_text_to_enum_value(trigger_str);
    if(trigger == LV_EVENT_LAST)  {
        LV_LOG_WARN("Couldn't apply <screen_load_event> because `%s` trigger is invalid.", trigger_str);
        return;
    }

    int32_t duration = lv_xml_atoi(duration_str);
    int32_t delay = lv_xml_atoi(delay_str);
    lv_screen_load_anim_t anim_type = lv_xml_screen_load_anim_text_to_enum_value(anim_type_str);

    screen_load_anim_dsc_t * dsc = lv_malloc(sizeof(screen_load_anim_dsc_t));
    LV_ASSERT_MALLOC(dsc);
    lv_memzero(dsc, sizeof(screen_load_anim_dsc_t));
    dsc->anim_type = anim_type;
    dsc->duration = duration;
    dsc->delay = delay;
    dsc->screen_name = lv_strdup(screen_str);

    void * item = lv_xml_state_get_item(state);
    lv_obj_add_event_cb(item, screen_create_on_trigger_event_cb, trigger, dsc);
    lv_obj_add_event_cb(item, free_screen_create_user_data_on_delete_event_cb, LV_EVENT_DELETE, dsc);
}

void * lv_obj_xml_play_timeline_event_create(lv_xml_parser_state_t * state, const char ** attrs)
{
    LV_UNUSED(attrs);
    void * item = lv_xml_state_get_parent(state);
    return item;
}

void lv_obj_xml_play_timeline_event_apply(lv_xml_parser_state_t * state, const char ** attrs)
{

    if(state->view == NULL) {
        /*Shouldn't happen*/
        LV_LOG_WARN("view is not set, can't add the event");
        return;
    }

    const char * target_str = lv_xml_get_value_of(attrs, "target");
    const char * delay_str = lv_xml_get_value_of(attrs, "delay");
    const char * trigger_str = lv_xml_get_value_of(attrs, "trigger");
    const char * timeline_str = lv_xml_get_value_of(attrs, "timeline");
    const char * reverse_str = lv_xml_get_value_of(attrs, "reverse");

    if(target_str == NULL) {
        LV_LOG_WARN("`target` is missing in <lv_obj-play_animation_event>");
        return;
    }

    if(timeline_str == NULL) {
        LV_LOG_WARN("`timeline` is missing in <lv_obj-play_animation_event>");
        return;
    }

    if(delay_str == NULL) delay_str = "0";
    if(trigger_str == NULL) trigger_str = "clicked";
    if(reverse_str == NULL) reverse_str = "false";

    lv_event_code_t trigger = lv_xml_trigger_text_to_enum_value(trigger_str);
    if(trigger == LV_EVENT_LAST)  {
        LV_LOG_WARN("Couldn't apply <screen_load_event> because `%s` trigger is invalid.", trigger_str);
        return;
    }

    play_anim_dsc_t * dsc = lv_malloc(sizeof(play_anim_dsc_t));
    LV_ASSERT_MALLOC(dsc);
    lv_memzero(dsc, sizeof(play_anim_dsc_t));
    dsc->target_name = lv_strdup(target_str);
    dsc->timeline_name = lv_strdup(timeline_str);
    dsc->delay = lv_xml_atoi(delay_str);
    dsc->reverse = lv_xml_to_bool(reverse_str);
    dsc->base_obj = state->view;

    void * item = lv_xml_state_get_item(state);
    lv_obj_add_event_cb(item, play_anim_on_trigger_event_cb, trigger, dsc);
    lv_obj_add_event_cb(item, free_play_anim_user_data_on_delete_event_cb, LV_EVENT_DELETE, dsc);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static lv_obj_flag_t flag_to_enum(const char * txt)
{
    if(lv_streq("hidden", txt)) return LV_OBJ_FLAG_HIDDEN;
    if(lv_streq("clickable", txt)) return LV_OBJ_FLAG_CLICKABLE;
    if(lv_streq("click_focusable", txt)) return LV_OBJ_FLAG_CLICK_FOCUSABLE;
    if(lv_streq("checkable", txt)) return LV_OBJ_FLAG_CHECKABLE;
    if(lv_streq("scrollable", txt)) return LV_OBJ_FLAG_SCROLLABLE;
    if(lv_streq("scroll_elastic", txt)) return LV_OBJ_FLAG_SCROLL_ELASTIC;
    if(lv_streq("scroll_momentum", txt)) return LV_OBJ_FLAG_SCROLL_MOMENTUM;
    if(lv_streq("scroll_one", txt)) return LV_OBJ_FLAG_SCROLL_ONE;
    if(lv_streq("scroll_chain_hor", txt)) return LV_OBJ_FLAG_SCROLL_CHAIN_HOR;
    if(lv_streq("scroll_chain_ver", txt)) return LV_OBJ_FLAG_SCROLL_CHAIN_VER;
    if(lv_streq("scroll_chain", txt)) return LV_OBJ_FLAG_SCROLL_CHAIN;
    if(lv_streq("scroll_on_focus", txt)) return LV_OBJ_FLAG_SCROLL_ON_FOCUS;
    if(lv_streq("scroll_with_arrow", txt)) return LV_OBJ_FLAG_SCROLL_WITH_ARROW;
    if(lv_streq("snappable", txt)) return LV_OBJ_FLAG_SNAPPABLE;
    if(lv_streq("press_lock", txt)) return LV_OBJ_FLAG_PRESS_LOCK;
    if(lv_streq("event_bubble", txt)) return LV_OBJ_FLAG_EVENT_BUBBLE;
    if(lv_streq("event_trickle", txt)) return LV_OBJ_FLAG_EVENT_TRICKLE;
    if(lv_streq("state_trickle", txt)) return LV_OBJ_FLAG_STATE_TRICKLE;
    if(lv_streq("gesture_bubble", txt)) return LV_OBJ_FLAG_GESTURE_BUBBLE;
    if(lv_streq("adv_hittest", txt)) return LV_OBJ_FLAG_ADV_HITTEST;
    if(lv_streq("ignore_layout", txt)) return LV_OBJ_FLAG_IGNORE_LAYOUT;
    if(lv_streq("floating", txt)) return LV_OBJ_FLAG_FLOATING;
    if(lv_streq("send_draw_task_events", txt)) return LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS;
    if(lv_streq("overflow_visible", txt)) return LV_OBJ_FLAG_OVERFLOW_VISIBLE;
#ifdef LV_OBJ_FLAG_RADIO_BUTTON
    if(lv_streq("radio_button", txt)) return LV_OBJ_FLAG_RADIO_BUTTON;
#endif
    if(lv_streq("flex_in_new_track", txt)) return LV_OBJ_FLAG_FLEX_IN_NEW_TRACK;
    if(lv_streq("layout_1", txt)) return LV_OBJ_FLAG_LAYOUT_1;
    if(lv_streq("layout_2", txt)) return LV_OBJ_FLAG_LAYOUT_2;
    if(lv_streq("widget_1", txt)) return LV_OBJ_FLAG_WIDGET_1;
    if(lv_streq("widget_2", txt)) return LV_OBJ_FLAG_WIDGET_2;
    if(lv_streq("user_1", txt)) return LV_OBJ_FLAG_USER_1;
    if(lv_streq("user_2", txt)) return LV_OBJ_FLAG_USER_2;

    LV_LOG_WARN("%s is an unknown value for flag", txt);
    return 0; /*Return 0 in lack of a better option. */
}


static void apply_styles(lv_xml_parser_state_t * state, lv_obj_t * obj, const char * name, const char * value)
{
    char name_local[512];
    lv_strlcpy(name_local, name, sizeof(name_local));

    lv_style_selector_t selector;
    const char * prop_name = lv_xml_style_string_process(name_local, &selector);

    SET_STYLE_IF(width, lv_xml_to_size(value));
    else SET_STYLE_IF(min_width, lv_xml_to_size(value));
    else SET_STYLE_IF(max_width, lv_xml_to_size(value));
    else SET_STYLE_IF(height, lv_xml_to_size(value));
    else SET_STYLE_IF(min_height, lv_xml_to_size(value));
    else SET_STYLE_IF(max_height, lv_xml_to_size(value));
    else SET_STYLE_IF(length, lv_xml_to_size(value));
    else SET_STYLE_IF(radius, lv_xml_to_size(value));
    else SET_STYLE_IF(radial_offset, lv_xml_atoi(value));
    else SET_STYLE_IF(align, lv_xml_align_to_enum(value));

    else SET_STYLE_IF(pad_left, lv_xml_atoi(value));
    else SET_STYLE_IF(pad_right, lv_xml_atoi(value));
    else SET_STYLE_IF(pad_top, lv_xml_atoi(value));
    else SET_STYLE_IF(pad_bottom, lv_xml_atoi(value));
    else SET_STYLE_IF(pad_hor, lv_xml_atoi(value));
    else SET_STYLE_IF(pad_ver, lv_xml_atoi(value));
    else SET_STYLE_IF(pad_all, lv_xml_atoi(value));
    else SET_STYLE_IF(pad_row, lv_xml_atoi(value));
    else SET_STYLE_IF(pad_column, lv_xml_atoi(value));
    else SET_STYLE_IF(pad_gap, lv_xml_atoi(value));
    else SET_STYLE_IF(pad_radial, lv_xml_atoi(value));

    else SET_STYLE_IF(margin_left, lv_xml_atoi(value));
    else SET_STYLE_IF(margin_right, lv_xml_atoi(value));
    else SET_STYLE_IF(margin_top, lv_xml_atoi(value));
    else SET_STYLE_IF(margin_bottom, lv_xml_atoi(value));
    else SET_STYLE_IF(margin_hor, lv_xml_atoi(value));
    else SET_STYLE_IF(margin_ver, lv_xml_atoi(value));
    else SET_STYLE_IF(margin_all, lv_xml_atoi(value));

    else SET_STYLE_IF(base_dir, lv_xml_base_dir_to_enum(value));
    else SET_STYLE_IF(clip_corner, lv_xml_to_bool(value));

    else SET_STYLE_IF(bg_opa, lv_xml_to_opa(value));
    else SET_STYLE_IF(bg_color, lv_xml_to_color(value));
    else SET_STYLE_IF(bg_grad_dir, lv_xml_grad_dir_to_enum(value));
    else SET_STYLE_IF(bg_grad_color, lv_xml_to_color(value));
    else SET_STYLE_IF(bg_main_stop, lv_xml_atoi(value));
    else SET_STYLE_IF(bg_grad_stop, lv_xml_atoi(value));
    else SET_STYLE_IF(bg_grad, lv_xml_component_get_grad(&state->scope, value));

    else SET_STYLE_IF(bg_image_src, lv_xml_get_image(&state->scope, value));
    else SET_STYLE_IF(bg_image_tiled, lv_xml_to_bool(value));
    else SET_STYLE_IF(bg_image_recolor, lv_xml_to_color(value));
    else SET_STYLE_IF(bg_image_recolor_opa, lv_xml_to_opa(value));

    else SET_STYLE_IF(border_color, lv_xml_to_color(value));
    else SET_STYLE_IF(border_width, lv_xml_atoi(value));
    else SET_STYLE_IF(border_opa, lv_xml_to_opa(value));
    else SET_STYLE_IF(border_side, lv_xml_border_side_to_enum(value));
    else SET_STYLE_IF(border_post, lv_xml_to_bool(value));

    else SET_STYLE_IF(outline_color, lv_xml_to_color(value));
    else SET_STYLE_IF(outline_width, lv_xml_atoi(value));
    else SET_STYLE_IF(outline_opa, lv_xml_to_opa(value));
    else SET_STYLE_IF(outline_pad, lv_xml_atoi(value));

    else SET_STYLE_IF(shadow_width, lv_xml_atoi(value));
    else SET_STYLE_IF(shadow_color, lv_xml_to_color(value));
    else SET_STYLE_IF(shadow_offset_x, lv_xml_atoi(value));
    else SET_STYLE_IF(shadow_offset_y, lv_xml_atoi(value));
    else SET_STYLE_IF(shadow_spread, lv_xml_atoi(value));
    else SET_STYLE_IF(shadow_opa, lv_xml_to_opa(value));

    else SET_STYLE_IF(text_color, lv_xml_to_color(value));
    else SET_STYLE_IF(text_font, lv_xml_get_font(&state->scope, value));
    else SET_STYLE_IF(text_opa, lv_xml_to_opa(value));
    else SET_STYLE_IF(text_align, lv_xml_text_align_to_enum(value));
    else SET_STYLE_IF(text_letter_space, lv_xml_atoi(value));
    else SET_STYLE_IF(text_line_space, lv_xml_atoi(value));
    else SET_STYLE_IF(text_decor, lv_xml_text_decor_to_enum(value));

    else SET_STYLE_IF(image_opa, lv_xml_to_opa(value));
    else SET_STYLE_IF(image_recolor, lv_xml_to_color(value));
    else SET_STYLE_IF(image_recolor_opa, lv_xml_to_opa(value));

    else SET_STYLE_IF(line_color, lv_xml_to_color(value));
    else SET_STYLE_IF(line_opa, lv_xml_to_opa(value));
    else SET_STYLE_IF(line_width, lv_xml_atoi(value));
    else SET_STYLE_IF(line_dash_width, lv_xml_atoi(value));
    else SET_STYLE_IF(line_dash_gap, lv_xml_atoi(value));
    else SET_STYLE_IF(line_rounded, lv_xml_to_bool(value));

    else SET_STYLE_IF(arc_color, lv_xml_to_color(value));
    else SET_STYLE_IF(arc_opa, lv_xml_to_opa(value));
    else SET_STYLE_IF(arc_width, lv_xml_atoi(value));
    else SET_STYLE_IF(arc_rounded, lv_xml_to_bool(value));
    else SET_STYLE_IF(arc_image_src, lv_xml_get_image(&state->scope, value));

    else SET_STYLE_IF(opa, lv_xml_to_opa(value));
    else SET_STYLE_IF(opa_layered, lv_xml_to_opa(value));
    else SET_STYLE_IF(color_filter_opa, lv_xml_to_opa(value));
    else SET_STYLE_IF(anim_duration, lv_xml_atoi(value));
    else SET_STYLE_IF(blend_mode, lv_xml_blend_mode_to_enum(value));
    else SET_STYLE_IF(transform_width, lv_xml_atoi(value));
    else SET_STYLE_IF(transform_height, lv_xml_atoi(value));
    else SET_STYLE_IF(translate_x, lv_xml_to_size(value));
    else SET_STYLE_IF(translate_y, lv_xml_to_size(value));
    else SET_STYLE_IF(translate_radial, lv_xml_atoi(value));
    else SET_STYLE_IF(transform_scale_x, lv_xml_atoi(value));
    else SET_STYLE_IF(transform_scale_y, lv_xml_atoi(value));
    else SET_STYLE_IF(transform_rotation, lv_xml_atoi(value));
    else SET_STYLE_IF(transform_pivot_x, lv_xml_atoi(value));
    else SET_STYLE_IF(transform_pivot_y, lv_xml_atoi(value));
    else SET_STYLE_IF(transform_skew_x, lv_xml_atoi(value));
    else SET_STYLE_IF(transform_skew_y, lv_xml_atoi(value));
    else SET_STYLE_IF(bitmap_mask_src, lv_xml_get_image(&state->scope, value));
    else SET_STYLE_IF(rotary_sensitivity, lv_xml_atoi(value));
    else SET_STYLE_IF(recolor, lv_xml_to_color(value));
    else SET_STYLE_IF(recolor_opa, lv_xml_to_opa(value));
    else SET_STYLE_IF(blur_radius, lv_xml_atoi(value));
    else SET_STYLE_IF(blur_backdrop, lv_xml_to_bool(value));
    else SET_STYLE_IF(blur_quality, lv_xml_blur_quality_to_enum(value));

    else SET_STYLE_IF(layout, lv_xml_layout_to_enum(value));

    else SET_STYLE_IF(flex_flow, lv_xml_flex_flow_to_enum(value));
    else SET_STYLE_IF(flex_grow, lv_xml_atoi(value));
    else SET_STYLE_IF(flex_main_place, lv_xml_flex_align_to_enum(value));
    else SET_STYLE_IF(flex_cross_place, lv_xml_flex_align_to_enum(value));
    else SET_STYLE_IF(flex_track_place, lv_xml_flex_align_to_enum(value));

    else SET_STYLE_IF(grid_column_align, lv_xml_grid_align_to_enum(value));
    else SET_STYLE_IF(grid_row_align, lv_xml_grid_align_to_enum(value));
    else SET_STYLE_IF(grid_cell_column_pos, lv_xml_atoi(value));
    else SET_STYLE_IF(grid_cell_column_span, lv_xml_atoi(value));
    else SET_STYLE_IF(grid_cell_x_align, lv_xml_grid_align_to_enum(value));
    else SET_STYLE_IF(grid_cell_row_pos, lv_xml_atoi(value));
    else SET_STYLE_IF(grid_cell_row_span, lv_xml_atoi(value));
    else SET_STYLE_IF(grid_cell_y_align, lv_xml_grid_align_to_enum(value));
    else if(lv_streq(prop_name, "style_grid_column_dsc_array") ||
            lv_streq(prop_name, "style_grid_row_dsc_array")) {

        uint32_t item_cnt = 0;
        uint32_t i;
        for(i = 0; value[i] != '\0'; i++) {
            if(value[i] == ' ') item_cnt++;
        }

        int32_t * dsc_array = lv_malloc((item_cnt + 2) * sizeof(int32_t)); /*+2 for LV_GRID_TEMPLATE_LAST*/

        char * value_buf = (char *)value;
        item_cnt = 0;
        const char * sub_value = lv_xml_split_str(&value_buf, ' ');
        while(sub_value) {
            if(sub_value[0] == 'f' && sub_value[1] == 'r') {
                dsc_array[item_cnt] = LV_GRID_FR(lv_xml_atoi(sub_value + 3)); /*+3 to skip "fr("*/
            }
            else {
                dsc_array[item_cnt] = lv_xml_atoi(sub_value);
            }

            item_cnt++;
            sub_value = lv_xml_split_str(&value_buf, ' ');
        }

        dsc_array[item_cnt] = LV_GRID_TEMPLATE_LAST;

        lv_obj_add_event_cb(obj, lv_event_free_user_data_cb, LV_EVENT_DELETE, dsc_array);

        if(lv_streq(prop_name, "style_grid_column_dsc_array")) {
            lv_obj_set_style_grid_column_dsc_array(obj, dsc_array, selector);
        }
        else {
            lv_obj_set_style_grid_row_dsc_array(obj, dsc_array, selector);
        }
    }
}


static void screen_create_on_trigger_event_cb(lv_event_t * e)
{
    screen_load_anim_dsc_t * dsc = lv_event_get_user_data(e);
    LV_ASSERT_NULL(dsc);

    lv_obj_t * screen = lv_xml_create(NULL, dsc->screen_name, NULL);
    if(screen == NULL) {
        LV_LOG_WARN("Couldn't create screen `%s`", dsc->screen_name);
        return;
    }
    lv_screen_load_anim(screen, dsc->anim_type, dsc->duration, dsc->delay, false);
    lv_obj_add_event_cb(screen, delete_on_screen_unloaded_event_cb, LV_EVENT_SCREEN_UNLOADED, NULL);
    lv_obj_add_event_cb(screen, free_screen_create_user_data_on_delete_event_cb, LV_EVENT_SCREEN_UNLOADED, NULL);
}

static void screen_load_on_trigger_event_cb(lv_event_t * e)
{
    screen_load_anim_dsc_t * dsc = lv_event_get_user_data(e);
    LV_ASSERT_NULL(dsc);

    lv_obj_t * screen = lv_display_get_screen_by_name(NULL, dsc->screen_name);
    if(screen == NULL) {
        LV_LOG_WARN("No screen is found with `%s` name", dsc->screen_name);
        return;
    }

    lv_screen_load_anim(screen, dsc->anim_type, dsc->duration, dsc->delay, false);
}

static void delete_on_screen_unloaded_event_cb(lv_event_t * e)
{
    lv_obj_delete(lv_event_get_target_obj(e));
}

static void free_screen_create_user_data_on_delete_event_cb(lv_event_t * e)
{
    screen_load_anim_dsc_t * dsc = lv_event_get_user_data(e);
    lv_free((void *)dsc->screen_name);
    lv_free(dsc);
}

static void play_anim_on_trigger_event_cb(lv_event_t * e)
{
    play_anim_dsc_t * dsc = lv_event_get_user_data(e);
    LV_ASSERT_NULL(dsc);

    lv_obj_t * target;

    if(lv_streq(dsc->target_name, "self")) {
        target = dsc->base_obj;
    }
    else {
        target = lv_obj_find_by_name(dsc->base_obj, dsc->target_name);
    }

    if(target == NULL) {
        LV_LOG_WARN("No target widget is found with `%s` name", dsc->target_name);
        return;
    }

    lv_anim_timeline_t * timeline = NULL;
    lv_anim_timeline_t ** timeline_array = NULL;
    lv_obj_send_event(target, lv_event_xml_store_timeline, &timeline_array);
    if(timeline_array == NULL) {
        LV_LOG_WARN("No time lines are stored in `%s`", dsc->target_name);
        return;
    }

    uint32_t i;
    for(i = 0; timeline_array[i]; i++) {
        const char * name = lv_anim_timeline_get_user_data(timeline_array[i]);
        if(lv_streq(name, dsc->timeline_name)) {
            timeline = timeline_array[i];
            break;
        }
    }

    if(timeline == NULL) {
        LV_LOG_WARN("No timeline is found for `%s` with `%s` name", dsc->target_name, dsc->timeline_name);
        return;
    }

    /*Reset the progress only if the animation was finished*/
    uint16_t progress = lv_anim_timeline_get_progress(timeline);
    if(dsc->reverse) {
        if(progress == 0) {
            lv_anim_timeline_set_progress(timeline, LV_ANIM_TIMELINE_PROGRESS_MAX);
        }

        if(lv_anim_timeline_get_progress(timeline) == LV_ANIM_TIMELINE_PROGRESS_MAX) {
            lv_anim_timeline_set_delay(timeline, dsc->delay);
        }

        lv_anim_timeline_set_reverse(timeline, true);
    }
    else {
        if(progress == LV_ANIM_TIMELINE_PROGRESS_MAX) {
            lv_anim_timeline_set_progress(timeline, 0);
        }

        if(lv_anim_timeline_get_progress(timeline) == 0) {
            lv_anim_timeline_set_delay(timeline, dsc->delay);
        }

        lv_anim_timeline_set_reverse(timeline, false);
    }

    lv_anim_timeline_start(timeline);

}

static void free_play_anim_user_data_on_delete_event_cb(lv_event_t * e)
{
    play_anim_dsc_t * dsc = lv_event_get_user_data(e);
    lv_free((void *)dsc->target_name);
    lv_free((void *)dsc->timeline_name);
    lv_free(dsc);
}

#endif /* LV_USE_XML */
