#include "core/hw_console.h"

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>

#define CONSOLE_NODE DT_CHOSEN(zephyr_console)
#define HW_CONSOLE_RX_BUF_SIZE    128
/*
 * RX idle timeout used as "frame boundary" for AT input without CR/LF.
 * Keep it long enough to avoid splitting a single command when host sends
 * bytes with small gaps, but short enough to keep interactive feel.
 */
#define HW_CONSOLE_RX_TIMEOUT_US  50000

LOG_MODULE_REGISTER(hw_console, LOG_LEVEL_INF);

static const struct device *const console_dev = DEVICE_DT_GET(CONSOLE_NODE);
static bool echo_enabled = true;
static hw_console_rx_handler_t rx_handler;
static void *rx_handler_user_data;
static uint8_t rx_buf_a[HW_CONSOLE_RX_BUF_SIZE];
static uint8_t rx_buf_b[HW_CONSOLE_RX_BUF_SIZE];
static bool rx_buf_a_in_use;
static bool rx_buf_b_in_use;

static int hw_console_start_async_rx(void)
{
    int ret;

    memset(rx_buf_a, 0, sizeof(rx_buf_a));
    memset(rx_buf_b, 0, sizeof(rx_buf_b));
    rx_buf_a_in_use = true;
    rx_buf_b_in_use = false;

    ret = uart_rx_enable(console_dev, rx_buf_a, sizeof(rx_buf_a), HW_CONSOLE_RX_TIMEOUT_US);
    if (ret != 0) {
        LOG_ERR("uart_rx_enable failed: %d", ret);
        return ret;
    }

    return 0;
}

static void hw_console_uart_callback(const struct device *dev, struct uart_event *evt, void *user_data)
{
    ARG_UNUSED(user_data);

    switch (evt->type) {
    case UART_RX_RDY:
        if (rx_handler != NULL) {
            const uint8_t *data = &evt->data.rx.buf[evt->data.rx.offset];

            /* Data is ready, but this is NOT an idle/frame boundary. */
            rx_handler(data, evt->data.rx.len, false, rx_handler_user_data);
        }
        break;
    case UART_RX_BUF_REQUEST:
        if (!rx_buf_a_in_use) {
            rx_buf_a_in_use = true;
            (void)uart_rx_buf_rsp(dev, rx_buf_a, sizeof(rx_buf_a));
        } else if (!rx_buf_b_in_use) {
            rx_buf_b_in_use = true;
            (void)uart_rx_buf_rsp(dev, rx_buf_b, sizeof(rx_buf_b));
        } else {
            LOG_WRN("No free async RX buffer available");
        }
        break;
    case UART_RX_BUF_RELEASED:
        if (evt->data.rx_buf.buf == rx_buf_a) {
            rx_buf_a_in_use = false;
        } else if (evt->data.rx_buf.buf == rx_buf_b) {
            rx_buf_b_in_use = false;
        }
        break;
    case UART_RX_DISABLED:
        /*
         * Treat RX_DISABLED as an idle boundary (timeout). This is the only
         * place we signal "frame end" to the AT layer.
         */
        if (rx_handler != NULL) {
            rx_handler(NULL, 0, true, rx_handler_user_data);
        }
        (void)hw_console_start_async_rx();
        break;
    case UART_RX_STOPPED:
        LOG_WRN("UART RX stopped, reason=%d", evt->data.rx_stop.reason);
        if (rx_handler != NULL) {
            rx_handler(NULL, 0, true, rx_handler_user_data);
        }
        (void)uart_rx_disable(dev);
        break;
    default:
        break;
    }
}

int hw_console_init(void)
{
    int ret;

    if (!device_is_ready(console_dev)) {
        LOG_ERR("Console device not ready");
        return -ENODEV;
    }

    ret = uart_callback_set(console_dev, hw_console_uart_callback, NULL);
    if (ret != 0) {
        LOG_ERR("uart_callback_set failed: %d", ret);
        return ret;
    }

    ret = hw_console_start_async_rx();
    if (ret != 0) {
        return ret;
    }

    LOG_INF("Console device ready");
    return 0;
}

int hw_console_set_rx_handler(hw_console_rx_handler_t handler, void *user_data)
{
    rx_handler = handler;
    rx_handler_user_data = user_data;
    return 0;
}

void hw_console_putc(char c)
{
    uart_poll_out(console_dev, (unsigned char)c);
}

void hw_console_puts(const char *s)
{
    while (*s != '\0') {
        hw_console_putc(*s++);
    }
}

void hw_console_set_echo(bool enabled)
{
    echo_enabled = enabled;
}

bool hw_console_is_echo_enabled(void)
{
    return echo_enabled;
}
