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

/** Free the process-global resources. Registered with atexit(). */
static void helix_test_env_shutdown(void)
{
    if(!s_lvgl_up) return;

    s_lvgl_up = false;
    s_display = NULL;

    lv_deinit();

    free(s_draw_buf);
    s_draw_buf = NULL;
}

/** Bring LVGL and the headless display up. Runs at most once per process. */
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
    atexit(helix_test_env_shutdown);
}

/**
 * Swap in a brand-new active screen and delete the outgoing one.
 *
 * Recreating rather than lv_obj_clean()ing matters: a test can set styles,
 * flags, a layout or a scroll position directly on the screen, and only a new
 * object drops all of that.
 */
static void helix_test_swap_screen(void)
{
    lv_obj_t * outgoing = lv_display_get_screen_active(s_display);

    lv_obj_t * fresh = lv_obj_create(NULL);
    TEST_ASSERT_NOT_NULL_MESSAGE(fresh, "helix_test_env: could not create a screen");

    lv_screen_load(fresh);

    /* Safe here: we are in setUp()/tearDown(), not inside an LVGL event
     * dispatch or timer batch, and `outgoing` is no longer the active screen. */
    if(outgoing && outgoing != fresh) lv_obj_delete(outgoing);
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void helix_test_env_setup(void)
{
    if(!s_lvgl_up) helix_test_lvgl_start();

    helix_test_swap_screen();

    lv_xml_init();
}

void helix_test_env_teardown(void)
{
    /* Tolerate an unpaired teardown rather than dereferencing a NULL display:
     * lv_display_get_screen_active(NULL) would fall back to the default
     * display, which does not exist before the first setup. */
    if(!s_lvgl_up) return;

    /* Delete this test's widgets while the XML engine is still up, so any
     * destructor that reaches back into a component scope still finds it. */
    lv_obj_t * screen = lv_display_get_screen_active(s_display);
    if(screen) lv_obj_clean(screen);

    lv_xml_deinit();
}

lv_display_t * helix_test_env_display(void)
{
    return s_display;
}

lv_obj_t * helix_test_env_screen(void)
{
    return s_display ? lv_display_get_screen_active(s_display) : NULL;
}
