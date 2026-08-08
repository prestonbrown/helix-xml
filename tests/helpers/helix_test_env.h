/**
 * @file helix_test_env.h
 *
 * Per-test LVGL + helix-xml environment for the helix-xml test suite.
 *
 * Usage - every file in cases/ looks like this:
 *
 * @code
 *   #include "helpers/helix_test_env.h"
 *
 *   void setUp(void)    { helix_test_env_setup(); }
 *   void tearDown(void) { helix_test_env_teardown(); }
 *
 *   static void test_something(void) { ... }
 *
 *   int main(void)
 *   {
 *       UNITY_BEGIN();
 *       RUN_TEST(test_something);
 *       return UNITY_END();
 *   }
 * @endcode
 *
 * Unity calls setUp() before and tearDown() after *each* RUN_TEST, so every
 * test starts from a blank screen and an empty XML component registry, and
 * test order does not matter.
 *
 * ---------------------------------------------------------------------------
 * WHY LVGL IS NOT RE-INITIALISED PER TEST
 *
 * The obvious design - lv_init() in setUp(), lv_deinit() in tearDown() - does
 * not work against this engine, and the failure is silent-then-fatal rather
 * than a clean error:
 *
 *   lv_xml_widget.c keeps its widget-processor registry in a file-static list
 *   head (`widget_processor_head`), whose nodes are lv_malloc()'d.
 *   lv_xml_deinit() does not free that list and does not reset the head - it
 *   only unregisters test widgets, calls lv_xml_load_deinit() and frees the
 *   asset-path prefix. So after lv_deinit() resets LVGL's heap, the static
 *   head still points into reclaimed pool memory. The next lv_xml_init()
 *   pushes fresh nodes onto that garbage, and the first lv_xml_create() walks
 *   the list forever: the test does not crash, it HANGS until ctest's timeout.
 *
 *   lv_xml_component.c has the same shape - lv_xml_component_init() does a
 *   plain lv_ll_init() on `component_scope_ll`, orphaning whatever was there.
 *
 * Fixing that means changing the engine, which is a separate piece of work
 * from standing up a harness. So the harness works with what the engine
 * actually supports:
 *
 *   - lv_init() and the display are process-global, done once, on first use.
 *   - Per test: a brand-new screen (the old one is deleted, taking every
 *     widget, local style and flag a previous test set with it), and a
 *     lv_xml_deinit()/lv_xml_init() round trip, which resets the component,
 *     style, subject, font and image registries.
 *
 * What this buys you: widget trees, screens and the XML registries are fully
 * isolated between tests. What it does NOT buy you: LVGL's own process-wide
 * state - the heap, registered event ids, the widget-processor list - carries
 * across. Do not write a test that depends on those being pristine.
 * ---------------------------------------------------------------------------
 *
 * The display is headless: 800x480, LV_COLOR_DEPTH 32, a malloc'd full-frame
 * buffer and a flush callback that only calls lv_display_flush_ready(). Pixels
 * are produced but never inspected - see the rule in tests/lv_conf.h: no test
 * may assert on measured geometry or font metrics.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef HELIX_TEST_ENV_H
#define HELIX_TEST_ENV_H

#include <lvgl.h>

#include "helix_xml.h"
#include "unity.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Width of the headless test display, in pixels. */
#define HELIX_TEST_DISP_HOR_RES 800
/** Height of the headless test display, in pixels. */
#define HELIX_TEST_DISP_VER_RES 480

/**
 * Bring up the environment for one test.
 *
 * First call: lv_init() plus a headless display. Every call: a fresh active
 * screen, then lv_xml_init().
 */
void helix_test_env_setup(void);

/**
 * Tear the environment back down after one test.
 *
 * Empties the active screen (so widget destructors run while the XML engine is
 * still up), then lv_xml_deinit(). LVGL itself stays initialised - see the
 * note at the top of this file.
 */
void helix_test_env_teardown(void);

/** The headless display. Valid from the first helix_test_env_setup() on. */
lv_display_t * helix_test_env_display(void);

/**
 * The active screen. This is the usual parent to pass to lv_xml_create().
 * A different object on every test.
 */
lv_obj_t * helix_test_env_screen(void);

#ifdef __cplusplus
}
#endif

#endif /* HELIX_TEST_ENV_H */
