/*
 * Chord hold: hold a key down around the whole of the next chord, e.g. space +
 * dots for braille screen reader commands. The key is pressed just before the
 * chord's first key (so the host doesn't auto-repeat it while waiting) and
 * released after the chord's last key.
 *
 * Triggering the behavior again before the next chord sends a plain tap.
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_chord_hold

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>

#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static struct {
    bool active;
    uint32_t encoded;
    // Whether the key has been sent to the host yet.
    bool pressed;
    // Keycodes of the current chord that are still down.
    int held;
    // Whether the next chord has started.
    bool chord_started;
} state;

static bool is_own_keycode(const struct zmk_keycode_state_changed *ev) {
    return ZMK_HID_USAGE_ID(state.encoded) == ev->keycode &&
           (ZMK_HID_USAGE_PAGE(state.encoded) ?: HID_USAGE_KEY) == ev->usage_page;
}

static void press_held(int64_t timestamp) {
    state.pressed = true;
    raise_zmk_keycode_state_changed_from_encoded(state.encoded, true, timestamp);
}

static void release_held(int64_t timestamp) {
    state.active = false;
    raise_zmk_keycode_state_changed_from_encoded(state.encoded, false, timestamp);
}

static int on_chord_hold_binding_pressed(struct zmk_behavior_binding *binding,
                                         struct zmk_behavior_binding_event event) {
    if (state.active) {
        LOG_DBG("chord hold triggered again, tapping 0x%02X", state.encoded);
        if (!state.pressed) {
            press_held(event.timestamp);
        }
        release_held(event.timestamp);
        return ZMK_BEHAVIOR_OPAQUE;
    }

    state.active = true;
    state.encoded = binding->param1;
    state.pressed = false;
    state.held = 0;
    state.chord_started = false;
    LOG_DBG("chord hold waiting for next chord to press 0x%02X", state.encoded);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_chord_hold_binding_released(struct zmk_behavior_binding *binding,
                                          struct zmk_behavior_binding_event event) {
    // The key stays held until the next chord ends, not when the trigger is released.
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_chord_hold_driver_api = {
    .binding_pressed = on_chord_hold_binding_pressed,
    .binding_released = on_chord_hold_binding_released,
};

static int chord_hold_keycode_state_changed_listener(const zmk_event_t *eh);

ZMK_LISTENER(behavior_chord_hold, chord_hold_keycode_state_changed_listener);
ZMK_SUBSCRIPTION(behavior_chord_hold, zmk_keycode_state_changed);

static int chord_hold_keycode_state_changed_listener(const zmk_event_t *eh) {
    struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    if (ev == NULL || !state.active || is_own_keycode(ev)) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (ev->state) {
        if (!state.chord_started) {
            // Raised synchronously, so it reaches the host before this key.
            press_held(ev->timestamp);
        }
        state.held++;
        state.chord_started = true;
        return ZMK_EV_EVENT_BUBBLE;
    }

    // Ignore releases of keys that were already down before the chord started.
    if (state.held == 0) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    if (--state.held > 0 || !state.chord_started) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    // Last key of the chord released: let that release reach the host first, then release ours.
    const int64_t timestamp = ev->timestamp;
    struct zmk_keycode_state_changed_event dupe_ev = copy_raised_zmk_keycode_state_changed(ev);
    ZMK_EVENT_RAISE_AFTER(dupe_ev, behavior_chord_hold);
    release_held(timestamp);
    return ZMK_EV_EVENT_CAPTURED;
}

static int behavior_chord_hold_init(const struct device *dev) { return 0; }

#define CH_INST(n)                                                                                 \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_chord_hold_init, NULL, NULL, NULL, POST_KERNEL,           \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_chord_hold_driver_api);

DT_INST_FOREACH_STATUS_OKAY(CH_INST)

#endif
