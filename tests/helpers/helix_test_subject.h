/**
 * @file helix_test_subject.h
 *
 * Created subjects for the test cases: lv_subject_create() plus a first value,
 * in one call.
 *
 * A created subject lives on LVGL's global subject list, so lv_deinit() in
 * helix_test_env_teardown() deletes every one a test left behind. A
 * file-static pointer to one therefore dangles across a test boundary: assign
 * it in the test that uses it, and only lv_subject_delete() it within the
 * same test.
 *
 * Each first value goes through the ordinary setter, so the previous value
 * reads as the type's neutral one (0, "", NULL, black).
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef HELIX_TEST_SUBJECT_H
#define HELIX_TEST_SUBJECT_H

#include <lvgl.h>

#include "unity.h"

static inline lv_subject_t * helix_test_subject_int(int32_t value)
{
    lv_subject_t * s = lv_subject_create(LV_SUBJECT_TYPE_INT);
    TEST_ASSERT_NOT_NULL_MESSAGE(s, "lv_subject_create(INT) failed");
    lv_subject_set_int(s, value);
    return s;
}

#if LV_USE_FLOAT
static inline lv_subject_t * helix_test_subject_float(float value)
{
    lv_subject_t * s = lv_subject_create(LV_SUBJECT_TYPE_FLOAT);
    TEST_ASSERT_NOT_NULL_MESSAGE(s, "lv_subject_create(FLOAT) failed");
    lv_subject_set_float(s, value);
    return s;
}
#endif

static inline lv_subject_t * helix_test_subject_color(lv_color_t value)
{
    lv_subject_t * s = lv_subject_create(LV_SUBJECT_TYPE_COLOR);
    TEST_ASSERT_NOT_NULL_MESSAGE(s, "lv_subject_create(COLOR) failed");
    lv_subject_set_color(s, value);
    return s;
}

static inline lv_subject_t * helix_test_subject_pointer(void * value)
{
    lv_subject_t * s = lv_subject_create(LV_SUBJECT_TYPE_POINTER);
    TEST_ASSERT_NOT_NULL_MESSAGE(s, "lv_subject_create(POINTER) failed");
    lv_subject_set_pointer(s, value);
    return s;
}

/**
 * A string subject over caller-owned buffers. @p prev_buf may be NULL. The
 * buffers must outlive the subject. lv_subject_set_string_buffer_static()
 * clears @p buf before anything else, so @p value is copied aside first in
 * case it points into one of the buffers.
 */
static inline lv_subject_t * helix_test_subject_string(char * buf, char * prev_buf, size_t size,
                                                       const char * value)
{
    char * initial = lv_strdup(value);
    TEST_ASSERT_NOT_NULL_MESSAGE(initial, "OOM copying a string subject's first value");
    lv_subject_t * s = lv_subject_create(LV_SUBJECT_TYPE_STRING);
    TEST_ASSERT_NOT_NULL_MESSAGE(s, "lv_subject_create(STRING) failed");
    lv_subject_set_string_buffer_static(s, buf, prev_buf, size);
    lv_subject_set_string(s, initial);
    lv_free(initial);
    return s;
}

#endif /* HELIX_TEST_SUBJECT_H */
