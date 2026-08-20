# Copyright (c) 2026 RAKwireless Technology Limited
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Pin rak-fw to this BSP revision (vendored at modules/rak-fw).
list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/rak-fw)

# Optional unpublished factory extras as an external Zephyr module (not in this
# sample). Must be assigned before find_package(Zephyr).
#   -DRAK3162_AT_EXTRAS_DIR=/abs/path
#   or env RAK3162_AT_EXTRAS_DIR
#   or west-workspace sibling rak3162-at-factory/
# Use EXTRA_DTC_OVERLAY_FILE (append) rather than DTC_OVERLAY_FILE (would
# replace the board overlay discovered by Zephyr).
if(NOT RAK3162_AT_EXTRAS_DIR AND DEFINED ENV{RAK3162_AT_EXTRAS_DIR})
  set(RAK3162_AT_EXTRAS_DIR $ENV{RAK3162_AT_EXTRAS_DIR})
endif()
if(NOT RAK3162_AT_EXTRAS_DIR)
  get_filename_component(_west_ws ${CMAKE_CURRENT_SOURCE_DIR}/../../.. ABSOLUTE)
  if(EXISTS ${_west_ws}/rak3162-at-factory/zephyr/module.yml)
    set(RAK3162_AT_EXTRAS_DIR ${_west_ws}/rak3162-at-factory)
  endif()
endif()

if(RAK3162_AT_EXTRAS_DIR AND EXISTS ${RAK3162_AT_EXTRAS_DIR}/zephyr/module.yml)
  message(STATUS "at_firmware: factory extras module ${RAK3162_AT_EXTRAS_DIR}")
  list(APPEND ZEPHYR_EXTRA_MODULES ${RAK3162_AT_EXTRAS_DIR})
  if(EXISTS ${RAK3162_AT_EXTRAS_DIR}/factory.conf)
    set(EXTRA_CONF_FILE ${RAK3162_AT_EXTRAS_DIR}/factory.conf)
  endif()
  if(EXISTS ${RAK3162_AT_EXTRAS_DIR}/factory.overlay)
    set(EXTRA_DTC_OVERLAY_FILE ${RAK3162_AT_EXTRAS_DIR}/factory.overlay)
  endif()
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(rak3162_at_firmware)

target_include_directories(app PRIVATE src)
target_sources(app PRIVATE src/main.c)
