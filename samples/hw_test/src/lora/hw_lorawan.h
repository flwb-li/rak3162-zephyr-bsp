#ifndef HW_LORAWAN_H_
#define HW_LORAWAN_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/lorawan/lorawan.h>

void hw_lorawan_init(void);

int hw_lorawan_ensure_started(void);

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
int hw_lorawan_join_otaa_async(const uint8_t deveui[8], const uint8_t joineui[8],
			       const uint8_t appkey[16], const uint8_t nwkkey[16],
			       uint8_t interval_s, uint8_t attempts);

/**
 * @brief Stop an in-progress join (RUI3 AT+JOIN=0). Interruptible between attempts.
 * @return 0 if stop requested / idle; -EBUSY only if a non-join job is active.
 */
int hw_lorawan_join_stop(void);

/**
 * @brief If AT+JOIN Param2 auto-join is set and keys are valid, start join on boot.
 */
void hw_lorawan_autojoin_on_boot(void);

/**
 * @brief Queue an uplink (non-blocking). Emits +EVT:TX_DONE and optional confirm EVTs.
 */
int hw_lorawan_send_async(uint8_t port, const uint8_t *data, uint8_t len,
			  enum lorawan_message_type type);

bool hw_lorawan_is_joined(void);
bool hw_lorawan_is_started(void);
bool hw_lorawan_is_busy(void);
/** True while a join sequence (including reattempt waits) is active. */
bool hw_lorawan_is_joining(void);

enum lorawan_class hw_lorawan_get_class(void);

/** Map RUI3 AT+BAND code to Zephyr region; returns -EINVAL if unsupported. */
int hw_lorawan_band_to_region(uint8_t band, enum lorawan_region *region);

/** True if compiled-in stack supports this RUI3 band code. */
bool hw_lorawan_band_supported(uint8_t band);

void hw_lorawan_set_cfm(uint8_t cfm);
uint8_t hw_lorawan_get_cfm(void);
uint8_t hw_lorawan_get_cfs(void);

void hw_lorawan_set_adr(bool enable);
bool hw_lorawan_get_adr(void);

/**
 * @brief Format last downlink as RUI3 AT+RECV payload ("<port>:<hex>" or "0:").
 *        Clears the stored downlink after a successful read into out.
 */
int hw_lorawan_recv_format_and_clear(char *out, size_t out_len);

#endif /* HW_LORAWAN_H_ */
