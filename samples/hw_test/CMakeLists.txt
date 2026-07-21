cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(rak3162_hw_test)

target_include_directories(app PRIVATE
    src
)

target_sources(app PRIVATE
    src/main.c
    src/core/hw_app.c
    src/core/hw_console.c
    src/core/hw_led.c
    src/core/hw_xo_cap.c
    src/storage/hw_storage.c
    src/at/hw_at.c
    src/at/hw_at_lora.c
    src/at/hw_at_lorawan.c
    src/at/hw_at_test.c
    src/at/hw_at_ble_cw.c
    src/lora/hw_lorawan.c
)
