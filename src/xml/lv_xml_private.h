/**
 * @file lv_xml_private.h
 *
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2025 LVGL Kft
 */

#ifndef LV_XML_PRIVATE_H
#define LV_XML_PRIVATE_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include "lv_xml.h"
#if LV_USE_XML

#include "parsers/lv_xml_obj_parser.h"
#include "lv_xml_parser.h"
#include "lv_xml_attr_check.h"
#include "lv_xml_base_types.h"
#include "lv_xml_utils.h"
#include "lv_xml_style.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

typedef struct {
    const char * name;
    const lv_font_t * font;
    void (*font_destroy_cb)(lv_font_t *);
} lv_xml_font_t;

typedef struct {
    const char * name;
    const void * src;
    /** True only when `src` is the lv_strdup()'d, asset-path-prefixed copy that
     *  lv_xml_register_image() makes for LV_IMAGE_SRC_FILE sources. VARIABLE and
     *  SYMBOL sources are stored verbatim and belong to the caller - typically a
     *  compiled-in `static const lv_image_dsc_t`, which is not a heap address at
     *  all. Scope teardown must consult this before calling lv_free(). */
    bool src_is_owned;
} lv_xml_image_t;

typedef struct {
    const char * name;
    lv_event_cb_t cb;
} lv_xml_event_cb_t;

/**
 * Store the data of <include_timeline>
 */
typedef struct {
    const char * target_name;  /**< Include the timeline of this widget*/
    const char * timeline_name;   /**< Include this timeline */
    int32_t delay;
} lv_xml_anim_timeline_include_t;

typedef struct {
    bool is_anim;
    union {
        lv_anim_t anim;
        lv_xml_anim_timeline_include_t incl;
    } data;
} lv_xml_anim_timeline_child_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**********************
 *      MACROS
 **********************/

#endif /* LV_USE_XML */

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LV_XML_PRIVATE_H*/
