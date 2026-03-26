#include <zephyr/kernel.h>
#include <zmk/display/status_screen.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/event_manager.h>
#include <zmk/keymap.h>
#include <zmk/display.h>
#include <lvgl.h>

/* 画面オブジェクトの保持 */
static lv_obj_t *screen_main = NULL;
static lv_obj_t *screen_info = NULL;

/**
 * 表示内容をレイヤー状態に合わせて更新する関数
 */
static void update_screen_view(uint8_t layer_index) {
    if (screen_main == NULL || screen_info == NULL) {
        return;
    }

    // レイヤー0ならメイン画面、それ以外(タップ後)なら詳細画面を表示
    if (layer_index > 0) {
        lv_obj_add_flag(screen_main, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(screen_info, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(screen_main, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(screen_info, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * レイヤー変更イベントを購読するハンドラ
 */
static int layer_status_listener(const zmk_event_t *eh) {
    struct zmk_layer_state_changed *ev = as_zmk_layer_state_changed(eh);
    if (ev != NULL) {
        uint8_t highest_layer = zmk_keymap_highest_layer_active();
        update_screen_view(highest_layer);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

// リスナーとイベントの紐付け
ZMK_LISTENER(layer_status_listener, layer_status_listener);
ZMK_SUBSCRIPTION(layer_status_listener, zmk_layer_state_changed);

/**
 * ZMKがディスプレイ初期化時に呼び出すメイン関数
 */
lv_obj_t *zmk_display_status_screen() {
    // アクティブなスクリーン（親要素）を取得
    lv_obj_t *screen = lv_scr_act();

    // --- 1. 通常画面 (Main Screen) の構築 ---
    screen_main = lv_obj_create(screen);
    lv_obj_set_size(screen_main, lv_pct(100), lv_pct(100)); // 画面いっぱいに広げる
    lv_obj_set_style_border_width(screen_main, 0, 0);       // 枠線を消す
    
    lv_obj_t *label_main = lv_label_create(screen_main);
    lv_label_set_text(label_main, "NORMAL MODE");
    lv_obj_center(label_main);

    // --- 2. 詳細画面 (Info Screen) の構築 ---
    screen_info = lv_obj_create(screen);
    lv_obj_set_size(screen_info, lv_pct(100), lv_pct(100)); // 画面いっぱいに広げる
    lv_obj_set_style_border_width(screen_info, 0, 0);
    lv_obj_add_flag(screen_info, LV_OBJ_FLAG_HIDDEN);       // 初期状態は非表示
    
    lv_obj_t *label_info = lv_label_create(screen_info);
    lv_label_set_text(label_info, "DETAILED INFO\nPAGE 2");
    lv_obj_set_style_text_align(label_info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label_info);

    // 起動時の初期レイヤー状態を反映
    update_screen_view(zmk_keymap_highest_layer_active());

    return screen;
}
