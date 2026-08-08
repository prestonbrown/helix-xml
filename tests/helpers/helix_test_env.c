/**
 * @file helix_test_env.c
 *
 * SPDX-License-Identifier: MIT
 */

#include "helpers/helix_test_env.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

/**********************
 *  STATIC VARIABLES
 **********************/

static bool s_lvgl_up;
static lv_display_t * s_display;
static void * s_draw_buf;

/**********************
 *  STATIC FUNCTIONS
 **********************/

/**
 * Headless flush: throw the pixels away and tell LVGL the buffer is free
 * again. Without acknowledging, lv_refr_now()/lv_timer_handler() would stall
 * on `disp->flushing` and helix_test_pump() would spin forever.
 */
static void helix_test_flush_cb(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map)
{
    LV_UNUSED(area);
    LV_UNUSED(px_map);
    lv_display_flush_ready(disp);
}

/** Tear LVGL and the headless display back down. */
static void helix_test_lvgl_stop(void)
{
    if(!s_lvgl_up) return;

    s_lvgl_up = false;
    s_display = NULL;

    /* One call: lv_deinit() deletes the displays, groups, timers and screens
     * itself. Deleting them by hand first is how you get a double free. */
    lv_deinit();

    free(s_draw_buf);
    s_draw_buf = NULL;
}

/** Bring LVGL and the headless display up. */
static void helix_test_lvgl_start(void)
{
    lv_init();

    /* LV_COLOR_DEPTH is 32 in tests/lv_conf.h. Ask LVGL for the pixel size
     * rather than hardcoding it, so changing the colour depth cannot silently
     * under-allocate the buffer. */
    const uint32_t px_size = lv_color_format_get_size(LV_COLOR_FORMAT_NATIVE);
    const uint32_t buf_size = (uint32_t)HELIX_TEST_DISP_HOR_RES * HELIX_TEST_DISP_VER_RES * px_size;

    s_draw_buf = malloc(buf_size);
    TEST_ASSERT_NOT_NULL_MESSAGE(s_draw_buf, "helix_test_env: draw buffer allocation failed");

    s_display = lv_display_create(HELIX_TEST_DISP_HOR_RES, HELIX_TEST_DISP_VER_RES);
    TEST_ASSERT_NOT_NULL_MESSAGE(s_display, "helix_test_env: lv_display_create() returned NULL");

    lv_display_set_flush_cb(s_display, helix_test_flush_cb);
    lv_display_set_buffers(s_display, s_draw_buf, NULL, buf_size, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_default(s_display);

    s_lvgl_up = true;
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void helix_test_env_setup(void)
{
    /* Defensive: a test that aborts between setUp and tearDown leaves LVGL up.
     * Unity does not run tearDown on a failed assertion, so start clean. */
    if(s_lvgl_up) helix_test_lvgl_stop();

    helix_test_lvgl_start();

    lv_xml_init();
}

void helix_test_env_teardown(void)
{
    /* Tolerate an unpaired teardown rather than dereferencing a NULL display. */
    if(!s_lvgl_up) return;

    /* Delete this test's widgets while the XML engine is still up, so any
     * destructor that reaches back into a component scope still finds it.
     * lv_deinit() below would delete them too, but only after lv_xml_deinit()
     * has already freed the scopes out from under them. */
    lv_obj_t * screen = lv_display_get_screen_active(s_display);
    if(screen) lv_obj_clean(screen);

    /* Order matters: the XML registries live in LVGL's heap, so they have to be
     * released before lv_deinit() reclaims the pool underneath them. */
    lv_xml_deinit();

    helix_test_lvgl_stop();
}

lv_display_t * helix_test_env_display(void)
{
    return s_display;
}

lv_obj_t * helix_test_env_screen(void)
{
    return s_display ? lv_display_get_screen_active(s_display) : NULL;
}
