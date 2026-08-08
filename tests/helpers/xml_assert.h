/**
 * @file xml_assert.h
 *
 * Assertions for XML-built widget trees, layered over Unity.
 *
 * ---------------------------------------------------------------------------
 * THE RULE: NO ASSERTION MAY DEPEND ON MEASURED PIXEL GEOMETRY OR FONT METRICS.
 *
 * There is deliberately no ASSERT_WIDTH, ASSERT_POS or ASSERT_TEXT_WIDTH here,
 * and none may be added. tests/lv_conf.h configures LVGL for the test build -
 * colour depth, default font, enabled widgets, theme - and those are not the
 * values any real device runs. A geometry assertion would encode this config
 * into the test and would break for reasons that have nothing to do with the
 * XML engine.
 *
 * Assert on STRUCTURE and on VALUES YOU SET:
 *   - a widget exists / does not exist, and is where you expect in the tree
 *   - names, child counts, parent/child relationships
 *   - label text, flags, states
 *   - style properties whose value the XML under test declared
 *
 * If you genuinely need to prove a layout behaviour, prove it by a property
 * the XML set (e.g. `lv_obj_get_style_flex_flow`), never by the pixels that
 * came out of it.
 * ---------------------------------------------------------------------------
 *
 * These are macros, not functions, so Unity reports the line in your test file
 * rather than a line in this header. The ones that yield a value use a GNU
 * statement expression, which both gcc and clang support - the only two
 * compilers this suite is built with.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef HELIX_XML_ASSERT_H
#define HELIX_XML_ASSERT_H

#include <stdarg.h>
#include <stdio.h>

#include <lvgl.h>

#include "helix_xml.h"
#include "unity.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Format a one-off assertion message.
 *
 * Unity takes a plain `const char *`, so anything with context in it has to be
 * rendered somewhere. The buffer is static and per-translation-unit: fine,
 * because the process is on its way to exit() as soon as one of these is
 * actually used for a failure.
 */
static inline const char * helix_xml_assert_msgf(const char * fmt, ...)
{
    static char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return buf;
}

/** Human-readable name of an object, for failure messages. */
static inline const char * helix_xml_assert_name_of(const lv_obj_t * obj)
{
#if LV_USE_OBJ_NAME
    const char * n = obj ? lv_obj_get_name(obj) : NULL;
    return n ? n : "<unnamed>";
#else
    LV_UNUSED(obj);
    return "<names disabled>";
#endif
}

/*---------------------------------------------------------------------------
 * Registration and creation
 *--------------------------------------------------------------------------*/

/**
 * Register an inline XML component and assert it parsed.
 * @param name         component name, as later passed to lv_xml_create()
 * @param xml_literal  the component's XML source
 */
#define ASSERT_XML_REGISTERS(name, xml_literal)                                          \
    TEST_ASSERT_EQUAL_INT_MESSAGE(                                                       \
        LV_RESULT_OK,                                                                    \
        (int)lv_xml_register_component_from_data((name), (xml_literal)),                 \
        helix_xml_assert_msgf(                                                           \
            "lv_xml_register_component_from_data(\"%s\") failed - malformed XML, "        \
            "unknown widget, or a name already registered", (name)))

/**
 * Create a registered component/widget, assert it was created, and evaluate to
 * the new object.
 * @param parent  parent object
 * @param name    registered component or widget name
 * @param attrs   NULL-terminated attribute/value pair list, or NULL
 */
#define XML_CREATE(parent, name, attrs)                                                  \
    __extension__({                                                                      \
        lv_obj_t * hx_created_ = (lv_obj_t *)lv_xml_create((parent), (name), (attrs));   \
        TEST_ASSERT_NOT_NULL_MESSAGE(                                                    \
            hx_created_,                                                                 \
            helix_xml_assert_msgf(                                                       \
                "lv_xml_create(parent, \"%s\", attrs) returned NULL - not registered, "   \
                "or the component body failed to build", (name)));                       \
        hx_created_;                                                                     \
    })

/*---------------------------------------------------------------------------
 * Tree structure
 *--------------------------------------------------------------------------*/

/**
 * Assert a named descendant exists under @p root, and evaluate to it.
 * Search is recursive, exactly as lv_obj_find_by_name().
 */
#define ASSERT_NAMED(root, n)                                                            \
    __extension__({                                                                      \
        lv_obj_t * hx_root_ = (lv_obj_t *)(root);                                        \
        lv_obj_t * hx_found_ = lv_obj_find_by_name(hx_root_, (n));                       \
        TEST_ASSERT_NOT_NULL_MESSAGE(                                                    \
            hx_found_,                                                                   \
            helix_xml_assert_msgf(                                                       \
                "no widget named \"%s\" under \"%s\" (%u direct children)",                \
                (n), helix_xml_assert_name_of(hx_root_),                                 \
                (unsigned)(hx_root_ ? lv_obj_get_child_count(hx_root_) : 0u)));          \
        hx_found_;                                                                       \
    })

/** Assert NO descendant of @p root is named @p n. */
#define ASSERT_NO_NAMED(root, n)                                                         \
    do {                                                                                 \
        lv_obj_t * hx_root_ = (lv_obj_t *)(root);                                        \
        TEST_ASSERT_NULL_MESSAGE(                                                        \
            lv_obj_find_by_name(hx_root_, (n)),                                          \
            helix_xml_assert_msgf(                                                       \
                "widget named \"%s\" still exists under \"%s\" but should not",           \
                (n), helix_xml_assert_name_of(hx_root_)));                               \
    } while(0)

/** Assert @p obj has exactly @p n direct children. */
#define ASSERT_CHILD_COUNT(obj, n)                                                       \
    do {                                                                                 \
        lv_obj_t * hx_obj_ = (lv_obj_t *)(obj);                                          \
        TEST_ASSERT_NOT_NULL_MESSAGE(hx_obj_, "ASSERT_CHILD_COUNT on a NULL object");    \
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(                                                \
            (uint32_t)(n), lv_obj_get_child_count(hx_obj_),                              \
            helix_xml_assert_msgf("wrong number of children under \"%s\"",                \
                                  helix_xml_assert_name_of(hx_obj_)));                   \
    } while(0)

/*---------------------------------------------------------------------------
 * Content
 *--------------------------------------------------------------------------*/

/** Assert @p obj is a label whose text equals @p s. */
#define ASSERT_LABEL_TEXT(obj, s)                                                        \
    do {                                                                                 \
        lv_obj_t * hx_obj_ = (lv_obj_t *)(obj);                                          \
        TEST_ASSERT_NOT_NULL_MESSAGE(hx_obj_, "ASSERT_LABEL_TEXT on a NULL object");     \
        TEST_ASSERT_TRUE_MESSAGE(                                                        \
            lv_obj_check_type(hx_obj_, &lv_label_class),                                 \
            helix_xml_assert_msgf("\"%s\" is not a label, so it has no text",             \
                                  helix_xml_assert_name_of(hx_obj_)));                   \
        TEST_ASSERT_EQUAL_STRING_MESSAGE(                                                \
            (s), lv_label_get_text(hx_obj_),                                             \
            helix_xml_assert_msgf("wrong text on label \"%s\"",                           \
                                  helix_xml_assert_name_of(hx_obj_)));                   \
    } while(0)

/*---------------------------------------------------------------------------
 * Flags, states, styles
 *--------------------------------------------------------------------------*/

/** Assert @p obj has flag @p f set (e.g. LV_OBJ_FLAG_HIDDEN). */
#define ASSERT_FLAG(obj, f)                                                              \
    do {                                                                                 \
        lv_obj_t * hx_obj_ = (lv_obj_t *)(obj);                                          \
        TEST_ASSERT_NOT_NULL_MESSAGE(hx_obj_, "ASSERT_FLAG on a NULL object");           \
        TEST_ASSERT_TRUE_MESSAGE(                                                        \
            lv_obj_has_flag(hx_obj_, (f)),                                               \
            helix_xml_assert_msgf("\"%s\" is missing flag %s",                            \
                                  helix_xml_assert_name_of(hx_obj_), #f));               \
    } while(0)

/** Assert @p obj does NOT have flag @p f set. */
#define ASSERT_NO_FLAG(obj, f)                                                           \
    do {                                                                                 \
        lv_obj_t * hx_obj_ = (lv_obj_t *)(obj);                                          \
        TEST_ASSERT_NOT_NULL_MESSAGE(hx_obj_, "ASSERT_NO_FLAG on a NULL object");        \
        TEST_ASSERT_FALSE_MESSAGE(                                                       \
            lv_obj_has_flag(hx_obj_, (f)),                                               \
            helix_xml_assert_msgf("\"%s\" unexpectedly has flag %s",                      \
                                  helix_xml_assert_name_of(hx_obj_), #f));               \
    } while(0)

/** Assert @p obj is in state @p s (e.g. LV_STATE_CHECKED). */
#define ASSERT_STATE(obj, s)                                                             \
    do {                                                                                 \
        lv_obj_t * hx_obj_ = (lv_obj_t *)(obj);                                          \
        TEST_ASSERT_NOT_NULL_MESSAGE(hx_obj_, "ASSERT_STATE on a NULL object");          \
        TEST_ASSERT_TRUE_MESSAGE(                                                        \
            lv_obj_has_state(hx_obj_, (s)),                                              \
            helix_xml_assert_msgf("\"%s\" is not in state %s",                            \
                                  helix_xml_assert_name_of(hx_obj_), #s));               \
    } while(0)

/**
 * Assert an integer-valued style property.
 * @param obj       object to read
 * @param prop      lv_style_prop_t, e.g. LV_STYLE_PAD_ALL
 * @param selector  part/state selector, e.g. LV_PART_MAIN
 * @param expected  expected `.num` value
 *
 * Only for properties the XML under test actually set. Do not use it to read
 * back a value LVGL computed from layout - see the rule at the top of this file.
 */
#define ASSERT_STYLE_INT(obj, prop, selector, expected)                                  \
    do {                                                                                 \
        lv_obj_t * hx_obj_ = (lv_obj_t *)(obj);                                          \
        TEST_ASSERT_NOT_NULL_MESSAGE(hx_obj_, "ASSERT_STYLE_INT on a NULL object");      \
        lv_style_value_t hx_val_ = lv_obj_get_style_prop(hx_obj_, (selector), (prop));   \
        TEST_ASSERT_EQUAL_INT32_MESSAGE(                                                 \
            (int32_t)(expected), (int32_t)hx_val_.num,                                   \
            helix_xml_assert_msgf("wrong %s on \"%s\" (selector %s)",                     \
                                  #prop, helix_xml_assert_name_of(hx_obj_), #selector)); \
    } while(0)

#ifdef __cplusplus
}
#endif

#endif /* HELIX_XML_ASSERT_H */
