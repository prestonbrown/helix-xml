/**
 * @file lv_xml_types.h
 *
 * Forward declarations for helix-xml types.
 * These lived in LVGL's lv_types.h until v9.5 dropped XML support.
 * All XML headers should include this instead of relying on lv_types.h.
 *
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 356C LLC
 */

#ifndef LV_XML_TYPES_H
#define LV_XML_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _lv_xml_component_scope_t lv_xml_component_scope_t;
typedef struct _lv_xml_parser_state_t lv_xml_parser_state_t;
typedef struct _lv_xml_load_t lv_xml_load_t;

#ifdef __cplusplus
}
#endif

#endif /* LV_XML_TYPES_H */
