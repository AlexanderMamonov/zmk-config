/*
 * Copyright (c) 2026 Aleksandr
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_wake_cycle

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <drivers/behavior.h>
#include <dt-bindings/zmk/keys.h>
#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/position_state_changed.h>

#define WAKE_CYCLE_INTERVAL_MS 15000
#define WAKE_CYCLE_TAP_MS 30
#define WAKE_CYCLE_DURATION_MS (8LL * 60 * 60 * 1000)
#define WAKE_CYCLE_TRIGGER_POSITION 10

static const uint32_t wake_cycle_keys[] = {A, S, D, F};

struct behavior_wake_cycle_data {
    struct k_work_delayable work;
    bool active;
    bool key_down;
    bool suppress_start;
    uint8_t key_index;
    int64_t stop_at;
    int64_t suppress_start_timestamp;
};

static struct behavior_wake_cycle_data wake_cycle_data;

static void wake_cycle_stop(struct behavior_wake_cycle_data *data) {
    data->active = false;
    k_work_cancel_delayable(&data->work);
    if (data->key_down) {
        uint32_t key = wake_cycle_keys[data->key_index];
        raise_zmk_keycode_state_changed_from_encoded(key, false, k_uptime_get());
        data->key_down = false;
    }
}

static int wake_cycle_auto_stop_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *event = as_zmk_position_state_changed(eh);
    if (event && event->state) {
        if (wake_cycle_data.active) {
            if (event->position == WAKE_CYCLE_TRIGGER_POSITION) {
                wake_cycle_data.suppress_start = true;
                wake_cycle_data.suppress_start_timestamp = event->timestamp;
            }
            wake_cycle_stop(&wake_cycle_data);
        } else if (wake_cycle_data.suppress_start &&
                   event->timestamp != wake_cycle_data.suppress_start_timestamp) {
            wake_cycle_data.suppress_start = false;
        }
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(wake_cycle_auto_stop, wake_cycle_auto_stop_listener);
ZMK_SUBSCRIPTION(wake_cycle_auto_stop, zmk_position_state_changed);

static void wake_cycle_work_handler(struct k_work *work) {
    struct k_work_delayable *delayable = k_work_delayable_from_work(work);
    struct behavior_wake_cycle_data *data =
        CONTAINER_OF(delayable, struct behavior_wake_cycle_data, work);

    if (!data->active) {
        return;
    }

    int64_t now = k_uptime_get();
    uint32_t key = wake_cycle_keys[data->key_index];

    if (data->key_down) {
        raise_zmk_keycode_state_changed_from_encoded(key, false, now);
        data->key_down = false;
        data->key_index = (data->key_index + 1) % ARRAY_SIZE(wake_cycle_keys);

        if (now >= data->stop_at) {
            data->active = false;
            return;
        }

        k_work_schedule(&data->work, K_MSEC(WAKE_CYCLE_INTERVAL_MS));
        return;
    }

    if (now >= data->stop_at) {
        data->active = false;
        return;
    }

    key = wake_cycle_keys[data->key_index];
    raise_zmk_keycode_state_changed_from_encoded(key, true, now);
    data->key_down = true;
    k_work_schedule(&data->work, K_MSEC(WAKE_CYCLE_TAP_MS));
}

static int on_wake_cycle_binding_pressed(struct zmk_behavior_binding *binding,
                                         struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct behavior_wake_cycle_data *data = dev->data;

    if (data->active) {
        wake_cycle_stop(data);
        return ZMK_BEHAVIOR_OPAQUE;
    }

    if (data->suppress_start && event.position == WAKE_CYCLE_TRIGGER_POSITION &&
        event.timestamp == data->suppress_start_timestamp) {
        data->suppress_start = false;
        return ZMK_BEHAVIOR_OPAQUE;
    }

    data->active = true;
    data->key_down = false;
    data->key_index = 0;
    data->stop_at = k_uptime_get() + WAKE_CYCLE_DURATION_MS;
    k_work_schedule(&data->work, K_NO_WAIT);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_wake_cycle_binding_released(struct zmk_behavior_binding *binding,
                                          struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api wake_cycle_driver_api = {
    .binding_pressed = on_wake_cycle_binding_pressed,
    .binding_released = on_wake_cycle_binding_released,
};

static int wake_cycle_init(const struct device *dev) {
    struct behavior_wake_cycle_data *data = dev->data;
    k_work_init_delayable(&data->work, wake_cycle_work_handler);
    return 0;
}

BEHAVIOR_DT_INST_DEFINE(0, wake_cycle_init, NULL, &wake_cycle_data, NULL,
                        POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
                        &wake_cycle_driver_api);
