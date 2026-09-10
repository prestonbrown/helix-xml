/**
 * @file lv_xml_style.c
 *
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2025 LVGL Kft
 * SPDX-FileCopyrightText: 2026 356C LLC
 */

/*********************
 *      INCLUDES
 *********************/
#include <lvgl.h>
#if LV_USE_XML

#include "lv_xml_base_types.h"
#include "lv_xml_parser.h"
#include "lv_xml_style.h"
#include "lv_xml_utils.h"
#include "lv_xml_component_private.h"
#include <string.h>

/*********************
 *      DEFINES
 *********************/
#ifdef _MSC_VER
    #define strtok_r strtok_s  // Use strtok_s as an equivalent to strtok_r in Visual Studio
#endif

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

static lv_anim_path_cb_t transition_easing_to_cb(const char * txt);
static bool transition_time_to_ms(const char * txt, const char * style_name,
                                  const char * attr_name, uint32_t * out_ms);
static void style_transition_install(lv_xml_style_t * xs, const char * props_str,
                                     uint32_t time, uint32_t delay, lv_anim_path_cb_t path,
                                     const char * style_name);
static bool transition_parse_shorthand(const char * value, char * buf, size_t buf_len,
                                       const char * style_name, const char ** props_str,
                                       uint32_t * duration, lv_anim_path_cb_t * easing,
                                       uint32_t * delay);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/*Expands to e.g.
  if(lv_streq(name, "height")) lv_style_set_height(style, lv_xml_to_size(value));
 */
#define SET_STYLE_IF(prop, value) if(lv_streq(name, #prop)) lv_style_set_##prop(style, value)

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

lv_result_t lv_xml_register_style(lv_xml_component_scope_t * scope, const char ** attrs)
{
    const char * style_name =  lv_xml_get_value_of(attrs, "name");
    if(style_name == NULL) {
        LV_LOG_WARN("'name' is missing from a style");
        return LV_RESULT_INVALID;
    }

    if(scope == NULL) scope = lv_xml_component_get_scope("globals");
    if(scope == NULL) return LV_RESULT_INVALID;


    lv_xml_style_t * xml_style;
    /*If a style with the same name is already created, use it */
    bool found = false;
    LV_LL_READ(&scope->style_ll, xml_style) {
        if(lv_streq(xml_style->name, style_name)) {
            found = true;
            LV_LOG_INFO("Style %s is already registered. Extending it with new properties.", style_name);
            break;
        }
    }

    if(!found) {
        xml_style = lv_ll_ins_tail(&scope->style_ll);
        /* lv_ll_ins_tail returns uninitialised storage; zero it before use so
         * lv_style_init's LV_USE_ASSERT_STYLE check doesn't read an uninitialised
         * sentinel/prop_cnt (and name/long_name start from a known state). */
        lv_memzero(xml_style, sizeof(*xml_style));
        xml_style->name = lv_strdup(style_name);
        lv_style_init(&xml_style->style);
        size_t long_name_len = lv_strlen(scope->name) + 1 + lv_strlen(style_name) + 1;
        xml_style->long_name = lv_malloc(long_name_len);
        lv_snprintf((char *)xml_style->long_name, long_name_len, "%s.%s", scope->name, style_name); /*E.g. my_button.style1*/
    }

    lv_style_t * style = &xml_style->style;

    /* The transition descriptor cannot be built until every attribute that
     * feeds it is known, so the loop below collects them here instead of
     * applying each inline, and the descriptor is built once after the loop.
     * Longhand and shorthand (`transition="..."`) can both appear on one
     * element in either order; each `_set` flag records that a LONGHAND
     * attribute claimed that field, so the merge after the loop can prefer
     * it over the shorthand's value regardless of which attribute the loop
     * reached first. */
    struct {
        const char * props_str;
        uint32_t duration;
        uint32_t delay;
        lv_anim_path_cb_t easing;
        bool seen;
        bool props_set;
        bool duration_set;
        bool delay_set;
        bool easing_set;
    } trans = { NULL, 0, 0, NULL, false, false, false, false, false };

    struct {
        const char * props_str;
        uint32_t duration;
        uint32_t delay;
        lv_anim_path_cb_t easing;
        bool seen;
    } trans_short = { NULL, 0, 0, NULL, false };
    char trans_short_buf[256];

    int32_t i;
    for(i = 0; attrs[i]; i += 2) {
        const char * name = attrs[i];
        const char * value = attrs[i + 1];
        if(lv_streq(name, "name")) continue;
        if(lv_streq(name, "help")) continue;
        if(lv_streq(name, "figma_node_id")) continue;

        if(value[0] == '#') {
            const char * value_clean = &value[1];
            bool const_found = false;
            lv_xml_const_t * c;
            LV_LL_READ(&scope->const_ll, c) {
                if(lv_streq(c->name, value_clean)) {
                    value = c->value;
                    const_found = true;
                    break;
                }
            }
            if(!const_found) {
                lv_xml_component_scope_t * global_scope = lv_xml_component_get_scope("globals");
                if(global_scope) {
                    LV_LL_READ(&global_scope->const_ll, c) {
                        if(lv_streq(c->name, value_clean)) {
                            value = c->value;
                            const_found = true;
                            break;
                        }
                    }
                }
            }
            if(!const_found) {
                LV_LOG_WARN("Unknown const `#%s` in style of component `%s` (property `%s`) - "
                            "property skipped",
                            value_clean,
                            (scope && scope->name) ? scope->name : "<unknown>",
                            name);
                continue;
            }
        }

        if(lv_streq(value, "remove")) {
            lv_style_prop_t prop = lv_xml_style_prop_to_enum(name);
            if(prop != LV_STYLE_PROP_INV) lv_style_remove_prop(style, prop);
            else if(lv_streq(name, "pad_all")) {
                lv_style_remove_prop(style, LV_STYLE_PAD_TOP);
                lv_style_remove_prop(style, LV_STYLE_PAD_BOTTOM);
                lv_style_remove_prop(style, LV_STYLE_PAD_LEFT);
                lv_style_remove_prop(style, LV_STYLE_PAD_RIGHT);
            }
            else if(lv_streq(name, "pad_hor")) {
                lv_style_remove_prop(style, LV_STYLE_PAD_LEFT);
                lv_style_remove_prop(style, LV_STYLE_PAD_RIGHT);
            }
            else if(lv_streq(name, "pad_ver")) {
                lv_style_remove_prop(style, LV_STYLE_PAD_TOP);
                lv_style_remove_prop(style, LV_STYLE_PAD_BOTTOM);
            }
            else if(lv_streq(name, "pad_gap")) {
                lv_style_remove_prop(style, LV_STYLE_PAD_COLUMN);
                lv_style_remove_prop(style, LV_STYLE_PAD_ROW);
            }
            else if(lv_streq(name, "margin_all")) {
                lv_style_remove_prop(style, LV_STYLE_MARGIN_TOP);
                lv_style_remove_prop(style, LV_STYLE_MARGIN_BOTTOM);
                lv_style_remove_prop(style, LV_STYLE_MARGIN_LEFT);
                lv_style_remove_prop(style, LV_STYLE_MARGIN_RIGHT);
            }
            else if(lv_streq(name, "margin_hor")) {
                lv_style_remove_prop(style, LV_STYLE_MARGIN_LEFT);
                lv_style_remove_prop(style, LV_STYLE_MARGIN_RIGHT);
            }
            else if(lv_streq(name, "margin_ver")) {
                lv_style_remove_prop(style, LV_STYLE_MARGIN_TOP);
                lv_style_remove_prop(style, LV_STYLE_MARGIN_BOTTOM);
            }
        }
        else SET_STYLE_IF(width, lv_xml_to_size(value));
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
        else SET_STYLE_IF(bg_grad, lv_xml_component_get_grad(scope, value));

        else SET_STYLE_IF(bg_image_src, lv_xml_get_image(scope, value));
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
        else SET_STYLE_IF(text_font, lv_xml_get_font(scope, value));
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
        else SET_STYLE_IF(arc_image_src, lv_xml_get_image(scope, value));

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
        else SET_STYLE_IF(transform_pivot_x, lv_xml_to_size(value));
        else SET_STYLE_IF(transform_pivot_y, lv_xml_to_size(value));
        else SET_STYLE_IF(transform_skew_x, lv_xml_atoi(value));
        else SET_STYLE_IF(transform_skew_y, lv_xml_atoi(value));
        else SET_STYLE_IF(bitmap_mask_src, lv_xml_get_image(scope, value));
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
        else if(lv_streq(name, "grid_column_dsc_array") ||
                lv_streq(name, "grid_row_dsc_array")) {

            uint32_t item_cnt = 0;
            uint32_t c;
            for(c = 0; value[c] != '\0'; c++) {
                if(value[c] == ' ') item_cnt++;
            }

            /*This not freed automatically as the styles doesn't have any mechanisms to detect
             * removal of properties. It's assumed that the styles are created once and never freed. */
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

            if(lv_streq(name, "grid_column_dsc_array")) {
                lv_style_set_grid_column_dsc_array(style, dsc_array);
            }
            else {
                lv_style_set_grid_row_dsc_array(style, dsc_array);
            }
        }

        else if(lv_streq(name, "transition_props")) {
            trans.props_str = value;
            trans.seen = true;
            trans.props_set = true;
        }
        else if(lv_streq(name, "transition_duration")) {
            if(transition_time_to_ms(value, style_name, "transition_duration", &trans.duration)) {
                trans.seen = true;
                trans.duration_set = true;
            }
        }
        else if(lv_streq(name, "transition_easing")) {
            lv_anim_path_cb_t cb = transition_easing_to_cb(value);
            if(cb == NULL) {
                LV_LOG_WARN("`%s` is not a known easing name, in `transition_easing` of style `%s`",
                            value, style_name);
            }
            else {
                trans.easing = cb;
                trans.seen = true;
                trans.easing_set = true;
            }
        }
        else if(lv_streq(name, "transition_delay")) {
            if(transition_time_to_ms(value, style_name, "transition_delay", &trans.delay)) {
                trans.seen = true;
                trans.delay_set = true;
            }
        }
        else if(lv_streq(name, "transition")) {
            if(transition_parse_shorthand(value, trans_short_buf, sizeof(trans_short_buf), style_name,
                                          &trans_short.props_str, &trans_short.duration,
                                          &trans_short.easing, &trans_short.delay)) {
                trans_short.seen = true;
                trans.seen = true;
            }
        }

        else {
            LV_LOG_WARN("%s style property is not supported", name);
        }
    }

    if(trans.seen) {
        /* A longhand attribute always wins its own field over whatever the
         * shorthand said, no matter which one the loop above reached first. */
        const char * props_str = trans.props_set ? trans.props_str : trans_short.props_str;
        uint32_t duration = trans.duration_set ? trans.duration : trans_short.duration;
        uint32_t delay = trans.delay_set ? trans.delay : trans_short.delay;
        lv_anim_path_cb_t easing = trans.easing_set ? trans.easing : trans_short.easing;

        style_transition_install(xml_style, props_str, duration, delay, easing, style_name);
    }

    return LV_RESULT_OK;
}

const char * lv_xml_style_string_process(char * txt, lv_style_selector_t * selector)
{
    *selector = 0;

    char * style_name = lv_xml_split_str(&txt, '-');
    char * selector_str = lv_xml_split_str(&txt, '-');
    while(selector_str != NULL) {
        /* Handle different states and parts.
         *
         * The two _to_enum() helpers both answer 0 for an unknown token, and 0
         * is also `default` and `main`, so ORing them meant a typo
         * (`style_radius-presed`) collapsed to LV_STATE_DEFAULT|LV_PART_MAIN
         * and the property was applied unconditionally, with nothing logged.
         * The `selector="..."` attribute path already warns on the same string;
         * both now go through the one resolver that reports unknown tokens. */
        if(!lv_xml_style_selector_token_to_enum(selector_str, selector)) {
            LV_LOG_WARN("%s is an unknown token in style selector for `%s`", selector_str, style_name);
        }

        /* Move to the next token */
        selector_str = lv_xml_split_str(&txt, '-');
    }

    return style_name;
}

lv_xml_style_t * lv_xml_get_style_by_name(lv_xml_component_scope_t * scope, const char * style_name_raw)
{
    /* Who is ASKING, before the `component.` prefix below reassigns `scope` to
     * whoever OWNS the style. A hit in a different scope means a raw
     * lv_style_t* is about to cross a scope boundary, which is the one thing
     * the owner's instance counter cannot see - see `styles_borrowed`.
     * Compared by name: a parse works off a by-value copy of the scope
     * (lv_xml_create_in_scope: `state.scope = *scope`), so the pointers differ
     * even when it is the same scope. */
    const char * requester_name = (scope != NULL) ? scope->name : NULL;

    const char * style_name = strrchr(style_name_raw, '.');

    if(style_name) {
        char component_name[256];
        size_t len = (size_t)(style_name - style_name_raw);
        if(len >= sizeof(component_name)) {
            LV_LOG_WARN("style reference '%s' has an over-long component name; ignoring",
                        style_name_raw);
            return NULL;
        }
        lv_memcpy(component_name, style_name_raw, len);
        component_name[len] = '\0';
        scope = lv_xml_component_get_scope(component_name);
        if(scope == NULL) {
            LV_LOG_WARN("'%s' component or widget is not found", component_name);
        }
        style_name++; /*Skip the dot*/
    }
    else {
        style_name = style_name_raw;
    }

    /*Use the global scope is not specified*/
    if(scope == NULL) scope = lv_xml_component_get_scope("globals");
    if(scope == NULL) return NULL;

    lv_xml_style_t * xml_style;
    LV_LL_READ(&scope->style_ll, xml_style) {
        if(lv_streq(xml_style->name, style_name)) {
            /* `globals` is exempt: it is shared metadata, never retired, so its
             * style storage cannot be pulled out from under a borrower. */
            if(scope->name != NULL && requester_name != NULL &&
               !lv_streq(scope->name, requester_name) && !lv_streq(scope->name, "globals")) {
                scope->styles_borrowed = 1;
            }
            return xml_style;
        }
    }

    /*If not found in the component check the global space*/
    if(!lv_streq(scope->name, "globals")) {
        scope = lv_xml_component_get_scope("globals");
        if(scope) {
            LV_LL_READ(&scope->style_ll, xml_style) {
                if(lv_streq(xml_style->name, style_name)) return xml_style;
            }
        }
    }

    LV_LOG_WARN("No style found with %s name", style_name_raw);

    return NULL;
}

lv_grad_dsc_t * lv_xml_component_get_grad(lv_xml_component_scope_t * scope, const char * name)
{
    lv_xml_grad_t * d;
    LV_LL_READ(&scope->gradient_ll, d) {
        if(lv_streq(d->name, name)) return &d->grad_dsc;
    }

    return NULL;
}

void lv_xml_style_transition_clear(lv_xml_style_t * xs)
{
    if(xs == NULL || xs->trans_dsc == NULL) return;

    lv_style_remove_prop(&xs->style, LV_STYLE_TRANSITION);
    lv_free(xs->trans_dsc);
    lv_free(xs->trans_props);
    xs->trans_dsc = NULL;
    xs->trans_props = NULL;
    xs->trans_authored_time = 0;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static lv_anim_path_cb_t transition_easing_to_cb(const char * txt)
{
    if(lv_streq(txt, "linear"))      return lv_anim_path_linear;
    if(lv_streq(txt, "ease_in"))     return lv_anim_path_ease_in;
    if(lv_streq(txt, "ease_out"))    return lv_anim_path_ease_out;
    if(lv_streq(txt, "ease_in_out")) return lv_anim_path_ease_in_out;
    if(lv_streq(txt, "overshoot"))   return lv_anim_path_overshoot;
    if(lv_streq(txt, "bounce"))      return lv_anim_path_bounce;
    if(lv_streq(txt, "step"))        return lv_anim_path_step;
    return NULL;
}

/**
 * Parse a transition duration or delay: a non-negative decimal integer,
 * optionally followed by an `ms` suffix, and nothing else. A value ending in
 * a bare `s` gets its own message, since writing a duration in seconds is
 * the single most common mistake carried over from CSS.
 * @param txt         the raw attribute value or shorthand token
 * @param style_name  the style's name, for warning messages
 * @param attr_name   the attribute or shorthand field `txt` came from, for
 *                    warning messages
 * @param out_ms      set to the parsed value in ms on success; untouched on failure
 * @return true if `txt` parsed as a valid duration; false if it was refused
 */
static bool transition_time_to_ms(const char * txt, const char * style_name,
                                  const char * attr_name, uint32_t * out_ms)
{
    if(txt[0] == '-') {
        LV_LOG_WARN("`%s` is a negative value in `%s` of style `%s`; transition durations "
                    "cannot be negative", txt, attr_name, style_name);
        return false;
    }

    const char * p = txt;
    while(*p >= '0' && *p <= '9') p++;
    bool well_formed = (p != txt) && (p[0] == '\0' || lv_streq(p, "ms"));

    if(!well_formed) {
        size_t len = lv_strlen(txt);
        bool trailing_s = len > 0 && txt[len - 1] == 's' &&
                          !(len > 1 && txt[len - 2] == 'm');
        if(trailing_s) {
            LV_LOG_WARN("`%s` in `%s` of style `%s` looks like seconds; transition durations "
                        "are written in milliseconds", txt, attr_name, style_name);
        }
        else {
            LV_LOG_WARN("`%s` is not a valid duration in `%s` of style `%s`; expected a number "
                        "optionally followed by `ms`", txt, attr_name, style_name);
        }
        return false;
    }

    *out_ms = (uint32_t)lv_xml_atoi(txt);
    return true;
}

/**
 * Parse the CSS-style `transition="<props> <duration> [easing] [delay]"`
 * shorthand into the same fields the longhand attributes fill. Tokens split
 * on whitespace: the first is the property list (`|` or `,` separated), the second
 * the duration, and each token after that is classified on its own terms -
 * a recognised easing name or a number - since either may be omitted and
 * their order is otherwise fixed. A token that is neither is refused rather
 * than guessed at.
 * @param value         the raw `transition` attribute value
 * @param buf           scratch buffer the parser tokenises in place; must
 *                      outlive `*props_str`, which points into it
 * @param buf_len       size of `buf`
 * @param style_name    the style's name, for warning messages
 * @param props_str     set to the property-list token on success
 * @param duration      set to the parsed duration in ms on success
 * @param easing        set to the parsed easing callback, or left NULL for linear
 * @param delay         set to the parsed delay in ms, or left 0
 * @return true if the shorthand parsed; false if it was refused, in which
 *         case none of the output parameters are meaningful
 */
static bool transition_parse_shorthand(const char * value, char * buf, size_t buf_len,
                                       const char * style_name, const char ** props_str,
                                       uint32_t * duration, lv_anim_path_cb_t * easing,
                                       uint32_t * delay)
{
    lv_strncpy(buf, value, buf_len);
    buf[buf_len - 1] = '\0';

    char * bufp = buf;
    const char * props_tok = lv_xml_split_str(&bufp, ' ');
    const char * duration_tok = lv_xml_split_str(&bufp, ' ');
    if(props_tok == NULL || duration_tok == NULL ||
       !(duration_tok[0] >= '0' && duration_tok[0] <= '9')) {
        LV_LOG_WARN("`transition` shorthand needs a property list and a numeric duration, "
                    "in style `%s`", style_name);
        return false;
    }

    if(!transition_time_to_ms(duration_tok, style_name, "transition duration", duration)) {
        return false;
    }
    *props_str = props_tok;
    *easing = NULL;
    *delay = 0;

    const char * tok;
    while((tok = lv_xml_split_str(&bufp, ' ')) != NULL) {
        lv_anim_path_cb_t cb = transition_easing_to_cb(tok);
        if(cb != NULL) {
            *easing = cb;
        }
        else if(tok[0] >= '0' && tok[0] <= '9') {
            if(!transition_time_to_ms(tok, style_name, "transition delay", delay)) {
                return false;
            }
        }
        else {
            LV_LOG_WARN("`%s` is neither an easing name nor a number, in `transition` shorthand "
                        "of style `%s`", tok, style_name);
            return false;
        }
    }

    return true;
}

/**
 * Build the transition descriptor for a style from its longhand attributes
 * and install it, replacing whatever transition the style already owns. The
 * new descriptor and its property array are fully built before anything is
 * torn down, so a style with an existing transition is left with that
 * transition, untouched, on any failure - never with neither the old
 * transition nor a new one.
 * @param xs            the style to install onto
 * @param props_str     style property names separated by `|`, `,` or
 *                      whitespace, or NULL for none
 * @param time          transition duration in ms
 * @param delay         transition delay in ms
 * @param path          easing callback, or NULL for linear
 * @param style_name    the style's name, for warning messages
 */
static void style_transition_install(lv_xml_style_t * xs, const char * props_str,
                                     uint32_t time, uint32_t delay, lv_anim_path_cb_t path,
                                     const char * style_name)
{
    if(props_str == NULL) {
        LV_LOG_WARN("transition attributes given with no `transition_props`, in style `%s`; "
                    "its transition, if any, is unchanged", style_name);
        return;
    }

    char buf[256];
    lv_strncpy(buf, props_str, sizeof(buf));
    buf[sizeof(buf) - 1] = '\0';

    /* `,` and whitespace are accepted as well as `|` and normalized to it here,
     * the same delimiter set lv_xml_border_side_to_enum() accepts. */
    for(char * p = buf; *p; p++) {
        if(*p == ',' || *p == ' ' || *p == '\t') *p = '|';
    }

    /* Count first so the array is allocated once, then fill. */
    uint32_t cnt = 1;
    for(const char * p = buf; *p; p++) if(*p == '|') cnt++;

    lv_style_prop_t * arr = lv_malloc((cnt + 1) * sizeof(lv_style_prop_t));
    LV_ASSERT_MALLOC(arr);
    if(arr == NULL) return;

    uint32_t n = 0;
    char * bufp = buf;
    const char * tok = lv_xml_split_str(&bufp, '|');
    while(tok) {
        lv_style_prop_t prop = lv_xml_style_prop_to_enum(tok);
        if(prop == LV_STYLE_PROP_INV) {
            LV_LOG_WARN("`%s` is not a style property, in transition of style `%s`", tok, style_name);
            lv_free(arr);
            return;
        }
        if(lv_xml_style_prop_anim_type(prop) == LV_XML_STYLE_PROP_ANIM_UNKNOWN) {
            LV_LOG_WARN("`%s` cannot be interpolated, in transition of style `%s`", tok, style_name);
            lv_free(arr);
            return;
        }
        arr[n++] = prop;
        tok = lv_xml_split_str(&bufp, '|');
    }
    arr[n] = 0;

    lv_style_transition_dsc_t * dsc = lv_malloc(sizeof(*dsc));
    LV_ASSERT_MALLOC(dsc);
    if(dsc == NULL) { lv_free(arr); return; }

    /* Built at the scale in effect now, so a style registered while motion is
     * scaled down is born scaled rather than waiting for the next
     * lv_xml_set_transition_scale() call. trans_authored_time keeps the
     * unscaled value so later calls retime from the same source. */
    uint32_t scaled = (uint32_t)(((uint64_t)time * lv_xml_get_transition_scale()) >> 8);
    lv_style_transition_dsc_init(dsc, arr, path ? path : lv_anim_path_linear, scaled, delay, NULL);

    /* The replacement is fully built - only now is it safe to let go of
     * whatever the style had before. */
    lv_xml_style_transition_clear(xs);
    xs->trans_dsc = dsc;
    xs->trans_props = arr;
    xs->trans_authored_time = time;
    lv_style_set_transition(&xs->style, dsc);
}

#endif /* LV_USE_XML */
