/**
 * @file lv_xml_parser.h
 *
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2025 LVGL Kft
 * SPDX-FileCopyrightText: 2026 356C LLC
 */

#ifndef LV_XML_PARSER_H
#define LV_XML_PARSER_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include <misc/lv_types.h>
#include "lv_xml_types.h"
#include <misc/lv_style.h>
#if LV_USE_XML

#include "lv_xml_component.h"
#include "lv_xml_component_private.h"

/*********************
 *      DEFINES
 *********************/

/** Attribute slots (names AND values, so half this many attributes) the
 *  unknown-attribute check can track for one element. Past it the check is
 *  skipped entirely - see lv_xml_attr_check_begin(). */
#define LV_XML_ATTR_CHECK_MAX 128

/**********************
 *      TYPEDEFS
 **********************/

typedef enum {
    LV_XML_PARSER_SECTION_NONE,
    LV_XML_PARSER_SECTION_API,
    LV_XML_PARSER_SECTION_CONSTS,
    LV_XML_PARSER_SECTION_GRAD,
    LV_XML_PARSER_SECTION_GRAD_STOP,
    LV_XML_PARSER_SECTION_STYLES,
    LV_XML_PARSER_SECTION_FONTS,
    LV_XML_PARSER_SECTION_IMAGES,
    LV_XML_PARSER_SECTION_SUBJECTS,
    LV_XML_PARSER_SECTION_ANIMATION,
    LV_XML_PARSER_SECTION_INCLUDE_TIMELINE,
    LV_XML_PARSER_SECTION_TIMELINE,
    LV_XML_PARSER_SECTION_VIEW
} lv_xml_parser_section_t;

/** One entry per open element during view parsing; accumulates PCDATA so
 *  inline text (`<text_muted>Foo</text_muted>`) can be applied at end-tag. */
typedef struct {
    lv_obj_t * item;    /**< widget the element created (NULL if none) */
    char * buf;         /**< accumulated character data (lv_malloc'd) */
    size_t len;
    size_t cap;
    bool has_conflict;  /**< element already had text=/bind_text=/translation_tag= */
} lv_xml_pcdata_entry_t;

/** One buffered SAX event captured inside a `<repeat>` body. The strings are
 *  deep copies (owned) so each replay iteration re-parses pristine, sigil-bearing
 *  attribute values — this is what defeats the destructive in-place mutation that
 *  `resolve_params`/`resolve_consts` perform. */
typedef struct {
    int kind;            /**< 0=start, 1=end, 2=chardata */
    char * name;         /**< element name (start/end) or text (chardata); owned */
    char ** attrs;       /**< NULL-terminated name/val array, all owned; NULL for end/chardata */
} xml_frag_event_t;

/** State for a single `<repeat>` or `<if>` expansion, hung off `state->context`
 *  for the duration of the fragment body. On the `<repeat>`/`<if>` start tag a
 *  fresh instance is allocated; the body is buffered while `active && !replaying`;
 *  on the matching close tag the buffered events are replayed through the normal
 *  element handlers — `count` times with `current_index` injected for `$i`
 *  (`<repeat>`), or once over the selected true/false slice (`<if>`, `is_if`).
 *  Freed after replay (literal `<repeat>` / static `<if>` — this task) or retained
 *  for reactive rebuild (subject-bound `<repeat>` count; reactive `<if>` cond is
 *  Task 4). */
typedef struct {
    bool     active;             /**< currently buffering a `<repeat>` body */
    uint32_t base_depth;         /**< parent_ll length at the `<repeat>` start tag */
    char *   count_raw;          /**< raw `count` attr (literal / #const / subject name), owned */
    xml_frag_event_t * events;
    uint32_t event_count;
    uint32_t event_cap;
    int32_t  current_index;      /**< `$i` value during replay */
    bool     replaying;          /**< true while replaying (handlers create, not buffer) */
    /* Transient `$i`-formatted strings produced during replay; freed at the end
     * of each expansion so they outlive the per-element handler call that copies
     * them but never accumulate across iterations or rebuilds. */
    char **  idx_strings;
    uint32_t idx_count;
    uint32_t idx_cap;
    /* <if> only: index into events[] where the false-body begins (the <else> split).
     * has_else=false => the whole buffer is the true-body; false-body is empty. */
    uint32_t else_split;
    bool     has_else;
    bool     is_if;              /* distinguishes an <if> capture from a <repeat> capture */
    char *   cond_raw;           /* <if> cond attr (owned); NULL for <repeat> */
} xml_frag_capture_t;

struct _lv_xml_parser_state_t {
    const char * tag_name;
    lv_xml_component_scope_t scope;
    lv_ll_t parent_ll;
    lv_ll_t pcdata_ll;  /*Stack of lv_xml_pcdata_entry_t mirroring open elements*/
    lv_obj_t * parent;
    lv_obj_t * item;
    lv_obj_t * view;    /*Pointer to the created view during component creation*/
    void * context;     /*Custom data stored during parsing. During a view-def parse
                          *it holds the active xml_frag_capture_t* (or NULL); the
                          *component metadata parser reuses it for the current timeline.*/
    const char ** parent_attrs;
    lv_xml_component_scope_t * parent_scope;
    lv_xml_parser_section_t section;
    /* Transient strings produced by embedded `${name}` composition in
     * resolve_params (e.g. bind_text="demo_${i}_v" -> "demo_2_v"). This is the
     * only substitution path that ALLOCATES — the whole-value `$name`/`#const`
     * paths merely repoint an attribute slot at non-owned storage. The composed
     * strings are owned here and freed exactly once at parse end
     * (lv_xml_create_in_scope), so a composed value outlives every element-handler
     * call within the parse but never leaks. resolve_consts never touches them
     * (they don't start with `#`). */
    char **  composed_strings;
    uint32_t composed_count;
    uint32_t composed_cap;
    /* Unknown-attribute check for the element currently being applied. Armed by
     * the caller in lv_xml.c, and only for engine-registered widgets; see
     * lv_xml_attr_check.h for why the bookkeeping is a miss COUNT rather than a
     * table of legal names. `attr_check_attrs` doubles as the armed flag and is
     * borrowed, never owned - the array outlives the apply_cb call it describes.
     * Not nested: apply_cb chains call each other but never a second element's. */
    const char ** attr_check_attrs;
    const char *  attr_check_widget;
    uint8_t attr_check_count;         /**< tracked slots, names and values both */
    uint8_t attr_check_participants;  /**< chains that promised to report misses */
    uint8_t attr_check_miss[LV_XML_ATTR_CHECK_MAX];
    uint8_t attr_check_handled[LV_XML_ATTR_CHECK_MAX];
};

/**********************
 * GLOBAL PROTOTYPES
 **********************/

void lv_xml_parser_state_init(lv_xml_parser_state_t * state);

void lv_xml_parser_start_section(lv_xml_parser_state_t * state, const char * name);

void lv_xml_parser_end_section(lv_xml_parser_state_t * state, const char * name);

void * lv_xml_state_get_parent(lv_xml_parser_state_t * state);

void * lv_xml_state_get_item(lv_xml_parser_state_t * state);

/**********************
 *      MACROS
 **********************/

#endif /* LV_USE_XML */

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LV_XML_PARSER_H*/
