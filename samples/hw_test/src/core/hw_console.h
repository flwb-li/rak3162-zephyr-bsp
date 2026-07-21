#ifndef HW_CONSOLE_H_
#define HW_CONSOLE_H_

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

typedef void (*hw_console_rx_handler_t)(const uint8_t *data, size_t len, bool idle_boundary,
                    void *user_data);

int hw_console_init(void);
int hw_console_set_rx_handler(hw_console_rx_handler_t handler, void *user_data);
void hw_console_putc(char c);
void hw_console_puts(const char *s);

void hw_console_set_echo(bool enabled);
bool hw_console_is_echo_enabled(void);

#endif /* HW_CONSOLE_H_ */
