/**
 * @file lv_xml_component.h
 *
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2025 LVGL Kft
 * SPDX-FileCopyrightText: 2026 356C LLC
 */

#ifndef LV_XML_COMPONENT_H
#define LV_XML_COMPONENT_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include <misc/lv_types.h>
#include "lv_xml_types.h"
#if LV_USE_XML

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Process a component during parsing an XML. It create a widget and apply all the attributes
 * @param state     the current parsing state
 * @param name      name of the component
 * @param attrs     attributes of the widget
 * @return
 */
lv_obj_t * lv_xml_component_process(lv_xml_parser_state_t * state, const char * name, const char ** attrs);

/**
 * Load the styles, constants, and other data of the Component. It needs to be called only once for each Component.
 * @param name      The name as the component will be referenced later in other components
 * @param xml_def   The XML definition of the component as a NULL terminated string
 * @return          LV_RESULT_OK: loaded successfully, LV_RES_INVALID: otherwise
 */
lv_result_t lv_xml_register_component_from_data(const char * name, const char * xml_def);

/**
 * Load the styles, constants, and other data of the Component. It needs to be called only once for each Component.
 * @param path      Path to an XML file
 * @return          LV_RESULT_OK: loaded successfully, LV_RES_INVALID: otherwise
 */
lv_result_t lv_xml_register_component_from_file(const char * path);

/**
 * Get the scope of a Component which was registered by
 * `lv_xml_register_component_from_data()` or `lv_xml_register_component_from_file()`
 * @param component_name    Name of the Component
 * @return                  Pointer to the scope or NULL if not found
 */
lv_xml_component_scope_t * lv_xml_component_get_scope(const char * component_name);

/**
 * Callback for `lv_xml_component_foreach`, invoked once per registered scope.
 * @param name       the component/scope name (the built-in "globals" scope is included)
 * @param user_data  the opaque pointer passed to `lv_xml_component_foreach`
 */
typedef void (*lv_xml_component_iter_cb_t)(const char * name, void * user_data);

/**
 * Iterate every registered component scope, invoking `cb` with each scope name.
 * The registry has only register + get-by-name accessors otherwise; this is the
 * enumeration primitive. Order is most-recently-registered first. The built-in
 * "globals" scope is included — callers wanting only real components should skip
 * it by name.
 * @param cb          callback invoked per scope (no-op if NULL)
 * @param user_data   opaque pointer forwarded to every `cb` invocation
 */
void lv_xml_component_foreach(lv_xml_component_iter_cb_t cb, void * user_data);

/**
 * Remove a component from from the list.
 * @param name      the name of the component (used during registration)
 * @return          LV_RESULT_OK on successful  unregistration, LV_RESULT_INVALID otherwise.
 */
lv_result_t lv_xml_component_unregister(const char * name);

/**********************
 *      MACROS
 **********************/

#endif /* LV_USE_XML */

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LV_XML_COMPONENT_H*/


