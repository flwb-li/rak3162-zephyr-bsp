/**
 * @file hw_lora_test.h
 * @brief RAK P2P LoRa test: static (RAM-only) parameters and PSEND over SX1262.
 */
#ifndef HW_LORA_TEST_H_
#define HW_LORA_TEST_H_

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Initialize P2P defaults and the USP RAC transaction context.
 *        Safe to call once after the kernel is running.
 */
void hw_lora_test_init(void);

enum hw_lora_radio_low_power_mode {
    HW_LORA_RADIO_LOW_POWER_SLEEP_WARM = 0,
    HW_LORA_RADIO_LOW_POWER_SLEEP_COLD,
    HW_LORA_RADIO_LOW_POWER_STANDBY_RC,
    HW_LORA_RADIO_LOW_POWER_STANDBY_XOSC,
};

/**
 * @brief Put SX1262 into a selected low-power state before MCU sleep/poweroff.
 * @return 0 on success, -EBUSY if a P2P/CW transaction is active, negative errno otherwise.
 */
int hw_lora_radio_enter_low_power(enum hw_lora_radio_low_power_mode mode);

/**
 * @brief Set P2P parameters from a string
 *        "<freq_hz>:<sf>:<bw>:<cr>:<preamble>:<tx_power>".
 *        Bandwidth: 0–2 (125/250/500 kHz) or 125/250/500.
 *        CR: 0=4/5 … 3=4/8 (RUI3 index).
 * @return 0 on success, negative on invalid input.
 */
int hw_lora_p2p_params_set(const char *param);

/**
 * @brief Format current P2P parameters into a buffer (RUI3-style, BW in kHz).
 * @return 0 on success.
 */
int hw_lora_p2p_params_format(char *out, size_t out_len);

/**
 * @brief Submit a P2P send (raw bytes) through USP RAC.
 *        Transmission is asynchronous; completion is reported by AT event print.
 * @return 0 on submit, -EBUSY if a send is in progress, -EINVAL for bad args.
 */
int hw_lora_p2p_send_payload(const uint8_t *data, size_t len);

/**
 * @brief Configure P2P receive mode/window in milliseconds.
 *        Supports special values 65533/65534/65535 and 0 to stop RX mode.
 * @return 0 on success, -EBUSY when RX mode blocks the change, -EINVAL on bad input.
 */
int hw_lora_p2p_recv_set(uint16_t time_ms);

/**
 * @brief Get the current PRECV configuration value.
 */
uint16_t hw_lora_p2p_recv_get(void);

/**
 * @brief Set CW parameters and start an unmodulated carrier test.
 *        Input format: "<freq_hz>:<tx_power>:<time_ms>".
 * @return 0 on success, -EBUSY if radio is occupied, -EINVAL on bad input.
 */
int hw_lora_cw_start(const char *param);

/**
 * @brief Format current CW parameters into a buffer.
 * @return 0 on success.
 */
int hw_lora_cw_format(char *out, size_t out_len);

#endif /* HW_LORA_TEST_H_ */
