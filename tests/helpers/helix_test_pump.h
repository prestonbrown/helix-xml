/**
 * @file helix_test_pump.h
 *
 * Drive LVGL's clock and timer loop forward inside a test.
 *
 * ---------------------------------------------------------------------------
 * WHY THIS IS MANDATORY BEFORE ASSERTING ON ANYTHING REACTIVE
 *
 * Setting a subject does not immediately rebuild the widgets bound to it. The
 * reactive constructs in the XML engine - `<repeat count="a_subject">` and
 * reactive `<if cond="...">` - cannot delete and recreate their subtree from
 * inside the observer callback that notified them: that would free objects
 * LVGL is still walking. So they defer the rebuild onto an async path, which
 * only runs when LVGL's timer handler next runs.
 *
 * Two consequences:
 *
 *  1. Asserting straight after `lv_subject_set_int()` reads the OLD tree. The
 *     test passes or fails on timing, not on behaviour.
 *  2. Several changes made before a pump COALESCE into one rebuild. If a test
 *     wants to observe an intermediate state, it must pump between the
 *     changes - a single pump at the end will only ever show the final one.
 *
 * So: mutate, pump, assert. Never mutate then assert.
 * ---------------------------------------------------------------------------
 *
 * The pump advances LVGL's tick in small steps rather than one big jump,
 * because animations and timers with a period shorter than the total would
 * otherwise fire once instead of the number of times real time would produce.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef HELIX_TEST_PUMP_H
#define HELIX_TEST_PUMP_H

#include <stdint.h>

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Tick granularity of the pump, in milliseconds. */
#define HELIX_TEST_PUMP_STEP_MS 5u

/**
 * Advance LVGL by @p ms milliseconds of simulated time.
 *
 * Ticks forward in HELIX_TEST_PUMP_STEP_MS slices, running lv_timer_handler()
 * after each slice, so timers, animations and deferred rebuilds all get a
 * chance to run the same number of times they would in real time.
 *
 * A pump of 0 still runs one lv_timer_handler() pass, which is enough to flush
 * a pending async rebuild without moving the clock.
 *
 * @param ms  simulated milliseconds to advance. 30-50 is plenty for a
 *            deferred rebuild; use more only when waiting on an animation.
 */
static inline void helix_test_pump(uint32_t ms)
{
    uint32_t elapsed = 0;

    do {
        uint32_t step = ms - elapsed;
        if(step > HELIX_TEST_PUMP_STEP_MS) step = HELIX_TEST_PUMP_STEP_MS;

        lv_tick_inc(step);
        lv_timer_handler();

        elapsed += step;
    } while(elapsed < ms);
}

#ifdef __cplusplus
}
#endif

#endif /* HELIX_TEST_PUMP_H */
