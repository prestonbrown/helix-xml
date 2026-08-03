/**
 * @file lv_xml.h
 *
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2025 LVGL Kft
 * SPDX-FileCopyrightText: 2026 356C LLC
 */

#ifndef LV_XML_H
#define LV_XML_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include <misc/lv_types.h>
#include "lv_xml_types.h"

#if LV_USE_XML
#include <misc/lv_event.h>
#include <core/lv_observer.h>
#include "lv_xml_test.h"
#include "lv_xml_translation.h"
#include "lv_xml_component.h"
#include "lv_xml_widget.h"
#include "lv_xml_load.h"

/*********************
 *      DEFINES
 *********************/

#define LV_XML_MAX_PATH_LENGTH 256

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 * GLOBAL PROTOTYPES
 **********************/

void lv_xml_init(void);

void lv_xml_deinit(void);

/**
 * Create a UI element from XML.
 * @param parent    Pointer to the parent
 * @param name      The name of an already-registered Component or Widget
 * @param attrs     Pointer to a list of attribute/value pairs to pass.
 *                  The last two items should be `NULL`.
 * @return          Pointer to the created UI element
 * @example         If `name` is "lv_slider" and
 *                  `attrs` is {"width", "100", "value", "30", NULL, NULL};
 *                  it's equivalent to `<lv_slider width="100" value="30"/>`
 * @example         not only components can be created this way but e.g
 *                  - `name`="lv_chart-series", `attrs`={"color", "0xf00", "axis", "primary_y", NULL, NULL}
 *                  - `name`="style", `attrs`={"name", "style1", "selector", "pressed|knob", NULL, NULL}
 *                  - `name`="bind_flag_if_eq",
 *                    `attrs`={"subject", "subject1", "flag", "hidden", "ref_value", "1", NULL, NULL}
 */
void * lv_xml_create(lv_obj_t * parent, const char * name, const char ** attrs);

/**
 * Create a Screen from XML.
 * @param name  The name of an already-registered Screen
 * @return      Pointer to the created Screen
 * @note        If required, can be loaded with `lv_screen_load()`.
 */
lv_obj_t * lv_xml_create_screen(const char * name);

void * lv_xml_create_in_scope(lv_obj_t * parent, lv_xml_component_scope_t * parent_ctx,
                              lv_xml_component_scope_t * scope,
                              const char ** attrs);

/**
 * Set a path to prefix the image and font file source paths.
 *
 * In globals.xml usually the source path is like "images/logo.png".
 * But on the actual device it can be located at e.g. "A:ui/assets/images/logo.png".
 * By setting "A:ui/assets/" the path set in the XML files will be prefixed accordingly.
 *
 * @param path_prefix   the path to be used as prefix
 */
void lv_xml_set_default_asset_path(const char * path_prefix);

lv_result_t lv_xml_register_font(lv_xml_component_scope_t * scope, const char * name, const lv_font_t * font);

const lv_font_t * lv_xml_get_font(lv_xml_component_scope_t * scope, const char * name);

/**
 * Silent variant of lv_xml_get_font: returns NULL if the font isn't registered
 * instead of warning and returning the default font. Use when the caller needs
 * to distinguish "present" from "absent" (e.g. for tier-aware fallback logic).
 */
const lv_font_t * lv_xml_get_font_silent(lv_xml_component_scope_t * scope, const char * name);

lv_result_t lv_xml_register_image(lv_xml_component_scope_t * scope, const char * name, const void * src);

const void * lv_xml_get_image(lv_xml_component_scope_t * scope, const char * name);

/**
 * Map globally available subject name to an actual subject variable
 * @param name      name of the subject
 * @param subject   pointer to a subject
 * @return          `LV_RESULT_OK`: success
 */
lv_result_t lv_xml_register_subject(lv_xml_component_scope_t * scope, const char * name, lv_subject_t * subject);

/**
 * Remove a subject name from the XML registry, unlinking and freeing the
 * internal record and its strdup'd name copy.
 *
 * What happens to the `lv_subject_t` follows how it was registered. A subject
 * you passed to `lv_xml_register_subject()` is yours: it is neither deinit'd
 * nor freed, and its lifetime remains entirely your business. A subject the
 * parser created for a `<subject>` / `<subject_expr>` element belongs to the
 * scope, so removing its name here also deinits and frees it — exactly as
 * unregistering the whole component would.
 * @param scope     The scope to remove the subject from. If `NULL` the
 *                  `"globals"` scope is used (mirrors `lv_xml_register_subject`).
 * @param name      Name of the subject to remove.
 * @return          `LV_RESULT_OK` if removed, `LV_RESULT_INVALID` if not found or no scope.
 */
lv_result_t lv_xml_unregister_subject(lv_xml_component_scope_t * scope, const char * name);

/**
 * Get a subject by name.
 * @param scope     If specified start searching in that component's subject list,
 *                  and if not found search in the global space.
 *                  If `NULL` search in global space immediately.
 * @param name      Name of the subject to find.
 * @return          Pointer to the subject or NULL if not found.
 */
lv_subject_t * lv_xml_get_subject(lv_xml_component_scope_t * scope, const char * name);

lv_result_t lv_xml_register_const(lv_xml_component_scope_t * scope, const char * name, const char * value);

lv_result_t lv_xml_update_const(lv_xml_component_scope_t * scope, const char * name, const char * value);

const char * lv_xml_get_const(lv_xml_component_scope_t * scope, const char * name);

const char * lv_xml_get_const_silent(lv_xml_component_scope_t * scope, const char * name);

lv_result_t lv_xml_register_event_cb(lv_xml_component_scope_t * scope, const char * name, lv_event_cb_t cb);

lv_event_cb_t lv_xml_get_event_cb(lv_xml_component_scope_t * scope, const char * name);

/**
 * Callback for `lv_xml_event_cb_foreach`, invoked once per registered event cb.
 * @param name       the registered callback name
 * @param cb         the registered `lv_event_cb_t`
 * @param user_data  the opaque pointer passed to `lv_xml_event_cb_foreach`
 */
typedef void (*lv_xml_event_cb_iter_cb_t)(const char * name, lv_event_cb_t cb, void * user_data);

/**
 * Iterate every event callback registered in `scope`, invoking `cb` per entry.
 * Companion enumeration primitive to `lv_xml_get_event_cb` (which only looks up
 * by name). Does NOT fall back to the global scope — pass the "globals" scope
 * explicitly (or NULL, which resolves to it) to walk global registrations.
 * @param scope       scope to walk; NULL resolves to the "globals" scope
 * @param cb          callback invoked per entry (no-op if NULL)
 * @param user_data   opaque pointer forwarded to every `cb` invocation
 */
void lv_xml_event_cb_foreach(lv_xml_component_scope_t * scope, lv_xml_event_cb_iter_cb_t cb, void * user_data);

lv_result_t lv_xml_register_timeline(lv_xml_component_scope_t * scope, const char * name);

void * lv_xml_get_timeline(lv_xml_component_scope_t * scope, const char * name);

/**********************
 *      MACROS
 **********************/

#endif /* LV_USE_XML */

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LV_XML_H*/
