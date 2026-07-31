/**
 * @file lv_xml_component_private.h
 *
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2025 LVGL Kft
 * SPDX-FileCopyrightText: 2026 356C LLC
 */

#ifndef LV_XML_COMPONENT_PRIVATE_H
#define LV_XML_COMPONENT_PRIVATE_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include "lv_xml.h"
#if LV_USE_XML

#include "lv_xml_utils.h"
#include "../misc/lv_ll.h"
#include "../misc/lv_style.h"
#include "../core/lv_observer.h"
#include "lv_xml_expr.h"

/**********************
 *      TYPEDEFS
 **********************/

typedef  void * (*lv_xml_component_process_cb_t)(lv_obj_t * parent, const char * data, const char ** attrs);

struct _lv_xml_component_scope_t {
    const char * name;
    lv_ll_t style_ll;
    lv_ll_t const_ll;
    lv_ll_t param_ll;
    lv_ll_t gradient_ll;
    lv_ll_t subjects_ll;
    lv_ll_t subject_expr_ll;   /**< <subject_expr> derived subjects: (expr, ctx) pairs freed at scope teardown */
    lv_ll_t frag_ll;           /**< subject-bound <repeat>/<if> records (xml_frag_record_t): retained capture + observer, freed at scope teardown */
    lv_ll_t timeline_ll;
    lv_ll_t font_ll;
    lv_ll_t image_ll;
    lv_ll_t event_ll;
    const char * view_def;
    const char * extends;
    uint32_t is_widget : 1;
    uint32_t is_screen : 1;
    struct _lv_xml_component_scope_t * next;
};

typedef struct {
    const char * name;
    const char * value;
} lv_xml_const_t;

typedef struct {
    const char * name;
    lv_subject_t * subject;
} lv_xml_subject_t;

/**
 * A `<subject_expr>` derived-subject record: the compiled expression and its
 * shared observer context, kept only so scope teardown can free them. The
 * derived `lv_subject_t*` itself lives in `subjects_ll` (via lv_xml_subject_t)
 * like any other subject and is freed by the existing subjects_ll cleanup.
 */
typedef struct {
    lv_xml_expr_t * expr;
    void * ctx;                  /* subject_expr_ctx_t*, defined in lv_xml_component.c */
    lv_observer_t ** observers;  /* one per distinct input subject, detached at teardown */
    uint32_t observer_count;
} lv_xml_subject_expr_t;

/**
 * A subject-bound fragment record: `<repeat count="subj">` (and, in later tasks,
 * `<if>`). When the bound subject changes, the expansion re-materializes: an
 * observer tears down the prior expansion (async, off-tree reparent — never a
 * sync delete inside the observer) and replays the captured body. The record
 * retains everything the rebuild needs — the captured SAX-event body, a value
 * snapshot of the resolution scope, and a deep copy of the component's parent
 * attributes — because the original parse state is long gone by rebuild time.
 *
 * Lifetime is tied to the INSTANCE, not the scope: an LV_EVENT_DELETE callback on
 * `view_root` detaches the observer, frees the record heap, and unlinks the record
 * from `frag_ll` when the instance is deleted. Because the bound subject is
 * shared (a scope subject reused across instances, or a global), an observer that
 * outlived its instance would fire the rebuild on freed roots (use-after-free).
 * The `frag_ll` walk in lv_xml_component_unregister is the fallback for records
 * whose instances are still alive at unregister; it also removes the pending
 * delete callback so it cannot fire on an already-freed record.
 */
typedef struct {
    lv_obj_t *      parent;         /* enclosing element the expansion's children attach to */
    lv_obj_t *      view_root;      /* instance view root; its LV_EVENT_DELETE reclaims this record */
    lv_subject_t *  count_subject;  /* the bound count subject (<repeat>-specific) */
    lv_observer_t * observer;       /* retained so teardown can detach before the subject is freed */
    void *          capture;        /* xml_frag_capture_t*, the retained body events (owned) */
    lv_obj_t **     roots;          /* top-level objects of the current expansion (array owned) */
    uint32_t        root_count;
    bool            in_rebuild;     /* reentrancy guard for the rebuild callback */
    lv_xml_component_scope_t   scope;         /* value snapshot; list heads shared read-only with the registered scope */
    lv_xml_component_scope_t * parent_scope;  /* stable pointer into a registered scope, or NULL */
    char **         parent_attrs;   /* deep-copied NULL-terminated snapshot, owned (or NULL) */
    void *          bind;           /* <if> only: lv_xml_expr_bind_t* handle; detached in the frag sweep. NULL for <repeat>. */
} xml_frag_record_t;

typedef struct {
    const char * name;
    lv_ll_t anims_ll;
} lv_xml_timeline_t;

typedef struct {
    const char * name;
    const char * def;
    const char * type;
} lv_xml_param_t;

typedef struct {
    const char * name;
    lv_grad_dsc_t grad_dsc;
} lv_xml_grad_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Initialize the components system.
 */
void lv_xml_component_init(void);

/**
 * Initialize the linked lists of a component context
 * @param scope     pointer to a component contexts
 */
void lv_xml_component_scope_init(lv_xml_component_scope_t * scope);

/**
 * Detach the observer and free the retained body/snapshots of a subject-bound
 * fragment record (implemented in lv_xml.c, which owns the capture type). Does
 * NOT delete the expansion's widgets: the mandatory teardown order deletes the
 * component instance before unregister, so the expansion roots are already gone
 * and touching them would be a use-after-free.
 * @param frag      pointer to a xml_frag_record_t record
 */
void lv_xml_frag_record_free(xml_frag_record_t * frag);

/**********************
 *      MACROS
 **********************/

#endif /* LV_USE_XML */

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LV_XML_COMPONENT_PRIVATE_H*/
