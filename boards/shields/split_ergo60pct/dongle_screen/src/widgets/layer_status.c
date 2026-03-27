/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/display.h>
#include <zmk/display/widgets/layer_status.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/event_manager.h>
#include <zmk/endpoints.h>
#include <zmk/keymap.h>

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

#define LAYER_STATUS_DEBOUNCE_MS 80

struct layer_status_state
{
    uint8_t index;
    const char *label;
};

static struct layer_status_state pending_state;
static bool pending_state_valid = false;

static void set_layer_symbol(lv_obj_t *label, struct layer_status_state state)
{
    if (state.label == NULL)
    {
        char text[7] = {};

        sprintf(text, "%i", state.index);

        lv_label_set_text(label, text);
    }
    else
    {
        char text[13] = {};

        snprintf(text, sizeof(text), "%s", state.label);

        lv_label_set_text(label, text);
    }
}

static void apply_layer_status_state(struct layer_status_state state)
{
    struct zmk_widget_layer_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_layer_symbol(widget->obj, state); }
}

static void layer_status_deferred_update(struct k_work *work)
{
    ARG_UNUSED(work);

    if (!pending_state_valid) {
        return;
    }

    apply_layer_status_state(pending_state);
}

static K_WORK_DELAYABLE_DEFINE(layer_status_update_work, layer_status_deferred_update);

static void layer_status_update_cb(struct layer_status_state state)
{
    pending_state = state;
    pending_state_valid = true;
    k_work_reschedule(&layer_status_update_work, K_MSEC(LAYER_STATUS_DEBOUNCE_MS));
}

static struct layer_status_state layer_status_get_state(const zmk_event_t *eh)
{
    uint8_t index = zmk_keymap_highest_layer_active();
    return (struct layer_status_state){
        .index = index,
        .label = zmk_keymap_layer_name(index)};
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_layer_status, struct layer_status_state, layer_status_update_cb,
                            layer_status_get_state)

ZMK_SUBSCRIPTION(widget_layer_status, zmk_layer_state_changed);

int zmk_widget_layer_status_init(struct zmk_widget_layer_status *widget, lv_obj_t *parent)
{
    widget->obj = lv_label_create(parent);

    lv_obj_set_style_text_font(widget->obj, &lv_font_montserrat_40, 0);

    sys_slist_append(&widgets, &widget->node);

    widget_layer_status_init();
    return 0;
}

lv_obj_t *zmk_widget_layer_status_obj(struct zmk_widget_layer_status *widget)
{
    return widget->obj;
}