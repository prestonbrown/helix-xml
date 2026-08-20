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
#include <misc/lv_ll.h>
#include <misc/lv_style.h>
#include <core/lv_observer.h>
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
    lv_ll_t instance_ll;       /**< one lv_xml_scope_instance_t per live instance; see `instance_cnt` */
    lv_ll_t timeline_ll;
    lv_ll_t font_ll;
    lv_ll_t image_ll;
    lv_ll_t event_ll;
    const char * view_def;
    const char * extends;
    /** Live instances built from this scope: one per view root created by
     *  lv_xml_create_in_scope(), decremented by that root's LV_EVENT_DELETE.
     *  Gates the scope's own teardown - see `pending_free`. Kept alongside
     *  `instance_ll` rather than derived from it so the gate stays O(1) and
     *  reads as what it is; the list exists for the one thing a bare counter
     *  cannot do, namely disarming the delete handlers of instances that
     *  outlive the engine at lv_xml_component_deinit(). */
    uint32_t instance_cnt;
    uint32_t is_widget : 1;
    uint32_t is_screen : 1;
    /** Set when a free was requested while `instance_cnt` was non-zero. The
     *  scope is already out of the live registry (moved to the pending-free
     *  list) so lookups cannot find it, but its lv_style_t storage is still
     *  handed out to the surviving instances, so the memory is held until the
     *  last one is deleted. See component_scope_retire(). */
    uint32_t pending_free : 1;
    /** Set once this scope has reported an instance-site `name` displacing the
     *  one its own <view> sets. The condition is a static authoring defect, so
     *  it is identical on every instantiation and re-reporting it says nothing
     *  new: one HelixScreen debug bundle carried 363 copies of the warning,
     *  109 of them for a single component. Re-registering the component builds
     *  a fresh scope, so a corrected (or still-wrong) definition is reported
     *  again on the next load. */
    uint32_t name_clash_warned : 1;
    /** Set once another scope has resolved one of our styles through the
     *  `component.style` form, which hands that scope's widgets a raw
     *  lv_style_t* into OUR style_ll. `instance_cnt` cannot see those widgets -
     *  they are instances of the borrower, not of us - so without this flag a
     *  styles-only library (a <styles> block nothing ever instantiates, whose
     *  count is therefore permanently zero) took the eager-free path in
     *  component_scope_retire() and pulled the storage out from under every
     *  widget that had borrowed one. See that function.
     *
     *  Deliberately one-way: it records that a style LEFT this scope, not how
     *  many are live, so a borrowed-from scope is held until
     *  lv_xml_component_deinit(). The precise version would claim the lender
     *  per instantiating view root; this is the conservative form, and it costs
     *  one held scope per re-registration of a file that lends styles. */
    uint32_t styles_borrowed : 1;
    struct _lv_xml_component_scope_t * next;
};

/**
 * One live instance of a component scope. Allocated as a node in
 * `scope->instance_ll` and handed to the view root's LV_EVENT_DELETE handler as
 * its user data, which makes the decrement O(1) and gives
 * lv_xml_component_deinit() something to walk when it has to disarm the handler
 * of an instance that is about to outlive its scope.
 */
typedef struct {
    lv_obj_t * root;                     /**< the instance's view root */
    lv_xml_component_scope_t * scope;    /**< the scope that built it (owns this node) */
} lv_xml_scope_instance_t;

typedef struct {
    const char * name;
    const char * value;
} lv_xml_const_t;

typedef struct {
    const char * name;
    lv_subject_t * subject;
    /** true  = this scope allocated the lv_subject_t (<subject> / <subject_expr>);
                teardown must deinit and free it.
        false = borrowed. C++ owns the storage and registered it via
                lv_xml_register_subject(); the scope only holds the pointer. */
    bool owned;
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
 * The `frag_ll` walk in component_scope_free() is the fallback for records whose
 * instances are somehow still alive when the scope goes; it also removes the
 * pending delete callback so it cannot fire on an already-freed record. Since a
 * scope with live instances is now HELD rather than freed, that fallback is only
 * reached on the forced teardown in lv_xml_component_deinit() and for records
 * that never had a view root to hang the delete callback on.
 */
typedef struct {
    lv_obj_t *      parent;         /* enclosing element the expansion's children attach to */
    lv_obj_t *      view_root;      /* instance view root; its LV_EVENT_DELETE reclaims this record */
    /* The scope whose `frag_ll` owns this record. Held as a pointer rather than
     * re-derived from `scope.name`: a hot reload replaces the definition under
     * the same name while this instance is still alive, and a by-name lookup at
     * delete time would then return the NEW scope and lv_ll_remove() this node
     * from a list it was never in - which leaves the OLD scope's frag_ll head
     * pointing at freed memory. The pointer stays valid because a scope with
     * live instances is never freed (see `pending_free`). NULL only for records
     * that never found a registered home. */
    lv_xml_component_scope_t * owner_scope;
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
 * Free every registered component scope and reset the registry to empty.
 *
 * Called from lv_xml_deinit(). lv_xml_component_init() only lv_ll_init()s the
 * list, so without this every scope registered in the previous cycle is
 * orphaned. Must run before lv_deinit().
 */
void lv_xml_component_deinit(void);

/**
 * Initialize the linked lists of a component context
 * @param scope     pointer to a component contexts
 */
void lv_xml_component_scope_init(lv_xml_component_scope_t * scope);

/**
 * Count one live instance of `scope` and tie its lifetime to `view_root`.
 *
 * Widgets built from a component hold RAW `lv_style_t *` pointers into their
 * scope's `style_ll`, so the scope must outlive every instance it produced.
 * This is the one place that establishes that: the count is incremented here
 * and decremented from an LV_EVENT_DELETE handler on `view_root`, and
 * `component_scope_free()` refuses to free while the count is non-zero.
 *
 * Called from lv_xml_create_in_scope(), which is the single funnel every
 * instance passes through - top-level (`lv_xml_create`, `lv_xml_create_screen`)
 * and nested (`lv_xml_component_process`) alike, so a nested component counts
 * against ITS OWN scope, not its host's.
 *
 * No-op for the `"globals"` scope (not a component; never retired) and for a
 * NULL `view_root` (degenerate parse - there is no instance to protect and
 * nowhere to hang the decrement, so nothing is counted).
 *
 * @param scope      the scope the instance was built from
 * @param view_root  the instance's view root; may be NULL
 */
void lv_xml_component_scope_instance_attach(lv_xml_component_scope_t * scope, lv_obj_t * view_root);

/**
 * Detach the observer and free the retained body/snapshots of a subject-bound
 * fragment record (implemented in lv_xml.c, which owns the capture type). Does
 * NOT delete the expansion's widgets: the mandatory teardown order deletes the
 * component instance before unregister, so the expansion roots are already gone
 * and touching them would be a use-after-free.
 * @param frag      pointer to a xml_frag_record_t record
 */
void lv_xml_frag_record_free(xml_frag_record_t * frag);

/**
 * Register a subject the SCOPE owns: the caller allocated the `lv_subject_t` for
 * a `<subject>` / `<subject_expr>` element and hands ownership over, so scope
 * teardown deinits and frees it. Internal to the parser -- deliberately NOT in
 * lv_xml.h, because a public "the library now owns your subject" entry point is
 * exactly the footgun this flag exists to prevent. Everything outside the parser
 * uses `lv_xml_register_subject()`, which records the subject as borrowed.
 * @param scope     the scope to register into; NULL means the `"globals"` scope
 * @param name      name to register under (copied)
 * @param subject   the subject; ownership transfers to `scope`
 * @return          LV_RESULT_OK on success
 */
lv_result_t lv_xml_register_subject_owned(lv_xml_component_scope_t * scope, const char * name,
                                          lv_subject_t * subject);

/**
 * Release what a `subjects_ll` record owns: always its name copy, plus the
 * `lv_subject_t` itself when `owned` is set. Single home for the ownership
 * rule -- see the comment on the definition. Does not unlink the record.
 * @param s   the record; NULL is a no-op
 */
void lv_xml_subject_record_release(lv_xml_subject_t * s);

/**
 * Release only the `lv_subject_t` a `subjects_ll` record owns, keeping the
 * record and its name copy. For re-registration, which replaces the subject
 * under an existing name. No-op for a borrowed record -- see the comment on
 * lv_xml_subject_record_release()'s definition for why that is not optional.
 * Leaves `s->subject` dangling; the caller must overwrite or drop it.
 * @param s   the record; NULL is a no-op
 */
void lv_xml_subject_record_release_storage(lv_xml_subject_t * s);

/**
 * Tear down the `subject_expr_ll` record whose derived subject is @p derived,
 * if there is one: detach its observers from their input subjects, free the
 * expression and the shared observer context, and unlink the record. No-op when
 * @p derived did not come from a `<subject_expr>`.
 *
 * Needed before releasing an owned subject OUTSIDE scope teardown -- a
 * re-registration of the same name. The derived lv_subject_t lives in
 * `subjects_ll`, but the observers that write into it live here, and they would
 * keep firing into the freed subject. Scope teardown does not need it: it drains
 * `subject_expr_ll` in full, in the right order, on its own.
 * @param scope     the scope holding the record; NULL is a no-op
 * @param derived   the derived subject to match; NULL is a no-op
 */
void lv_xml_subject_expr_drop_for_subject(lv_xml_component_scope_t * scope, const lv_subject_t * derived);

/**********************
 *      MACROS
 **********************/

#endif /* LV_USE_XML */

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LV_XML_COMPONENT_PRIVATE_H*/
