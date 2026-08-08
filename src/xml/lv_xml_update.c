/**
 * @file lv_xml_update.c
 *
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2025 LVGL Kft
 * SPDX-FileCopyrightText: 2026 356C LLC
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_xml_update.h"
#if LV_USE_XML &&  LV_USE_OBJ_NAME

#include <lvgl.h>
#include "lv_xml_widget.h"
#include "lv_xml_parser.h"
#include "../libs/expat/expat.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**
 * expat's start handler cannot influence XML_Parse()'s return value, so every
 * dispatch failure used to be a bare LV_LOG_WARN and the caller saw
 * LV_RESULT_OK for a snippet that updated nothing. The handler now records the
 * failure here instead.
 *
 * `state` is first so `&ctx->state` and `ctx` are interchangeable for the
 * widget apply_cb, which only ever sees the parser state.
 */
typedef struct {
    lv_xml_parser_state_t state;
    bool failed;
} update_ctx_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void start_handler(void * user_data, const char * name, const char ** attrs);
static void end_handler(void * user_data, const char * name);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/


lv_result_t lv_xml_update_from_data(const char * xml_def)
{
    /*Create a dummy parser state*/
    update_ctx_t ctx;
    lv_xml_parser_state_init(&ctx.state);
    ctx.failed = false;

    /* Parse the XML to extract metadata */
    XML_Parser parser = XML_ParserCreate(NULL);
    XML_SetUserData(parser, &ctx);
    XML_SetElementHandler(parser, start_handler, end_handler);

    if(XML_Parse(parser, xml_def, lv_strlen(xml_def), XML_TRUE) == XML_STATUS_ERROR) {
        LV_LOG_ERROR("XML parsing error: %s on line %lu",
                     XML_ErrorString(XML_GetErrorCode(parser)),
                     (unsigned long)XML_GetCurrentLineNumber(parser));
        XML_ParserFree(parser);
        return LV_RESULT_INVALID;
    }
    XML_ParserFree(parser);

    /* At least one element could not be dispatched: wrong tag prefix, unknown
     * widget type, no `name`, or no widget by that name on the active screen. */
    return ctx.failed ? LV_RESULT_INVALID : LV_RESULT_OK;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void start_handler(void * user_data, const char * name, const char ** attrs)
{
    update_ctx_t * ctx = (update_ctx_t *)user_data;
    lv_xml_parser_state_t * state = &ctx->state;

    /* XML allows exactly one root element, so a snippet carrying more than one
     * update needs a container. `<updates>` is it: not an update itself, and
     * not a failure either. Before the return value carried dispatch failures
     * this fell into the prefix check below and logged a spurious warning. */
    if(lv_streq(name, "updates")) return;

    size_t update_len = lv_strlen("update-");
    size_t name_len = lv_strlen(name);
    if(name_len < update_len) {
        LV_LOG_WARN("%s doesn't start with `update-`", name);
        ctx->failed = true;
        return;
    }

    if(lv_memcmp(name, "update-", update_len)) {
        LV_LOG_WARN("%s doesn't start with `update-`", name);
        ctx->failed = true;
        return;
    }

    name = &name[update_len]; /*Get e.g. update-lv_slider -> lv_slider*/
    name_len -= update_len;

    lv_widget_processor_t * proc = lv_xml_widget_get_processor(name);
    if(proc == NULL) {
        LV_LOG_WARN("%s is not a known widget", name);
        ctx->failed = true;
        return;
    }

    const char * widget_name = lv_xml_get_value_of(attrs, "name");
    if(widget_name == NULL) {
        LV_LOG_WARN("There is no name property");
        ctx->failed = true;
        return;
    }

    lv_obj_t * obj = lv_obj_get_child_by_name(lv_screen_active(), widget_name);
    if(obj == NULL) {
        LV_LOG_WARN("No widget is found with the name of `%s`", widget_name);
        ctx->failed = true;
        return;
    }

    /*Clean the name property the prevent applying it*/
    for(int i = 0; attrs[i]; i += 2) {
        if(lv_streq(attrs[i], "name")) {
            attrs[i] = "";
            attrs[i + 1] = "";
        }
    }
    state->item = obj;
    proc->apply_cb(state, attrs);
}

static void end_handler(void * user_data, const char * name)
{
    LV_UNUSED(user_data);
    LV_UNUSED(name);
}

#endif /* LV_USE_XML */
