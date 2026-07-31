/**
 * @file lv_xml_globals.c
 *
 * Storage for helix-xml global state.
 *
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 356C LLC
 */

#include "lv_xml_globals.h"
#if LV_USE_XML

char * lv_xml_path_prefix = NULL;
uint32_t     lv_xml_event_store_timeline = 0;
lv_ll_t      lv_xml_loads_ll;

#endif /* LV_USE_XML */
