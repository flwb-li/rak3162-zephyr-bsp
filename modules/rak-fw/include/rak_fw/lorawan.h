/*
 * Copyright (c) 2026 RAKwireless Technology Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RAK_FW_LORAWAN_H_
#define RAK_FW_LORAWAN_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/lorawan/lorawan.h>

typedef void (*rak_fw_lorawan_event_handler_t)(const char *line);
typedef void (*rak_fw_lorawan_rf_state_handler_t)(bool active);
/** Called with true after successful join; false when the session is no longer joined. */
typedef void (*rak_fw_lorawan_join_status_cb_t)(bool joined);
/**
 * Called after a queued uplink finishes in the radio worker.
 * @param result 0 on TX success, -EAGAIN when duty-cycle deferred, other negative errno on failure.
 */
typedef void (*rak_fw_lorawan_send_done_cb_t)(int result);

/**
 * @brief Register event / RF-state callbacks.
 *
 * The radio service does not depend on an AT transport. A board adapter may
 * translate event lines to AT +EVT output and RF state to console LP policy.
 */
void rak_fw_lorawan_set_handlers(rak_fw_lorawan_event_handler_t event_handler,
				 rak_fw_lorawan_rf_state_handler_t rf_state_handler);

void rak_fw_lorawan_set_join_status_cb(rak_fw_lorawan_join_status_cb_t cb);
void rak_fw_lorawan_set_send_done_cb(rak_fw_lorawan_send_done_cb_t cb);

void rak_fw_lorawan_init(void);

int rak_fw_lorawan_ensure_started(void);

/**
 * @brief Queue an OTAA join (non-blocking), RUI3 AT+JOIN semantics.
 *
 * Emits +EVT:JOINED on success, or +EVT:JOIN FAILED after attempts are
 * exhausted. Param4 (attempts) 0 means retry until success or stop.
 *
 * @param interval_s Reattempt interval in seconds (7–255; values <7 coerced to 8).
 * @param attempts   Max join attempts; 0 = unlimited until stop/success.
 * @return 0 if queued; negative errno if rejected immediately.
 */
int rak_fw_lorawan_join_otaa_async(const uint8_t deveui[8], const uint8_t joineui[8],
				   const uint8_t appkey[16], const uint8_t nwkkey[16],
				   uint8_t interval_s, uint8_t attempts);

/**
 * @brief Stop an in-progress join (RUI3 AT+JOIN=0). Interruptible between attempts.
 * @return 0 if stop requested / idle; -EBUSY only if a non-join job is active.
 */
int rak_fw_lorawan_join_stop(void);

/**
 * @brief Queue an uplink (non-blocking). Emits +EVT:TX_DONE on success and
 *        optional confirm EVTs.
 */
int rak_fw_lorawan_send_async(uint8_t port, const uint8_t *data, uint8_t len,
			      enum lorawan_message_type type);

/**
 * @brief Read the next LoRaWAN uplink frame counter.
 *
 * The value is restored with the LoRaMAC NVM session and can be used as a
 * reboot-safe application sequence number.
 */
int rak_fw_lorawan_get_uplink_counter(uint32_t *counter);

/** Put SX126x into warm sleep (WarmStart=1) between RF windows / before System OFF. */
void rak_fw_lorawan_radio_cold_sleep(void);

bool rak_fw_lorawan_is_joined(void);
bool rak_fw_lorawan_is_started(void);
bool rak_fw_lorawan_is_busy(void);
/** True while a join sequence (including reattempt waits) is active. */
bool rak_fw_lorawan_is_joining(void);

enum lorawan_class rak_fw_lorawan_get_class(void);

/** Map RUI3 AT+BAND code to Zephyr region; returns -EINVAL if unsupported. */
int rak_fw_lorawan_band_to_region(uint8_t band, enum lorawan_region *region);

/** True if compiled-in stack supports this RUI3 band code. */
bool rak_fw_lorawan_band_supported(uint8_t band);

void rak_fw_lorawan_set_cfm(uint8_t cfm);
uint8_t rak_fw_lorawan_get_cfm(void);
uint8_t rak_fw_lorawan_get_cfs(void);

void rak_fw_lorawan_set_adr(bool enable);
bool rak_fw_lorawan_get_adr(void);

/**
 * @brief Format last downlink as RUI3 AT+RECV payload ("<port>:<hex>" or "0:").
 *        Clears the stored downlink after a successful read into out.
 */
int rak_fw_lorawan_recv_format_and_clear(char *out, size_t out_len);

#endif /* RAK_FW_LORAWAN_H_ */
