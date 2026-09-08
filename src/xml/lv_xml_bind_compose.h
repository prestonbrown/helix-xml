/**
 * @file lv_xml_bind_compose.h
 *
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 356C LLC
 */
#ifndef LV_XML_BIND_COMPOSE_H
#define LV_XML_BIND_COMPOSE_H

/*********************
 *      INCLUDES
 *********************/
#include <lvgl.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      DEFINES
 *********************/

/** Bindings one (object, kind, bits) triple can compose; one bit each in a uint32_t. */
#define LV_XML_BIND_MAX_REASONS 32

/**********************
 *      TYPEDEFS
 **********************/

/** Which bitfield of an object a binding drives. */
typedef enum {
    LV_XML_BIND_STATE, /**< lv_obj_add_state()  / lv_obj_remove_state()  */
    LV_XML_BIND_FLAG,  /**< lv_obj_add_flag()   / lv_obj_remove_flag()   */
} lv_xml_bind_kind_t;

/** One binding's share of a composed bitfield. Opaque; owned by the object. */
typedef struct lv_xml_bind_reason_t lv_xml_bind_reason_t;

/**
 * One binding's handle on the state or flag bits it drives. Embed it by value in
 * the binding's context and drive it with lv_xml_bind_target_set().
 */
typedef struct {
    lv_xml_bind_reason_t * reason; /**< NULL when no share could be claimed */
    lv_obj_t * obj;
    uint32_t bits;
    uint8_t kind;
} lv_xml_bind_target_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Claim a share of `bits` of `obj`'s state or flag bitfield for one binding.
 *
 * Every binding on the same (object, kind, bits) triple shares one applied
 * result: the bits are set while ANY of them holds and cleared only once none
 * does. Two bindings targeting one state on one widget therefore reach the same
 * outcome whatever order their subjects notify in, and neither can clear what
 * the other asserts.
 *
 * The share starts unheld and nothing is applied until the first
 * lv_xml_bind_target_set(), so a state or flag set by an XML attribute stands
 * until the binding first evaluates.
 *
 * A share that cannot be claimed - out of memory, or more than
 * LV_XML_BIND_MAX_REASONS bindings on one triple - leaves `t` writing the bits
 * directly, so the binding still works and the triple degrades to
 * last-writer-wins rather than dropping a binding on the floor.
 *
 * @param t     the handle to initialise; must not be NULL
 * @param obj   the object to drive; must not be NULL
 * @param kind  state bitfield or flag bitfield
 * @param bits  the state/flag mask this binding drives
 */
void lv_xml_bind_target_init(lv_xml_bind_target_t * t, lv_obj_t * obj, lv_xml_bind_kind_t kind, uint32_t bits);

/** Record whether this binding holds its bits, and re-apply the composed result. */
void lv_xml_bind_target_set(lv_xml_bind_target_t * t, bool held);

#ifdef __cplusplus
}
#endif
#endif /* LV_XML_BIND_COMPOSE_H */
