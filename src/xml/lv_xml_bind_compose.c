/**
 * @file lv_xml_bind_compose.c
 *
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 356C LLC
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_xml_bind_compose.h"
#if LV_USE_XML

#include <lvgl.h>

/**********************
 *      TYPEDEFS
 **********************/

/**
 * The bindings that share one (object, kind, bits) triple, and the bit each one
 * owns in `held`. `held != 0` is what the object wears.
 */
typedef struct {
    lv_obj_t * obj;
    uint32_t bits;
    uint32_t held;
    struct lv_xml_bind_reason_t * reasons;
    uint8_t kind;
    uint8_t reason_count;
} bind_group_t;

struct lv_xml_bind_reason_t {
    bind_group_t * group;
    struct lv_xml_bind_reason_t * next;
    uint32_t mask;
};

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void group_delete_event_cb(lv_event_t * e);
static bind_group_t * group_find(lv_obj_t * obj, lv_xml_bind_kind_t kind, uint32_t bits);
static void write_bits(lv_obj_t * obj, lv_xml_bind_kind_t kind, uint32_t bits, bool on);

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_xml_bind_target_init(lv_xml_bind_target_t * t, lv_obj_t * obj, lv_xml_bind_kind_t kind, uint32_t bits)
{
    if(t == NULL) return;

    t->reason = NULL;
    t->obj = obj;
    t->kind = (uint8_t)kind;
    t->bits = bits;
    if(obj == NULL) return;

    bind_group_t * g = group_find(obj, kind, bits);
    if(g == NULL) {
        g = lv_malloc_zeroed(sizeof(bind_group_t));
        LV_ASSERT_MALLOC(g);
        if(g == NULL) return;

        g->obj = obj;
        g->kind = (uint8_t)kind;
        g->bits = bits;

        /* The delete handler is also where the group LIVES: group_find reads it
         * back off the object, so a group cannot outlive its object and there is
         * no registry to reset between lv_init() cycles. */
        lv_obj_add_event_cb(obj, group_delete_event_cb, LV_EVENT_DELETE, g);
    }

    if(g->reason_count >= LV_XML_BIND_MAX_REASONS) {
        LV_LOG_WARN("More than %d bindings drive one state/flag of a widget; the extras cannot compose",
                    LV_XML_BIND_MAX_REASONS);
        return;
    }

    lv_xml_bind_reason_t * r = lv_malloc_zeroed(sizeof(lv_xml_bind_reason_t));
    LV_ASSERT_MALLOC(r);
    if(r == NULL) return;

    r->group = g;
    r->mask = 1u << g->reason_count;
    r->next = g->reasons;
    g->reasons = r;
    g->reason_count++;

    t->reason = r;
}

void lv_xml_bind_target_set(lv_xml_bind_target_t * t, bool held)
{
    if(t == NULL || t->obj == NULL) return;

    if(t->reason == NULL) {
        write_bits(t->obj, (lv_xml_bind_kind_t)t->kind, t->bits, held);
        return;
    }

    bind_group_t * g = t->reason->group;
    if(held) g->held |= t->reason->mask;
    else g->held &= ~t->reason->mask;

    write_bits(g->obj, (lv_xml_bind_kind_t)g->kind, g->bits, g->held != 0);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/** The group for a triple, or NULL if this is the first binding to ask for it. */
static bind_group_t * group_find(lv_obj_t * obj, lv_xml_bind_kind_t kind, uint32_t bits)
{
    uint32_t count = lv_obj_get_event_count(obj);
    for(uint32_t i = 0; i < count; i++) {
        lv_event_dsc_t * dsc = lv_obj_get_event_dsc(obj, i);
        if(lv_event_dsc_get_cb(dsc) != group_delete_event_cb) continue;

        bind_group_t * g = (bind_group_t *)lv_event_dsc_get_user_data(dsc);
        if(g != NULL && g->kind == (uint8_t)kind && g->bits == bits) return g;
    }
    return NULL;
}

static void write_bits(lv_obj_t * obj, lv_xml_bind_kind_t kind, uint32_t bits, bool on)
{
    if(kind == LV_XML_BIND_STATE) {
        if(on) lv_obj_add_state(obj, (lv_state_t)bits);
        else lv_obj_remove_state(obj, (lv_state_t)bits);
    }
    else {
        if(on) lv_obj_add_flag(obj, (lv_obj_flag_t)bits);
        else lv_obj_remove_flag(obj, (lv_obj_flag_t)bits);
    }
}

/**
 * The group outlives every binding that shares it, so it is the object - not the
 * bindings - that owns the shares and frees them.
 */
static void group_delete_event_cb(lv_event_t * e)
{
    bind_group_t * dead = (bind_group_t *)lv_event_get_user_data(e);

    lv_xml_bind_reason_t * r = dead->reasons;
    while(r != NULL) {
        lv_xml_bind_reason_t * next = r->next;
        lv_free(r);
        r = next;
    }

    lv_free(dead);
}

#endif /* LV_USE_XML */
