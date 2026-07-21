#include "lora/hw_lora_test.h"

#include <errno.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/sys/util.h>
#include <zephyr/usp/smtc_sw_platform_helper.h>
#include "at/hw_at.h"

#include <radio_planner_types.h>
#include <ral.h>
#include <ral_defs.h>
#include <ralf.h>
#include <smtc_rac_api.h>
#include <smtc_modem_hal.h>

LOG_MODULE_REGISTER(hw_lora_test, LOG_LEVEL_INF);

/* Ping-pong style compile-time defaults, kept aligned with the AT P2P default values. */
#ifndef RF_FREQ_IN_HZ
#define RF_FREQ_IN_HZ 868000000U
#endif

#ifndef TX_OUTPUT_POWER_DBM
#define TX_OUTPUT_POWER_DBM 14
#endif

#ifndef LORA_PREAMBLE_LENGTH
#define LORA_PREAMBLE_LENGTH 8U
#endif

#define P2P_DEFAULT_FREQ_HZ  RF_FREQ_IN_HZ
#define P2P_DEFAULT_SF       7U
#define P2P_DEFAULT_BW_STR   "125"
#define P2P_DEFAULT_CR       0U
#define P2P_DEFAULT_PREAMBLE LORA_PREAMBLE_LENGTH
#define P2P_DEFAULT_TX_DBM   TX_OUTPUT_POWER_DBM

#define P2P_PRECV_STOP              0U
#define P2P_PRECV_CONT_TX_ALLOWED   65533U
#define P2P_PRECV_CONTINUOUS_LOCKED 65534U
#define P2P_PRECV_SINGLE_PACKET     65535U

enum p2p_bw_id {
    P2P_BW_125 = 0,
    P2P_BW_250,
    P2P_BW_500,
    P2P_BW_7_8,
    P2P_BW_10_4,
    P2P_BW_15_63,
    P2P_BW_20_83,
    P2P_BW_31_25,
    P2P_BW_41_67,
    P2P_BW_62_5,
};

struct p2p_params {
    uint32_t freq_hz;
    uint8_t sf; /* 6-12, maps to ral_lora_sf_t */
    enum p2p_bw_id bw;
    uint8_t cr; /* 0..3 RUI3 -> RAL 4/5..4/8 */
    uint16_t preamble;
    int8_t tx_dbm;
};

enum p2p_radio_op {
    P2P_RADIO_OP_NONE = 0,
    P2P_RADIO_OP_TX,
    P2P_RADIO_OP_RX,
    P2P_RADIO_OP_CW,
};

enum p2p_rx_mode {
    P2P_RX_MODE_OFF = 0,
    P2P_RX_MODE_WINDOW,
    P2P_RX_MODE_SINGLE_PACKET,
    P2P_RX_MODE_CONTINUOUS_LOCKED,
    P2P_RX_MODE_CONTINUOUS_TX_ALLOWED,
};

struct p2p_rx_state {
    uint16_t configured_time_ms;
    uint16_t pending_time_ms;
    enum p2p_rx_mode mode;
    bool in_progress;
    bool pending_reconfigure;
    bool resume_after_tx;
};

struct p2p_tx_request {
    struct p2p_params params;
    size_t len;
    bool pending;
};

struct cw_params {
    uint32_t freq_hz;
    int8_t tx_dbm;
    uint16_t time_ms;
};

static struct p2p_params p2p;
static struct p2p_rx_state p2p_rx;
static struct p2p_tx_request pending_tx;
static struct cw_params cw;
static struct k_mutex p2p_lock;

static uint8_t radio_access_id = RAC_INVALID_RADIO_ID;
static smtc_rac_context_t *tx_ctx;
static uint8_t tx_buf[256];
static uint8_t rx_buf[256];

static struct k_work p2p_evt_work;
static struct k_work_delayable cw_stop_work;
static char p2p_evt_line[600];
static void p2p_evt_work_handler(struct k_work *work);
static void cw_stop_work_handler(struct k_work *work);

static bool rac_us_ready;
static bool tx_in_progress;
static bool cw_in_progress;
static enum p2p_radio_op current_radio_op;

#if IS_ENABLED(CONFIG_REGULATOR) && DT_NODE_HAS_STATUS(DT_NODELABEL(lora_ant_sw), okay)
static const struct device *lora_ant_sw = DEVICE_DT_GET(DT_NODELABEL(lora_ant_sw));
#endif

static bool split_p2p_param_string(char *buf, char **parts, size_t part_count)
{
    char *cursor = buf;

    if ((buf == NULL) || (parts == NULL) || (part_count == 0U)) {
        return false;
    }

    for (size_t i = 0; i < part_count; i++) {
        char *sep;

        if ((cursor == NULL) || (*cursor == '\0')) {
            return false;
        }

        parts[i] = cursor;
        sep = strchr(cursor, ':');
        if (i == (part_count - 1U)) {
            if (sep != NULL) {
                return false;
            }
            continue;
        }
        if (sep == NULL) {
            return false;
        }

        *sep = '\0';
        cursor = sep + 1;
    }

    return true;
}

static bool parse_u32_strict(const char *s, uint32_t min, uint32_t max, uint32_t *out)
{
    char *end = NULL;
    unsigned long v;

    if ((s == NULL) || (*s == '\0') || (out == NULL)) {
        return false;
    }

    errno = 0;
    v = strtoul(s, &end, 10);
    if ((errno != 0) || (end == s) || (*end != '\0') || (v < min) || (v > max)) {
        return false;
    }

    *out = (uint32_t)v;
    return true;
}

static bool parse_bw_token(const char *token, enum p2p_bw_id *bw)
{
    if ((token == NULL) || (bw == NULL)) {
        return false;
    }

    if ((strcmp(token, "0") == 0) || (strcmp(token, "125") == 0)) {
        *bw = P2P_BW_125;
        return true;
    }
    if ((strcmp(token, "1") == 0) || (strcmp(token, "250") == 0)) {
        *bw = P2P_BW_250;
        return true;
    }
    if ((strcmp(token, "2") == 0) || (strcmp(token, "500") == 0)) {
        *bw = P2P_BW_500;
        return true;
    }
    if ((strcmp(token, "3") == 0) || (strcmp(token, "7.8") == 0)) {
        *bw = P2P_BW_7_8;
        return true;
    }
    if ((strcmp(token, "4") == 0) || (strcmp(token, "10.4") == 0)) {
        *bw = P2P_BW_10_4;
        return true;
    }
    if ((strcmp(token, "5") == 0) || (strcmp(token, "15.63") == 0)) {
        *bw = P2P_BW_15_63;
        return true;
    }
    if ((strcmp(token, "6") == 0) || (strcmp(token, "20.83") == 0)) {
        *bw = P2P_BW_20_83;
        return true;
    }
    if ((strcmp(token, "7") == 0) || (strcmp(token, "31.25") == 0)) {
        *bw = P2P_BW_31_25;
        return true;
    }
    if ((strcmp(token, "8") == 0) || (strcmp(token, "41.67") == 0)) {
        *bw = P2P_BW_41_67;
        return true;
    }
    if ((strcmp(token, "9") == 0) || (strcmp(token, "62.5") == 0)) {
        *bw = P2P_BW_62_5;
        return true;
    }

    return false;
}

static const char *bw_to_string(enum p2p_bw_id bw)
{
    switch (bw) {
    case P2P_BW_250:
        return "250";
    case P2P_BW_500:
        return "500";
    case P2P_BW_7_8:
        return "7.8";
    case P2P_BW_10_4:
        return "10.4";
    case P2P_BW_15_63:
        return "15.63";
    case P2P_BW_20_83:
        return "20.83";
    case P2P_BW_31_25:
        return "31.25";
    case P2P_BW_41_67:
        return "41.67";
    case P2P_BW_62_5:
        return "62.5";
    case P2P_BW_125:
    default:
        return P2P_DEFAULT_BW_STR;
    }
}

static ral_lora_sf_t sf_to_ral(uint8_t sf)
{
    switch (sf) {
    case 6U:
        return RAL_LORA_SF6;
    case 7U:
        return RAL_LORA_SF7;
    case 8U:
        return RAL_LORA_SF8;
    case 9U:
        return RAL_LORA_SF9;
    case 10U:
        return RAL_LORA_SF10;
    case 11U:
        return RAL_LORA_SF11;
    case 12U:
        return RAL_LORA_SF12;
    default:
        return RAL_LORA_SF7;
    }
}

static ral_lora_bw_t bw_to_ral(enum p2p_bw_id bw)
{
    switch (bw) {
    case P2P_BW_250:
        return RAL_LORA_BW_250_KHZ;
    case P2P_BW_500:
        return RAL_LORA_BW_500_KHZ;
    case P2P_BW_7_8:
        return RAL_LORA_BW_007_KHZ;
    case P2P_BW_10_4:
        return RAL_LORA_BW_010_KHZ;
    case P2P_BW_15_63:
        return RAL_LORA_BW_015_KHZ;
    case P2P_BW_20_83:
        return RAL_LORA_BW_020_KHZ;
    case P2P_BW_31_25:
        return RAL_LORA_BW_031_KHZ;
    case P2P_BW_41_67:
        return RAL_LORA_BW_041_KHZ;
    case P2P_BW_62_5:
        return RAL_LORA_BW_062_KHZ;
    case P2P_BW_125:
    default:
        return RAL_LORA_BW_125_KHZ;
    }
}

static ral_lora_cr_t user_cr_to_ral(uint8_t cr)
{
    static const ral_lora_cr_t m[4] = { RAL_LORA_CR_4_5, RAL_LORA_CR_4_6, RAL_LORA_CR_4_7, RAL_LORA_CR_4_8 };

    if (cr < 4U) {
        return m[cr];
    }
    return RAL_LORA_CR_4_5;
}

static enum p2p_rx_mode p2p_rx_mode_from_time(uint16_t time_ms)
{
    switch (time_ms) {
    case P2P_PRECV_SINGLE_PACKET:
        return P2P_RX_MODE_SINGLE_PACKET;
    case P2P_PRECV_CONTINUOUS_LOCKED:
        return P2P_RX_MODE_CONTINUOUS_LOCKED;
    case P2P_PRECV_CONT_TX_ALLOWED:
        return P2P_RX_MODE_CONTINUOUS_TX_ALLOWED;
    case P2P_PRECV_STOP:
        return P2P_RX_MODE_OFF;
    default:
        return P2P_RX_MODE_WINDOW;
    }
}

static bool p2p_rx_mode_allows_tx(enum p2p_rx_mode mode)
{
    return mode == P2P_RX_MODE_CONTINUOUS_TX_ALLOWED;
}

static bool p2p_rx_mode_restarts_after_packet(enum p2p_rx_mode mode)
{
    return (mode == P2P_RX_MODE_CONTINUOUS_LOCKED) || (mode == P2P_RX_MODE_CONTINUOUS_TX_ALLOWED);
}

static bool p2p_rx_mode_is_continuous_locked(enum p2p_rx_mode mode)
{
    return mode == P2P_RX_MODE_CONTINUOUS_LOCKED;
}

static uint32_t p2p_rx_timeout_ms_for_value(uint16_t time_ms)
{
    switch (time_ms) {
    case P2P_PRECV_SINGLE_PACKET:
    case P2P_PRECV_CONTINUOUS_LOCKED:
    case P2P_PRECV_CONT_TX_ALLOWED:
        return RAL_RX_TIMEOUT_CONTINUOUS_MODE;
    default:
        return time_ms;
    }
}

static void p2p_bytes_to_hex_upper(const uint8_t *src, size_t len, char *dst)
{
    static const char hex[] = "0123456789ABCDEF";

    for (size_t i = 0; i < len; i++) {
        dst[2U * i] = hex[(src[i] >> 4) & 0x0F];
        dst[(2U * i) + 1U] = hex[src[i] & 0x0F];
    }
    dst[2U * len] = '\0';
}

static void p2p_evt_submit(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vsnprintk(p2p_evt_line, sizeof(p2p_evt_line), fmt, ap);
    va_end(ap);
    (void)k_work_submit(&p2p_evt_work);
}

static void p2p_apply_common_lora_params(const struct p2p_params *local)
{
    smtc_rac_radio_lora_params_t *l = &tx_ctx->radio_params.lora;

    l->is_ranging_exchange = false;
    l->frequency_in_hz = local->freq_hz;
    l->tx_power_in_dbm = (uint8_t)local->tx_dbm;
    l->sf = sf_to_ral(local->sf);
    l->bw = bw_to_ral(local->bw);
    l->cr = user_cr_to_ral(local->cr);
    l->preamble_len_in_symb = local->preamble;
    l->header_type = RAL_LORA_PKT_EXPLICIT;
    l->invert_iq_is_on = 0U;
    l->crc_is_on = 1U;
    l->sync_word = LORA_PRIVATE_NETWORK_SYNCWORD;
    l->max_rx_size = sizeof(rx_buf);
    tx_ctx->cw_context.cw_enabled = false;
    tx_ctx->cw_context.infinite_preamble = false;
}

static int p2p_submit_tx_locked(const struct p2p_params *local, size_t len)
{
    smtc_rac_radio_lora_params_t *l = &tx_ctx->radio_params.lora;
    smtc_rac_return_code_t rc;

    p2p_apply_common_lora_params(local);
    l->is_tx = true;
    l->tx_size = (uint16_t)len;
    l->rx_timeout_ms = 0U;
    l->symb_nb_timeout = 0U;

    tx_in_progress = true;
    current_radio_op = P2P_RADIO_OP_TX;
    tx_ctx->scheduler_config.scheduling = SMTC_RAC_ASAP_TRANSACTION;
    tx_ctx->scheduler_config.start_time_ms = smtc_modem_hal_get_time_in_ms();
    tx_ctx->scheduler_config.duration_time_ms = 0U;

    rc = SMTC_SW_PLATFORM(smtc_rac_submit_radio_transaction(radio_access_id));
    if (rc != SMTC_RAC_SUCCESS) {
        tx_in_progress = false;
        current_radio_op = P2P_RADIO_OP_NONE;
        LOG_ERR("smtc_rac_submit_radio_transaction(tx): %d", (int)rc);
        return -EIO;
    }

    return 0;
}

static int cw_submit_locked(const struct cw_params *local)
{
    struct p2p_params cw_radio_params = p2p;
    smtc_rac_radio_lora_params_t *l = &tx_ctx->radio_params.lora;
    smtc_rac_return_code_t rc;

    cw_radio_params.freq_hz = local->freq_hz;
    cw_radio_params.tx_dbm = local->tx_dbm;
    p2p_apply_common_lora_params(&cw_radio_params);

    l->is_tx = true;
    l->tx_size = 1U;
    l->rx_timeout_ms = 0U;
    l->symb_nb_timeout = 0U;
    tx_buf[0] = 0U;

    tx_ctx->cw_context.cw_enabled = true;
    tx_ctx->cw_context.infinite_preamble = false;

    cw_in_progress = true;
    current_radio_op = P2P_RADIO_OP_CW;
    tx_ctx->scheduler_config.scheduling = SMTC_RAC_ASAP_TRANSACTION;
    tx_ctx->scheduler_config.start_time_ms = smtc_modem_hal_get_time_in_ms();
    tx_ctx->scheduler_config.duration_time_ms = local->time_ms;

    rc = SMTC_SW_PLATFORM(smtc_rac_submit_radio_transaction(radio_access_id));
    if (rc != SMTC_RAC_SUCCESS) {
        tx_ctx->cw_context.cw_enabled = false;
        tx_ctx->cw_context.infinite_preamble = false;
        cw_in_progress = false;
        current_radio_op = P2P_RADIO_OP_NONE;
        LOG_ERR("smtc_rac_submit_radio_transaction(cw): %d", (int)rc);
        return -EIO;
    }

    return 0;
}

static int p2p_submit_rx_locked(uint16_t time_ms)
{
    smtc_rac_radio_lora_params_t *l = &tx_ctx->radio_params.lora;
    smtc_rac_return_code_t rc;

    if (time_ms == P2P_PRECV_STOP) {
        p2p_rx.mode = P2P_RX_MODE_OFF;
        p2p_rx.configured_time_ms = P2P_PRECV_STOP;
        p2p_rx.in_progress = false;
        p2p_rx.pending_reconfigure = false;
        p2p_rx.resume_after_tx = false;
        current_radio_op = P2P_RADIO_OP_NONE;
        return 0;
    }

    p2p_apply_common_lora_params(&p2p);
    l->is_tx = false;
    l->tx_size = 0U;
    l->rx_timeout_ms = p2p_rx_timeout_ms_for_value(time_ms);
    l->symb_nb_timeout = 0U;

    memset(rx_buf, 0, sizeof(rx_buf));

    p2p_rx.mode = p2p_rx_mode_from_time(time_ms);
    p2p_rx.configured_time_ms = time_ms;
    p2p_rx.in_progress = true;
    current_radio_op = P2P_RADIO_OP_RX;
    tx_ctx->scheduler_config.scheduling = SMTC_RAC_ASAP_TRANSACTION;
    tx_ctx->scheduler_config.start_time_ms = smtc_modem_hal_get_time_in_ms();
    tx_ctx->scheduler_config.duration_time_ms = p2p_rx_timeout_ms_for_value(time_ms);

    rc = SMTC_SW_PLATFORM(smtc_rac_submit_radio_transaction(radio_access_id));
    if (rc != SMTC_RAC_SUCCESS) {
        p2p_rx.in_progress = false;
        current_radio_op = P2P_RADIO_OP_NONE;
        LOG_ERR("smtc_rac_submit_radio_transaction(rx): %d", (int)rc);
        return -EIO;
    }

    return 0;
}

static void p2p_pre_cb(void)
{
    if ((current_radio_op == P2P_RADIO_OP_TX) || (current_radio_op == P2P_RADIO_OP_CW)) {
        set_led(SMTC_PF_LED_TX, true);
    } else if (current_radio_op == P2P_RADIO_OP_RX) {
        set_led(SMTC_PF_LED_RX, true);
    }
}

static void p2p_post_cb(rp_status_t status)
{
    bool submit_rx = false;
    bool submit_tx = false;
    bool emit_tx_done = false;
    bool emit_rx_timeout = false;
    bool emit_rx_packet = false;
    uint16_t next_rx_time = 0U;
    size_t next_tx_len = 0U;
    struct p2p_params next_tx_params = { 0 };
    uint8_t rx_payload[256];
    size_t rx_payload_len = 0U;
    int16_t rssi = 0;
    int8_t snr = 0;

    set_led(SMTC_PF_LED_TX, false);
    set_led(SMTC_PF_LED_RX, false);

    k_mutex_lock(&p2p_lock, K_FOREVER);
    if (current_radio_op == P2P_RADIO_OP_TX) {
        tx_in_progress = false;
        current_radio_op = P2P_RADIO_OP_NONE;

        if (status == RP_STATUS_TX_DONE) {
            emit_tx_done = true;
        } else {
            LOG_WRN("P2P TX post: unexpected rp_status %d", (int)status);
        }

        if (p2p_rx.resume_after_tx) {
            p2p_rx.resume_after_tx = false;
            submit_rx = true;
            next_rx_time = p2p_rx.configured_time_ms;
        }
    } else if (current_radio_op == P2P_RADIO_OP_RX) {
        p2p_rx.in_progress = false;
        current_radio_op = P2P_RADIO_OP_NONE;

        if (status == RP_STATUS_TASK_ABORTED) {
            if (p2p_rx.pending_reconfigure) {
                p2p_rx.pending_reconfigure = false;
                next_rx_time = p2p_rx.pending_time_ms;
                if (next_rx_time == P2P_PRECV_STOP) {
                    p2p_rx.mode = P2P_RX_MODE_OFF;
                    p2p_rx.configured_time_ms = P2P_PRECV_STOP;
                } else {
                    submit_rx = true;
                }
            } else if (pending_tx.pending) {
                next_tx_params = pending_tx.params;
                next_tx_len = pending_tx.len;
                pending_tx.pending = false;
                submit_tx = true;
            } else {
                LOG_INF("P2P RX aborted");
            }
        } else if (status == RP_STATUS_RX_PACKET) {
            rx_payload_len = MIN((size_t)tx_ctx->smtc_rac_data_result.rx_size, sizeof(rx_payload));
            memcpy(rx_payload, rx_buf, rx_payload_len);
            rssi = tx_ctx->smtc_rac_data_result.rssi_result;
            snr = tx_ctx->smtc_rac_data_result.snr_result;
            emit_rx_packet = true;

            if (p2p_rx_mode_restarts_after_packet(p2p_rx.mode)) {
                submit_rx = true;
                next_rx_time = p2p_rx.configured_time_ms;
            } else {
                p2p_rx.mode = P2P_RX_MODE_OFF;
                p2p_rx.configured_time_ms = P2P_PRECV_STOP;
            }
        } else if (status == RP_STATUS_RX_TIMEOUT) {
            emit_rx_timeout = true;
            p2p_rx.mode = P2P_RX_MODE_OFF;
            p2p_rx.configured_time_ms = P2P_PRECV_STOP;
        } else if (status == RP_STATUS_RX_CRC_ERROR) {
            LOG_WRN("P2P RX CRC error");
            submit_rx = true;
            next_rx_time = p2p_rx.configured_time_ms;
        } else {
            LOG_WRN("P2P RX post: unexpected rp_status %d", (int)status);
            submit_rx = (p2p_rx.mode != P2P_RX_MODE_OFF);
            next_rx_time = p2p_rx.configured_time_ms;
        }
    } else if (current_radio_op == P2P_RADIO_OP_CW) {
        cw_in_progress = false;
        current_radio_op = P2P_RADIO_OP_NONE;
        tx_ctx->cw_context.cw_enabled = false;
        tx_ctx->cw_context.infinite_preamble = false;
        (void)k_work_cancel_delayable(&cw_stop_work);

        if (status != RP_STATUS_TASK_ABORTED) {
            LOG_WRN("CW post: unexpected rp_status %d", (int)status);
        }
    } else {
        LOG_WRN("P2P post: no active operation for status %d", (int)status);
    }
    k_mutex_unlock(&p2p_lock);

    if (emit_tx_done) {
        p2p_evt_submit("+EVT:TXP2P DONE");
    }
    if (emit_rx_timeout) {
        p2p_evt_submit("+EVT:RXP2P RECEIVE TIMEOUT");
    }
    if (emit_rx_packet) {
        char payload_hex[(sizeof(rx_payload) * 2U) + 1U];

        p2p_bytes_to_hex_upper(rx_payload, rx_payload_len, payload_hex);
        p2p_evt_submit("+EVT:RXP2P:%d:%d:%s", (int)rssi, (int)snr, payload_hex);
    }

    if (submit_tx) {
        k_mutex_lock(&p2p_lock, K_FOREVER);
        (void)p2p_submit_tx_locked(&next_tx_params, next_tx_len);
        k_mutex_unlock(&p2p_lock);
    } else if (submit_rx) {
        k_mutex_lock(&p2p_lock, K_FOREVER);
        (void)p2p_submit_rx_locked(next_rx_time);
        k_mutex_unlock(&p2p_lock);
    }
}

static void p2p_evt_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);
    hw_at_resp_line("%s", p2p_evt_line);
}

static void cw_stop_work_handler(struct k_work *work)
{
    smtc_rac_return_code_t rc;

    ARG_UNUSED(work);

    k_mutex_lock(&p2p_lock, K_FOREVER);
    if (!cw_in_progress || (current_radio_op != P2P_RADIO_OP_CW)) {
        k_mutex_unlock(&p2p_lock);
        return;
    }

    rc = SMTC_SW_PLATFORM(smtc_rac_abort_radio_submit(radio_access_id));
    k_mutex_unlock(&p2p_lock);

    if (rc != SMTC_RAC_SUCCESS) {
        LOG_ERR("smtc_rac_abort_radio_submit(cw): %d", (int)rc);
    }
}

static void p2p_context_init_defaults(void)
{
    smtc_rac_radio_lora_params_t *l = &tx_ctx->radio_params.lora;

    tx_ctx->smtc_rac_data_buffer_setup.tx_payload_buffer = tx_buf;
    tx_ctx->smtc_rac_data_buffer_setup.rx_payload_buffer = rx_buf;
    tx_ctx->smtc_rac_data_buffer_setup.size_of_tx_payload_buffer = sizeof(tx_buf);
    tx_ctx->smtc_rac_data_buffer_setup.size_of_rx_payload_buffer = sizeof(rx_buf);

    tx_ctx->modulation_type = SMTC_RAC_MODULATION_LORA;
    memset(&tx_ctx->radio_params, 0, sizeof(tx_ctx->radio_params));
    p2p_apply_common_lora_params(&p2p);
    l->tx_size = 0U;

    tx_ctx->scheduler_config.start_time_ms = 0U;
    tx_ctx->scheduler_config.duration_time_ms = 0U;
    tx_ctx->scheduler_config.scheduling = SMTC_RAC_ASAP_TRANSACTION;
    tx_ctx->scheduler_config.callback_pre_radio_transaction = p2p_pre_cb;
    tx_ctx->scheduler_config.callback_post_radio_transaction = p2p_post_cb;
}

void hw_lora_test_init(void)
{
    k_mutex_init(&p2p_lock);
    k_work_init(&p2p_evt_work, p2p_evt_work_handler);
    k_work_init_delayable(&cw_stop_work, cw_stop_work_handler);

    p2p.freq_hz = P2P_DEFAULT_FREQ_HZ;
    p2p.sf = P2P_DEFAULT_SF;
    p2p.bw = P2P_BW_125;
    p2p.cr = P2P_DEFAULT_CR;
    p2p.preamble = P2P_DEFAULT_PREAMBLE;
    p2p.tx_dbm = P2P_DEFAULT_TX_DBM;
    cw.freq_hz = P2P_DEFAULT_FREQ_HZ;
    cw.tx_dbm = P2P_DEFAULT_TX_DBM;
    cw.time_ms = 5U;
    p2p_rx.configured_time_ms = P2P_PRECV_STOP;
    p2p_rx.pending_time_ms = P2P_PRECV_STOP;
    p2p_rx.mode = P2P_RX_MODE_OFF;
    p2p_rx.in_progress = false;
    p2p_rx.pending_reconfigure = false;
    p2p_rx.resume_after_tx = false;
    pending_tx.pending = false;
    tx_in_progress = false;
    cw_in_progress = false;
    current_radio_op = P2P_RADIO_OP_NONE;

    SMTC_SW_PLATFORM_INIT();
    SMTC_SW_PLATFORM_VOID(smtc_rac_init());

    radio_access_id = SMTC_SW_PLATFORM(smtc_rac_open_radio(RAC_HIGH_PRIORITY));
    if (radio_access_id == RAC_INVALID_RADIO_ID) {
        LOG_ERR("smtc_rac_open_radio failed");
        rac_us_ready = false;
        return;
    }

    tx_ctx = smtc_rac_get_context(radio_access_id);
    if (tx_ctx == NULL) {
        LOG_ERR("smtc_rac_get_context NULL");
        (void)SMTC_SW_PLATFORM(smtc_rac_close_radio(radio_access_id));
        radio_access_id = RAC_INVALID_RADIO_ID;
        rac_us_ready = false;
        return;
    }

    rac_us_ready = true;
    p2p_context_init_defaults();
}

int hw_lora_radio_enter_low_power(enum hw_lora_radio_low_power_mode mode)
{
    ralf_t *radio;
    ral_status_t status;
    int ret = 0;

    if (!rac_us_ready || (radio_access_id == RAC_INVALID_RADIO_ID)) {
        return -ENODEV;
    }

    k_mutex_lock(&p2p_lock, K_FOREVER);
    if (tx_in_progress || cw_in_progress || pending_tx.pending || p2p_rx.in_progress || p2p_rx.pending_reconfigure ||
        (p2p_rx.mode != P2P_RX_MODE_OFF)) {
        k_mutex_unlock(&p2p_lock);
        return -EBUSY;
    }
    k_mutex_unlock(&p2p_lock);

    radio = SMTC_SW_PLATFORM(smtc_rac_get_radio());
    if (radio == NULL) {
        return -ENODEV;
    }

    switch (mode) {
    case HW_LORA_RADIO_LOW_POWER_SLEEP_WARM:
        status = SMTC_SW_PLATFORM(ral_set_sleep(&radio->ral, true));
        break;
    case HW_LORA_RADIO_LOW_POWER_SLEEP_COLD:
        status = SMTC_SW_PLATFORM(ral_set_sleep(&radio->ral, false));
        break;
    case HW_LORA_RADIO_LOW_POWER_STANDBY_RC:
        status = SMTC_SW_PLATFORM(ral_set_standby(&radio->ral, RAL_STANDBY_CFG_RC));
        break;
    case HW_LORA_RADIO_LOW_POWER_STANDBY_XOSC:
        status = SMTC_SW_PLATFORM(ral_set_standby(&radio->ral, RAL_STANDBY_CFG_XOSC));
        break;
    default:
        return -EINVAL;
    }

    if (status != RAL_STATUS_OK) {
        return -EIO;
    }

#if IS_ENABLED(CONFIG_REGULATOR) && DT_NODE_HAS_STATUS(DT_NODELABEL(lora_ant_sw), okay)
    if (device_is_ready(lora_ant_sw)) {
        ret = regulator_disable(lora_ant_sw);
        if ((ret != 0) && (ret != -ENOTSUP)) {
            return ret;
        }
    }
#endif

    return 0;
}

int hw_lora_p2p_params_set(const char *param)
{
    char buf[80];
    char *parts[6];
    uint32_t freq_hz;
    uint32_t sf;
    uint32_t cr;
    uint32_t preamble;
    uint32_t tx_power;
    enum p2p_bw_id bw;

    if (param == NULL) {
        return -EINVAL;
    }
    if ((strlen(param) == 0U) || (strlen(param) >= sizeof(buf))) {
        return -EINVAL;
    }

    snprintf(buf, sizeof(buf), "%s", param);
    if (!split_p2p_param_string(buf, parts, ARRAY_SIZE(parts))) {
        return -EINVAL;
    }

    if (!parse_u32_strict(parts[0], 150000000U, 960000000U, &freq_hz)) {
        return -EINVAL;
    }
    if (!parse_u32_strict(parts[1], 6U, 12U, &sf)) {
        return -EINVAL;
    }
    if (!parse_bw_token(parts[2], &bw)) {
        return -EINVAL;
    }
    if (!parse_u32_strict(parts[3], 0U, 3U, &cr)) {
        return -EINVAL;
    }
    if (!parse_u32_strict(parts[4], 2U, 65535U, &preamble)) {
        return -EINVAL;
    }
    if (!parse_u32_strict(parts[5], 5U, 22U, &tx_power)) {
        return -EINVAL;
    }

    k_mutex_lock(&p2p_lock, K_FOREVER);
    if (tx_in_progress || cw_in_progress || pending_tx.pending || (p2p_rx.mode != P2P_RX_MODE_OFF) || p2p_rx.in_progress) {
        k_mutex_unlock(&p2p_lock);
        return -EBUSY;
    }
    p2p.freq_hz = freq_hz;
    p2p.sf = (uint8_t)sf;
    p2p.bw = bw;
    p2p.cr = (uint8_t)cr;
    p2p.preamble = (uint16_t)preamble;
    p2p.tx_dbm = (int8_t)tx_power;
    k_mutex_unlock(&p2p_lock);
    return 0;
}

int hw_lora_p2p_params_format(char *out, size_t out_len)
{
    struct p2p_params local;

    if ((out == NULL) || (out_len == 0U)) {
        return -EINVAL;
    }
    k_mutex_lock(&p2p_lock, K_FOREVER);
    memcpy(&local, &p2p, sizeof(local));
    k_mutex_unlock(&p2p_lock);
    if (snprintf(out, out_len, "%u:%u:%s:%u:%u:%d", local.freq_hz, local.sf, bw_to_string(local.bw), local.cr,
             local.preamble, (int)local.tx_dbm) >= out_len) {
        return -ENOSPC;
    }
    return 0;
}

int hw_lora_p2p_send_payload(const uint8_t *data, size_t len)
{
    struct p2p_params local;
    smtc_rac_return_code_t rc;

    if (!rac_us_ready || (tx_ctx == NULL) || (radio_access_id == RAC_INVALID_RADIO_ID)) {
        return -ENODEV;
    }
    if ((data == NULL) || (len == 0U) || (len > sizeof(tx_buf))) {
        return -EINVAL;
    }

    k_mutex_lock(&p2p_lock, K_FOREVER);
    if (tx_in_progress || cw_in_progress || pending_tx.pending || p2p_rx.pending_reconfigure) {
        k_mutex_unlock(&p2p_lock);
        return -EBUSY;
    }

    memcpy(&local, &p2p, sizeof(local));
    memcpy(tx_buf, data, len);
    if (p2p_rx.in_progress) {
        if (!p2p_rx_mode_allows_tx(p2p_rx.mode)) {
            k_mutex_unlock(&p2p_lock);
            return -EBUSY;
        }

        pending_tx.params = local;
        pending_tx.len = len;
        pending_tx.pending = true;
        p2p_rx.resume_after_tx = true;

        rc = SMTC_SW_PLATFORM(smtc_rac_abort_radio_submit(radio_access_id));
        if (rc != SMTC_RAC_SUCCESS) {
            pending_tx.pending = false;
            p2p_rx.resume_after_tx = false;
            k_mutex_unlock(&p2p_lock);
            LOG_ERR("smtc_rac_abort_radio_submit: %d", (int)rc);
            return -EIO;
        }
        k_mutex_unlock(&p2p_lock);
        return 0;
    }

    rc = p2p_submit_tx_locked(&local, len);
    k_mutex_unlock(&p2p_lock);
    return rc;
}

uint16_t hw_lora_p2p_recv_get(void)
{
    uint16_t value;

    k_mutex_lock(&p2p_lock, K_FOREVER);
    value = p2p_rx.configured_time_ms;
    k_mutex_unlock(&p2p_lock);
    return value;
}

int hw_lora_p2p_recv_set(uint16_t time_ms)
{
    smtc_rac_return_code_t rc;
    int ret;

    if (!rac_us_ready || (tx_ctx == NULL) || (radio_access_id == RAC_INVALID_RADIO_ID)) {
        return -ENODEV;
    }

    k_mutex_lock(&p2p_lock, K_FOREVER);
    if (tx_in_progress || cw_in_progress || pending_tx.pending || p2p_rx.pending_reconfigure) {
        k_mutex_unlock(&p2p_lock);
        return -EBUSY;
    }

    if (p2p_rx_mode_is_continuous_locked(p2p_rx.mode) && (time_ms != P2P_PRECV_STOP)) {
        k_mutex_unlock(&p2p_lock);
        return -EBUSY;
    }

    if (p2p_rx.in_progress) {
        p2p_rx.pending_reconfigure = true;
        p2p_rx.pending_time_ms = time_ms;
        rc = SMTC_SW_PLATFORM(smtc_rac_abort_radio_submit(radio_access_id));
        if (rc != SMTC_RAC_SUCCESS) {
            p2p_rx.pending_reconfigure = false;
            k_mutex_unlock(&p2p_lock);
            LOG_ERR("smtc_rac_abort_radio_submit: %d", (int)rc);
            return -EIO;
        }
        k_mutex_unlock(&p2p_lock);
        return 0;
    }

    ret = p2p_submit_rx_locked(time_ms);
    k_mutex_unlock(&p2p_lock);
    return ret;
}

int hw_lora_cw_format(char *out, size_t out_len)
{
    struct cw_params local;

    if ((out == NULL) || (out_len == 0U)) {
        return -EINVAL;
    }

    k_mutex_lock(&p2p_lock, K_FOREVER);
    memcpy(&local, &cw, sizeof(local));
    k_mutex_unlock(&p2p_lock);

    if (snprintf(out, out_len, "%u:%d:%u", local.freq_hz, (int)local.tx_dbm, (unsigned int)local.time_ms) >= out_len) {
        return -ENOSPC;
    }

    return 0;
}

int hw_lora_cw_start(const char *param)
{
    char buf[48];
    char *parts[3];
    uint32_t freq_hz;
    uint32_t tx_power;
    uint32_t time_ms;
    int ret;

    if (!rac_us_ready || (tx_ctx == NULL) || (radio_access_id == RAC_INVALID_RADIO_ID)) {
        return -ENODEV;
    }
    if (param == NULL) {
        return -EINVAL;
    }
    if ((strlen(param) == 0U) || (strlen(param) >= sizeof(buf))) {
        return -EINVAL;
    }

    snprintf(buf, sizeof(buf), "%s", param);
    if (!split_p2p_param_string(buf, parts, ARRAY_SIZE(parts))) {
        return -EINVAL;
    }

    if (!parse_u32_strict(parts[0], 150000000U, 960000000U, &freq_hz)) {
        return -EINVAL;
    }
    if (!parse_u32_strict(parts[1], 5U, 22U, &tx_power)) {
        return -EINVAL;
    }
    if (!parse_u32_strict(parts[2], 0U, 65535U, &time_ms)) {
        return -EINVAL;
    }

    k_mutex_lock(&p2p_lock, K_FOREVER);
    if (tx_in_progress || cw_in_progress || pending_tx.pending || p2p_rx.in_progress || p2p_rx.pending_reconfigure ||
        (p2p_rx.mode != P2P_RX_MODE_OFF)) {
        k_mutex_unlock(&p2p_lock);
        return -EBUSY;
    }

    cw.freq_hz = freq_hz;
    cw.tx_dbm = (int8_t)tx_power;
    cw.time_ms = (uint16_t)time_ms;

    ret = cw_submit_locked(&cw);
    if (ret == 0) {
        (void)k_work_cancel_delayable(&cw_stop_work);
        if (cw.time_ms > 0U) {
            (void)k_work_schedule(&cw_stop_work, K_MSEC(cw.time_ms));
        }
    }
    k_mutex_unlock(&p2p_lock);

    return ret;
}
