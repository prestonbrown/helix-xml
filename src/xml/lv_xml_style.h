/**
 * @file lv_xml_style.h
 *
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2025 LVGL Kft
 * SPDX-FileCopyrightText: 2026 356C LLC
 */

#ifndef LV_XML_STYLE_H
#define LV_XML_STYLE_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include <misc/lv_types.h>
#include "lv_xml_types.h"
#include <misc/lv_style.h>
#include <core/lv_obj_style.h>

#if LV_USE_XML

/**********************
 *      TYPEDEFS
 **********************/

typedef struct _lv_xml_style_t {
    const char * name;
    const char * long_name;
    lv_style_t style;
    /* Engine-owned transition. LVGL stores only a pointer in the style, and
     * lv_style_reset() does not follow it, so the record owns both allocations.
     * trans_authored_time is the pre-scale duration: scaling reads it rather
     * than the live value, so repeated scaling does not compound. */
    lv_style_transition_dsc_t * trans_dsc;
    lv_style_prop_t * trans_props;
    uint32_t trans_authored_time;
} lv_xml_style_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Add a style to `ctx` and set the style properties from `attrs`
 * @param scope     add styles here. (Constants should be already added as style properties might use them)
 * @param attrs     list of attribute names and values
 */
lv_result_t lv_xml_register_style(lv_xml_component_scope_t * scope, const char ** attrs);

/**
 * Decompose a string like `"style1:pressed:checked:knob"` to style name and selector
 * @param txt           the input string
 * @param selector      store the selectors here
 * @return              the style name or `NULL` on any error
 */
const char * lv_xml_style_string_process(char * txt, lv_style_selector_t * selector);

/**
 * Find a style by name which was added by `lv_xml_register_style`
 * @param scope     the default context to search in
 * @param name      the name of the style. Can start with a component name prefix (e.g. `my_button.blue`) to overwrite the ctx
 * @return          the style structure
 */
lv_xml_style_t * lv_xml_get_style_by_name(lv_xml_component_scope_t * scope, const char * name);

/**
 * Get a gradient descriptor defined for a component
 * @param scope component context where the gradient should be found
 * @param name  name of the gradient
 * @return      a gradient descriptor
 */
lv_grad_dsc_t * lv_xml_component_get_grad(lv_xml_component_scope_t * scope, const char * name);

/**
 * Drop the style's transition, freeing the engine-owned descriptor and its
 * property array. Safe on a style that has none.
 */
void lv_xml_style_transition_clear(lv_xml_style_t * xs);

/**********************
 *      MACROS
 **********************/

#endif /* LV_USE_XML */

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LV_XML_STYLE_H*/
