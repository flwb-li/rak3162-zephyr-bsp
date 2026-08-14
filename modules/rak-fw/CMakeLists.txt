# Copyright (c) 2026 RAKwireless Technology Limited
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_RAK_FW)
  zephyr_include_directories(include)

  zephyr_library()

  if(CONFIG_RAK_AT)
    zephyr_library_sources(
      src/at/core/rak_at_core.c
      src/at/core/rak_at_runtime.c
      src/at/port/rak_at_port_uart.c
      src/at/util/rak_at_util.c
      src/at/cmds/rak_at_cmd_system.c
      src/lora/board.c
    )
    zephyr_library_sources_ifdef(CONFIG_RAK_AT_LORAWAN src/at/cmds/rak_at_cmd_lorawan.c)
    zephyr_library_sources_ifdef(CONFIG_RAK_AT_LORA_P2P src/at/cmds/rak_at_cmd_lora.c)
  endif()

  if(CONFIG_RAK_FW_LORAWAN)
    zephyr_library_sources(src/lora/lorawan.c)
    zephyr_library_include_directories(
      ${ZEPHYR_LORAMAC_NODE_MODULE_DIR}/src/boards
      ${ZEPHYR_LORAMAC_NODE_MODULE_DIR}/src/system
      ${ZEPHYR_LORAMAC_NODE_MODULE_DIR}/src/mac
      ${ZEPHYR_LORAMAC_NODE_MODULE_DIR}/src/mac/region
      ${ZEPHYR_LORAMAC_NODE_MODULE_DIR}/src/radio
    )
  endif()
  zephyr_library_sources_ifdef(CONFIG_RAK_FW_LORA_P2P src/lora/lora_p2p.c)
endif()
