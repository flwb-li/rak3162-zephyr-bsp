#ifndef HW_LORAWAN_H_
#define HW_LORAWAN_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/lorawan/lorawan.h>

int hw_lorawan_ensure_started(void);
int hw_lorawan_join_otaa(const uint8_t deveui[8], const uint8_t joineui[8], const uint8_t appkey[16],
			 const uint8_t nwkkey[16]);
int hw_lorawan_send(uint8_t port, const uint8_t *data, uint8_t len, enum lorawan_message_type type);
bool hw_lorawan_is_joined(void);
bool hw_lorawan_is_started(void);
enum lorawan_class hw_lorawan_get_class(void);

#endif /* HW_LORAWAN_H_ */
