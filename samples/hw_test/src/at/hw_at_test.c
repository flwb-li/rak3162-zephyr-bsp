#include "at/hw_at.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>

#include <zephyr/bluetooth/assigned_numbers.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/timer/nrf_grtc_timer.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys_clock.h>

LOG_MODULE_REGISTER(hw_at_test, LOG_LEVEL_INF);

#define TEST_REPEAT_COUNT 100U
#define TEST_INTERVAL_MS  10U

#define GRTC_DEFAULT_SQUARE_HZ 32768U

#define TEST_I2C_NODE  DT_NODELABEL(rak_i2c)
#define TEST_SPI_NODE  DT_NODELABEL(rak_ext_spi)
#define TEST_UART_NODE DT_NODELABEL(rak_uart)

#if !DT_NODE_HAS_STATUS(TEST_I2C_NODE, okay)
#error "Unsupported board: rak_i2c node is not enabled"
#endif

#if !DT_NODE_HAS_STATUS(TEST_SPI_NODE, okay)
#error "Unsupported board: rak_ext_spi node is not enabled"
#endif

#if !DT_NODE_HAS_PROP(TEST_SPI_NODE, cs_gpios)
#error "Unsupported board: spi00 needs cs-gpios in DTS (see rak3162_common.dtsi)"
#endif

#if !DT_NODE_HAS_STATUS(TEST_UART_NODE, okay)
#error "Unsupported board: rak_uart node is not enabled"
#endif

static const struct device *const test_i2c = DEVICE_DT_GET(TEST_I2C_NODE);
static const struct device *const test_spi = DEVICE_DT_GET(TEST_SPI_NODE);
static const struct device *const test_uart = DEVICE_DT_GET(TEST_UART_NODE);

static bool ble_enabled;
static bool ble_adv_started;

#if DT_NODE_EXISTS(DT_NODELABEL(lf_mon_out))

struct grtc_run_state {
	const struct gpio_dt_spec *spec;
	int32_t chan;
	uint32_t hz;
	uint32_t toggle_hz;
	uint32_t acc;
};

static const struct gpio_dt_spec grtc_lf_gpio =
	GPIO_DT_SPEC_GET(DT_NODELABEL(lf_mon_out), gpios);

static struct grtc_run_state grtc_state = { .chan = -1 };
static struct k_mutex grtc_lock;
static atomic_t grtc_running;
static bool grtc_lock_ready;

static uint64_t grtc_next_period_ticks(struct grtc_run_state *s)
{
	s->acc += s->hz % s->toggle_hz;
	uint64_t d = (uint64_t)s->hz / (uint64_t)s->toggle_hz;

	if (s->acc >= s->toggle_hz) {
		d++;
		s->acc -= s->toggle_hz;
	}

	return d;
}

static void grtc_toggle_handler(int32_t chan, uint64_t expire_time, void *user_data)
{
	struct grtc_run_state *s = user_data;

	if (!atomic_get(&grtc_running)) {
		return;
	}

	(void)gpio_pin_toggle_dt(s->spec);

	const uint64_t delta = grtc_next_period_ticks(s);
	const uint64_t next = expire_time + delta;

	if (z_nrf_grtc_timer_set(chan, next, grtc_toggle_handler, s) != 0) {
		atomic_set(&grtc_running, false);
	}
}

static void grtc_lock_init_once(void)
{
	if (!grtc_lock_ready) {
		k_mutex_init(&grtc_lock);
		grtc_lock_ready = true;
	}
}

static int grtc_start_locked(uint32_t square_hz)
{
	int err;

	if (atomic_get(&grtc_running)) {
		return -EBUSY;
	}
	if (!gpio_is_ready_dt(&grtc_lf_gpio)) {
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&grtc_lf_gpio, GPIO_OUTPUT_LOW);
	if (err != 0) {
		return err;
	}

	memset(&grtc_state, 0, sizeof(grtc_state));
	grtc_state.spec = &grtc_lf_gpio;
	grtc_state.hz = sys_clock_hw_cycles_per_sec();
	if ((square_hz == 0U) || (grtc_state.hz == 0U)) {
		return -EINVAL;
	}
	grtc_state.toggle_hz = square_hz * 2U;
	if (grtc_state.toggle_hz < 2U) {
		return -EINVAL;
	}

	grtc_state.chan = z_nrf_grtc_timer_chan_alloc();
	if (grtc_state.chan < 0) {
		return -ENOMEM;
	}

	atomic_set(&grtc_running, true);

	const uint64_t now = z_nrf_grtc_timer_read();
	const uint64_t first = now + grtc_next_period_ticks(&grtc_state);

	err = z_nrf_grtc_timer_set(grtc_state.chan, first, grtc_toggle_handler, &grtc_state);
	if (err != 0) {
		atomic_set(&grtc_running, false);
		z_nrf_grtc_timer_chan_free(grtc_state.chan);
		grtc_state.chan = -1;
		return err;
	}

	LOG_INF("GRTC test: square %u Hz on P2.06, SYSCOUNTER %u Hz", square_hz, grtc_state.hz);
	return 0;
}

static int grtc_stop_locked(void)
{
	int32_t chan;

	if (!atomic_get(&grtc_running)) {
		return -EINVAL;
	}

	atomic_set(&grtc_running, false);
	chan = grtc_state.chan;
	if (chan >= 0) {
		z_nrf_grtc_timer_abort(chan);
		z_nrf_grtc_timer_chan_free(chan);
		grtc_state.chan = -1;
	}
	(void)gpio_pin_set_dt(&grtc_lf_gpio, 0);
	return 0;
}

#endif /* DT_NODE_EXISTS(DT_NODELABEL(lf_mon_out)) */

static void resp_param_error(void)
{
    hw_at_resp_line("AT_PARAM_ERROR");
}

static int test_iic_waveform(void)
{
    uint8_t byte = 0xA5;

    if (!device_is_ready(test_i2c)) {
        hw_at_resp_error(NULL);
        return -ENODEV;
    }

    for (uint32_t i = 0U; i < TEST_REPEAT_COUNT; i++) {
        /*
         * A missing slave may NACK, but the address/write attempt still creates
         * observable SDA/SCL activity on P0.02/P0.03.
         */
        (void)i2c_write(test_i2c, &byte, sizeof(byte), 0x50);
        k_msleep(TEST_INTERVAL_MS);
        byte++;
    }

    hw_at_resp_line("AT+TEST=IIC");
    hw_at_resp_ok();
    return 0;
}

static int test_spi_waveform(void)
{
    /*
     * spi00: SCK=P2.01, MOSI=P2.02, MISO=P2.04, CS=P2.05 (cs-gpios in DTS, active-low).
     * CS is driven by the SPI driver via spi_config.cs (GPIO chip select).
     *
     * Use spi_transceive() with a same-length dummy RX buffer: on nRF SPIM, TX-only
     * (rx_length == 0) can produce incomplete clocking with some EasyDMA/DMM paths;
     * matching TX/RX lengths yields one SCK edge per bit as expected (4 bytes => 32
     * rising edges in SPI mode 0).
     */
    static uint8_t tx[] = { 0xA5, 0xA5, 0x5A, 0x5A };
    const struct spi_buf tx_buf = {
        .buf = tx,
        .len = sizeof(tx),
    };
    const struct spi_buf_set tx_set = {
        .buffers = &tx_buf,
        .count = 1U,
    };
    const struct spi_cs_control cs_ctrl = {
        .gpio = GPIO_DT_SPEC_GET(TEST_SPI_NODE, cs_gpios),
        .delay = 0U,
        .cs_is_gpio = true,
    };
    struct spi_config spi_cfg = {
        .frequency = 2000000U,
        .operation = SPI_OP_MODE_MASTER | SPI_TRANSFER_MSB | SPI_WORD_SET(8) | SPI_MODE_CPOL | SPI_MODE_CPHA,
        .slave = 0U,
        .cs = cs_ctrl,
    };

    if (!device_is_ready(test_spi)) {
        hw_at_resp_error(NULL);
        return -ENODEV;
    }

    if (!gpio_is_ready_dt(&cs_ctrl.gpio)) {
        hw_at_resp_error(NULL);
        return -ENODEV;
    }

    for (uint32_t i = 0U; i < TEST_REPEAT_COUNT; i++) {
        int ret = spi_write(test_spi, &spi_cfg, &tx_set);

        if (ret != 0) {
            hw_at_resp_error(NULL);
            return ret;
        }
        k_msleep(TEST_INTERVAL_MS);
    }

    hw_at_resp_line("AT+TEST=SPI");
    hw_at_resp_ok();
    return 0;
}

static int test_uart_waveform(void)
{
    static const char msg[] = "RAK3162 SECONDARY UART TEST\r\n";

    if (!device_is_ready(test_uart)) {
        hw_at_resp_error(NULL);
        return -ENODEV;
    }

    for (uint32_t i = 0U; i < TEST_REPEAT_COUNT; i++) {
        for (size_t j = 0U; j < (sizeof(msg) - 1U); j++) {
            uart_poll_out(test_uart, msg[j]);
        }
        k_msleep(TEST_INTERVAL_MS);
    }

    hw_at_resp_line("AT+TEST=UART");
    hw_at_resp_ok();
    return 0;
}

static int test_ble_advertising(void)
{
    static const uint8_t flags = BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR;
    const char *name;
    int ret;

    if (!ble_enabled) {
        ret = bt_enable(NULL);
        if (ret != 0) {
            hw_at_resp_error(NULL);
            return ret;
        }
        ble_enabled = true;
    }

    name = bt_get_name();

    if (!ble_adv_started) {
        const struct bt_data ad[] = {
            BT_DATA(BT_DATA_FLAGS, &flags, sizeof(flags)),
            BT_DATA(BT_DATA_NAME_COMPLETE, name, strlen(name)),
        };

        ret = bt_le_adv_start(BT_LE_ADV_NCONN, ad, ARRAY_SIZE(ad), NULL, 0);
        if (ret != 0) {
            hw_at_resp_error(NULL);
            return ret;
        }
        ble_adv_started = true;
    }

    hw_at_resp_line("AT+TEST=BLE");
    hw_at_resp_line("BLE_ADV_NAME=%s", name);
    hw_at_resp_ok();
    return 0;
}

static int test_grtc_start(void)
{
#if !DT_NODE_EXISTS(DT_NODELABEL(lf_mon_out))
	hw_at_resp_error(NULL);
	return -ENODEV;
#else
	int ret;

	grtc_lock_init_once();
	k_mutex_lock(&grtc_lock, K_FOREVER);
	ret = grtc_start_locked(GRTC_DEFAULT_SQUARE_HZ);
	k_mutex_unlock(&grtc_lock);

	if (ret == -EBUSY) {
		hw_at_resp_line("AT_BUSY_ERROR");
		return -EBUSY;
	}
	if (ret != 0) {
		hw_at_resp_error(NULL);
		return ret;
	}

	hw_at_resp_line("AT+TEST=GRTC");
	hw_at_resp_ok();
	return 0;
#endif
}

static int test_grtc_stop(void)
{
#if !DT_NODE_EXISTS(DT_NODELABEL(lf_mon_out))
	hw_at_resp_error(NULL);
	return -ENODEV;
#else
	int ret;

	grtc_lock_init_once();
	k_mutex_lock(&grtc_lock, K_FOREVER);
	ret = grtc_stop_locked();
	k_mutex_unlock(&grtc_lock);

	if (ret == -EINVAL) {
		hw_at_resp_line("AT+TEST=GRTCSTOP (not running)");
		hw_at_resp_ok();
		return 0;
	}
	if (ret != 0) {
		hw_at_resp_error(NULL);
		return ret;
	}

	hw_at_resp_line("AT+TEST=GRTCSTOP");
	hw_at_resp_ok();
	return 0;
#endif
}

int hw_at_cmd_test(const struct hw_at_request *req)
{
    if (req->form == HW_AT_FORM_HELP) {
        hw_at_resp_line("Description: peripheral waveform test");
        hw_at_resp_line("AT+TEST=IIC   Generate I2C SCL/SDA waveform on P0.03/P0.02");
        hw_at_resp_line("AT+TEST=SPI   SPI00: SCK P2.01, MOSI P2.02, MISO P2.04, CS P2.05 (probe MCU pins)");
        hw_at_resp_line("AT+TEST=UART  Generate TX waveform on Secondary UART P2.08");
        hw_at_resp_line("AT+TEST=BLE   Start BLE advertising as CONFIG_BT_DEVICE_NAME");
        hw_at_resp_line("AT+TEST=GRTC       ~32.768 kHz square on P2.06 (GPIO4), LFXO via GRTC");
        hw_at_resp_line("AT+TEST=GRTCSTOP  Stop GRTC GPIO output");
        hw_at_resp_ok();
        return 0;
    }

    if ((req->form != HW_AT_FORM_SET) || (req->args == NULL)) {
        resp_param_error();
        return -EINVAL;
    }

    if (strcasecmp(req->args, "IIC") == 0) {
        return test_iic_waveform();
    }
    if (strcasecmp(req->args, "SPI") == 0) {
        return test_spi_waveform();
    }
    if (strcasecmp(req->args, "UART") == 0) {
        return test_uart_waveform();
    }
    if (strcasecmp(req->args, "BLE") == 0) {
        return test_ble_advertising();
    }
    if (strcasecmp(req->args, "GRTC") == 0) {
        return test_grtc_start();
    }
    if (strcasecmp(req->args, "GRTCSTOP") == 0) {
        return test_grtc_stop();
    }
    resp_param_error();
    return -EINVAL;
}

