/**
 * @file lv_xml_globals.h
 *
 * Global state for the helix-xml engine.
 * These variables lived in LVGL's lv_global_t until v9.5 removed XML support.
 *
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 356C LLC
 */

#ifndef LV_XML_GLOBALS_H
#define LV_XML_GLOBALS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <misc/lv_types.h>
#include <misc/lv_ll.h>

extern char * lv_xml_path_prefix;
extern uint32_t     lv_xml_event_store_timeline;
extern lv_ll_t      lv_xml_loads_ll;

#ifdef __cplusplus
}
#endif

#endif /* LV_XML_GLOBALS_H */
