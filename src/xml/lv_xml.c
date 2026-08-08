/**
 * @file lv_xml.c
 *
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2025 LVGL Kft
 * SPDX-FileCopyrightText: 2026 356C LLC
 */

/*********************
 *      INCLUDES
 *********************/

#include "lv_xml.h"
#if LV_USE_XML

#if LV_USE_OBJ_NAME == 0
    #error "LV_USE_OBJ_NAME is required to use XMLs"
#endif

#if LV_USE_OBSERVER == 0
    #error "LV_USE_OBSERVER is required to use XMLs"
#endif

#include "lv_xml.h"
#include "lv_xml_base_types.h"
#include "lv_xml_parser.h"
#include "lv_xml_component.h"
#include "lv_xml_component_private.h"
#include "lv_xml_widget.h"
#include "lv_xml_style.h"
#include "lv_xml_translation.h"
#include "lv_xml_utils.h"
#include "lv_xml_load_private.h"
#include "lv_xml_private.h"
#include "lv_xml_expr.h"
#include "parsers/lv_xml_obj_parser.h"
#include "parsers/lv_xml_button_parser.h"
#include "parsers/lv_xml_label_parser.h"
#include "parsers/lv_xml_image_parser.h"
#include "parsers/lv_xml_bar_parser.h"
#include "parsers/lv_xml_slider_parser.h"
#include "parsers/lv_xml_tabview_parser.h"
#include "parsers/lv_xml_chart_parser.h"
#include "parsers/lv_xml_table_parser.h"
#include "parsers/lv_xml_dropdown_parser.h"
#include "parsers/lv_xml_imagebutton_parser.h"
#include "parsers/lv_xml_roller_parser.h"
#include "parsers/lv_xml_scale_parser.h"
#include "parsers/lv_xml_buttonmatrix_parser.h"
#include "parsers/lv_xml_spangroup_parser.h"
#include "parsers/lv_xml_textarea_parser.h"
#include "parsers/lv_xml_keyboard_parser.h"
#include "parsers/lv_xml_arc_parser.h"
#include "parsers/lv_xml_switch_parser.h"
#include "parsers/lv_xml_spinbox_parser.h"
#include "parsers/lv_xml_checkbox_parser.h"
#include "parsers/lv_xml_canvas_parser.h"
#include "parsers/lv_xml_calendar_parser.h"
#include "parsers/lv_xml_spinner_parser.h"
#include "parsers/lv_xml_qrcode_parser.h"
#include "../libs/expat/expat.h"
#include <draw/lv_draw_image.h>
#include <misc/lv_anim_timeline_private.h>

/*********************
 *      DEFINES
 *********************/
#include "lv_xml_globals.h"
#define xml_path_prefix lv_xml_path_prefix
#define lv_event_xml_store_timeline lv_xml_event_store_timeline

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void view_start_element_handler(void * user_data, const char * name, const char ** attrs);
static void view_end_element_handler(void * user_data, const char * name);
static void view_character_data_handler(void * user_data, const XML_Char * s, int len);
static void collapse_whitespace(char * s);
static void apply_pending_inline_text(lv_xml_parser_state_t * state, const char * name);
static void free_pcdata_ll(lv_xml_parser_state_t * state);
static char ** xml_frag_copy_attrs(const char ** attrs);
static void xml_frag_buffer_event(xml_frag_capture_t * cap, int kind, const char * name,
                                  const char ** attrs);
static void xml_frag_capture_free(xml_frag_capture_t * cap);
static const char * xml_repeat_index_string(xml_frag_capture_t * cap, int32_t index);
static int32_t xml_repeat_resolve_count(lv_xml_parser_state_t * state, const char * count_raw);
static bool xml_repeat_count_is_subject(const char * count_raw);
static void xml_frag_expand(lv_xml_parser_state_t * state, xml_frag_capture_t * cap,
                            uint32_t lo, uint32_t hi, int32_t count,
                            lv_obj_t *** out_roots, uint32_t * out_root_count);
static xml_frag_record_t * xml_frag_retain(lv_xml_parser_state_t * state, xml_frag_capture_t * cap);
static void xml_frag_teardown(xml_frag_record_t * r);
static void xml_frag_rebuild(xml_frag_record_t * r, uint32_t lo, uint32_t hi, int32_t count);
static void xml_frag_rebuild_cb(lv_observer_t * observer, lv_subject_t * subject);
static void if_cond_changed_cb(void * record, int32_t value);
static void xml_frag_record_free_heap(xml_frag_record_t * r);
static void xml_frag_instance_delete_cb(lv_event_t * e);
static bool xml_value_has_compose(const char * value);
static char * xml_compose_indexed(lv_xml_parser_state_t * state, const char * raw);
static bool xml_state_track_string(lv_xml_parser_state_t * state, char * owned);
static const char * xml_state_concat2(lv_xml_parser_state_t * state, const char * a, const char * b);
static void xml_state_free_composed(lv_xml_parser_state_t * state);
static void destroy_partial_view(lv_xml_parser_state_t * state, lv_obj_t * parent, uint32_t children_before);
static void create_timeline_instances(lv_xml_parser_state_t * state);
static void get_timeline_from_event_cb(lv_event_t * e);
static void free_timelines_event_cb(lv_event_t * e);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_xml_init(void)
{
    xml_path_prefix = lv_strdup("");

    /*It will be sued to store animation time lines in user_data*/
    lv_event_xml_store_timeline = lv_event_register_id();

    lv_xml_component_init();

    lv_xml_register_font(NULL, "lv_font_default", lv_font_get_default());

    lv_xml_register_widget("lv_obj", lv_xml_obj_create, lv_xml_obj_apply);

#if LV_USE_BUTTON
    lv_xml_register_widget("lv_button", lv_xml_button_create, lv_xml_button_apply);
#endif

#if LV_USE_LABEL
    lv_xml_register_widget("lv_label", lv_xml_label_create, lv_xml_label_apply);
#endif

#if LV_USE_IMAGE
    lv_xml_register_widget("lv_image", lv_xml_image_create, lv_xml_image_apply);
#endif

#if LV_USE_BAR
    lv_xml_register_widget("lv_bar", lv_xml_bar_create, lv_xml_bar_apply);
#endif

#if LV_USE_SLIDER
    lv_xml_register_widget("lv_slider", lv_xml_slider_create, lv_xml_slider_apply);
#endif

#if LV_USE_SPINBOX
    lv_xml_register_widget("lv_spinbox", lv_xml_spinbox_create, lv_xml_spinbox_apply);
#endif

#if LV_USE_TABVIEW
    lv_xml_register_widget("lv_tabview", lv_xml_tabview_create, lv_xml_tabview_apply);
    lv_xml_register_widget("lv_tabview-tab_bar", lv_xml_tabview_tab_bar_create, lv_xml_tabview_tab_bar_apply);
    lv_xml_register_widget("lv_tabview-tab", lv_xml_tabview_tab_create, lv_xml_tabview_tab_apply);
    lv_xml_register_widget("lv_tabview-tab_button", lv_xml_tabview_tab_button_create, lv_xml_tabview_tab_button_apply);
#endif

#if LV_USE_CHART
    lv_xml_register_widget("lv_chart", lv_xml_chart_create, lv_xml_chart_apply);
    lv_xml_register_widget("lv_chart-cursor", lv_xml_chart_cursor_create, lv_xml_chart_cursor_apply);
    lv_xml_register_widget("lv_chart-series", lv_xml_chart_series_create, lv_xml_chart_series_apply);
    lv_xml_register_widget("lv_chart-axis", lv_xml_chart_axis_create, lv_xml_chart_axis_apply);
#endif

#if LV_USE_TABLE
    lv_xml_register_widget("lv_table", lv_xml_table_create, lv_xml_table_apply);
    lv_xml_register_widget("lv_table-column", lv_xml_table_column_create, lv_xml_table_column_apply);
    lv_xml_register_widget("lv_table-cell", lv_xml_table_cell_create, lv_xml_table_cell_apply);
#endif

#if LV_USE_DROPDOWN
    lv_xml_register_widget("lv_dropdown", lv_xml_dropdown_create, lv_xml_dropdown_apply);
    lv_xml_register_widget("lv_dropdown-list", lv_xml_dropdown_list_create, lv_xml_dropdown_list_apply);
#endif

#if LV_USE_IMAGEBUTTON
    lv_xml_register_widget("lv_imagebutton", lv_xml_imagebutton_create, lv_xml_imagebutton_apply);
    lv_xml_register_widget("lv_imagebutton-src_left", lv_xml_imagebutton_src_left_create,
                           lv_xml_imagebutton_src_left_apply);
    lv_xml_register_widget("lv_imagebutton-src_right", lv_xml_imagebutton_src_right_create,
                           lv_xml_imagebutton_src_right_apply);
    lv_xml_register_widget("lv_imagebutton-src_mid", lv_xml_imagebutton_src_mid_create, lv_xml_imagebutton_src_mid_apply);
#endif

#if LV_USE_ROLLER
    lv_xml_register_widget("lv_roller", lv_xml_roller_create, lv_xml_roller_apply);
#endif

#if LV_USE_SCALE
    lv_xml_register_widget("lv_scale", lv_xml_scale_create, lv_xml_scale_apply);
    lv_xml_register_widget("lv_scale-section", lv_xml_scale_section_create, lv_xml_scale_section_apply);
#endif

#if LV_USE_SPAN
    lv_xml_register_widget("lv_spangroup", lv_xml_spangroup_create, lv_xml_spangroup_apply);
    lv_xml_register_widget("lv_spangroup-span", lv_xml_spangroup_span_create, lv_xml_spangroup_span_apply);
#endif

#if LV_USE_BUTTONMATRIX
    lv_xml_register_widget("lv_buttonmatrix", lv_xml_buttonmatrix_create, lv_xml_buttonmatrix_apply);
#endif

#if LV_USE_TEXTAREA
    lv_xml_register_widget("lv_textarea", lv_xml_textarea_create, lv_xml_textarea_apply);
#endif

#if LV_USE_KEYBOARD
    lv_xml_register_widget("lv_keyboard", lv_xml_keyboard_create, lv_xml_keyboard_apply);
#endif

#if LV_USE_ARC
    lv_xml_register_widget("lv_arc", lv_xml_arc_create, lv_xml_arc_apply);
#endif

#if LV_USE_SWITCH
    lv_xml_register_widget("lv_switch", lv_xml_switch_create, lv_xml_switch_apply);
#endif

#if LV_USE_CHECKBOX
    lv_xml_register_widget("lv_checkbox", lv_xml_checkbox_create, lv_xml_checkbox_apply);
#endif

#if LV_USE_CANVAS
    lv_xml_register_widget("lv_canvas", lv_xml_canvas_create, lv_xml_canvas_apply);
#endif

#if LV_USE_CALENDAR
    lv_xml_register_widget("lv_calendar", lv_xml_calendar_create, lv_xml_calendar_apply);
#if LV_USE_CALENDAR_HEADER_ARROW
    lv_xml_register_widget("lv_calendar-header_arrow", lv_xml_calendar_header_arrow_create,
                           lv_xml_calendar_header_arrow_apply);
#endif
#if LV_USE_CALENDAR_HEADER_DROPDOWN
    lv_xml_register_widget("lv_calendar-header_dropdown", lv_xml_calendar_header_dropdown_create,
                           lv_xml_calendar_header_dropdown_apply);
#endif
#endif

#if LV_USE_SPINNER
    lv_xml_register_widget("lv_spinner", lv_xml_spinner_create, lv_xml_spinner_apply);
#endif

#if LV_USE_QRCODE
    lv_xml_register_widget("lv_qrcode", lv_xml_qrcode_create, lv_xml_qrcode_apply);
#endif

    lv_xml_register_widget("lv_obj-style", lv_obj_xml_style_create, lv_obj_xml_style_apply);
    lv_xml_register_widget("lv_obj-remove_style", lv_obj_xml_remove_style_create, lv_obj_xml_remove_style_apply);
    lv_xml_register_widget("lv_obj-remove_style_all", lv_obj_xml_remove_style_all_create,
                           lv_obj_xml_remove_style_all_apply);

    lv_xml_register_widget("lv_obj-event_cb", lv_obj_xml_event_cb_create, lv_obj_xml_event_cb_apply);

    lv_xml_register_widget("lv_obj-subject_toggle_event", lv_obj_xml_subject_toggle_create,
                           lv_obj_xml_subject_toggle_apply);
    lv_xml_register_widget("lv_obj-subject_set_int_event", lv_obj_xml_subject_set_create, lv_obj_xml_subject_set_apply);
    lv_xml_register_widget("lv_obj-subject_set_float_event", lv_obj_xml_subject_set_create, lv_obj_xml_subject_set_apply);
    lv_xml_register_widget("lv_obj-subject_set_string_event", lv_obj_xml_subject_set_create, lv_obj_xml_subject_set_apply);
    lv_xml_register_widget("lv_obj-subject_increment_event", lv_obj_xml_subject_increment_create,
                           lv_obj_xml_subject_increment_apply);

    lv_xml_register_widget("lv_obj-screen_load_event", lv_obj_xml_screen_load_event_create,
                           lv_obj_xml_screen_load_event_apply);
    lv_xml_register_widget("lv_obj-screen_create_event", lv_obj_xml_screen_create_event_create,
                           lv_obj_xml_screen_create_event_apply);

    lv_xml_register_widget("lv_obj-play_timeline_event", lv_obj_xml_play_timeline_event_create,
                           lv_obj_xml_play_timeline_event_apply);

    lv_xml_register_widget("lv_obj-bind_style", lv_obj_xml_bind_style_create, lv_obj_xml_bind_style_apply);
    lv_xml_register_widget("lv_obj-bind_style_if_eq", lv_obj_xml_bind_style_cmp_create, lv_obj_xml_bind_style_cmp_apply);
    lv_xml_register_widget("lv_obj-bind_style_if_not_eq", lv_obj_xml_bind_style_cmp_create, lv_obj_xml_bind_style_cmp_apply);
    lv_xml_register_widget("lv_obj-bind_style_if_gt", lv_obj_xml_bind_style_cmp_create, lv_obj_xml_bind_style_cmp_apply);
    lv_xml_register_widget("lv_obj-bind_style_if_ge", lv_obj_xml_bind_style_cmp_create, lv_obj_xml_bind_style_cmp_apply);
    lv_xml_register_widget("lv_obj-bind_style_if_lt", lv_obj_xml_bind_style_cmp_create, lv_obj_xml_bind_style_cmp_apply);
    lv_xml_register_widget("lv_obj-bind_style_if_le", lv_obj_xml_bind_style_cmp_create, lv_obj_xml_bind_style_cmp_apply);
    lv_xml_register_widget("lv_obj-bind_style_prop", lv_obj_xml_bind_style_prop_create, lv_obj_xml_bind_style_prop_apply);
    lv_xml_register_widget("lv_obj-bind_flag_if_eq", lv_obj_xml_bind_flag_create, lv_obj_xml_bind_flag_apply);
    lv_xml_register_widget("lv_obj-bind_flag_if_not_eq", lv_obj_xml_bind_flag_create, lv_obj_xml_bind_flag_apply);
    lv_xml_register_widget("lv_obj-bind_flag_if_gt", lv_obj_xml_bind_flag_create, lv_obj_xml_bind_flag_apply);
    lv_xml_register_widget("lv_obj-bind_flag_if_lt", lv_obj_xml_bind_flag_create, lv_obj_xml_bind_flag_apply);
    lv_xml_register_widget("lv_obj-bind_flag_if_ge", lv_obj_xml_bind_flag_create, lv_obj_xml_bind_flag_apply);
    lv_xml_register_widget("lv_obj-bind_flag_if_le", lv_obj_xml_bind_flag_create, lv_obj_xml_bind_flag_apply);
    lv_xml_register_widget("lv_obj-bind_flag_if", lv_obj_xml_bind_flag_if_create, lv_obj_xml_bind_flag_if_apply);
    lv_xml_register_widget("lv_obj-bind_state_if", lv_obj_xml_bind_state_if_create, lv_obj_xml_bind_state_if_apply);
    lv_xml_register_widget("lv_obj-bind_style_if", lv_obj_xml_bind_style_if_create, lv_obj_xml_bind_style_if_apply);

    lv_xml_register_widget("lv_obj-bind_state_if_eq", lv_obj_xml_bind_state_create, lv_obj_xml_bind_state_apply);
    lv_xml_register_widget("lv_obj-bind_state_if_not_eq", lv_obj_xml_bind_state_create, lv_obj_xml_bind_state_apply);
    lv_xml_register_widget("lv_obj-bind_state_if_gt", lv_obj_xml_bind_state_create, lv_obj_xml_bind_state_apply);
    lv_xml_register_widget("lv_obj-bind_state_if_lt", lv_obj_xml_bind_state_create, lv_obj_xml_bind_state_apply);
    lv_xml_register_widget("lv_obj-bind_state_if_ge", lv_obj_xml_bind_state_create, lv_obj_xml_bind_state_apply);
    lv_xml_register_widget("lv_obj-bind_state_if_le", lv_obj_xml_bind_state_create, lv_obj_xml_bind_state_apply);

    /* Everything registered above is ours, and only ours reports its unknown
     * attributes. Anything an application registers afterwards stays unmarked
     * and is never checked - see lv_xml_attr_check.h. Must stay LAST in the
     * registration block. */
    lv_xml_widget_mark_all_builtin();

    lv_xml_load_init();
}

void lv_xml_deinit(void)
{
#if LV_USE_TEST
    lv_xml_test_unregister();
#endif

    lv_xml_load_deinit();

    /* Symmetry with lv_xml_init(): both registries are file-static and
     * heap-backed, so leaving them behind is not just a leak - the widget list
     * head survives lv_deinit() pointing at reclaimed memory and hangs the next
     * lv_xml_create(). Components first: a scope teardown reaches into styles,
     * subjects and observers, none of which depend on the widget registry. */
    lv_xml_component_deinit();
    lv_xml_widget_deinit();

    lv_free((void *)xml_path_prefix);
    xml_path_prefix = NULL;
}

void * lv_xml_create_in_scope(lv_obj_t * parent, lv_xml_component_scope_t * parent_scope,
                              lv_xml_component_scope_t * scope,
                              const char ** attrs)
{
    /* Initialize the parser state */
    lv_xml_parser_state_t state;
    lv_xml_parser_state_init(&state);
    state.scope = *scope; /*Scope won't be modified here, so it's safe to copy it by value*/
    state.parent = parent;
    state.parent_attrs = attrs;
    state.parent_scope = parent_scope;

    lv_obj_t ** parent_node = lv_ll_ins_head(&state.parent_ll);
    if(parent_node == NULL) {
        LV_LOG_ERROR("OOM: failed to allocate parent node");
        return NULL;
    }
    *parent_node = parent;

    /*Watermark for the failure path: everything this parse builds under `parent`
     *lands at an index at or above this one, so the partial tree can be identified
     *without ever touching a child the caller already had.*/
    const uint32_t parent_children_before = parent ? lv_obj_get_child_count(parent) : 0;

    /* Create an XML parser and set handlers */
    XML_Memory_Handling_Suite mem_handlers;
    mem_handlers.malloc_fcn = lv_malloc;
    mem_handlers.realloc_fcn = lv_realloc;
    mem_handlers.free_fcn = lv_free;
    XML_Parser parser = XML_ParserCreate_MM(NULL, &mem_handlers, NULL);
    XML_SetUserData(parser, &state);
    XML_SetElementHandler(parser, view_start_element_handler, view_end_element_handler);
    XML_SetCharacterDataHandler(parser, view_character_data_handler);

    /* Parse the XML */
    if(XML_Parse(parser, scope->view_def, lv_strlen(scope->view_def), XML_TRUE) == XML_STATUS_ERROR) {
        LV_LOG_WARN("XML parsing error: %s on line %lu", XML_ErrorString(XML_GetErrorCode(parser)),
                    XML_GetCurrentLineNumber(parser));
        /*An unclosed <repeat> leaves an active capture on state.context; free it.*/
        if(state.context) {
            xml_frag_capture_free((xml_frag_capture_t *)state.context);
            state.context = NULL;
        }

        /*Destroy whatever the aborted parse already built. The caller is about to
         *receive NULL, so it has no handle to clean up with, and anything left
         *behind stays parented to ITS parent - a repeatedly-failing hot reload
         *would pile partial subtrees onto the live screen. Deleted before the
         *state teardown below so the widgets' LV_EVENT_DELETE callbacks (frag
         *records, timeline arrays) still run against intact state.*/
        destroy_partial_view(&state, parent, parent_children_before);

        xml_state_free_composed(&state);
        lv_ll_clear(&state.parent_ll);
        free_pcdata_ll(&state);
        XML_ParserFree(parser);
        return NULL;
    }

    /*Well-formed input closes every <repeat>, but malformed input (parse aborted
     *without an error status, or a stray unclosed body) can leave a live capture.
     *Free it so the heap capture never leaks.*/
    if(state.context) {
        xml_frag_capture_free((xml_frag_capture_t *)state.context);
        state.context = NULL;
    }

    state.item = state.view;

#if LV_USE_OBJ_NAME
    /*A screen is named after its component; there is no instance site to defer
     *to. Non-screens are deliberately left alone here: both callers of this
     *function (lv_xml_create() and lv_xml_component_process()) apply the
     *"<component>_#" default themselves, after the instance-site attributes
     *have been applied, which is the only point at which "does this object
     *already have a name?" can be answered correctly.*/
    if(state.item && state.scope.is_screen) {
        lv_obj_set_name(state.item, scope->name);
    }
#endif

    create_timeline_instances(&state);

    /*Register the instance against the scope that built it. This is the single
     *funnel every component instance passes through - lv_xml_create() and
     *lv_xml_create_screen() for a top-level one, lv_xml_component_process() for a
     *nested one, which is why a nested instance counts against ITS OWN scope. The
     *scope may not be freed while the count is non-zero: state.view and its whole
     *subtree hold raw lv_style_t pointers into scope->style_ll.
     *
     *Done LAST, after the parse: every nested instance created during it has
     *already registered, so the delete hooks come off in child-before-parent order
     *within one view root (LVGL fires LV_EVENT_DELETE callbacks in registration
     *order), which is also the order the frag records rely on.*/
    lv_xml_component_scope_instance_attach(scope, state.view);

    xml_state_free_composed(&state);
    lv_ll_clear(&state.parent_ll);
    free_pcdata_ll(&state);
    XML_ParserFree(parser);

    return state.view;
}

void * lv_xml_create(lv_obj_t * parent, const char * name, const char ** attrs)
{
    lv_obj_t * item = NULL;

    /* Select the widget specific parser type based on the name */
    lv_widget_processor_t * p = lv_xml_widget_get_processor(name);
    if(p) {
        lv_xml_parser_state_t state;
        lv_xml_parser_state_init(&state);
        state.parent = parent;

        /* When a component is just created there is no scope where
         * its styles, constants, etc are stored.
         * So leave state.scope = NULL which means the global context.*/

        state.item = p->create_cb(&state, attrs);
        if(state.item == NULL) {
            LV_LOG_WARN("Couldn't create widget.");
            return NULL;
        }
        if(attrs) {
            if(p->builtin) lv_xml_attr_check_begin(&state, attrs, name);
            p->apply_cb(&state, attrs);
            lv_xml_attr_check_end(&state);
        }
        return state.item;
    }

    lv_xml_component_scope_t * scope = lv_xml_component_get_scope(name);
    if(scope) {
        item = lv_xml_create_in_scope(parent, NULL, scope, attrs);
        if(item == NULL) {
            LV_LOG_WARN("Couldn't create component.");
            return NULL;
        }
        const char * value_of_name = NULL;
#if LV_USE_OBJ_NAME
        /*Report an instance-site `name` displacing one the component set on its
         *own <view> root. Checked HERE, before apply_cb: apply_cb applies the
         *instance-site name itself and frees the string lv_obj_get_name()
         *returns, so afterwards the two are indistinguishable.*/
        {
            const char * site_name = attrs ? lv_xml_get_value_of(attrs, "name") : NULL;
            const char * view_name = lv_obj_get_name(item);
            if(site_name && view_name && !lv_streq(view_name, site_name)) {
                LV_LOG_WARN("Component '%s' sets name=\"%s\" on its own <view>; "
                            "the instance-site name=\"%s\" takes precedence",
                            scope->name, view_name, site_name);
            }
        }
#endif
        if(attrs) {
            lv_xml_parser_state_t state;
            lv_xml_parser_state_init(&state);
            state.parent = parent;
            state.item = item;

            /* When a component is just created there is no scope where
             * its styles, constants, etc are stored.
             * So leave state.scope = NULL which means the global context.*/

            p = lv_xml_widget_get_extended_widget_processor(scope->extends);
            p->apply_cb(&state, attrs);
#if LV_USE_OBJ_NAME
            /*The instance site wins over a name the component set on its own
             *<view> root - the instance is the more specific statement, and
             *callers rely on being able to name the thing they placed. The
             *displaced attribute was written on purpose though, so the override
             *is reported above rather than applied silently.*/
            value_of_name = lv_xml_get_value_of(attrs, "name");
            if(value_of_name) lv_obj_set_name(item, value_of_name);
#endif
        }

        /*Set a default indexed name for non screens.
         *Only for an object that has no name at all: `<view name="...">` on the
         *component's own root is a deliberate statement and is kept, where it
         *used to be overwritten by "<component>_#" with no diagnostic. Order of
         *precedence: instance site > the component's own <view name> > default.*/
#if LV_USE_OBJ_NAME
        if(lv_obj_get_parent(item) && value_of_name == NULL && lv_obj_get_name(item) == NULL) {
            char name_buf[128];
            lv_snprintf(name_buf, sizeof(name_buf), "%s_#", scope->name);
            lv_obj_set_name(item, name_buf);
        }
#endif

        return item;
    }

    /* If it isn't a component either then it is unknown — see the note in
     * view_start_element_handler. Usually a stale binary missing a
     * C++-registered widget; the XML was updated without a rebuild. */
    LV_LOG_ERROR("XML tag '%s' is not a known widget/element/component — "
                 "likely an unregistered widget in a STALE BINARY (rebuild required)",
                 name);
    return NULL;
}


lv_obj_t * lv_xml_create_screen(const char * name)
{
    return lv_xml_create(NULL, name, NULL);

}

void lv_xml_set_default_asset_path(const char * path_prefix)
{
    lv_free((void *)xml_path_prefix);
    if(path_prefix == NULL) path_prefix = "";
    xml_path_prefix = lv_strdup(path_prefix);
}


lv_result_t lv_xml_register_font(lv_xml_component_scope_t * scope, const char * name, const lv_font_t * font)
{

    if(scope == NULL) scope = lv_xml_component_get_scope("globals");
    if(scope == NULL) {
        LV_LOG_WARN("No component found to register font `%s`", name);
        return LV_RESULT_INVALID;
    }

    lv_xml_font_t * f;
    LV_LL_READ(&scope->font_ll, f) {
        if(lv_streq(f->name, name)) {
            LV_LOG_INFO("Font `%s` is already registered. Don't register it again.", name);
            return LV_RESULT_OK;
        }
    }

    f = lv_ll_ins_head(&scope->font_ll);
    if(f == NULL) {
        LV_LOG_ERROR("OOM: failed to register font '%s'", name);
        return LV_RESULT_INVALID;
    }
    lv_memzero(f, sizeof(*f));
    f->name = lv_strdup(name);
    f->font = font;

    return LV_RESULT_OK;
}

/**
 * Check if a pointer looks like a valid user-space address.
 * Catches corrupted pointers (e.g., garbage values from heap corruption)
 * that would crash in lv_strcmp/lv_streq.
 */
static bool lv_xml_ptr_looks_valid(const void * p)
{
    if(p == NULL) return false;
#ifdef LV_ARCH_64
    /* Mask the upper byte before checking: ARM64 TBI (Top Byte Ignore)
     * allows the kernel/allocator to store metadata (MTE tags, HWASan)
     * in the upper byte, e.g. 0xb400007832... on Android. */
    lv_uintptr_t addr = (lv_uintptr_t)p & 0x00FFFFFFFFFFFFFF;
    /* Valid user-space addresses have bits 56..47 either all-zero (low half)
     * or all-one (high half / kernel). Reject obvious garbage. */
    return addr != 0 && (addr >> 48) <= 0x7F;
#else
    return true;
#endif
}

/**
 * Iterate a font linked list searching for a font by name.
 * Validates each node's name pointer before access to survive heap corruption.
 * Returns the font if found, NULL otherwise. Sets *corruption_detected if a bad node is found.
 */
static const lv_font_t * lv_xml_search_font_ll(lv_ll_t * font_ll, const char * scope_name,
                                                 const char * name, bool * corruption_detected)
{
    lv_xml_font_t * f;
    int node_index = 0;
    LV_LL_READ(font_ll, f) {
        if(!lv_xml_ptr_looks_valid(f->name)) {
            LV_LOG_ERROR("HEAP_CORRUPTION: font_ll node[%d] at %p has corrupted name pointer %p "
                         "(font_ptr=%p, scope=%s, searching for \"%s\")",
                         node_index, (void *)f, f->name, (void *)f->font,
                         scope_name ? scope_name : "NULL", name);
            if(corruption_detected) *corruption_detected = true;
            node_index++;
            continue;
        }
        if(lv_streq(f->name, name)) return f->font;
        node_index++;
    }
    return NULL;
}

const lv_font_t * lv_xml_get_font_silent(lv_xml_component_scope_t * scope, const char * name)
{
    bool corruption_detected = false;
    const lv_font_t * result;

    if(scope) {
        result = lv_xml_search_font_ll(&scope->font_ll, scope->name, name, &corruption_detected);
        if(result) return result;
    }

    /*If not found in the component check the global space*/
    if((scope == NULL || scope->name == NULL) || !lv_streq(scope->name, "globals")) {
        scope = lv_xml_component_get_scope("globals");
        if(scope) {
            result = lv_xml_search_font_ll(&scope->font_ll, "globals", name, &corruption_detected);
            if(result) return result;
        }
    }

    return NULL;
}

const lv_font_t * lv_xml_get_font(lv_xml_component_scope_t * scope, const char * name)
{
    bool corruption_detected = false;
    const lv_font_t * result;

    if(scope) {
        result = lv_xml_search_font_ll(&scope->font_ll, scope->name, name, &corruption_detected);
        if(result) return result;
    }

    /*If not found in the component check the global space*/
    if((scope == NULL || scope->name == NULL) || !lv_streq(scope->name, "globals")) {
        scope = lv_xml_component_get_scope("globals");
        if(scope) {
            result = lv_xml_search_font_ll(&scope->font_ll, "globals", name, &corruption_detected);
            if(result) return result;
        }
    }

    if(corruption_detected) {
        LV_LOG_ERROR("HEAP_CORRUPTION: font \"%s\" not found after skipping corrupted node(s) — "
                     "returning default font to prevent crash", name);
    }
    else {
        LV_LOG_WARN("No font was found with name \"%s\". Using LV_FONT_DEFAULT instead.", name);
    }
    return lv_font_get_default();
}

/**
 * Shared body of the two registration entry points. `owned` records who has to
 * free the `lv_subject_t`: the scope (a parser-allocated `<subject>` /
 * `<subject_expr>`) or the caller (C++ storage merely lent to the scope). The
 * scope's teardown reads it back — see the `subjects_ll` walk in
 * `lv_xml_component_unregister`.
 */
static lv_result_t register_subject_impl(lv_xml_component_scope_t * scope, const char * name,
                                         lv_subject_t * subject, bool owned)
{
    if(scope == NULL) scope = lv_xml_component_get_scope("globals");
    if(scope == NULL) {
        LV_LOG_WARN("No component found to register subject `%s`", name);
        return LV_RESULT_INVALID;
    }

    lv_xml_subject_t * s;
    LV_LL_READ(&scope->subjects_ll, s) {
        if(lv_streq(s->name, name)) {
            /* Update the pointer — the subject may have moved after a
             * destroy/recreate cycle (e.g., soft restart). Provenance follows
             * the new pointer: whoever registered last is the authority on who
             * owns the storage now. */
            s->subject = subject;
            s->owned = owned;
            return LV_RESULT_OK;
        }
    }

    s = lv_ll_ins_head(&scope->subjects_ll);
    if(s == NULL) {
        LV_LOG_ERROR("OOM: failed to register subject '%s'", name);
        return LV_RESULT_INVALID;
    }
    lv_memzero(s, sizeof(*s));
    s->name = lv_strdup(name);
    s->subject = subject;
    s->owned = owned;

    return LV_RESULT_OK;
}

lv_result_t lv_xml_register_subject(lv_xml_component_scope_t * scope, const char * name, lv_subject_t * subject)
{
    /* Public entry point: the caller keeps ownership of `subject`. It is
     * routinely a C++ static/member (e.g. a modal's `static inline
     * lv_subject_t`), so the scope must never free or deinit it. */
    return register_subject_impl(scope, name, subject, false);
}

lv_result_t lv_xml_register_subject_owned(lv_xml_component_scope_t * scope, const char * name,
                                          lv_subject_t * subject)
{
    return register_subject_impl(scope, name, subject, true);
}

lv_subject_t * lv_xml_get_subject(lv_xml_component_scope_t * scope, const char * name)
{
    lv_xml_subject_t * s;
    if(scope) {
        LV_LL_READ(&scope->subjects_ll, s) {
            if(lv_streq(s->name, name)) return s->subject;
        }
    }

    /*If not found in the component check the global space*/
    if((scope == NULL || scope->name == NULL) || !lv_streq(scope->name, "globals")) {
        scope = lv_xml_component_get_scope("globals");
        if(scope) {
            LV_LL_READ(&scope->subjects_ll, s) {
                if(lv_streq(s->name, name)) return s->subject;
            }
        }
    }

    LV_LOG_WARN("No subject was found with name \"%s\".", name);
    return NULL;
}

lv_result_t lv_xml_unregister_subject(lv_xml_component_scope_t * scope, const char * name)
{
    if(scope == NULL) scope = lv_xml_component_get_scope("globals");
    if(scope == NULL) return LV_RESULT_INVALID;

    lv_xml_subject_t * s;
    LV_LL_READ(&scope->subjects_ll, s) {
        if(lv_streq(s->name, name)) {
            /* Same ownership split as scope teardown: an owned subject dies with
             * its record, a borrowed one belongs to the caller and is only
             * dropped. Freeing unconditionally would abort on C++ storage;
             * freeing nothing leaked every parser-allocated subject removed by
             * name. */
            lv_xml_subject_record_release(s);
            lv_ll_remove(&scope->subjects_ll, s);
            lv_free(s); /* the record; s->subject was handled above */
            return LV_RESULT_OK;
        }
    }
    return LV_RESULT_INVALID;
}


lv_result_t lv_xml_register_timeline(lv_xml_component_scope_t * scope, const char * name)
{
    if(scope == NULL) scope = lv_xml_component_get_scope("globals");
    if(scope == NULL) {
        LV_LOG_WARN("No component found to register subject `%s`", name);
        return LV_RESULT_INVALID;
    }

    lv_xml_timeline_t * at;
    LV_LL_READ(&scope->timeline_ll, at) {
        if(lv_streq(at->name, name)) {
            LV_LOG_INFO("Animation timeline `%s` is already registered. Don't register it again.", name);
            return LV_RESULT_OK;
        }
    }

    at = lv_ll_ins_head(&scope->timeline_ll);
    if(at == NULL) {
        LV_LOG_ERROR("OOM: failed to register timeline '%s'", name);
        return LV_RESULT_INVALID;
    }
    at->name = lv_strdup(name);
    lv_ll_init(&at->anims_ll, sizeof(lv_xml_anim_timeline_child_t));

    return LV_RESULT_OK;
}

void * lv_xml_get_timeline(lv_xml_component_scope_t * scope, const char * name)
{
    lv_xml_timeline_t * at;
    if(scope) {
        LV_LL_READ(&scope->timeline_ll, at) {
            if(lv_streq(at->name, name)) return at;
        }
    }

    /*If not found in the component check the global space*/
    if((scope == NULL || scope->name == NULL) || !lv_streq(scope->name, "globals")) {
        scope = lv_xml_component_get_scope("globals");
        if(scope) {
            LV_LL_READ(&scope->timeline_ll, at) {
                if(lv_streq(at->name, name)) return at;
            }
        }
    }

    LV_LOG_WARN("No timeline was found with name \"%s\".", name);
    return NULL;
}


lv_result_t lv_xml_register_const(lv_xml_component_scope_t * scope, const char * name, const char * value)
{
    if(scope == NULL) scope = lv_xml_component_get_scope("globals");
    if(scope == NULL) {
        LV_LOG_WARN("No component found to register constant `%s`", name);
        return LV_RESULT_INVALID;
    }

    lv_xml_const_t * cnst;
    LV_LL_READ(&scope->const_ll, cnst) {
        if(lv_streq(cnst->name, name)) {
            LV_LOG_INFO("Const `%s` is already registered. Don't register it again.", name);
            return LV_RESULT_OK;
        }
    }

    cnst = lv_ll_ins_head(&scope->const_ll);
    if(cnst == NULL) {
        LV_LOG_ERROR("OOM: failed to register const '%s'", name);
        return LV_RESULT_INVALID;
    }
    lv_memzero(cnst, sizeof(*cnst));

    cnst->name = lv_strdup(name);
    cnst->value = lv_strdup(value);

    return LV_RESULT_OK;
}

lv_result_t lv_xml_update_const(lv_xml_component_scope_t * scope, const char * name, const char * value)
{
    if(scope == NULL) scope = lv_xml_component_get_scope("globals");
    if(scope == NULL) {
        LV_LOG_WARN("No component found to update constant `%s`", name);
        return LV_RESULT_INVALID;
    }

    lv_xml_const_t * cnst;
    LV_LL_READ(&scope->const_ll, cnst) {
        if(lv_streq(cnst->name, name)) {
            lv_free((void *)cnst->value);
            cnst->value = lv_strdup(value);
            return LV_RESULT_OK;
        }
    }

    LV_LOG_WARN("Const `%s` not found for update, registering as new.", name);
    return lv_xml_register_const(scope, name, value);
}

static const char * lv_xml_get_const_internal(lv_xml_component_scope_t * scope, const char * name, bool silent)
{

    if(scope == NULL) scope = lv_xml_component_get_scope("globals");
    if(scope == NULL) return NULL;

    lv_xml_const_t * cnst;
    if(scope) {
        LV_LL_READ(&scope->const_ll, cnst) {
            if(lv_streq(cnst->name, name)) return cnst->value;
        }
    }

    /*If not found in the component check the global space*/
    if((scope == NULL || scope->name == NULL) || !lv_streq(scope->name, "globals")) {
        scope = lv_xml_component_get_scope("globals");
        if(scope) {
            LV_LL_READ(&scope->const_ll, cnst) {
                if(lv_streq(cnst->name, name)) return cnst->value;
            }
        }
    }

    if(!silent) {
        LV_LOG_WARN("No constant was found with name \"%s\".", name);
    }
    return NULL;
}

const char * lv_xml_get_const(lv_xml_component_scope_t * scope, const char * name)
{
    return lv_xml_get_const_internal(scope, name, false);
}

const char * lv_xml_get_const_silent(lv_xml_component_scope_t * scope, const char * name)
{
    return lv_xml_get_const_internal(scope, name, true);
}


lv_result_t lv_xml_register_image(lv_xml_component_scope_t * scope, const char * name, const void * src)
{
    if(scope == NULL) scope = lv_xml_component_get_scope("globals");
    if(scope == NULL) {
        LV_LOG_WARN("No component found to register image `%s`", name);
        return LV_RESULT_INVALID;
    }

    lv_xml_image_t * img;
    LV_LL_READ(&scope->image_ll, img) {
        if(lv_streq(img->name, name)) {
            LV_LOG_INFO("Image `%s` is already registered. Don't register it again.", name);
            return LV_RESULT_OK;
        }
    }

    img = lv_ll_ins_head(&scope->image_ll);
    if(img == NULL) {
        LV_LOG_ERROR("OOM: failed to register image '%s'", name);
        return LV_RESULT_INVALID;
    }
    lv_memzero(img, sizeof(*img));
    img->name = lv_strdup(name);
    if(lv_image_src_get_type(src) == LV_IMAGE_SRC_FILE) {
        char buf[LV_XML_MAX_PATH_LENGTH];
        lv_snprintf(buf, sizeof(buf), "%s%s", xml_path_prefix, src);
        img->src = lv_strdup(buf);
        img->src_is_owned = true;
    }
    else {
        /* VARIABLE (an lv_image_dsc_t) or SYMBOL: stored verbatim, owned by the
         * caller. Usually a compiled-in `static const lv_image_dsc_t`, so this
         * pointer must never reach lv_free() - see component_scope_free(). */
        img->src = src;
        img->src_is_owned = false;
    }

    return LV_RESULT_OK;
}

const void * lv_xml_get_image(lv_xml_component_scope_t * scope, const char * name)
{
    /* Empty/missing name is expected for unset bind_src attributes and string
     * subjects that default to "". Skip the lookup and the warning — nothing
     * actionable, and a rebuild storm against a component with an empty
     * default can emit dozens of these per frame (seen on K1 Max during
     * Print Status overlay rebuild). */
    if(name == NULL || name[0] == '\0') return NULL;

    if(scope == NULL) scope = lv_xml_component_get_scope("globals");
    if(scope == NULL) return NULL;

    lv_xml_image_t * img;
    if(scope) {
        LV_LL_READ(&scope->image_ll, img) {
            if(lv_streq(img->name, name)) return img->src;
        }
    }

    /*If not found in the component check the global space*/
    if((scope == NULL || scope->name == NULL) || !lv_streq(scope->name, "globals")) {
        scope = lv_xml_component_get_scope("globals");
        if(scope) {
            LV_LL_READ(&scope->image_ll, img) {
                if(lv_streq(img->name, name)) return img->src;
            }
        }
    }

    LV_LOG_WARN("No image was found with name \"%s\"", name);
    return NULL;
}

lv_result_t lv_xml_register_event_cb(lv_xml_component_scope_t * scope, const char * name, lv_event_cb_t cb)
{
    if(scope == NULL) scope = lv_xml_component_get_scope("globals");
    if(scope == NULL) {
        LV_LOG_WARN("No component found to register event `%s`", name);
        return LV_RESULT_INVALID;
    }

    /* Last-write-wins: replace existing entry so a later owner can override an earlier one.
     * Original first-write-wins caused crashes when a shared XML component (e.g.
     * wifi_network_item) is used by two C++ classes with different user_data layouts —
     * the second class's registration was silently dropped, the first class's handler
     * fired against the wrong struct, and the cast crashed on dereference. */
    lv_xml_event_cb_t * e;
    LV_LL_READ(&scope->event_ll, e) {
        if(lv_streq(e->name, name)) {
            e->cb = cb;
            return LV_RESULT_OK;
        }
    }

    e = lv_ll_ins_head(&scope->event_ll);
    if(e == NULL) {
        LV_LOG_ERROR("OOM: failed to register event_cb '%s'", name);
        return LV_RESULT_INVALID;
    }
    lv_memzero(e, sizeof(*e));
    e->name = lv_strdup(name);
    e->cb = cb;

    return LV_RESULT_OK;
}


lv_event_cb_t lv_xml_get_event_cb(lv_xml_component_scope_t * scope, const char * name)
{
    if(scope == NULL) scope = lv_xml_component_get_scope("globals");
    if(scope == NULL) return NULL;

    lv_xml_event_cb_t * e;
    if(scope) {
        LV_LL_READ(&scope->event_ll, e) {
            if(lv_streq(e->name, name)) return e->cb;
        }
    }

    /*If not found in the component check the global space*/
    if((scope == NULL || scope->name == NULL) || !lv_streq(scope->name, "globals")) {
        scope = lv_xml_component_get_scope("globals");
        if(scope) {
            LV_LL_READ(&scope->event_ll, e) {
                if(lv_streq(e->name, name)) return e->cb;
            }
        }
    }

    LV_LOG_WARN("No event was found with name \"%s\"", name);
    return NULL;
}

void lv_xml_event_cb_foreach(lv_xml_component_scope_t * scope, lv_xml_event_cb_iter_cb_t cb, void * user_data)
{
    if(cb == NULL) return;
    if(scope == NULL) scope = lv_xml_component_get_scope("globals");
    if(scope == NULL) return;

    lv_xml_event_cb_t * e;
    LV_LL_READ(&scope->event_ll, e) {
        cb(e->name, e->cb, user_data);
    }
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static const char * get_param_type(lv_xml_component_scope_t * scope, const char * name)
{
    lv_xml_param_t * p;
    LV_LL_READ(&scope->param_ll, p) {
        if(lv_streq(p->name, name)) return p->type;
    }
    return NULL;
}

static const char * get_param_default(lv_xml_component_scope_t * scope, const char * name)
{
    lv_xml_param_t * p;
    LV_LL_READ(&scope->param_ll, p) {
        if(lv_streq(p->name, name)) return p->def;
    }
    return NULL;
}

static void resolve_params(lv_xml_parser_state_t * state, lv_xml_component_scope_t * item_scope,
                           lv_xml_component_scope_t * parent_scope,
                           const char ** item_attrs, const char ** parent_attrs)
{
    uint32_t i;
    for(i = 0; item_attrs[i]; i += 2) {
        const char * value = item_attrs[i + 1];

        /*Embedded `${name}` composition — splice the loop index (`${i}`) or a
         *component param (`${grp}`) into a larger string, e.g.
         *bind_text="demo_${i}_v" -> "demo_2_v". Detected by the "${" marker so it
         *fires wherever it appears (leading or embedded); the whole-value `$name`
         *and `#const` paths below never see a "${". This is the only allocating
         *branch: the result is owned by state->composed_strings, freed at parse
         *end. resolve_consts won't touch it (composed names don't start with `#`).*/
        if(xml_value_has_compose(value)) {
            item_attrs[i + 1] = xml_compose_indexed(state, value);
            continue;
        }

        if(value[0] == '$') {
            /*E.g. the ${my_color} value is the my_color attribute name on the parent*/
            const char * name_clean = &value[1]; /*skips `$`*/

            /*`$i` — the <repeat> loop index (bare, zero-based). Resolved from the
             *active repeat capture on state->context, only during replay. Substitute
             *a per-expansion transient string that outlives this handler call.*/
            xml_frag_capture_t * rc = state ? (xml_frag_capture_t *)state->context : NULL;
            if(rc && rc->replaying && lv_streq(name_clean, "i")) {
                item_attrs[i + 1] = xml_repeat_index_string(rc, rc->current_index);
                continue;
            }

            /*`$prop|ref` — a param reference followed by a literal comparison
             *value, used by hidden_if_prop_eq / hidden_if_prop_not_eq. Resolve
             *only `prop`; the `|ref` suffix is carried through verbatim so the
             *obj parser can split on the pipe. Param names never contain `|`, so
             *the split is unambiguous. Without this, the whole "prop|ref" was
             *looked up as one param name, failed, blanked the attribute, and
             *silently disabled the hide (bundle ET5ACW4S).*/
            const char * pipe = lv_strchr(name_clean, '|');
            char namebuf[128];
            const char * lookup_name = name_clean;
            if(pipe) {
                size_t nlen = (size_t)(pipe - name_clean);
                if(nlen < sizeof(namebuf)) {
                    lv_memcpy(namebuf, name_clean, nlen);
                    namebuf[nlen] = '\0';
                    lookup_name = namebuf;
                }
                else {
                    /*Pathologically long name — fall back to whole-string lookup
                     *(warns + blanks) rather than overrun namebuf.*/
                    pipe = NULL;
                }
            }

            const char * type = get_param_type(item_scope, lookup_name);
            if(type == NULL) {
                /*Not declared in <api>, so there is no type to resolve against and
                 *no default to fall back on. Drop the attribute, exactly as the
                 *"declared but unset with no default" path below does. Falling
                 *through with type == NULL used to reach lv_streq(type, "style")
                 *whenever a value WAS supplied - lv_strcmp dereferences
                 *unconditionally, so an ordinary typo'd prop name was a NULL-deref
                 *crash rather than a warning.*/
                LV_LOG_WARN("'%s' parameter is not defined on '%s'", lookup_name, item_scope->name);
                item_attrs[i] = "";
                item_attrs[i + 1] = "";
                continue;
            }
            const char * ext_value = lv_xml_get_value_of(parent_attrs, lookup_name);
            if(ext_value) {
                /*If the value is not resolved earlier (e.g. it's a top level element created manually)
                 * use the default value. Note: Only check for '$' (unresolved props), not '#' which
                 * indicates const/token references that will be resolved by resolve_consts later.*/
                if(ext_value[0] == '$') {
                    ext_value = get_param_default(item_scope, lookup_name);
                }
                else if(lv_streq(type, "style")) {
                    lv_xml_style_t * s = lv_xml_get_style_by_name(parent_scope, ext_value);
                    if(s != NULL) {
                        ext_value = s->long_name;
                    }
                    else {
                        LV_LOG_WARN("style '%s' referenced on '%s' not found; using default",
                                    ext_value, item_scope->name);
                        ext_value = get_param_default(item_scope, lookup_name);
                    }
                }
            }
            else {
                /*If the API attribute is not provide don't set it*/
                ext_value = get_param_default(item_scope, lookup_name);
            }
            if(pipe) {
                /*Reattach the literal `|ref` suffix to the resolved value (empty
                 *if the param was unset with no default).*/
                item_attrs[i + 1] = xml_state_concat2(state, ext_value ? ext_value : "", pipe);
            }
            else if(ext_value) {
                item_attrs[i + 1] = ext_value;
            }
            else {
                /*Not set and no default value either
                 *Don't set this property*/
                item_attrs[i] = "";
                item_attrs[i + 1] = "";
            }
        }
    }
}

/**
 * Check if a string is a valid hex color (exactly 6 hex digits).
 * This allows values like "#1F1F1F" to pass through without being
 * treated as const references after prop substitution.
 */
static bool is_hex_color(const char * str)
{
    if(str == NULL) return false;
    size_t len = lv_strlen(str);
    if(len != 6) return false;
    for(size_t i = 0; i < 6; i++) {
        char c = str[i];
        bool is_hex = (c >= '0' && c <= '9') ||
                      (c >= 'a' && c <= 'f') ||
                      (c >= 'A' && c <= 'F');
        if(!is_hex) return false;
    }
    return true;
}

static void resolve_consts(const char ** item_attrs, lv_xml_component_scope_t * scope)
{
    uint32_t i;
    for(i = 0; item_attrs[i]; i += 2) {
        const char * name = item_attrs[i];
        const char * value = item_attrs[i + 1];
        if(lv_streq(name, "styles")) continue; /*Styles will handle it themselves*/
        if(value[0] == '#') {
            const char * value_clean = &value[1];

            /*If the value is already a hex color (e.g. "#1F1F1F" from prop substitution),
             *don't try to look it up as a const - pass it through unchanged*/
            if(is_hex_color(value_clean)) {
                continue;  /*Keep original value with #*/
            }

            const char * const_value = lv_xml_get_const_silent(scope, value_clean);
            if(const_value) {
                item_attrs[i + 1] = const_value;
            }
            /*Unknown const: drop the attribute so the widget keeps its default,
             *but say WHERE it was, or the message cannot be acted on in a tree
             *this size. Parsing deliberately continues.*/
            else {
                LV_LOG_WARN("Unknown const `#%s` in component `%s` (attribute `%s`) - "
                            "attribute dropped",
                            value_clean,
                            (scope && scope->name) ? scope->name : "<unknown>",
                            name);
                item_attrs[i] = "";
                item_attrs[i + 1] = "";
            }
        }
    }
}

/** Trim leading/trailing whitespace and collapse internal runs of
 *  space/tab/CR/LF to a single space (HTML PCDATA semantics), in place.
 *  MUST stay byte-identical to collapse_whitespace() in
 *  scripts/translations/extractor.py — the collapsed string is the
 *  translation key on both sides. */
static void collapse_whitespace(char * s)
{
    char * src = s;
    char * dst = s;
    bool in_ws = true; /*true drops leading whitespace*/
    for(; *src; src++) {
        char c = *src;
        if(c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            in_ws = true;
            continue;
        }
        if(in_ws && dst != s) *dst++ = ' ';
        in_ws = false;
        *dst++ = c;
    }
    *dst = '\0';
}

static void view_character_data_handler(void * user_data, const XML_Char * s, int len)
{
    lv_xml_parser_state_t * state = (lv_xml_parser_state_t *)user_data;
    if(len <= 0) return;

    /*Buffer inline text while capturing a <repeat> body so it replays with the
     *elements it belongs to. The chardata handler receives a length, not a NUL-
     *terminated string, so make a bounded copy for the event.*/
    xml_frag_capture_t * cap = (xml_frag_capture_t *)state->context;
    if(cap && cap->active && !cap->replaying) {
        char * text = lv_malloc((size_t)len + 1);
        if(text) {
            lv_memcpy(text, s, (size_t)len);
            text[len] = '\0';
            xml_frag_buffer_event(cap, /*kind=*/2, text, NULL);
            lv_free(text);
        }
        return;
    }

    lv_xml_pcdata_entry_t * entry = lv_ll_get_tail(&state->pcdata_ll);
    if(entry == NULL) return; /*Character data outside any element (prolog etc.)*/

    size_t needed = entry->len + (size_t)len + 1;
    if(needed > entry->cap) {
        size_t new_cap = entry->cap ? entry->cap * 2 : 64;
        while(new_cap < needed) new_cap *= 2;
        char * new_buf = lv_realloc(entry->buf, new_cap);
        if(new_buf == NULL) return; /*OOM: drop this chunk*/
        entry->buf = new_buf;
        entry->cap = new_cap;
    }
    lv_memcpy(entry->buf + entry->len, s, (size_t)len);
    entry->len += (size_t)len;
    entry->buf[entry->len] = '\0';
}

/** Pop the PCDATA entry for the element just closed and, if it captured real
 *  text, apply it as `text` + `translation_tag` through the element's normal
 *  apply_cb — i.e. `<text_muted>Foo</text_muted>` behaves like
 *  `<text_muted text="Foo" translation_tag="Foo"/>`. */
static void apply_pending_inline_text(lv_xml_parser_state_t * state, const char * name)
{
    lv_xml_pcdata_entry_t * entry = lv_ll_get_tail(&state->pcdata_ll);
    if(entry == NULL) return;

    char * buf = entry->buf;
    lv_obj_t * item = entry->item;
    bool has_conflict = entry->has_conflict;
    lv_ll_remove(&state->pcdata_ll, entry);
    lv_free(entry);

    if(buf == NULL) return;
    collapse_whitespace(buf);
    if(buf[0] == '\0' || item == NULL) {
        lv_free(buf);
        return;
    }
    if(has_conflict) {
        LV_LOG_WARN("Inline text ignored on <%s>: element also has "
                    "text/bind_text/translation_tag", name);
        lv_free(buf);
        return;
    }

    /*Resolve $prop/#const exactly like attribute values (whole-value)*/
    const char * synth[5] = {"text", buf, "translation_tag", buf, NULL};
    resolve_params(state, &state->scope, state->parent_scope, synth, state->parent_attrs);
    resolve_consts(synth, &state->scope);
    if(synth[0][0] == '\0' || synth[2][0] == '\0') { /*unresolved -> dropped pair*/
        lv_free(buf);
        return;
    }

    lv_widget_processor_t * p = lv_xml_widget_get_processor(name);
    if(p == NULL) {
        /*Component instance: apply through the widget it extends*/
        lv_xml_component_scope_t * comp_scope = lv_xml_component_get_scope(name);
        if(comp_scope) p = lv_xml_widget_get_extended_widget_processor(comp_scope->extends);
    }
    if(p) {
        lv_obj_t * item_saved = state->item;
        state->item = item;
        p->apply_cb(state, synth); /*apply_cb copies values; buf freed below*/
        state->item = item_saved;
    }
    lv_free(buf);
}

static void free_pcdata_ll(lv_xml_parser_state_t * state)
{
    lv_xml_pcdata_entry_t * e;
    LV_LL_READ(&state->pcdata_ll, e) {
        if(e->buf) lv_free(e->buf);
    }
    lv_ll_clear(&state->pcdata_ll);
}

/*======================
 *   <repeat> support
 *=====================*/

/** Deep-copy a NULL-terminated name/value attribute array. The copies keep any
 *  `$`/`#` sigils intact so each replay iteration's resolve_params/resolve_consts
 *  run against pristine values. */
static char ** xml_frag_copy_attrs(const char ** attrs)
{
    size_t n = 0;
    if(attrs) {
        while(attrs[n]) n++;                 /*counts name+val entries*/
    }
    char ** out = lv_malloc(sizeof(char *) * (n + 1));
    if(out == NULL) return NULL;
    for(size_t i = 0; i < n; i++) {
        size_t len = lv_strlen(attrs[i]);
        out[i] = lv_malloc(len + 1);
        if(out[i] == NULL) {
            /*Roll back what we have to avoid a leak on OOM*/
            for(size_t j = 0; j < i; j++) lv_free(out[j]);
            lv_free(out);
            return NULL;
        }
        lv_memcpy(out[i], attrs[i], len + 1);
    }
    out[n] = NULL;
    return out;
}

/** Append one buffered SAX event to the capture buffer. `kind` 0=start (attrs
 *  deep-copied), 1=end (name only), 2=chardata (text stored in `name`). */
static void xml_frag_buffer_event(xml_frag_capture_t * cap, int kind, const char * name,
                                  const char ** attrs)
{
    if(cap->event_count == cap->event_cap) {
        uint32_t new_cap = cap->event_cap ? cap->event_cap * 2 : 8;
        xml_frag_event_t * ne = lv_realloc(cap->events, sizeof(xml_frag_event_t) * new_cap);
        if(ne == NULL) {
            LV_LOG_ERROR("OOM: failed to grow <frag> capture buffer");
            return;
        }
        cap->events = ne;
        cap->event_cap = new_cap;
    }

    xml_frag_event_t * ev = &cap->events[cap->event_count];
    ev->kind = kind;
    ev->attrs = NULL;
    ev->name = NULL;

    size_t len = lv_strlen(name);
    ev->name = lv_malloc(len + 1);
    if(ev->name == NULL) {
        LV_LOG_ERROR("OOM: failed to copy <frag> event name");
        return;
    }
    lv_memcpy(ev->name, name, len + 1);

    if(kind == 0) {
        ev->attrs = xml_frag_copy_attrs(attrs);
    }

    cap->event_count++;
}

/** Free all owned strings held by the capture (events, count_raw, idx_strings). */
static void xml_frag_capture_free(xml_frag_capture_t * cap)
{
    if(cap == NULL) return;
    for(uint32_t e = 0; e < cap->event_count; e++) {
        xml_frag_event_t * ev = &cap->events[e];
        if(ev->name) lv_free(ev->name);
        if(ev->attrs) {
            for(size_t a = 0; ev->attrs[a]; a++) lv_free(ev->attrs[a]);
            lv_free(ev->attrs);
        }
    }
    lv_free(cap->events);
    for(uint32_t k = 0; k < cap->idx_count; k++) lv_free(cap->idx_strings[k]);
    lv_free(cap->idx_strings);
    lv_free(cap->count_raw);
    lv_free(cap->cond_raw);
    lv_free(cap);
}

/** Format `index` into a freshly allocated string tracked on the capture. Freed
 *  at the end of the current expansion so it outlives the handler call that
 *  copies it but never accumulates across iterations. Returns "" on OOM. */
static const char * xml_repeat_index_string(xml_frag_capture_t * cap, int32_t index)
{
    char buf[16];
    lv_snprintf(buf, sizeof(buf), "%d", (int)index);
    size_t len = lv_strlen(buf);
    char * s = lv_malloc(len + 1);
    if(s == NULL) return "";
    lv_memcpy(s, buf, len + 1);

    if(cap->idx_count == cap->idx_cap) {
        uint32_t new_cap = cap->idx_cap ? cap->idx_cap * 2 : 8;
        char ** na = lv_realloc(cap->idx_strings, sizeof(char *) * new_cap);
        if(na == NULL) {
            lv_free(s);
            return "";
        }
        cap->idx_strings = na;
        cap->idx_cap = new_cap;
    }
    cap->idx_strings[cap->idx_count++] = s;
    return s;
}

/** Resolve the raw `count` into a concrete iteration count. Literal digits ->
 *  atoi; leading `#` -> const lookup then atoi; otherwise treated as a subject
 *  name and read at current value (reactive rebuild is Task 3). Clamped [0,256]. */
static int32_t xml_repeat_resolve_count(lv_xml_parser_state_t * state, const char * count_raw)
{
    int32_t count = 0;
    if(count_raw == NULL || count_raw[0] == '\0') {
        count = 0;
    }
    else if(count_raw[0] == '#') {
        const char * cv = lv_xml_get_const(&state->scope, &count_raw[1]);
        count = cv ? lv_xml_atoi(cv) : 0;
    }
    else if((count_raw[0] >= '0' && count_raw[0] <= '9') || count_raw[0] == '-' || count_raw[0] == '+') {
        count = lv_xml_atoi(count_raw);
    }
    else {
        /*Subject name: read the current value; Task 3 adds the reactive observer.*/
        lv_subject_t * subject = lv_xml_get_subject(&state->scope, count_raw);
        count = subject ? lv_subject_get_int(subject) : 0;
    }

    if(count < 0) count = 0;
    if(count > 256) {
        LV_LOG_WARN("<repeat count='%s'> resolved to %d, clamping to 256", count_raw, (int)count);
        count = 256;
    }
    return count;
}

/** True if `count_raw` should be treated as a subject name (i.e. not empty, not a
 *  `#const`, not a signed literal). Only such a `count` gets a reactive observer;
 *  literals and consts expand once at parse time. */
static bool xml_repeat_count_is_subject(const char * count_raw)
{
    if(count_raw == NULL || count_raw[0] == '\0') return false;
    if(count_raw[0] == '#') return false;
    char c = count_raw[0];
    if((c >= '0' && c <= '9') || c == '-' || c == '+') return false;
    return true;
}

/** Replay the buffered body's `[lo,hi)` event slice `count` times through the real
 *  element handlers, injecting `$i` per iteration. During replay cap->replaying is
 *  true so the handlers create objects instead of buffering. When `out_roots` is
 *  non-NULL the top-level objects created by the expansion (the sliced body's
 *  direct children) are collected into a freshly allocated array handed back to
 *  the caller, which then owns it — this is what lets the subject-bound rebuild
 *  find and tear down the prior expansion. `<repeat>` passes the full range
 *  `[0, cap->event_count)`; `<if>` (Task 3) passes the selected true/false slice. */
static void xml_frag_expand(lv_xml_parser_state_t * state, xml_frag_capture_t * cap,
                            uint32_t lo, uint32_t hi, int32_t count,
                            lv_obj_t *** out_roots, uint32_t * out_root_count)
{
    if(count < 0) count = 0;
    if(hi > cap->event_count) hi = cap->event_count;

    lv_obj_t ** roots = NULL;
    uint32_t root_count = 0;
    uint32_t root_cap = 0;

    cap->replaying = true;
    for(int32_t i = 0; i < count; i++) {
        cap->current_index = i;
        /*Track nesting within this iteration's (balanced) event stream so the
         *body's DIRECT children — the expansion roots — can be identified as the
         *start events seen at nest depth 0, independent of any other children the
         *enclosing parent already holds.*/
        int nest = 0;
        for(uint32_t e = lo; e < hi; e++) {
            xml_frag_event_t * ev = &cap->events[e];
            if(ev->kind == 0) {
                /*Build a FRESH shallow array of pointers over the owned deep-copied
                 *strings. resolve_params/resolve_consts mutate the attribute slots
                 *in place, so the throwaway array absorbs that — ev->attrs stays
                 *pristine (sigils intact) for the next iteration. Mutating ev->attrs
                 *directly would repoint owned slots at const/transient storage and
                 *double-free at teardown.*/
                size_t n = 0;
                while(ev->attrs && ev->attrs[n]) n++;
                const char ** shallow = lv_malloc(sizeof(char *) * (n + 1));
                if(shallow == NULL) {
                    LV_LOG_ERROR("OOM: <frag> replay shallow attrs");
                    continue;
                }
                for(size_t a = 0; a < n; a++) shallow[a] = ev->attrs[a];
                shallow[n] = NULL;
                bool top_level = (nest == 0);
                view_start_element_handler(state, ev->name, shallow);
                lv_free(shallow);
                if(top_level && out_roots && state->item) {
                    if(root_count == root_cap) {
                        uint32_t new_cap = root_cap ? root_cap * 2 : 8;
                        lv_obj_t ** nr = lv_realloc(roots, sizeof(lv_obj_t *) * new_cap);
                        if(nr) {
                            roots = nr;
                            root_cap = new_cap;
                        }
                    }
                    if(root_count < root_cap) roots[root_count++] = state->item;
                }
                nest++;
            }
            else if(ev->kind == 1) {
                nest--;
                view_end_element_handler(state, ev->name);
            }
            else {
                view_character_data_handler(state, ev->name, (int)lv_strlen(ev->name));
            }
        }
    }
    cap->replaying = false;

    /*Drop the per-expansion $i strings now that every apply_cb has copied them.*/
    for(uint32_t k = 0; k < cap->idx_count; k++) lv_free(cap->idx_strings[k]);
    cap->idx_count = 0;

    if(out_roots) {
        *out_roots = roots;
        *out_root_count = root_count;
    }
    else if(roots) {
        lv_free(roots);   /*defensive: only built when out_roots requested*/
    }
}

/** Async-tear-down the current expansion of a subject-bound fragment record. This
 *  runs from the observer (synchronous, inside a UpdateQueue drain batch), so it
 *  MUST NOT synchronously delete widgets [L081][L059]. Mirrors the C++
 *  helix::ui::safe_delete_subtree algorithm in pure C: reparent every root into an
 *  off-tree, hidden, layout-less "condemned" sink (a synchronous detach from the
 *  live parent's child list) and then `lv_obj_delete_async` the sink so the actual
 *  destruction happens outside the batch. Never `lv_obj_is_valid()` here [L076]. */
static void xml_frag_teardown(xml_frag_record_t * r)
{
    if(r->roots == NULL || r->root_count == 0) {
        lv_free(r->roots);   /*NULL-safe; keeps the record consistent*/
        r->roots = NULL;
        r->root_count = 0;
        return;
    }

    lv_obj_t * condemned = lv_obj_create(lv_layer_top());
    if(condemned) {
        lv_obj_add_flag(condemned, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(condemned, 0, 0);
        lv_obj_set_layout(condemned, LV_LAYOUT_NONE);
        for(uint32_t k = 0; k < r->root_count; k++) {
            if(r->roots[k]) {
                lv_obj_set_layout(r->roots[k], LV_LAYOUT_NONE);
                lv_obj_set_parent(r->roots[k], condemned);   /*sync-detach from live parent*/
            }
        }
        lv_obj_delete_async(condemned);                       /*async, outside the batch*/
    }
    else {
        /*OOM on the sink is catastrophic; fall back to per-root async delete so we
         *still never sync-delete inside the observer batch.*/
        LV_LOG_ERROR("OOM: <frag> teardown sink; falling back to per-root async delete");
        for(uint32_t k = 0; k < r->root_count; k++) {
            if(r->roots[k]) lv_obj_delete_async(r->roots[k]);
        }
    }

    lv_free(r->roots);
    r->roots = NULL;
    r->root_count = 0;
}

/* Shared rebuild core: async-teardown the prior expansion, then replay events
 * [lo,hi) `count` times into r->parent via a reconstructed parser state. Used by
 * both <repeat> (lo=0, hi=event_count, count=subject value) and <if> (count=1,
 * lo/hi = the selected true/false slice). */
static void xml_frag_rebuild(xml_frag_record_t * r, uint32_t lo, uint32_t hi, int32_t count)
{
    if(r == NULL || r->in_rebuild) return;
    r->in_rebuild = true;

    xml_frag_teardown(r);   /* async off-tree; frees r->roots array */

    /*Reconstruct a minimal parser state to drive the captured body. Start from a
     *fully-initialized state (so no field is missed), then override with the
     *record's snapshots.*/
    lv_xml_parser_state_t tmp_state;
    lv_xml_parser_state_init(&tmp_state);
    tmp_state.scope = r->scope;                          /*value copy; list heads shared read-only*/
    tmp_state.parent = r->parent;
    tmp_state.parent_attrs = (const char **)r->parent_attrs;
    tmp_state.parent_scope = r->parent_scope;
    tmp_state.context = r->capture;

    lv_obj_t ** pnode = lv_ll_ins_head(&tmp_state.parent_ll);
    if(pnode) *pnode = r->parent;

    xml_frag_expand(&tmp_state, (xml_frag_capture_t *)r->capture, lo, hi, count,
                    &r->roots, &r->root_count);

    /*Free everything the replay allocated on the temp state. The composed-strings
     *list (from `${name}` in the body) is per-state and would leak per rebuild
     *without this. The scope snapshot shares nodes with the registered scope, so
     *do NOT clear its linked lists here.*/
    xml_state_free_composed(&tmp_state);
    lv_ll_clear(&tmp_state.parent_ll);
    free_pcdata_ll(&tmp_state);

    r->in_rebuild = false;
}

/** <repeat> count-subject observer: re-materialize the full body `count` times for
 *  the new count. Also serves the INITIAL expansion via the immediate fire that
 *  lv_subject_add_observer performs at registration. */
static void xml_frag_rebuild_cb(lv_observer_t * observer, lv_subject_t * subject)
{
    LV_UNUSED(subject);
    xml_frag_record_t * r = (xml_frag_record_t *)lv_observer_get_user_data(observer);
    if(r == NULL) return;
    int32_t count = r->count_subject ? lv_subject_get_int(r->count_subject) : 0;
    if(count < 0) count = 0;
    if(count > 256) {
        LV_LOG_WARN("<repeat> count subject resolved to %d, clamping to 256", (int)count);
        count = 256;
    }
    xml_frag_rebuild(r, 0, ((xml_frag_capture_t *)r->capture)->event_count, count);
}

/** <if> reactive trigger: pick the true/false slice from the new cond value and
 *  rebuild it (count=1). Runs on the main thread inside a UpdateQueue drain
 *  (subject-set path) and via lv_xml_expr_bind's immediate fire for the initial
 *  build. */
static void if_cond_changed_cb(void * record, int32_t value)
{
    xml_frag_record_t * r = (xml_frag_record_t *)record;
    xml_frag_capture_t * cap = (xml_frag_capture_t *)r->capture;
    uint32_t lo, hi;
    if(value != 0) {
        lo = 0;
        hi = cap->has_else ? cap->else_split : cap->event_count;
    }
    else {
        lo = cap->has_else ? cap->else_split : cap->event_count;
        hi = cap->event_count;
    }
    xml_frag_rebuild(r, lo, hi, 1);
}

/** Generic: move a captured body into a retained fragment record so its expansion
 *  can be reactively rebuilt (implemented in lv_xml.c, which owns the capture
 *  type). Creates the record in the REGISTERED scope's `frag_ll` (recovered by
 *  name) — NOT `state->scope`, whose `frag_ll` head is a by-value copy that the
 *  registered scope never sees. Wires NO trigger/observer — the caller (the
 *  `</repeat>` close path today; `<if>` in a later task) owns which subject drives
 *  the rebuild and registers it after this returns. */
static xml_frag_record_t * xml_frag_retain(lv_xml_parser_state_t * state, xml_frag_capture_t * cap)
{
    lv_xml_component_scope_t * reg = lv_xml_component_get_scope(state->scope.name);
    xml_frag_record_t * r = reg ? lv_ll_ins_tail(&reg->frag_ll) : NULL;
    if(r == NULL) {
        /*No home for the record (unnamed scope or OOM): the caller falls back to a
         *one-shot expansion + capture_free so the tree is still populated.*/
        if(reg == NULL) LV_LOG_WARN("<if>/<repeat> subject-bound in an unnamed scope; reactivity disabled");
        else LV_LOG_ERROR("OOM: failed to retain frag record; reactivity disabled");
        return NULL;   /* caller falls back to a one-shot expansion + capture_free */
    }

    lv_memzero(r, sizeof(*r));
    r->capture = cap;                 /*ownership moves into the record*/
    r->owner_scope = reg;             /*the frag_ll this node lives in; see the field's comment*/

    lv_obj_t ** tail = lv_ll_get_tail(&state->parent_ll);
    r->parent = tail ? *tail : state->parent;
    r->view_root = state->view;       /*instance root; its delete reclaims this record*/

    r->scope = state->scope;          /*value snapshot; read-only during rebuild*/
    r->parent_scope = state->parent_scope;
    r->parent_attrs = state->parent_attrs ? xml_frag_copy_attrs(state->parent_attrs) : NULL;

    /*The capture is now owned by the record; detach it from the live parse state
     *before the caller wires the observer so the live state never double-frees it.*/
    state->context = NULL;

    /*Tie the record + observer lifetime to the INSTANCE. The bound subject is
     *shared (a scope subject reused across instances, or a global) and outlives any
     *one instance, so the observer must go when the instance's tree is deleted —
     *otherwise it fires the rebuild on freed roots (UAF). Mirror the timeline
     *cleanup: an LV_EVENT_DELETE cb on the view root. If there is no view root
     *(degenerate parse), fall back to the unregister-time sweep only.*/
    if(r->view_root) {
        lv_obj_add_event_cb(r->view_root, xml_frag_instance_delete_cb, LV_EVENT_DELETE, r);
    }
    else {
        LV_LOG_WARN("frag subject-bound with no view root; lifetime falls back to unregister");
    }
    return r;
}

/** Free the owned heap of a fragment record: detach the observer, free the
 *  captured body, the roots ARRAY (never the widgets — the instance owns those),
 *  and the parent-attrs snapshot. Does NOT free the record node itself, remove the
 *  delete cb, or unlink from `frag_ll` — the two callers do that differently. */
static void xml_frag_record_free_heap(xml_frag_record_t * r)
{
    /*<repeat> reactive count observer: it is obj-bound to r->view_root, so on the
     *instance-delete path (this function, called from xml_frag_instance_delete_cb)
     *LVGL's unsubscribe_on_delete_cb on that same view root removes it — and if the
     *count subject was deinited first, lv_subject_deinit already freed the node and
     *removed that unsubscribe cb. Either way the node must NOT be removed again here;
     *doing so double-frees / reads a freed observer (UAF at shutdown). Just drop the
     *pointer. The unregister-sweep path (lv_xml_frag_record_free) removes it BEFORE
     *reaching this function, while the instance + subject are still alive. Mirrors the
     *r->bind handling just below.*/
    r->observer = NULL;
    /*<if> reactive bind: on the instance-delete path (this function, called from
     *xml_frag_instance_delete_cb) the bind's OWN expr_bind_delete_cb is also
     *registered on this SAME view_root's LV_EVENT_DELETE and frees the bind itself
     *(observers + expr + bind ctx). Calling lv_xml_expr_unbind here would double-free
     *it. Just drop the pointer; the unregister-sweep path (lv_xml_frag_record_free)
     *is the one that must call _unbind, BEFORE reaching this function.*/
    r->bind = NULL;
    xml_frag_capture_free((xml_frag_capture_t *)r->capture);
    r->capture = NULL;
    lv_free(r->roots);                /*the array only — widgets are freed by LVGL*/
    r->roots = NULL;
    r->root_count = 0;
    if(r->parent_attrs) {
        for(size_t a = 0; r->parent_attrs[a]; a++) lv_free(r->parent_attrs[a]);
        lv_free(r->parent_attrs);
        r->parent_attrs = NULL;
    }
}

/** LV_EVENT_DELETE on the instance view root: reclaim the record for THIS instance.
 *  The instance's widget tree (including this expansion's roots) is being freed by
 *  LVGL right now, so never touch roots[] — only detach the observer, free the
 *  record heap, unlink the record from `frag_ll`, and free the node. Unlinking is
 *  what keeps lv_xml_component_unregister from double-freeing it later. This cb
 *  never runs during a rebuild's own teardown: that deletes an off-tree condemned
 *  container (which holds the old roots), not the view root. */
static void xml_frag_instance_delete_cb(lv_event_t * e)
{
    xml_frag_record_t * r = (xml_frag_record_t *)lv_event_get_user_data(e);
    if(r == NULL) return;

    /*The owning scope is the one this node was inserted into, NOT whatever answers
     *to the name today: a hot reload replaces the definition under the same name
     *while this instance is still alive, so a by-name lookup would unlink the node
     *from the NEW scope's frag_ll and leave the old one's head dangling. The
     *pointer is safe to hold because a scope with live instances is never freed.*/
    lv_xml_component_scope_t * reg = r->owner_scope;

    xml_frag_record_free_heap(r);

    if(reg) lv_ll_remove(&reg->frag_ll, r);
    lv_free(r);                       /*the record IS the ll node (see lv_ll_ins_tail)*/
}

/** Free a fragment record from the scope-teardown path - records whose instances
 *  are still alive when the scope itself goes away. A scope with live instances
 *  is now held rather than freed, so that is the forced lv_xml_component_deinit()
 *  teardown, plus records that never had a view root to hang the instance-delete
 *  cb on. Removes the pending instance-delete cb first so
 *  it cannot fire on the freed record after the instance is later deleted, then
 *  frees the record heap. The node itself is freed by the caller's lv_ll_clear. */
void lv_xml_frag_record_free(xml_frag_record_t * r)
{
    if(r == NULL) return;
    /*<repeat> reactive count observer: the instance and its count subject are still
     *alive on this unregister-sweep path (called BEFORE subjects_ll teardown), so
     *remove the observer here — free_heap no longer does. lv_observer_remove also
     *strips the view root's unsubscribe_on_delete_cb (for_obj+target), so the later
     *instance delete cannot re-remove a freed node. Done before nulling view_root so
     *the target is still valid; a no-op for <if> records (observer is NULL).*/
    if(r->observer) {
        lv_observer_remove(r->observer);
        r->observer = NULL;
    }
    if(r->view_root) {
        lv_obj_remove_event_cb_with_user_data(r->view_root, xml_frag_instance_delete_cb, r);
        r->view_root = NULL;
    }
    /*<if> reactive bind: the instance is still alive here (this is the unregister
     *sweep, called BEFORE subjects_ll teardown), so the bind's expr_bind_delete_cb
     *has NOT fired and its observers still sit on subjects about to be freed.
     *_unbind removes those observers AND the expr_bind_delete_cb hook from
     *r->view_root, so the later lv_obj_delete(v) does not double-free.*/
    if(r->bind) {
        lv_xml_expr_unbind((lv_xml_expr_bind_t *)r->bind);
        r->bind = NULL;
    }
    xml_frag_record_free_heap(r);
}

/** True if `value` contains an embedded `${...}` composition marker anywhere.
 *  A bare `$name` (whole-value param / `$i`) has no `{` after the `$`, so it is
 *  left to the whole-value branch in resolve_params. */
static bool xml_value_has_compose(const char * value)
{
    const char * d = value;
    while((d = lv_strchr(d, '$')) != NULL) {
        if(d[1] == '{') return true;
        d++;
    }
    return false;
}

/** True if `s` parses fully as a base-10 integer: optional surrounding whitespace,
 *  optional sign, at least one digit, nothing else. Decides whether a component
 *  param may serve as a numeric `${expr}` operand. */
static bool xml_str_is_integer(const char * s)
{
    if(s == NULL) return false;
    while(*s == ' ' || *s == '\t') s++;
    if(*s == '+' || *s == '-') s++;
    if(!(*s >= '0' && *s <= '9')) return false;    /* require at least one digit */
    while(*s >= '0' && *s <= '9') s++;
    while(*s == ' ' || *s == '\t') s++;
    return *s == '\0';
}

/** True if `s` is a single bare identifier: matches ^[A-Za-z_][A-Za-z0-9_]*$ with no
 *  operators, spaces, or leading digit. A bare-identifier token keeps the legacy
 *  name-substitution path (`${i}`, `${grp}`); anything else is an integer expression. */
static bool xml_token_is_bare_identifier(const char * s)
{
    if(s == NULL || *s == '\0') return false;
    char c0 = *s;
    if(!((c0 >= 'A' && c0 <= 'Z') || (c0 >= 'a' && c0 <= 'z') || c0 == '_')) return false;
    for(const char * p = s + 1; *p; p++) {
        char c = *p;
        if(!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
             (c >= '0' && c <= '9') || c == '_')) return false;
    }
    return true;
}

/** Resolve a single `${name}` token. `${i}` yields the current `<repeat>` loop
 *  index (only meaningful during replay); any other name resolves against the
 *  component's parent attributes then its param defaults. `scratch` backs the
 *  formatted index. Returns NULL when the name cannot be resolved. */
static const char * xml_compose_lookup(lv_xml_parser_state_t * state, const char * name,
                                       char * scratch, size_t scratch_sz)
{
    if(lv_streq(name, "i")) {
        xml_frag_capture_t * rc = state ? (xml_frag_capture_t *)state->context : NULL;
        if(rc == NULL || !rc->replaying) return NULL; /*`$i` only exists during a replay*/
        lv_snprintf(scratch, scratch_sz, "%d", (int)rc->current_index);
        return scratch;
    }
    const char * v = lv_xml_get_value_of(state->parent_attrs, name);
    /*An unresolved param passed down as a `$`-sigil is not a usable value.*/
    if(v == NULL || v[0] == '$') v = get_param_default(&state->scope, name);
    return v; /*may be NULL*/
}

/** Max distinct operands an inline `${expr}` may reference — matches the evaluator's
 *  internal subject cap. */
#define XML_COMPOSE_EXPR_MAX_OPERANDS 32

/** Resolver context for an inline `${expr}`. Lives on the stack for exactly one
 *  compose call (compile+eval+free), so index_subject / param_subjects are
 *  stack-lifetime and deinit'd right after eval. No observers are ever attached. */
typedef struct {
    lv_xml_parser_state_t * state;
    xml_frag_capture_t * rc;                      /* NULL unless in a replaying <repeat> */
    lv_subject_t index_subject;                   /* seeded to rc->current_index for `i` */
    lv_subject_t param_subjects[XML_COMPOSE_EXPR_MAX_OPERANDS]; /* numeric-param operands (Task 2) */
    uint32_t param_count;
} xml_compose_expr_ctx_t;

/** Maps an identifier in a `${expr}` to a subject: `i` -> the loop index; a numeric
 *  component param -> a transient int subject (Task 2); otherwise a real scope subject.
 *  Returns NULL for an unresolvable name, which makes lv_xml_expr_compile fail. */
static lv_subject_t * xml_compose_expr_resolver(void * vctx, const char * name)
{
    xml_compose_expr_ctx_t * c = (xml_compose_expr_ctx_t *)vctx;

    /* 1. loop index */
    if(lv_streq(name, "i"))
        return (c->rc && c->rc->replaying) ? &c->index_subject : NULL;

    /* 2. numeric component-param / parent-attr -> transient int operand.
     *    Mirrors xml_compose_lookup's param resolution (parent attrs, then default). */
    {
        const char * pv = lv_xml_get_value_of(c->state->parent_attrs, name);
        if(pv == NULL || pv[0] == '$') pv = get_param_default(&c->state->scope, name);
        if(pv && xml_str_is_integer(pv)) {
            if(c->param_count >= XML_COMPOSE_EXPR_MAX_OPERANDS) return NULL;
            lv_subject_t * s = &c->param_subjects[c->param_count++];
            lv_subject_init_int(s, lv_xml_atoi(pv));
            return s;
        }
    }

    /* 3. real scope subject (also finds globally registered subjects) */
    return lv_xml_get_subject(&c->state->scope, name);
}

/** Splice every `${name}` in `raw` with its resolved value, returning a freshly
 *  allocated string tracked on state->composed_strings (freed at parse end).
 *  An unresolved `${name}` splices empty and warns. Returns "" on OOM. */
static char * xml_compose_indexed(lv_xml_parser_state_t * state, const char * raw)
{
    size_t cap = lv_strlen(raw) + 1;
    char * out = lv_malloc(cap);
    if(out == NULL) return "";
    size_t len = 0; /*bytes used, excluding the terminating NUL*/

    /*Ensure room for `n` more bytes plus a NUL, growing geometrically.*/
#define XML_COMPOSE_APPEND(src, n)                                   \
    do {                                                             \
        size_t add_ = (n);                                           \
        if(len + add_ + 1 > cap) {                                   \
            while(len + add_ + 1 > cap) cap *= 2;                    \
            char * grown_ = lv_realloc(out, cap);                    \
            if(grown_ == NULL) { lv_free(out); return ""; }         \
            out = grown_;                                            \
        }                                                            \
        lv_memcpy(out + len, (src), add_);                          \
        len += add_;                                                 \
    } while(0)

    const char * p = raw;
    while(*p) {
        if(p[0] == '$' && p[1] == '{') {
            const char * close = lv_strchr(p + 2, '}');
            if(close == NULL) {
                /*Unterminated `${` — copy the remainder verbatim and stop.*/
                XML_COMPOSE_APPEND(p, lv_strlen(p));
                break;
            }
            size_t nlen = (size_t)(close - (p + 2));
            char nbuf[256];
            if(nlen >= sizeof(nbuf)) {
                LV_LOG_WARN("${...} token too long in '%s'; splicing empty", raw);
            }
            else {
                lv_memcpy(nbuf, p + 2, nlen);
                nbuf[nlen] = '\0';

                if(xml_token_is_bare_identifier(nbuf)) {
                    /*Legacy name substitution: ${i}, ${grp}, ${prop}.*/
                    char scratch[16];
                    const char * rep = xml_compose_lookup(state, nbuf, scratch, sizeof(scratch));
                    if(rep == NULL) {
                        LV_LOG_WARN("${%s} could not be resolved in '%s'; splicing empty", nbuf, raw);
                    }
                    else {
                        XML_COMPOSE_APPEND(rep, lv_strlen(rep));
                    }
                }
                else {
                    /*Integer expression: evaluate once and splice the result as text.
                     *Resolve-once — compile/eval/free all happen here; no observers.*/
                    xml_compose_expr_ctx_t ectx;
                    ectx.state = state;
                    ectx.rc = state ? (xml_frag_capture_t *)state->context : NULL;
                    ectx.param_count = 0;
                    lv_subject_init_int(&ectx.index_subject,
                                        (ectx.rc && ectx.rc->replaying) ? (int32_t)ectx.rc->current_index : 0);

                    lv_xml_expr_t * ex = lv_xml_expr_compile(nbuf, xml_compose_expr_resolver, &ectx);
                    if(ex == NULL) {
                        LV_LOG_WARN("${%s} could not be evaluated in '%s'; splicing empty", nbuf, raw);
                    }
                    else {
                        char exprbuf[16];
                        lv_snprintf(exprbuf, sizeof(exprbuf), "%d", (int)lv_xml_expr_eval(ex));
                        lv_xml_expr_free(ex); /*free before APPEND, which may return early on OOM*/
                        XML_COMPOSE_APPEND(exprbuf, lv_strlen(exprbuf));
                    }
                    for(uint32_t k = 0; k < ectx.param_count; k++)
                        lv_subject_deinit(&ectx.param_subjects[k]);
                    lv_subject_deinit(&ectx.index_subject);
                }
            }
            p = close + 1;
        }
        else {
            XML_COMPOSE_APPEND(p, 1);
            p++;
        }
    }
    out[len] = '\0';
#undef XML_COMPOSE_APPEND

    /*Track the owned string on state; freed once at parse end.*/
    if(!xml_state_track_string(state, out)) { lv_free(out); return ""; }
    return out;
}

/** Track an owned heap string on state->composed_strings so it is freed once at
 *  parse end (same pool as the `${...}` composition results). Returns false on
 *  OOM — the caller still owns `owned` and must free it. */
static bool xml_state_track_string(lv_xml_parser_state_t * state, char * owned)
{
    if(state->composed_count == state->composed_cap) {
        uint32_t new_cap = state->composed_cap ? state->composed_cap * 2 : 8;
        char ** na = lv_realloc(state->composed_strings, sizeof(char *) * new_cap);
        if(na == NULL) return false;
        state->composed_strings = na;
        state->composed_cap = new_cap;
    }
    state->composed_strings[state->composed_count++] = owned;
    return true;
}

/** Allocate `a` concatenated with `b` as a NUL-terminated string owned by
 *  `state` (freed at parse end). Either argument may be NULL (treated as ""),
 *  Returns "" on OOM. */
static const char * xml_state_concat2(lv_xml_parser_state_t * state, const char * a, const char * b)
{
    size_t la = a ? lv_strlen(a) : 0;
    size_t lb = b ? lv_strlen(b) : 0;
    char * out = lv_malloc(la + lb + 1);
    if(out == NULL) return "";
    if(la) lv_memcpy(out, a, la);
    if(lb) lv_memcpy(out + la, b, lb);
    out[la + lb] = '\0';
    if(!xml_state_track_string(state, out)) { lv_free(out); return ""; }
    return out;
}

/** Free all composed strings tracked on `state`. Called once per parse (both the
 *  error and success exits of lv_xml_create_in_scope). A reactive-rebuild caller
 *  (subject-bound `<repeat>`, Task 3) that drives xml_frag_expand through its
 *  own parser state must call this after each rebuild for the same reason. */
static void xml_state_free_composed(lv_xml_parser_state_t * state)
{
    for(uint32_t k = 0; k < state->composed_count; k++) lv_free(state->composed_strings[k]);
    lv_free(state->composed_strings);
    state->composed_strings = NULL;
    state->composed_count = 0;
    state->composed_cap = 0;
}

/**
 * Destroy the widgets an ABORTED lv_xml_create_in_scope() parse already built.
 *
 * The caller of a failed create gets NULL back and therefore no handle to the
 * partial tree, so if this does not run the fragment stays attached to the
 * caller's own parent forever.
 *
 * Identification is by watermark, not by `state->view`: the view root is only
 * assigned once the `<view>` element itself has been created, and a parse that
 * fails earlier - or one whose `<view>` create_cb returns NULL - still builds
 * following elements straight onto the caller's parent with `state->view` left
 * NULL. Deleting from the tail down to `children_before` covers both shapes and
 * cannot reach a child the caller already had.
 *
 * `parent == NULL` is the screen case: nothing was appended to a parent, so the
 * only thing to reclaim is the orphan screen in `state->view`.
 */
static void destroy_partial_view(lv_xml_parser_state_t * state, lv_obj_t * parent, uint32_t children_before)
{
    if(parent == NULL) {
        if(state->view) lv_obj_delete(state->view);
    }
    else {
        uint32_t count = lv_obj_get_child_count(parent);
        while(count > children_before) {
            lv_obj_delete(lv_obj_get_child(parent, (int32_t)count - 1));
            uint32_t now = lv_obj_get_child_count(parent);
            /*A delete that does not shrink the parent would spin forever; bail
             *rather than hang, leaving the remainder attached.*/
            if(now >= count) break;
            count = now;
        }
    }

    state->view = NULL;
    state->item = NULL;
}

/* Resolver shim: <if cond="..."> looks up subjects referenced by the expression
 * in the enclosing component's scope, same as every other expression-consuming
 * tag (mirrors cond_flag_scope_resolver in lv_xml_obj_parser.c). */
static lv_subject_t * frag_cond_resolver(void * ctx, const char * name)
{
    return lv_xml_get_subject((lv_xml_component_scope_t *)ctx, name);
}

static void view_start_element_handler(void * user_data, const char * name, const char ** attrs)
{
    lv_xml_parser_state_t * state = (lv_xml_parser_state_t *)user_data;

    xml_frag_capture_t * cap = (xml_frag_capture_t *)state->context;

    /*Enter capture on <repeat>. Must run before the pcdata push below: <repeat>
     *creates no object and pushes no stack node, so it owns no pcdata entry.
     *A capture is ALWAYS allocated (even on missing/unparseable count) so the
     *matching </repeat> is still intercepted and the parent stack stays balanced
     *— a bare early-return would let </repeat> pop a frame it never pushed and
     *mis-parent every following sibling. Missing count => expand zero times.*/
    if(lv_streq(name, "repeat") && (cap == NULL || !cap->replaying)) {
        const char * cnt = lv_xml_get_value_of(attrs, "count");
        if(cnt == NULL) {
            LV_LOG_WARN("<repeat> is missing the required 'count' attribute; expanding zero times");
        }
        cap = lv_zalloc(sizeof(xml_frag_capture_t));
        if(cap == NULL) {
            /*Catastrophic: without a capture the stack cannot be kept balanced.
             *Log loudly; the tree may be corrupt but there is nothing to recover.*/
            LV_LOG_ERROR("OOM: failed to allocate <repeat> capture; tree may be corrupt");
            return;
        }
        cap->active = true;
        cap->base_depth = (uint32_t)lv_ll_get_len(&state->parent_ll);
        if(cnt) {
            size_t clen = lv_strlen(cnt);
            cap->count_raw = lv_malloc(clen + 1);
            /*On copy OOM leave count_raw NULL: xml_repeat_resolve_count treats NULL
             *as 0, so the body still expands zero times and the stack stays balanced.*/
            if(cap->count_raw) lv_memcpy(cap->count_raw, cnt, clen + 1);
        }
        state->context = cap;
        return;                          /*<repeat> creates no object, no stack push*/
    }

    /*Enter capture on <if>. Same balanced-stack reasoning as <repeat> above: a
     *capture is always allocated (even with a missing/unparseable cond) so the
     *matching </if> is still intercepted. Missing cond => treated as false at
     *</if> (v defaults to 0 when ex is NULL).*/
    if(lv_streq(name, "if") && (cap == NULL || !cap->replaying)) {
        const char * cnd = lv_xml_get_value_of(attrs, "cond");
        if(cnd == NULL) LV_LOG_WARN("<if> is missing the required 'cond' attribute; treated as false");
        cap = lv_zalloc(sizeof(xml_frag_capture_t));
        if(cap == NULL) { LV_LOG_ERROR("OOM: <if> capture; tree may be corrupt"); return; }
        cap->active = true;
        cap->is_if = true;
        cap->base_depth = (uint32_t)lv_ll_get_len(&state->parent_ll);
        if(cnd) {
            size_t l = lv_strlen(cnd);
            cap->cond_raw = lv_malloc(l + 1);
            if(cap->cond_raw) lv_memcpy(cap->cond_raw, cnd, l + 1);
        }
        state->context = cap;
        return;                          /*<if> creates no object, no stack push*/
    }

    /*Buffer body events while capturing (and not yet replaying).*/
    if(cap && cap->active && !cap->replaying) {
        if(cap->is_if && lv_streq(name, "else")) {
            if(cap->has_else) {
                LV_LOG_WARN("<if> has more than one <else>; the first split wins");
            }
            else {
                cap->else_split = cap->event_count;   /*false-body starts here*/
                cap->has_else = true;
            }
            return;                      /*skip the marker's OWN start event*/
        }
        xml_frag_buffer_event(cap, /*kind=*/0, name, attrs);
        return;                          /*no creation, no stack mutation*/
    }

    /*Stray <else> outside any <if>: no active capture reached this element.*/
    if(lv_streq(name, "else")) {
        LV_LOG_WARN("<else> outside <if>; ignored");
        return;
    }

    state->tag_name = name;

    lv_xml_pcdata_entry_t * pcdata = lv_ll_ins_tail(&state->pcdata_ll);
    if(pcdata) {
        lv_memzero(pcdata, sizeof(*pcdata));
        if(attrs) {
            pcdata->has_conflict = lv_xml_get_value_of(attrs, "text") != NULL ||
                                   lv_xml_get_value_of(attrs, "bind_text") != NULL ||
                                   lv_xml_get_value_of(attrs, "translation_tag") != NULL;
        }
    }

    bool is_view = false;
    if(lv_streq(name, "view")) {
        const char * extends = lv_xml_get_value_of(attrs, "extends");
        name = extends ? extends : "lv_obj";
        is_view = true;
    }

    lv_obj_t ** current_parent_p = lv_ll_get_tail(&state->parent_ll);
    if(current_parent_p == NULL) {
        if(state->parent == NULL) {
            LV_LOG_ERROR("There is no parent object available for %s. This should never happen.", name);
            return;
        }
        else {
            current_parent_p = &state->parent;
        }
    }
    else {
        state->parent = *current_parent_p;
    }

    /*In `state->attrs` we have parameters of the component creation
     *E.g. <my_button x="10" title="Hello"/>
     *In `attrs` we have the attributes of child of the view.
     *E.g. in `my_button` `<lv_label x="5" text="${title}".
     *This function changes the pointers in the child attributes if the start with '$'
     *with the corresponding parameter. E.g. "text", "${title}" -> "text", "Hello" */
    resolve_params(state, &state->scope, state->parent_scope, attrs, state->parent_attrs);

    resolve_consts(attrs, &state->scope);

    state->item = NULL;
    /* Select the widget specific parser type based on the name */
    lv_widget_processor_t * p = lv_xml_widget_get_processor(name);
    if(p) {
        state->item = p->create_cb(state, attrs);
        if(state->item) {
            /*If it's a widget remove all styles. E.g. if it extends an `lv_button`
             *now it has the button theme styles. However if it were a real widget
             *it had e.g. `my_widget_class` so the button's theme wouldn't apply on it.
             *Removing the style will ensure a better preview*/
            if(state->scope.is_widget && is_view) lv_obj_remove_style_all(state->item);

            /*Apply the attributes from e.g. `<lv_slider value="30" x="20">`*/
            if(p->builtin) lv_xml_attr_check_begin(state, attrs, name);
            p->apply_cb(state, attrs);
            lv_xml_attr_check_end(state);
        }
    }

    /* If not a widget, check if it is a component */
    if(state->item == NULL) {
        state->item = lv_xml_component_process(state, name, attrs);
    }

    /* If not a component either, check if it is a slot, e.g. my_button-icon */
    if(state->item == NULL) {
        char buf[128];
        if(lv_strlen(name) >= sizeof(buf)) {
            LV_LOG_WARN("Component/slot name '%s' is too long (max 127 chars); skipping slot parsing.", name);
        }
        else {
            lv_strlcpy(buf, name, sizeof(buf));
            char * bufp = buf;
            const char * comp_name = lv_xml_split_str(&bufp, '-');
            const char * slot_name = bufp;
            lv_xml_component_scope_t * comp_scope = lv_xml_component_get_scope(comp_name);
            if(comp_scope && lv_streq(comp_name, comp_scope->name)) {
                state->item = lv_obj_find_by_name(state->parent, slot_name);
            }
        }
    }

    /* If it isn't a slot either then it is unknown. This is almost always a
     * STALE BINARY: a C++-registered widget (lv_xml_register_widget) exists in
     * the XML but not in the running binary, so the XML was updated without a
     * rebuild. Log loudly at ERROR so it is not lost in the noise.
     *
     * The unknown element creates no object, but its closing tag is still
     * delivered and view_end_element_handler pops unconditionally — so it must
     * push a frame anyway or the stack loses one level per unknown tag and
     * every following sibling mis-parents one level too high (the sibling after
     * the tag escapes the component's view root entirely and lands on the
     * screen at 0,0, bleeding across panels). The frame repeats the CURRENT
     * parent, which both balances the pop and keeps the unknown tag's children
     * attached to the nearest real ancestor rather than dropping them: the
     * missing widget is a wrapper, and its contents are still valid widgets the
     * layout expects to see. */
    if(state->item == NULL) {
        LV_LOG_ERROR("XML tag '%s' is not a known widget/element/component/slot — "
                     "likely an unregistered widget in a STALE BINARY (rebuild required). "
                     "Its children are attached to the enclosing parent instead.",
                     name);

        lv_obj_t ** unknown_parent = lv_ll_ins_tail(&state->parent_ll);
        if(unknown_parent == NULL) {
            LV_LOG_ERROR("OOM: failed to allocate parent node for unknown tag '%s'; "
                         "the parent stack is now unbalanced", name);
            return;
        }
        *unknown_parent = state->parent;
        return;
    }

    if(pcdata) pcdata->item = state->item;

    void ** new_parent = lv_ll_ins_tail(&state->parent_ll);
    if(new_parent == NULL) {
        LV_LOG_ERROR("OOM: failed to allocate parent node for '%s'", name);
        return;
    }
    *new_parent = state->item;

    if(is_view) {
        state->view = state->item;
    }
}

static void view_end_element_handler(void * user_data, const char * name)
{
    lv_xml_parser_state_t * state = (lv_xml_parser_state_t *)user_data;

    xml_frag_capture_t * cap = (xml_frag_capture_t *)state->context;
    if(cap && cap->active && !cap->replaying) {
        uint32_t depth = (uint32_t)lv_ll_get_len(&state->parent_ll);
        if(lv_streq(name, "repeat") && depth == cap->base_depth) {
            /*Closing </repeat>: resolve count and replay the buffered body. If
             *`count` names a live subject, retain the capture in a record and wire
             *a count observer so the expansion re-materializes reactively; the
             *observer's immediate fire also drives the initial expansion. Otherwise
             *(literal / #const / unresolved subject name, or retain failure) expand
             *once and free the capture.*/
            if(xml_repeat_count_is_subject(cap->count_raw)) {
                lv_subject_t * cs = lv_xml_get_subject(&state->scope, cap->count_raw);
                if(cs) {
                    xml_frag_record_t * r = xml_frag_retain(state, cap);
                    if(r) {
                        r->count_subject = cs;
                        /*Bind the count observer to the INSTANCE view root (not a
                         *plain observer). This makes the observer node co-owned by
                         *LVGL's standard machinery: whichever dies first — the view
                         *root (unsubscribe_on_delete_cb) or the count subject
                         *(lv_subject_deinit sweeping subs_ll) — removes the node and
                         *disarms the other side. A plain lv_subject_add_observer is
                         *removed only by the frag record's own view-root delete cb, so
                         *if the subject is deinited first (e.g. a panel frees its
                         *scalar subjects at shutdown, BEFORE lv_deinit deletes the
                         *repeat widgets) the record keeps a dangling r->observer and
                         *the later free_heap double-removes a freed node (UAF).*/
                        r->observer = lv_subject_add_observer_obj(cs, xml_frag_rebuild_cb,
                                                                  r->view_root, r);
                        return;
                    }
                    /*retain failed: fall through to the one-shot path below
                     *(state->context still == cap)*/
                }
            }
            /*One-shot path: literal / #const / unresolved subject name, OR a
             *subject-bound count whose retain failed (unnamed scope or OOM, so
             *reactivity is disabled). xml_repeat_resolve_count clamps a subject
             *count to 256 — the pre-refactor retain fallback read the subject
             *value unclamped here, the lone expansion path without the cap. This
             *now clamps like every other path (rebuild_cb, literal), bounding
             *runaway widget creation in that degenerate case; the clamp is the
             *only intentional behavior change in the xml_frag_* extraction.*/
            int32_t count = xml_repeat_resolve_count(state, cap->count_raw);
            xml_frag_expand(state, cap, 0, cap->event_count, count, NULL, NULL);
            xml_frag_capture_free(cap);      /*literal path: buffer not retained*/
            state->context = NULL;
            return;
        }
        if(cap->is_if && lv_streq(name, "else")) {
            return;                      /*skip the marker's OWN end event; split already taken*/
        }
        if(cap->is_if && lv_streq(name, "if") && depth == cap->base_depth) {
            /*Evaluate cond once, then branch on whether it references any subjects.*/
            lv_xml_expr_t * ex = cap->cond_raw
                                  ? lv_xml_expr_compile(cap->cond_raw, frag_cond_resolver, &state->scope) : NULL;
            if(cap->cond_raw && ex == NULL) {
                LV_LOG_WARN("<if>: failed to compile cond '%s'; treated as false", cap->cond_raw);
            }

            if(ex && lv_xml_expr_subject_count(ex) > 0) {
                /*Reactive: retain the capture into a record and bind the cond to the
                 *instance root. lv_xml_expr_bind's immediate fire performs the initial
                 *build via if_cond_changed_cb, so there is nothing else to do here.
                 *_bind TAKES OWNERSHIP of `ex` (do not free it below), and xml_frag_retain
                 *moved `cap` into the record (do not xml_frag_capture_free it below).*/
                /*Eval the initial value BEFORE handing `ex` to _bind: _bind takes
                 *ownership of `ex` and, on its own OOM, frees it — so reading the value
                 *first lets a bind failure still expand the correct slice.*/
                int32_t v0 = lv_xml_expr_eval(ex);
                xml_frag_record_t * r = xml_frag_retain(state, cap);   /*clears state->context*/
                if(r) {
                    r->bind = lv_xml_expr_bind(ex, r->view_root, if_cond_changed_cb, r);
                    if(r->bind == NULL) {
                        /*_bind OOM: `ex` is already freed; the record still owns the
                         *capture. Expand the v0 slice once so the <if> renders (just
                         *non-reactive) instead of silently staying empty. The record
                         *stays in frag_ll; its teardown paths already handle a NULL bind.*/
                        LV_LOG_ERROR("OOM: <if> reactive bind failed; expanding once, non-reactive");
                        uint32_t lo, hi;
                        if(v0 != 0) {
                            lo = 0;
                            hi = cap->has_else ? cap->else_split : cap->event_count;
                        }
                        else {
                            lo = cap->has_else ? cap->else_split : cap->event_count;
                            hi = cap->event_count;
                        }
                        xml_frag_rebuild(r, lo, hi, /*count=*/1);
                    }
                    return;
                }
                /*retain failed (unnamed scope or OOM): fall through to a one-shot
                 *expansion at the current value (state->context still == cap).*/
                LV_LOG_WARN("<if> reactive retain failed; expanding once, non-reactive");
            }

            int32_t v = ex ? lv_xml_expr_eval(ex) : 0;
            uint32_t lo, hi;
            if(v != 0) {
                lo = 0;
                hi = cap->has_else ? cap->else_split : cap->event_count;
            }
            else {
                lo = cap->has_else ? cap->else_split : cap->event_count;
                hi = cap->event_count;
            }
            xml_frag_expand(state, cap, lo, hi, /*count=*/1, NULL, NULL);
            if(ex) lv_xml_expr_free(ex);
            xml_frag_capture_free(cap);
            state->context = NULL;
            return;
        }
        xml_frag_buffer_event(cap, /*kind=*/1, name, NULL);
        return;
    }

    /*Stray </else> with no active <if> capture: its start pushed no parent/pcdata
     *frame (warned there), so it must pop none. Returning here — symmetric with the
     *stray-<else> start guard — keeps parent_ll balanced; falling through to the
     *unconditional pop below would remove the enclosing element's still-open frame
     *early and mis-parent every following sibling.*/
    if(lv_streq(name, "else")) {
        return;
    }

    apply_pending_inline_text(state, name);

    lv_obj_t ** current_parent = lv_ll_get_tail(&state->parent_ll);
    if(current_parent) {
        lv_ll_remove(&state->parent_ll, current_parent);
        lv_free(current_parent);
    }
}

static lv_anim_timeline_t * get_timeline_by_name(lv_obj_t * obj, const char * timeline_name)
{
    /*Get all the timelines of the target*/
    lv_anim_timeline_t ** timeline_array = NULL;
    lv_obj_send_event(obj, lv_event_xml_store_timeline, &timeline_array);
    if(timeline_array == NULL) {
        LV_LOG_WARN("No time lines are stored in target");
        return NULL;
    }

    /*Find the timeline with the requested timeline name*/
    uint32_t i;
    for(i = 0; timeline_array[i]; i++) {
        const char * name = lv_anim_timeline_get_user_data(timeline_array[i]);
        if(lv_streq(name, timeline_name)) return timeline_array[i];
    }

    return NULL;
}

static void create_timeline_instances(lv_xml_parser_state_t * state)
{
    /*The timeline descriptors ("blueprints") created when the components was registered
     *are stored in the "scope".
     *Based on the descriptors timeline and animation instances will be created for this this component*/
    lv_xml_component_scope_t * scope = &state->scope;

    if(lv_ll_is_empty(&scope->timeline_ll))  return;

    /*At this stage all children are created so any UI elements that
     *the animations and timelines can reference are exist. */
    lv_xml_timeline_t * timeline_dsc;

    /*Create an array to store the created timeline pointers*/
    lv_anim_timeline_t ** timeline_array;
    timeline_array = lv_malloc((lv_ll_get_len(&scope->timeline_ll) + 1) * sizeof(lv_anim_timeline_t *));
    LV_ASSERT_MALLOC(timeline_array);
    if(timeline_array == NULL) {
        LV_LOG_WARN("Couldn't allocate memory");
        return;
    }

    /*Read the timeline descriptors of the component and create
     *timeline instances based on them.*/
    uint32_t timeline_index = 0;
    LV_LL_READ(&scope->timeline_ll, timeline_dsc) {
        /*Save the name of the timeline. It will reference by this name in XML
         * (e.g. <play_animation_event target="comp_name" timeline="timeline_name">)*/
        lv_anim_timeline_t * my_timeline = lv_anim_timeline_create();
        my_timeline->user_data = lv_strdup(timeline_dsc->name);
        LV_ASSERT_MALLOC(my_timeline->user_data);
        if(my_timeline->user_data == NULL) {
            lv_anim_timeline_delete(my_timeline);
            lv_free(timeline_array);
            LV_LOG_WARN("Couldn't allocate memory");
            return;
        }
        /*Check all saved animation or incluce_timeline data of the component
         *and add them to the timeline instance. */
        lv_xml_anim_timeline_child_t * timeline_child;
        LV_LL_READ(&timeline_dsc->anims_ll, timeline_child) {
            /*Simple add the animation descriptors to instance's timeline*/
            if(timeline_child->is_anim) {
                lv_anim_t * a = &timeline_child->data.anim;
                lv_obj_t * target = NULL;
                if(lv_streq(a->var, "self")) target = state->view;
                else target = lv_obj_find_by_name(state->view, a->var);

                if(target == NULL) {
                    LV_LOG_WARN("No target widget is found with `%s` name", (char *)a->var);
                    continue;
                }

                int32_t delay = -a->act_time;
                lv_anim_timeline_add(my_timeline, delay, a);

                /*Once the animation descriptor is duplicated and saved in the timeline
                 *replace the target name a pointer to the target.
                 *TODO add an event to every referenced widget to remove their anim from the
                 *     timeline when they are deleted.*/
                lv_anim_t * new_a = &my_timeline->anim_dsc[my_timeline->anim_dsc_cnt - 1].anim;
                new_a->var = target;
            }
            /*Or include (merge) the referenced timelines*/
            else {
                lv_xml_anim_timeline_include_t * incl = &timeline_child->data.incl;
                /*Get the target first*/
                lv_obj_t * target;
                if(lv_streq(incl->target_name, "self")) target = state->view;
                else target = lv_obj_find_by_name(state->view, incl->target_name);

                if(target == NULL) {
                    LV_LOG_WARN("No target widget is found with `%s` name", incl->target_name);
                    continue;
                }

                lv_anim_timeline_t * include_timeline = get_timeline_by_name(target, incl->timeline_name);
                if(include_timeline == NULL) {
                    LV_LOG_WARN("Timeline `%s` is not found in `%s` component", incl->timeline_name, incl->target_name);
                    continue;
                }

                /*Copy all animations of include_timeline to this instance's timeline*/
                lv_anim_timeline_merge(my_timeline, include_timeline, incl->delay);
            }
        }

        timeline_array[timeline_index] = my_timeline;
        timeline_index++;
    }

    timeline_array[timeline_index] = NULL; /*Closing to avoid storing the length*/

    lv_obj_add_event_cb(state->view, get_timeline_from_event_cb, lv_event_xml_store_timeline, timeline_array);
    lv_obj_add_event_cb(state->view, free_timelines_event_cb, LV_EVENT_DELETE, timeline_array);
}


static void get_timeline_from_event_cb(lv_event_t * e)
{
    void ** out = lv_event_get_param(e);
    *out = lv_event_get_user_data(e);
}

static void free_timelines_event_cb(lv_event_t * e)
{
    lv_anim_timeline_t ** at_array = lv_event_get_user_data(e);
    uint32_t i;
    for(i = 0; at_array[i]; i++) {
        lv_free(lv_anim_timeline_get_user_data(at_array[i]));
        lv_anim_timeline_delete(at_array[i]);
    }
    lv_free(at_array);
}

#endif /* LV_USE_XML */
