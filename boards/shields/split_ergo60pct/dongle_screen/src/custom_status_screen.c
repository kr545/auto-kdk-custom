/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "custom_status_screen.h"

#if CONFIG_DONGLE_SCREEN_OUTPUT_ACTIVE
#include "widgets/output_status.h"
static struct zmk_widget_output_status output_status_widget;
#endif

#if CONFIG_DONGLE_SCREEN_LAYER_ACTIVE
#include "widgets/layer_status.h"
static struct zmk_widget_layer_status layer_status_widget;
#endif

#if CONFIG_DONGLE_SCREEN_BATTERY_ACTIVE
#include "widgets/battery_status.h"
static struct zmk_widget_dongle_battery_status dongle_battery_status_widget;
#endif

#if CONFIG_DONGLE_SCREEN_WPM_ACTIVE
#include "widgets/wpm_status.h"
static struct zmk_widget_wpm_status wpm_status_widget;
#endif

#if CONFIG_DONGLE_SCREEN_MODIFIER_ACTIVE
#include "widgets/mod_status.h"
static struct zmk_widget_mod_status mod_widget;
#endif

#include <zmk/events/keycode_state_changed.h>
#include <zmk/hid_indicators.h>
#include <zmk/events/hid_indicators_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zephyr/logging/log.h>
#include <fonts.h>

struct zmk_mouse_gesture_state_changed {
    bool is_active;
};
ZMK_EVENT_DECLARE(zmk_mouse_gesture_state_changed);

extern const lv_font_t lv_font_montserrat_12;
#include <zephyr/sys/reboot.h>
#include <zephyr/input/input.h>
#include <zephyr/device.h>
#include <string.h>
#include <stdio.h>

// nRF52840 + Adafruit UF2 bootloader: write 0x57 to GPREGRET before cold reboot
#define BOOTLOADER_DFU_START      0x57U
#define NRF_POWER_GPREGRET_ADDR   ((volatile uint32_t *)0x4000051CUL)

// Split threshold for touch zones on the rotated screen.
#define TOUCH_ZONE_AXIS           last_touch_x
#define TOUCH_ZONE_MID            120

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

// =====================================================================
// Touch Button Grid – 14 buttons on Screen 3 (Key History)
// =====================================================================
//
// LVGL座標系 (270°回転後): 280×240
//   LVGL_X = 279 - raw_touch_Y
//   LVGL_Y = raw_touch_X
//
// 画面辺のボタンゾーン (LVGL座標で定義)
#define BTN_EDGE_TOP       40   // Top edge: LVGL Y = 0 .. BTN_EDGE_TOP-1
#define BTN_EDGE_BOTTOM   200   // Bottom edge: LVGL Y = BTN_EDGE_BOTTOM .. 239
#define BTN_EDGE_LEFT      40   // Left edge: LVGL X = 0 .. BTN_EDGE_LEFT-1
#define BTN_EDGE_RIGHT    240   // Right edge: LVGL X = BTN_EDGE_RIGHT .. 279

#include <zmk/ble.h>

#define BTN_COUNT 16

enum touch_btn_id {
    BTN_TL = 0, BTN_TR, BTN_BL, BTN_BR,  // Corners
    BTN_T1, BTN_T2, BTN_T3,              // Top edge
    BTN_B1, BTN_B2, BTN_B3,              // Bottom edge
    BTN_L1, BTN_L2, BTN_L3,              // Left edge
    BTN_R1, BTN_R2, BTN_R3,              // Right edge
};

enum btn_action_type {
    ACT_SYS = 0,
    ACT_BLE,
    ACT_HID_KEY,
    ACT_HID_CONS
};

struct touch_btn_zone {
    int16_t x_min, x_max;
    int16_t y_min, y_max;
    enum btn_action_type type;
    uint16_t usage_page;
    uint32_t val;
    uint8_t mods;
    const char *label;
};

static const struct touch_btn_zone btn_zones[BTN_COUNT] = {
    // Corners (60x60)
    [BTN_TL] = {  0,  59,   0,  59, ACT_SYS, 0, 1, 0, "Boot"},
    [BTN_TR] = {220, 279,   0,  59, ACT_SYS, 0, 2, 0, "Reboot"},
    [BTN_BL] = {  0,  59, 180, 239, ACT_HID_CONS, 0x0C, 0x032, 0, "Sleep"},
    [BTN_BR] = {220, 279, 180, 239, ACT_HID_KEY, 0x07, 0x0F, 0x08, "Lock"}, // LGUI + L
    // Top edge (3): BT controls, overlapping horizontally by 5px
    [BTN_T1] = { 55, 114,   0,  59, ACT_BLE, 0, 0, 0, "BT 1"},
    [BTN_T2] = {110, 169,   0,  59, ACT_BLE, 0, 1, 0, "BT 2"},
    [BTN_T3] = {165, 224,   0,  59, ACT_BLE, 0, 99, 0, "BT Clr"},
    // Bottom edge (3): Clipboard
    [BTN_B1] = { 55, 114, 180, 239, ACT_HID_KEY, 0x07, 0x7B, 0, "Cut"},
    [BTN_B2] = {110, 169, 180, 239, ACT_HID_KEY, 0x07, 0x7C, 0, "Copy"},
    [BTN_B3] = {165, 224, 180, 239, ACT_HID_KEY, 0x07, 0x7D, 0, "Paste"},
    // Left edge (3): Volume, overlapping vertically by 15px
    [BTN_L1] = {  0,  59,  45, 104, ACT_HID_CONS, 0x0C, 0xE9, 0, "Vol+"},
    [BTN_L2] = {  0,  59,  90, 149, ACT_HID_CONS, 0x0C, 0xE2, 0, "Mute"},
    [BTN_L3] = {  0,  59, 135, 194, ACT_HID_CONS, 0x0C, 0xEA, 0, "Vol-"},
    // Right edge (3): Media
    [BTN_R1] = {220, 279,  45, 104, ACT_HID_CONS, 0x0C, 0xB6, 0, "Prev"},
    [BTN_R2] = {220, 279,  90, 149, ACT_HID_CONS, 0x0C, 0xCD, 0, "Play"},
    [BTN_R3] = {220, 279, 135, 194, ACT_HID_CONS, 0x0C, 0xB5, 0, "Next"},
};

// LVGL button label objects
static lv_obj_t *btn_labels[BTN_COUNT];
static lv_obj_t *btn_bgs[BTN_COUNT];

lv_style_t global_style;

// 画面インデックス: 0=メイン, 1=System Control, 2=キーログ
#define SCREEN_COUNT 3
#define SCREEN_MAIN         0
#define SCREEN_SYSTEM       1
#define SCREEN_KEYLOG       2

static lv_obj_t *screen_main = NULL;
static lv_obj_t *screen_text = NULL;
static lv_obj_t *screen_keylog = NULL;
static int current_screen = SCREEN_MAIN;

// ---- Bongo Cat Globals ----
LV_IMG_DECLARE(none);
LV_IMG_DECLARE(left);
LV_IMG_DECLARE(right);
LV_IMG_DECLARE(both);
static lv_obj_t *bongo_img = NULL;
static int current_bongo_state = 0;
static bool bongo_is_right = false;

// Status Indicators
static lv_obj_t *caps_bg = NULL;
static lv_obj_t *caps_label = NULL;
static lv_obj_t *gesture_bg = NULL;
static lv_obj_t *gesture_label = NULL;

#define COLOR_OFF lv_color_hex(0x000000)
#define COLOR_TEXT_OFF lv_color_hex(0xFFFFFF)
#define COLOR_CAPS_ON lv_color_hex(0xCC2200)
#define COLOR_GESTURE_ON lv_color_hex(0x0077CC)
#define COLOR_TEXT_ON lv_color_white()

// キーログ表示ウィジェット (3行)
#define KEYLOG_COUNT 3
static lv_obj_t *keylog_labels[KEYLOG_COUNT];

// キー履歴バッファ (index 0 = 最新)
#define KEY_NAME_MAX 16
static char key_history[KEYLOG_COUNT][KEY_NAME_MAX];

// Track latest touch coordinates
static int32_t last_touch_x = 0;
static int32_t last_touch_y = 0;

// Pointer to the CST816S device
#define TOUCH_SENSOR_NODE  DT_NODELABEL(touch_sensor)
static const struct device *touch_dev = NULL;

// Define default keycode to toggle screens if not defined in Kconfig
#ifndef CONFIG_DONGLE_SCREEN_VIEW_TOGGLE_KEYCODE
#define CONFIG_DONGLE_SCREEN_VIEW_TOGGLE_KEYCODE 112 // F21
#endif

// キーコード → 文字列変換
static const char *keycode_to_str(uint32_t keycode) {
    // A-Z (HID 0x04-0x1D)
    if (keycode >= 0x04 && keycode <= 0x1D) {
        static char alpha[2] = {0, 0};
        alpha[0] = 'A' + (keycode - 0x04);
        return alpha;
    }
    // 1-9, 0 (HID 0x1E-0x27)
    if (keycode >= 0x1E && keycode <= 0x26) {
        static char num[2] = {0, 0};
        num[0] = '1' + (keycode - 0x1E);
        return num;
    }
    if (keycode == 0x27) return "0";

    // 特殊キー
    switch (keycode) {
        case 0x28: return "Enter";
        case 0x29: return "Esc";
        case 0x2A: return "Bksp";
        case 0x2B: return "Tab";
        case 0x2C: return "Space";
        case 0x2D: return "-";
        case 0x2E: return "=";
        case 0x2F: return "[";
        case 0x30: return "]";
        case 0x31: return "\\";
        case 0x33: return ";";
        case 0x34: return "'";
        case 0x35: return "`";
        case 0x36: return ",";
        case 0x37: return ".";
        case 0x38: return "/";
        case 0x39: return "Caps";
        case 0x3A: return "F1";
        case 0x3B: return "F2";
        case 0x3C: return "F3";
        case 0x3D: return "F4";
        case 0x3E: return "F5";
        case 0x3F: return "F6";
        case 0x40: return "F7";
        case 0x41: return "F8";
        case 0x42: return "F9";
        case 0x43: return "F10";
        case 0x44: return "F11";
        case 0x45: return "F12";
        case 0x4A: return "Home";
        case 0x4B: return "PgUp";
        case 0x4C: return "Del";
        case 0x4D: return "End";
        case 0x4E: return "PgDn";
        case 0x4F: return "Right";
        case 0x50: return "Left";
        case 0x51: return "Down";
        case 0x52: return "Up";
        case 0xE0: return "LCtrl";
        case 0xE1: return "LShft";
        case 0xE2: return "LAlt";
        case 0xE3: return "LGUI";
        case 0xE4: return "RCtrl";
        case 0xE5: return "RShft";
        case 0xE6: return "RAlt";
        case 0xE7: return "RGUI";
        default: {
            static char fallback[KEY_NAME_MAX];
            snprintf(fallback, sizeof(fallback), "0x%02X", (unsigned int)keycode);
            return fallback;
        }
    }
}

// キー履歴を更新してラベルを再描画
static void keylog_push(uint32_t keycode) {
    // 古いエントリをひとつずつ後ろにずらす
    for (int i = KEYLOG_COUNT - 1; i > 0; i--) {
        strncpy(key_history[i], key_history[i - 1], KEY_NAME_MAX - 1);
        key_history[i][KEY_NAME_MAX - 1] = '\0';
    }
    // 最新キーをインデックス0へ
    strncpy(key_history[0], keycode_to_str(keycode), KEY_NAME_MAX - 1);
    key_history[0][KEY_NAME_MAX - 1] = '\0';

    // ラベルを更新
    if (screen_keylog != NULL) {
        for (int i = 0; i < KEYLOG_COUNT; i++) {
            if (keylog_labels[i] != NULL) {
                lv_label_set_text(keylog_labels[i], key_history[i]);
            }
        }
    }
}

// 画面の表示/非表示を切り替えるヘルパー
static void switch_to_screen(int idx) {
    if (!screen_main || !screen_text || !screen_keylog) return;

    // 前の描画が残るのを防ぐため、親スクリーン全体を再描画対象にする
    lv_obj_t *parent = lv_obj_get_parent(screen_main);
    if (parent) {
        lv_obj_invalidate(parent);
    }

    lv_obj_add_flag(screen_main,   LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(screen_text,   LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(screen_keylog, LV_OBJ_FLAG_HIDDEN);

    switch (idx) {
        case SCREEN_MAIN:
            lv_obj_clear_flag(screen_main, LV_OBJ_FLAG_HIDDEN);
            break;
        case SCREEN_SYSTEM:
            lv_obj_clear_flag(screen_text, LV_OBJ_FLAG_HIDDEN);
            break;
        case SCREEN_KEYLOG:
            lv_obj_clear_flag(screen_keylog, LV_OBJ_FLAG_HIDDEN);
            break;
    }
    current_screen = idx;
}

// Event listener for screen toggling + key logging
static int view_toggle_listener(const zmk_event_t *eh) {
    struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    if (ev != NULL && ev->state) { // Only trigger on key press
        uint32_t toggle_keycode = (CONFIG_DONGLE_SCREEN_VIEW_TOGGLE_KEYCODE & 0xFFFF);

        uint16_t id = (ev->keycode & 0xFFFF);
        if (id == toggle_keycode) {
            // 3画面サイクル
            if (screen_main != NULL && screen_text != NULL && screen_keylog != NULL) {
                int next = (current_screen + 1) % SCREEN_COUNT;
                switch_to_screen(next);
            }
        } else {
            // トグルキー以外はキーログに記録
            keylog_push(ev->keycode);
        }
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(view_toggle_listener, view_toggle_listener);
ZMK_SUBSCRIPTION(view_toggle_listener, zmk_keycode_state_changed);

// Event listener for CAPS LOCK state
static int hid_indicators_listener(const zmk_event_t *eh) {
    const struct zmk_hid_indicators_changed *ev = as_zmk_hid_indicators_changed(eh);
    if (ev != NULL && caps_bg != NULL && caps_label != NULL) {
        bool caps_on = (ev->indicators & BIT(1)) != 0; // HID_USAGE_LED_CAPS_LOCK is bit 1
        lv_obj_set_style_text_color(caps_label, caps_on ? COLOR_CAPS_ON : COLOR_TEXT_OFF, LV_PART_MAIN);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(hid_indicators_listener, hid_indicators_listener);
ZMK_SUBSCRIPTION(hid_indicators_listener, zmk_hid_indicators_changed);

// Event listener for MOUSE GESTURE state
static int mouse_gesture_listener(const zmk_event_t *eh) {
    const struct zmk_mouse_gesture_state_changed *ev = as_zmk_mouse_gesture_state_changed(eh);
    if (ev != NULL && gesture_bg != NULL && gesture_label != NULL) {
        bool gst_on = ev->is_active;
        lv_obj_set_style_text_color(gesture_label, gst_on ? COLOR_GESTURE_ON : COLOR_TEXT_OFF, LV_PART_MAIN);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(mouse_gesture_listener, mouse_gesture_listener);
ZMK_SUBSCRIPTION(mouse_gesture_listener, zmk_mouse_gesture_state_changed);

// Event listener for BONGO CAT animation
static int bongo_cat_listener(const zmk_event_t *eh) {
    if (bongo_img == NULL) return ZMK_EV_EVENT_BUBBLE;
    struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (!ev) return ZMK_EV_EVENT_BUBBLE;

    uint8_t tmp = 1 << bongo_is_right;
    if (ev->state) {
        if (current_bongo_state & (1 | 2)) {
            tmp = 1 | 2; // both hands
        }
    } else {
        if (current_bongo_state ^ (1 | 2)) {
            tmp = 0; // release
            bongo_is_right = !bongo_is_right; // switch hand
        }
    }

    if (current_bongo_state == tmp) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    current_bongo_state = tmp;
    const void *images[] = { &none, &left, &right, &both };
    lv_img_set_src(bongo_img, images[current_bongo_state]);

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(widget_bongo_cat, bongo_cat_listener);
ZMK_SUBSCRIPTION(widget_bongo_cat, zmk_position_state_changed);

// Raise a ZMK keycode_state_changed event (press or release)
static void raise_keycode_event(uint16_t usage_page, uint32_t keycode, uint8_t modifiers, bool pressed) {
    struct zmk_keycode_state_changed ev = {
        .usage_page = usage_page,
        .keycode = keycode,
        .implicit_modifiers = modifiers,
        .explicit_modifiers = modifiers,
        .state = pressed,
        .timestamp = k_uptime_get(),
    };
    raise_zmk_keycode_state_changed(ev);
}

// Convert raw CST816S coordinates → LVGL coordinates (270°/90° rotation)
static void raw_to_lvgl(int32_t raw_x, int32_t raw_y,
                        int32_t *lvgl_x, int32_t *lvgl_y) {
    // 数学的に正確な270度（反時計回り）回転を適用する
    // デバイストリーのINPUT_TRANSFORM_XY_SWAPはこのレイアウトに影響しません。
    *lvgl_x = 279 - raw_y;
    *lvgl_y = raw_x;
}

// Determine which button (if any) was hit at LVGL coordinates
static int find_touched_button(int32_t lx, int32_t ly) {
    for (int i = 0; i < BTN_COUNT; i++) {
        if (lx >= btn_zones[i].x_min && lx <= btn_zones[i].x_max &&
            ly >= btn_zones[i].y_min && ly <= btn_zones[i].y_max) {
            return i;
        }
    }
    return -1;
}

// Visual feedback: briefly highlight a button
static void btn_highlight(int idx, bool on) {
    if (idx < 0 || idx >= BTN_COUNT) return;
    if (btn_bgs[idx] == NULL) return;
    if (on) {
        lv_obj_set_style_bg_color(btn_bgs[idx], lv_color_hex(0x0077CC), LV_PART_MAIN);
        lv_obj_set_style_text_color(btn_labels[idx], lv_color_white(), LV_PART_MAIN);
    } else {
        lv_obj_set_style_bg_color(btn_bgs[idx], lv_color_hex(0x333333), LV_PART_MAIN);
        lv_obj_set_style_text_color(btn_labels[idx], lv_color_hex(0xCCCCCC), LV_PART_MAIN);
    }
}

// Message Queue to properly sequence fast touch events for the UI/ZMK workqueue
struct touch_event_msg {
    int16_t idx;     // Button index, or -1 for screen swap
    bool pressed;    // true = pressed, false = released
};

K_MSGQ_DEFINE(touch_msgq, sizeof(struct touch_event_msg), 16, 4);

// Track if a physical touch is currently active to debounce ghost/multi touches (Input Thread exclusively)
static bool is_touching = false;
// This tracks which button is currently physically held down (Input Thread exclusively)
static int last_pressed_idx = -1;

static void execute_btn_action(int idx, bool pressed) {
    const struct touch_btn_zone *z = &btn_zones[idx];
    switch (z->type) {
        case ACT_SYS:
            if (pressed) {
                if (z->val == 1) {
                    *NRF_POWER_GPREGRET_ADDR = BOOTLOADER_DFU_START;
                    sys_reboot(SYS_REBOOT_COLD);
                } else if (z->val == 2) {
                    sys_reboot(SYS_REBOOT_COLD);
                }
            }
            break;
        case ACT_BLE:
            if (pressed) {
                if (z->val == 99) {
                    zmk_ble_clear_bonds();
                } else {
                    zmk_ble_prof_select(z->val);
                }
            }
            break;
        case ACT_HID_KEY:
        case ACT_HID_CONS:
            raise_keycode_event(z->usage_page, z->val, z->mods, pressed);
            break;
    }
}

static void touch_process_handler(struct k_work *work) {
    struct touch_event_msg msg;
    // Process all pending touch events sequentially
    while (k_msgq_get(&touch_msgq, &msg, K_NO_WAIT) == 0) {
        if (msg.idx >= 0 && msg.idx < BTN_COUNT) {
            btn_highlight(msg.idx, msg.pressed);
            execute_btn_action(msg.idx, msg.pressed);
            LOG_INF("Button %s %s", btn_zones[msg.idx].label, msg.pressed ? "pressed" : "released");
        } else if (msg.idx == -1 && msg.pressed) {
            int next = (current_screen + 1) % SCREEN_COUNT;
            switch_to_screen(next);
            LOG_INF("Screen tapped on empty area, switching to screen %d", next);
        }
    }
}

K_WORK_DEFINE(touch_process_work, touch_process_handler);

// Zephyr Input Listener – filtered to CST816S touch sensor only.
static void touch_input_callback(struct input_event *evt) {
    if (touch_dev != NULL && evt->dev != touch_dev) {
        return;
    }

    if (evt->type == INPUT_EV_ABS) {
        if (evt->code == INPUT_ABS_X) {
            last_touch_x = evt->value;
        } else if (evt->code == INPUT_ABS_Y) {
            last_touch_y = evt->value;
        }
    } else if (evt->type == INPUT_EV_KEY && evt->code == INPUT_BTN_TOUCH) {
        if (current_screen == SCREEN_SYSTEM) {
            int32_t lx, ly;
            raw_to_lvgl(last_touch_x, last_touch_y, &lx, &ly);
            LOG_INF("Touch on system page: raw(%d,%d) lvgl(%d,%d)",
                    last_touch_x, last_touch_y, lx, ly);

            if (evt->value == 1) {
                // Touch press
                if (is_touching) {
                    return; // Ignore supplementary ghost/multi touches
                }
                is_touching = true;

                int idx = find_touched_button(lx, ly);
                if (idx >= 0) {
                    last_pressed_idx = idx;
                    struct touch_event_msg msg = { .idx = idx, .pressed = true };
                    k_msgq_put(&touch_msgq, &msg, K_NO_WAIT);
                } else {
                    last_pressed_idx = -1;
                    struct touch_event_msg msg = { .idx = -1, .pressed = true };
                    k_msgq_put(&touch_msgq, &msg, K_NO_WAIT);
                }
                k_work_submit(&touch_process_work);
            } else {
                // Touch release
                is_touching = false;
                if (last_pressed_idx >= 0) {
                    struct touch_event_msg msg = { .idx = last_pressed_idx, .pressed = false };
                    k_msgq_put(&touch_msgq, &msg, K_NO_WAIT);
                    k_work_submit(&touch_process_work);
                    last_pressed_idx = -1;
                }
            }
        } else if (evt->value == 1) {
            // Touch press on SCREEN_MAIN or SCREEN_KEYLOG cycles the screen
            if (is_touching) {
                return; // Ignore supplementary touches
            }
            is_touching = true;
            struct touch_event_msg msg = { .idx = -1, .pressed = true };
            k_msgq_put(&touch_msgq, &msg, K_NO_WAIT);
            k_work_submit(&touch_process_work);
        } else {
            // Touch release on SCREEN_MAIN or SCREEN_KEYLOG
            is_touching = false;
        }
    }
}

INPUT_CALLBACK_DEFINE(NULL, touch_input_callback);

lv_obj_t *zmk_display_status_screen()
{
    lv_obj_t *screen;

    // Resolve the touch sensor device pointer for event filtering
    if (DT_NODE_HAS_STATUS(TOUCH_SENSOR_NODE, okay)) {
        touch_dev = DEVICE_DT_GET(TOUCH_SENSOR_NODE);
    }

    screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, 255, LV_PART_MAIN);

    // ---- Screen 1: Main ----
    screen_main = lv_obj_create(screen);
    lv_obj_set_size(screen_main, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(screen_main, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(screen_main, 0, LV_PART_MAIN);

    // ---- Status Indicators (Caps Lock, Gesture) ----
    caps_bg = lv_obj_create(screen_main);
    lv_obj_set_size(caps_bg, 85, 30);
    lv_obj_align(caps_bg, LV_ALIGN_TOP_LEFT, 5, 5);
    lv_obj_set_style_bg_color(caps_bg, COLOR_OFF, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(caps_bg, 255, LV_PART_MAIN);
    lv_obj_set_style_border_width(caps_bg, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(caps_bg, 4, LV_PART_MAIN);

    caps_label = lv_label_create(caps_bg);
    lv_label_set_text(caps_label, "CAPS");
    lv_obj_set_style_text_color(caps_label, COLOR_TEXT_OFF, LV_PART_MAIN);
    // 左揃え（少し左からマージンをとる）
    lv_obj_align(caps_label, LV_ALIGN_LEFT_MID, 5, 0);

    gesture_bg = lv_obj_create(screen_main);
    lv_obj_set_size(gesture_bg, 125, 30);
    lv_obj_align(gesture_bg, LV_ALIGN_TOP_LEFT, 5, 40);
    lv_obj_set_style_bg_color(gesture_bg, COLOR_OFF, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(gesture_bg, 255, LV_PART_MAIN);
    lv_obj_set_style_border_width(gesture_bg, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(gesture_bg, 4, LV_PART_MAIN);

    gesture_label = lv_label_create(gesture_bg);
    lv_label_set_text(gesture_label, "GESTURE");
    lv_obj_set_style_text_color(gesture_label, COLOR_TEXT_OFF, LV_PART_MAIN);
    // 左揃え
    lv_obj_align(gesture_label, LV_ALIGN_LEFT_MID, 5, 0);

    // ---- Screen 2: System Control ----
    screen_text = lv_obj_create(screen);
    lv_obj_set_size(screen_text, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(screen_text, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(screen_text, 0, LV_PART_MAIN);
    lv_obj_add_flag(screen_text, LV_OBJ_FLAG_HIDDEN);

    // System Control: Title (Icons)
    lv_obj_t *text_label = lv_label_create(screen_text);
    lv_label_set_text(text_label, "         "); // Keyboard, Bluetooth, Apple, Windows
    lv_obj_set_style_text_font(text_label, &NerdFonts_Regular_20, 0);
    lv_obj_set_style_text_align(text_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(text_label, LV_ALIGN_TOP_MID, 0, 12);

    // System Control: Bongo Cat Image
    bongo_img = lv_img_create(screen_text);
    lv_img_set_src(bongo_img, &none);
    // indexed 1-bit 画像での zoom はサポートされていないため等倍で表示します
    lv_obj_align(bongo_img, LV_ALIGN_CENTER, 0, 10);

    // ---- Screen 3: Key Log ----
    screen_keylog = lv_obj_create(screen);
    lv_obj_set_size(screen_keylog, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(screen_keylog, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(screen_keylog, 0, LV_PART_MAIN);
    lv_obj_add_flag(screen_keylog, LV_OBJ_FLAG_HIDDEN);

    // Key Log: Title
    lv_obj_t *keylog_title = lv_label_create(screen_keylog);
    lv_label_set_text(keylog_title, "Key History");
    lv_obj_set_style_text_align(keylog_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(keylog_title, LV_ALIGN_TOP_MID, 0, 12);

    // Key Log: 3つのキーラベル（大きめに表示）
    // 初期化：履歴バッファをクリア
    for (int i = 0; i < KEYLOG_COUNT; i++) {
        key_history[i][0] = '\0';
    }

    static const lv_coord_t key_label_x[KEYLOG_COUNT] = {-80, 0, 80};
    for (int i = 0; i < KEYLOG_COUNT; i++) {
        keylog_labels[i] = lv_label_create(screen_keylog);
        lv_label_set_text(keylog_labels[i], "---");
        lv_obj_set_style_text_align(keylog_labels[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(keylog_labels[i], LV_ALIGN_CENTER, key_label_x[i], 10);
    }

    // ---- Touch Buttons (16 buttons bounding the edges) ----
    for (int i = 0; i < BTN_COUNT; i++) {
        const struct touch_btn_zone *z = &btn_zones[i];
        int w = z->x_max - z->x_min + 1;
        int h = z->y_max - z->y_min + 1;
        // Centre of zone in LVGL coordinates
        int cx = z->x_min + w / 2;
        int cy = z->y_min + h / 2;
        // Offset from screen centre (140, 120)
        int off_x = cx - 140;
        int off_y = cy - 120;

        // Background rectangle on System Screen instead of KeyLog
        btn_bgs[i] = lv_obj_create(screen_text);
        lv_obj_set_size(btn_bgs[i], w - 2, h - 2);
        lv_obj_align(btn_bgs[i], LV_ALIGN_CENTER, off_x, off_y);
        lv_obj_set_style_bg_color(btn_bgs[i], lv_color_hex(0x333333), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(btn_bgs[i], 220, LV_PART_MAIN);
        lv_obj_set_style_border_color(btn_bgs[i], lv_color_hex(0x666666), LV_PART_MAIN);
        lv_obj_set_style_border_width(btn_bgs[i], 1, LV_PART_MAIN);
        // Using radius 20 makes 40x40 objects circular, giving a nice edge button appearance
        lv_obj_set_style_radius(btn_bgs[i], 20, LV_PART_MAIN);
        lv_obj_clear_flag(btn_bgs[i], LV_OBJ_FLAG_SCROLLABLE);

        // Label
        btn_labels[i] = lv_label_create(btn_bgs[i]);
        lv_label_set_text(btn_labels[i], z->label);
        lv_obj_set_style_text_color(btn_labels[i], lv_color_hex(0xCCCCCC), LV_PART_MAIN);
        lv_obj_set_style_text_font(btn_labels[i], &lv_font_montserrat_12, 0);
        lv_obj_center(btn_labels[i]);
    }

    // ---- グローバルスタイル ----
    lv_style_init(&global_style);
    lv_style_set_text_color(&global_style, lv_color_white());
    lv_style_set_text_letter_space(&global_style, 1);
    lv_style_set_text_line_space(&global_style, 1);
    lv_obj_add_style(screen, &global_style, LV_PART_MAIN);

#if CONFIG_DONGLE_SCREEN_OUTPUT_ACTIVE
    zmk_widget_output_status_init(&output_status_widget, screen_main);
    lv_obj_align(zmk_widget_output_status_obj(&output_status_widget), LV_ALIGN_TOP_MID, 0, 10);
#endif

#if CONFIG_DONGLE_SCREEN_BATTERY_ACTIVE
    zmk_widget_dongle_battery_status_init(&dongle_battery_status_widget, screen_main);
    lv_obj_align(zmk_widget_dongle_battery_status_obj(&dongle_battery_status_widget), LV_ALIGN_BOTTOM_MID, 0, 0);
#endif

#if CONFIG_DONGLE_SCREEN_WPM_ACTIVE
    zmk_widget_wpm_status_init(&wpm_status_widget, screen_main);
    lv_obj_align(zmk_widget_wpm_status_obj(&wpm_status_widget), LV_ALIGN_TOP_LEFT, 20, 20);
#endif

#if CONFIG_DONGLE_SCREEN_LAYER_ACTIVE
    zmk_widget_layer_status_init(&layer_status_widget, screen_main);
    lv_obj_align(zmk_widget_layer_status_obj(&layer_status_widget), LV_ALIGN_CENTER, 0, 0);
#endif

#if CONFIG_DONGLE_SCREEN_MODIFIER_ACTIVE
    zmk_widget_mod_status_init(&mod_widget, screen_main);
    lv_obj_align(zmk_widget_mod_status_obj(&mod_widget), LV_ALIGN_CENTER, 0, 35);
#endif

    return screen;
}