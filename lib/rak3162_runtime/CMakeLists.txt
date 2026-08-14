# Copyright (c) 2026 RAKwireless Technology Limited
# SPDX-License-Identifier: Apache-2.0

zephyr_include_directories(src)

zephyr_library()
zephyr_library_sources(
  src/board/board_at_lp.c
  src/core/bus_pm.c
  src/core/led.c
  src/storage/storage.c
  src/radio/radio_bind.c
)
