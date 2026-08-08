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
 * WHAT RESETS BETWEEN TESTS: EVERYTHING
 *
 * Each test gets a complete lv_init() / lv_deinit() cycle. Nothing at all
 * carries across a test boundary:
 *
 *   - LVGL's heap. LV_USE_STDLIB_MALLOC is LV_STDLIB_BUILTIN, so lv_deinit()
 *     resets the whole pool. A leak in test N cannot starve test N+1, and the
 *     addresses handed out in test N+1 are very likely the same ones test N
 *     used - so never assert on pointer identity across a cycle.
 *   - The display, its draw buffer and the active screen: created in setUp(),
 *     destroyed in tearDown(). Every widget, local style, flag, layout and
 *     scroll position goes with them.
 *   - Registered event ids, timers, groups, animations, the log print callback
 *     the tests install - all LVGL globals.
 *   - The XML engine: components, styles, subjects, fonts, images, the asset
 *     path prefix, and the widget-processor registry.
 *
 * This used to be impossible, and the failure was silent-then-fatal rather
 * than a clean error. lv_xml_widget.c keeps its widget-processor registry in a
 * file-static list head whose nodes are lv_malloc()'d, and lv_xml_deinit()
 * neither freed them nor reset the head - so after lv_deinit() reclaimed the
 * pool, the static pointed into reclaimed memory, the next lv_xml_init()
 * pushed onto garbage, and the first lv_xml_create() walked the list forever:
 * a HANG until ctest's timeout, not a crash. lv_xml_component.c had the same
 * shape via a bare lv_ll_init() on its scope list. Both now have real deinits
 * (lv_xml_widget_deinit(), lv_xml_component_deinit()) that lv_xml_deinit()
 * calls, which is what makes the cycle below safe.
 *
 * ORDER, which is load-bearing in tearDown():
 *   1. lv_obj_clean(active screen) - widget destructors run while the XML
 *      engine is still up, so anything reaching back into a component scope
 *      still finds it.
 *   2. lv_xml_deinit()             - the registries live in LVGL's heap, so
 *                                    they must be released before the pool is
 *                                    reclaimed.
 *   3. lv_deinit()                 - one call; it deletes displays, groups,
 *                                    timers and screens itself. Deleting them
 *                                    by hand first is how you get a double free.
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
 * Bring up the environment for one test: lv_init(), a headless display and its
 * draw buffer, a blank active screen, then lv_xml_init().
 *
 * Idempotent-ish: if a previous cycle is still open (a failed Unity assertion
 * skips tearDown()) it is closed first, so the next test still starts clean.
 */
void helix_test_env_setup(void);

/**
 * Tear the environment back down after one test: empty the active screen, then
 * lv_xml_deinit(), then lv_deinit() and free the draw buffer. See the ORDER
 * note at the top of this file - it is not arbitrary.
 */
void helix_test_env_teardown(void);

/** The headless display. Valid only between setup and teardown of one test. */
lv_display_t * helix_test_env_display(void);

/**
 * The active screen. This is the usual parent to pass to lv_xml_create().
 * A fresh, empty object on every test - though possibly at the same ADDRESS as
 * the previous test's, since lv_deinit() resets the allocator pool.
 */
lv_obj_t * helix_test_env_screen(void);

#ifdef __cplusplus
}
#endif

#endif /* HELIX_TEST_ENV_H */
