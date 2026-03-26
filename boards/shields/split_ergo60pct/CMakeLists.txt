# シールド用のライブラリを定義
zephyr_library()

# ドングル（Central）のビルド時のみ実行される条件分岐
if (SHIELD MATCHES "dongle")
    message(STATUS "split_ergo60pct: Dongle build -> Adding custom status screen and widgets")

    # 1. カスタムステータス画面とウィジェットとフォントソースファイルを追加
    zephyr_library_include_directories(${ZEPHYR_LVGL_MODULE_DIR})
    zephyr_library_include_directories(${ZEPHYR_BASE}/lib/gui/lvgl/)
    zephyr_library_include_directories(${ZEPHYR_BASE}/drivers)
    zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)
    zephyr_library_include_directories(${ZEPHYR_CURRENT_MODULE_DIR}/include)
    zephyr_library_include_directories(${ZEPHYR_CURRENT_CMAKE_DIR}/include)
    zephyr_library_include_directories(dongle_screen/include)
    
    zephyr_library_sources(src/brightness.c)
    zephyr_library_sources(src/screen_rotate_init.c)
    zephyr_library_sources(dongle_screen/src/custom_status_screen.c)
    zephyr_library_sources(dongle_screen/src/bongo_img.c)
    zephyr_library_sources(dongle_screen/src/widgets/output_status.c)
    zephyr_library_sources(dongle_screen/src/widgets/battery_status.c)
    zephyr_library_sources(dongle_screen/src/widgets/layer_status.c)
    zephyr_library_sources(dongle_screen/src/widgets/wpm_status.c)
    zephyr_library_sources(dongle_screen/src/widgets/mod_status.c)
    
    file(GLOB font_sources dongle_screen/src/fonts/*.c)
    zephyr_library_sources(${font_sources})

else()
    # 左右のキーボード（Peripheral）ビルド時はここを通る
    message(STATUS "split_ergo60pct: Peripheral build (Left/Right) -> Skipping display code")
endif()
