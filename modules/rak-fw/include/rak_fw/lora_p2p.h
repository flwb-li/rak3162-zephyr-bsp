/*
 * Copyright (c) 2026 RAKwireless Technology Limited
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file lora_p2p.h
 * @brief LoRa P2P helpers over Zephyr lora0.
 */
#ifndef RAK_FW_LORA_P2P_H_
#define RAK_FW_LORA_P2P_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void (*rak_fw_lora_p2p_event_handler_t)(const char *line);

/** Register an optional consumer for RUI-style P2P event lines. */
void rak_fw_lora_p2p_set_event_handler(rak_fw_lora_p2p_event_handler_t handler);

void rak_fw_lora_p2p_init(void);

/**
 * @brief True while P2P RX/TX occupies the radio.
 */
bool rak_fw_lora_p2p_is_busy(void);

/**
 * @brief True if PRECV mode is non-zero or TX is in progress.
 */
bool rak_fw_lora_p2p_is_active(void);

/**
 * @brief Set P2P parameters from "<freq>:<sf>:<bw>:<cr>:<preamble>:<tx_power>".
 *        BW: 0/125, 1/250, 2/500 (Zephyr LoRa supports these only).
 *        CR: 0=4/5 … 3=4/8 (RUI3 index).
 */
int rak_fw_lora_p2p_params_set(const char *param);

int rak_fw_lora_p2p_params_format(char *out, size_t out_len);

int rak_fw_lora_p2p_send_payload(const uint8_t *data, size_t len);

/**
 * @brief Configure P2P receive window.
 *        0 stop; 1..65532 timed ms; 65533 continuous (TX allowed);
 *        65534 continuous locked; 65535 until one packet.
 */
int rak_fw_lora_p2p_recv_set(uint16_t time_ms);

uint16_t rak_fw_lora_p2p_recv_get(void);

#endif /* RAK_FW_LORA_P2P_H_ */
