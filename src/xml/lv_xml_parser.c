/**
 * @file lv_xml_parser.c
 *
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2025 LVGL Kft
 * SPDX-FileCopyrightText: 2026 356C LLC
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_xml.h"
#if LV_USE_XML

#include "lv_xml_private.h"
#include "lv_xml_parser.h"
#include "lv_xml_style.h"
#include "lv_xml_component_private.h"
#include "lv_xml_base_types.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_xml_parser_state_init(lv_xml_parser_state_t * state)
{
    lv_memzero(state, sizeof(lv_xml_parser_state_t));
    lv_ll_init(&state->parent_ll, sizeof(lv_obj_t *));
    lv_ll_init(&state->pcdata_ll, sizeof(lv_xml_pcdata_entry_t));
    lv_xml_component_scope_init(&state->scope);
}

void lv_xml_parser_start_section(lv_xml_parser_state_t * state, const char * name)
{
    /* Check for context changes */
    if(lv_streq(name, "api")) {
        state->section = LV_XML_PARSER_SECTION_API;
        return;
    }
    if(lv_streq(name, "gradients")) {
        state->section = LV_XML_PARSER_SECTION_GRAD;
        return;
    }
    if(state->section == LV_XML_PARSER_SECTION_GRAD && lv_streq(name, "stop")) {
        state->section = LV_XML_PARSER_SECTION_GRAD_STOP;
        return;
    }
    else if(lv_streq(name, "consts")) {
        state->section = LV_XML_PARSER_SECTION_CONSTS;
        return;
    }
    else if(lv_streq(name, "styles")) {
        state->section = LV_XML_PARSER_SECTION_STYLES;
        return;
    }
    else if(lv_streq(name, "images")) {
        state->section = LV_XML_PARSER_SECTION_IMAGES;
        return;
    }
    else if(lv_streq(name, "fonts")) {
        state->section = LV_XML_PARSER_SECTION_FONTS;
        return;
    }
    else if(lv_streq(name, "subjects")) {
        state->section = LV_XML_PARSER_SECTION_SUBJECTS;
        return;
    }
    else if(lv_streq(name, "animation")) {
        state->section = LV_XML_PARSER_SECTION_ANIMATION;
        return;
    }
    else if(lv_streq(name, "include_timeline")) {
        state->section = LV_XML_PARSER_SECTION_INCLUDE_TIMELINE;
        return;
    }
    else if(lv_streq(name, "timeline")) {
        state->section = LV_XML_PARSER_SECTION_TIMELINE;
        return;
    }
    else if(lv_streq(name, "view")) {
        state->section = LV_XML_PARSER_SECTION_VIEW;
        return;
    }
}

void lv_xml_parser_end_section(lv_xml_parser_state_t * state, const char * name)
{
    /* Every block name lv_xml_parser_start_section() can open must appear here,
     * or its close tag leaves the section LATCHED: everything between that close
     * tag and the next block opener is then handed to the wrong element
     * processor (a stray tag after `</subjects>` reaches process_subject_element
     * and is warned about as a malformed subject). It self-corrects in practice
     * only because the next tag is nearly always another opener - which is why
     * `</api>`, `</fonts>`, `</images>`, `</subjects>`, `</animation>`,
     * `</timeline>` and `</include_timeline>` were able to go missing.
     *
     * `params` used to be in this list with no matching opener, so `</params>`
     * closed a section it could never have opened - in `<api><params>...`, the
     * exact document that shape appears in, it dropped the state out of
     * SECTION_API while `<api>` was still open. `<params>` is not part of the
     * grammar (`<api>` holds `<prop>` elements), so it is gone from here rather
     * than added to start_section: the two lists must stay symmetric. */
    static const char * const block_names[] = {
        "api",
        "consts",
        "gradients",
        "styles",
        "images",
        "fonts",
        "subjects",
        "animation",
        "include_timeline",
        "timeline",
        "view",
    };

    uint32_t i;
    for(i = 0; i < sizeof(block_names) / sizeof(block_names[0]); i++) {
        if(lv_streq(name, block_names[i])) {
            state->section = LV_XML_PARSER_SECTION_NONE;
            return;
        }
    }

    /*When processing gradient stops, but not a stop was closed got bacg to gradient processing
     * E.g. </linear_gradient>*/
    if(state->section == LV_XML_PARSER_SECTION_GRAD_STOP && !lv_streq(name, "stop")) {
        state->section = LV_XML_PARSER_SECTION_GRAD;
    }
}

void * lv_xml_state_get_parent(lv_xml_parser_state_t * state)
{
    return state->parent;
}

void * lv_xml_state_get_item(lv_xml_parser_state_t * state)
{
    return state->item;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

#endif /* LV_USE_XML */
