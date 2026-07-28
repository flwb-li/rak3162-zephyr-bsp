/*
 * Copyright (c) 2026 RAKwireless Technology Limited
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file hw_lora_p2p.h
 * @brief LoRa P2P / CW helpers over Zephyr lora0 (no Semtech USP).
 */
#ifndef HW_LORA_P2P_H_
#define HW_LORA_P2P_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void hw_lora_p2p_init(void);

/**
 * @brief True while P2P RX/TX/CW occupies the radio.
 */
bool hw_lora_p2p_is_busy(void);

/**
 * @brief True if PRECV mode is non-zero or CW/TX is in progress.
 */
bool hw_lora_p2p_is_active(void);

/**
 * @brief Set P2P parameters from "<freq>:<sf>:<bw>:<cr>:<preamble>:<tx_power>".
 *        BW: 0/125, 1/250, 2/500 (Zephyr LoRa supports these only).
 *        CR: 0=4/5 … 3=4/8 (RUI3 index).
 */
int hw_lora_p2p_params_set(const char *param);

int hw_lora_p2p_params_format(char *out, size_t out_len);

int hw_lora_p2p_send_payload(const uint8_t *data, size_t len);

/**
 * @brief Configure P2P receive window.
 *        0 stop; 1..65532 timed ms; 65533 continuous (TX allowed);
 *        65534 continuous locked; 65535 until one packet.
 */
int hw_lora_p2p_recv_set(uint16_t time_ms);

uint16_t hw_lora_p2p_recv_get(void);

/**
 * @brief Start CW from "<freq_hz>:<tx_power>:<time_ms>".
 *        time_ms=0 means long-running CW (driver timeout in seconds).
 */
int hw_lora_cw_start(const char *param);

int hw_lora_cw_format(char *out, size_t out_len);

#endif /* HW_LORA_P2P_H_ */
