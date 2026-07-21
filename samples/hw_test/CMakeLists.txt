cmake_minimum_required(VERSION 3.20.0)

# USP (Semtech) — must be registered before find_package(Zephyr).
# Paths assume west workspace layout:
#   <workspace>/rak3162-zephyr-bsp/          (this BSP)
#   <workspace>/rak3162-zephyr-bsp/modules/semtech/usp_zephyr
#   <workspace>/modules/lib/usp
set(ZEPHYR_EXTRA_MODULES
    "${CMAKE_CURRENT_SOURCE_DIR}/../../modules/semtech/usp_zephyr"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../modules/lib/usp"
    CACHE STRING "Zephyr extra modules" FORCE
)

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
    src/at/hw_at_test.c
    src/at/hw_at_ble_cw.c
    src/lora/hw_lora_test.c
)
