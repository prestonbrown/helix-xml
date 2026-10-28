/**
 * @file lv_xml_responsive_grid_parser.c
 *
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 356C LLC
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_xml_responsive_grid_parser.h"
#if LV_USE_XML

#include <lvgl.h>
#include <lvgl_private.h>
#include "../lv_xml_private.h"

/**********************
 *      TYPEDEFS
 **********************/

typedef struct {
    int32_t min_col_w;
    int32_t max_col_w;
    int32_t max_cols;
    bool fit_items;
} grid_params_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void params_delete_cb(lv_event_t * e);
static grid_params_t * find_params(lv_obj_t * obj);
static bool is_laid_out(lv_obj_t * item);
static lv_xml_grid_fit_t fit_for(lv_obj_t * cont, const grid_params_t * p);
static void place_item(lv_obj_t * item, int32_t x, int32_t y, int32_t w);
static void release_width(lv_obj_t * item);
static void layout_update(lv_obj_t * cont, void * user_data);

/**********************
 *  STATIC VARIABLES
 **********************/

static uint32_t layout_id;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

lv_xml_grid_fit_t lv_xml_grid_fit_columns(const lv_xml_grid_fit_input_t * in)
{
    const int32_t avail = LV_MAX(in->avail_w, 0);
    const int32_t gap = LV_MAX(in->gap, 0);

    int32_t cols = 1;
    if(in->min_col_w > 0) cols = LV_MAX(1, (avail + gap) / (in->min_col_w + gap));
    if(in->max_cols > 0) cols = LV_MIN(cols, in->max_cols);
    if(in->fit_items && in->item_count > 0) cols = LV_MIN(cols, in->item_count);

    int32_t col_w = LV_MAX(0, (avail - gap * (cols - 1)) / cols);
    if(in->max_col_w > 0) col_w = LV_MIN(col_w, in->max_col_w);

    lv_xml_grid_fit_t fit = {cols, col_w};
    return fit;
}

lv_xml_grid_fit_t lv_xml_responsive_grid_get_fit(lv_obj_t * grid)
{
    const grid_params_t * p = grid ? find_params(grid) : NULL;
    if(p == NULL) {
        lv_xml_grid_fit_t whole = {1, grid ? lv_obj_get_content_width(grid) : 0};
        return whole;
    }
    return fit_for(grid, p);
}

void lv_xml_responsive_grid_init(void)
{
    lv_layout_callbacks_t cbs = {.layout_update_cb = layout_update, .get_min_size_cb = NULL};
    layout_id = lv_layout_create(cbs, NULL);
}

void * lv_xml_responsive_grid_create(lv_xml_parser_state_t * state, const char ** attrs)
{
    LV_UNUSED(attrs);

    lv_obj_t * obj = lv_obj_create(lv_xml_state_get_parent(state));
    grid_params_t * p = lv_malloc_zeroed(sizeof(grid_params_t));
    LV_ASSERT_MALLOC(p);
    /*A bare layout box: no card chrome or padding, no scrolling of its own, sized by its rows.
     *Local styles rather than removing the theme's, so a grid made scrollable keeps the
     *theme's scrollbar.*/
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(obj, 0, LV_PART_MAIN);
    lv_obj_set_size(obj, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    /*The parameters ride on the DELETE handler that frees them; user_data stays the caller's*/
    lv_obj_add_event_cb(obj, params_delete_cb, LV_EVENT_DELETE, p);
    lv_obj_set_style_layout(obj, (uint16_t)layout_id, LV_PART_MAIN);
    return obj;
}

void lv_xml_responsive_grid_apply(lv_xml_parser_state_t * state, const char ** attrs)
{
    lv_obj_t * obj = lv_xml_state_get_item(state);
    lv_xml_obj_apply(state, attrs); /*Apply the common properties, e.g. width, height, styles flags etc*/

    grid_params_t * p = find_params(obj);
    if(p == NULL) return;

    lv_xml_attr_check_enter(state);

    for(int i = 0; attrs[i]; i += 2) {
        const char * name  = attrs[i];
        const char * value = attrs[i + 1];

        if(lv_streq("min_col_width", name)) p->min_col_w = lv_xml_atoi(value);
        else if(lv_streq("max_col_width", name)) p->max_col_w = lv_xml_atoi(value);
        else if(lv_streq("max_cols", name)) p->max_cols = lv_xml_atoi(value);
        else if(lv_streq("fit_items", name)) p->fit_items = lv_xml_to_bool(value);
        else lv_xml_attr_check_miss(state, name);
    }
    lv_obj_mark_layout_as_dirty(obj);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void params_delete_cb(lv_event_t * e)
{
    lv_free(lv_event_get_user_data(e));
}

static grid_params_t * find_params(lv_obj_t * obj)
{
    uint32_t n = lv_obj_get_event_count(obj);
    for(uint32_t i = 0; i < n; i++) {
        lv_event_dsc_t * dsc = lv_obj_get_event_dsc(obj, i);
        if(dsc && lv_event_dsc_get_cb(dsc) == params_delete_cb) return lv_event_dsc_get_user_data(dsc);
    }
    return NULL;
}

static bool is_laid_out(lv_obj_t * item)
{
    return !lv_obj_has_flag_any(item, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_FLOATING);
}

static lv_xml_grid_fit_t fit_for(lv_obj_t * cont, const grid_params_t * p)
{
    int32_t items = 0;
    uint32_t cnt = lv_obj_get_child_count(cont);
    for(uint32_t i = 0; i < cnt; i++) {
        if(is_laid_out(lv_obj_get_child(cont, (int32_t)i))) items++;
    }

    lv_xml_grid_fit_input_t in = {
        .avail_w = lv_obj_get_content_width(cont),
        .gap = lv_obj_get_style_pad_column(cont, LV_PART_MAIN),
        .min_col_w = p->min_col_w,
        .max_col_w = p->max_col_w,
        .max_cols = p->max_cols,
        .item_count = items,
        .fit_items = p->fit_items,
    };
    return lv_xml_grid_fit_columns(&in);
}

/**
 * Stretch the item to the column width and move it to (x, y), the way LVGL's
 * own grid places a STRETCH cell.
 */
static void place_item(lv_obj_t * item, int32_t x, int32_t y, int32_t w)
{
    int32_t inner_w = w - lv_obj_get_style_margin_left(item, LV_PART_MAIN) -
                      lv_obj_get_style_margin_right(item, LV_PART_MAIN);
    if(!item->w_layout) {
        item->w_layout = 1;
        lv_obj_mark_layout_as_dirty(item);
    }
    if(lv_area_get_width(&item->coords) != inner_w) {
        lv_area_t old_coords = item->coords;
        lv_obj_invalidate(item);
        lv_area_set_width(&item->coords, inner_w);
        lv_obj_invalidate(item);
        lv_obj_send_event(item, LV_EVENT_SIZE_CHANGED, &old_coords);
        lv_obj_send_event(lv_obj_get_parent(item), LV_EVENT_CHILD_CHANGED, item);
    }

    x += lv_obj_get_style_margin_left(item, LV_PART_MAIN);
    y += lv_obj_get_style_margin_top(item, LV_PART_MAIN);
    int32_t dx = x - item->coords.x1;
    int32_t dy = y - item->coords.y1;
    if(dx || dy) {
        lv_obj_invalidate(item);
        lv_area_move(&item->coords, dx, dy);
        lv_obj_invalidate(item);
        lv_obj_move_children_by(item, dx, dy, false);
    }
}

/**
 * Hand a child's width back to its own style once the grid stops placing it
 * (hidden, floating, ignoring layout), as LVGL's grid does for a cell it does
 * not stretch. Left set, the child would keep its last column width for good.
 */
static void release_width(lv_obj_t * item)
{
    if(item->w_layout) {
        item->w_layout = 0;
        lv_obj_mark_layout_as_dirty(item);
    }
}

static void layout_update(lv_obj_t * cont, void * user_data)
{
    LV_UNUSED(user_data);

    const grid_params_t * p = find_params(cont);
    if(p == NULL) return;

    lv_xml_grid_fit_t fit = fit_for(cont, p);
    int32_t col_gap = lv_obj_get_style_pad_column(cont, LV_PART_MAIN);
    int32_t row_gap = lv_obj_get_style_pad_row(cont, LV_PART_MAIN);
    int32_t origin_x = cont->coords.x1 + lv_obj_get_style_space_left(cont, LV_PART_MAIN) - lv_obj_get_scroll_x(cont);
    int32_t row_y = cont->coords.y1 + lv_obj_get_style_space_top(cont, LV_PART_MAIN) - lv_obj_get_scroll_y(cont);

    /*An item's height is read as it stands; one whose height follows its width settles on a
     *later pass, when its SIZE_CHANGED relayouts it and CHILD_CHANGED brings the grid back here*/
    uint32_t cnt = lv_obj_get_child_count(cont);
    uint32_t i = 0;
    while(i < cnt) {
        int32_t col = 0;
        int32_t row_h = 0;
        for(; i < cnt && col < fit.cols; i++) {
            lv_obj_t * item = lv_obj_get_child(cont, (int32_t)i);
            if(!is_laid_out(item)) {
                release_width(item);
                continue;
            }
            place_item(item, origin_x + col * (fit.col_w + col_gap), row_y, fit.col_w);
            int32_t h = lv_obj_get_height(item) + lv_obj_get_style_margin_top(item, LV_PART_MAIN) +
                        lv_obj_get_style_margin_bottom(item, LV_PART_MAIN);
            row_h = LV_MAX(row_h, h);
            col++;
        }
        if(col > 0) row_y += row_h + row_gap;
    }

    if(lv_obj_get_style_width(cont, LV_PART_MAIN) == LV_SIZE_CONTENT ||
       lv_obj_get_style_height(cont, LV_PART_MAIN) == LV_SIZE_CONTENT) {
        lv_obj_refr_size(cont);
    }
    lv_obj_send_event(cont, LV_EVENT_LAYOUT_CHANGED, NULL);
}

#endif /* LV_USE_XML */
