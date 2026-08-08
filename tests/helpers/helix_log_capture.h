/**
 * @file helix_log_capture.h
 *
 * Capture LVGL's log output inside a test so it can be asserted on.
 *
 * ---------------------------------------------------------------------------
 * WHY THE LOG IS OFTEN THE ONLY ASSERTION AVAILABLE
 *
 * Nearly every failure path in this engine is an LV_LOG_WARN followed by a
 * SUCCESS return. A malformed attribute is dropped and the widget keeps its
 * default; an unresolvable name resolves to 0; a bad row in a translation pack
 * is skipped and lv_xml_register_translation_from_data() still returns
 * LV_RESULT_OK. The return value and the resulting object tree look exactly the
 * same as if the input had never mentioned the thing at all.
 *
 * So for a large fraction of the suite the emitted warning is the ONLY
 * observable difference between "handled and reported" and "silently wrong" -
 * and for the `_silent` API variants, the ABSENCE of a warning is itself the
 * contract being tested. Without capturing the log those tests would assert
 * nothing.
 *
 * lv_log_register_print_cb() takes precedence over LV_LOG_PRINTF, so installing
 * one both captures the message and keeps the runner output clean.
 *
 * Usage:
 *
 * @code
 *   log_capture_start();
 *   lv_xml_create(screen, "widget_that_will_complain", NULL);
 *   log_capture_stop();
 *   TEST_ASSERT_TRUE(log_contains("unknown"));
 * @endcode
 *
 * Between-test state: helix_test_env_teardown() runs a full lv_deinit(), which
 * drops the registered print callback, so a test that forgets log_capture_stop()
 * cannot leak the callback into the next test. The buffer itself is only ever
 * reset by log_capture_start(), so always start before the code under test.
 *
 * This lived as a file-static copy in all eleven files under cases/ - every case
 * file links as its own executable, so there was nothing to share it through
 * until this header existed. The copies were identical apart from the buffer
 * size (2048/4096/8192); the largest wins here, since a bigger buffer only ever
 * captures a superset of what a smaller one would have.
 * ---------------------------------------------------------------------------
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef HELIX_LOG_CAPTURE_H
#define HELIX_LOG_CAPTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Capture buffer size.
 *
 * Sized for the worst case in the suite: a single malformed input can produce
 * several messages, and one of them quotes a 300-character tag name back at us.
 * Overflow is dropped silently rather than truncated mid-message (see
 * log_capture_cb), so an under-sized buffer would make log_contains() report a
 * false negative rather than fail loudly - hence the headroom.
 */
#ifndef HELIX_LOG_CAPTURE_BUF_SIZE
#define HELIX_LOG_CAPTURE_BUF_SIZE 8192
#endif

/** The accumulated log text since the last log_capture_start(). NUL-terminated. */
static char g_log_buf[HELIX_LOG_CAPTURE_BUF_SIZE];
static size_t g_log_len;

/** lv_log_register_print_cb() sink: append, or drop the message if it won't fit. */
static inline void log_capture_cb(lv_log_level_t level, const char * buf)
{
    LV_UNUSED(level);
    size_t n = strlen(buf);
    if(g_log_len + n + 1 >= sizeof(g_log_buf)) return;
    memcpy(g_log_buf + g_log_len, buf, n + 1);
    g_log_len += n;
}

/** Clear the buffer and start capturing. Call immediately before the code under test. */
static inline void log_capture_start(void)
{
    g_log_buf[0] = '\0';
    g_log_len = 0;
    lv_log_register_print_cb(log_capture_cb);
}

/** Stop capturing. The buffer keeps its contents for assertions. */
static inline void log_capture_stop(void)
{
    lv_log_register_print_cb(NULL);
}

/** True if @p needle appears anywhere in what was captured. */
static inline bool log_contains(const char * needle)
{
    return strstr(g_log_buf, needle) != NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* HELIX_LOG_CAPTURE_H */
