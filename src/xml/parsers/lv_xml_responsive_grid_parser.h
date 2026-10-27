/**
 * @file lv_xml_responsive_grid_parser.h
 *
 * `<responsive_grid>`: a container that lays its children out in equal-width
 * columns, as many as fit. Attributes (all optional):
 *   min_col_width  the narrowest column (default: one column)
 *   max_col_width  the widest column (default: no cap)
 *   max_cols       the most columns (default: no cap)
 *   fit_items      true: no more columns than visible children
 * style_pad_column / style_pad_row are the gaps. Each child is stretched to its
 * column's width whatever width it asks for; a row is as tall as its tallest
 * child, and a short last row keeps the column width.
 *
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 356C LLC
 */

#ifndef LV_XML_RESPONSIVE_GRID_PARSER_H
#define LV_XML_RESPONSIVE_GRID_PARSER_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include "../lv_xml.h"
#if LV_USE_XML

/**********************
 *      TYPEDEFS
 **********************/

/** What decides a responsive grid's columns. Widths and gaps in px. */
typedef struct {
    int32_t avail_w;    /**< content width the columns share */
    int32_t gap;        /**< space between two columns */
    int32_t min_col_w;  /**< narrowest a column may get; <= 0 means one column */
    int32_t max_col_w;  /**< widest a column may get; <= 0 means no cap */
    int32_t max_cols;   /**< most columns; <= 0 means no cap */
    int32_t item_count;
    /** Never more columns than items, so two items in a wide grid share the
     *  row instead of taking two narrow cells of a wider one. */
    bool fit_items;
} lv_xml_grid_fit_input_t;

typedef struct {
    int32_t cols;
    int32_t col_w;
} lv_xml_grid_fit_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * As many columns as fit at min_col_w, within max_cols (and the item count when
 * fit_items), sharing avail_w equally; capped at max_col_w, in which case the
 * grid leaves the remainder empty on the right. Always at least one column.
 */
lv_xml_grid_fit_t lv_xml_grid_fit_columns(const lv_xml_grid_fit_input_t * in);

/**
 * The columns a `<responsive_grid>` lays its visible children out in at its
 * current content width. A caller that needs the cell width before the first
 * layout pass calls lv_obj_update_layout() first.
 * @return {1, content width} for an object that is not a responsive_grid
 */
lv_xml_grid_fit_t lv_xml_responsive_grid_get_fit(lv_obj_t * grid);

/** Creates the grid's LVGL layout. lv_xml_init() calls it; lv_init() empties the layout table. */
void lv_xml_responsive_grid_init(void);

void * lv_xml_responsive_grid_create(lv_xml_parser_state_t * state, const char ** attrs);
void lv_xml_responsive_grid_apply(lv_xml_parser_state_t * state, const char ** attrs);

/**********************
 *      MACROS
 **********************/
#endif /* LV_USE_XML */

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LV_XML_RESPONSIVE_GRID_PARSER_H*/
