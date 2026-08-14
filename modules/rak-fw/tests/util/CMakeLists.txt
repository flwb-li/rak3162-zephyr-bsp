# Copyright (c) 2026 RAKwireless Technology Limited
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(rak_at_util_test)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
target_sources(app PRIVATE
  src/main.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/at/util/rak_at_util.c
)
