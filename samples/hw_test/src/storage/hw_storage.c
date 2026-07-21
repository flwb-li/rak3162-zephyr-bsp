#include "storage/hw_storage.h"

#include <stddef.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>

#define HW_SETTINGS_ROOT "hwtest"
#define HW_SETTINGS_ACTIVE_CFG_KEY HW_SETTINGS_ROOT "/active_cfg"

struct hw_storage_state {
    struct hw_runtime_cfg active_cfg;
    struct hw_runtime_cfg pending_cfg;
    bool pending_valid;
};

static struct hw_storage_state storage_state;

static void sanitize_cfg(struct hw_runtime_cfg *cfg)
{
    if (cfg != NULL) {
        cfg->sn[HW_SN_LEN] = '\0';
    }
}

static int save_active_cfg(const struct hw_runtime_cfg *cfg)
{
    return settings_save_one(HW_SETTINGS_ACTIVE_CFG_KEY, cfg, sizeof(*cfg));
}

static void clear_pending_cache(void)
{
    memset(&storage_state.pending_cfg, 0, sizeof(storage_state.pending_cfg));
    storage_state.pending_valid = false;
}

static int hw_settings_set(const char *name, size_t len_rd, settings_read_cb read_cb, void *cb_arg)
{
    ARG_UNUSED(len_rd);

    if (strcmp(name, "active_cfg") == 0) {
        ssize_t n = read_cb(cb_arg, &storage_state.active_cfg, sizeof(storage_state.active_cfg));

        if (n < 0) {
            return (int)n;
        }
        /* read_cb 成功时返回读到的字节数，不是 0；必须与结构体大小一致 */
        if ((size_t)n != sizeof(storage_state.active_cfg)) {
            memset(&storage_state.active_cfg, 0, sizeof(storage_state.active_cfg));
        }
        return 0;
    }

    return 0;
}

static int hw_settings_commit(void)
{
    sanitize_cfg(&storage_state.active_cfg);
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(hwtest_cfg, HW_SETTINGS_ROOT, NULL, hw_settings_set, hw_settings_commit, NULL);

int hw_storage_set_pending_cfg(const struct hw_runtime_cfg *cfg)
{
    if (cfg == NULL) {
        return -EINVAL;
    }

    storage_state.pending_cfg = *cfg;
    sanitize_cfg(&storage_state.pending_cfg);
    storage_state.pending_valid = true;
    return 0;
}

int hw_storage_apply_pending_cfg(void)
{
    if (!storage_state.pending_valid) {
        return 0;
    }

    storage_state.active_cfg = storage_state.pending_cfg;
    sanitize_cfg(&storage_state.active_cfg);
    clear_pending_cache();

    return save_active_cfg(&storage_state.active_cfg);
}

void hw_storage_get_active_cfg(struct hw_runtime_cfg *cfg)
{
    if (cfg != NULL) {
        *cfg = storage_state.active_cfg;
        sanitize_cfg(cfg);
    }
}

void hw_storage_get_pending_cfg(struct hw_runtime_cfg *cfg, bool *valid)
{
    if (cfg != NULL) {
        *cfg = storage_state.pending_cfg;
        sanitize_cfg(cfg);
    }

    if (valid != NULL) {
        *valid = storage_state.pending_valid;
    }
}

int hw_storage_init(void)
{
    int ret;

    memset(&storage_state, 0, sizeof(storage_state));

    ret = settings_subsys_init();
    if ((ret != 0) && (ret != -EALREADY)) {
        return ret;
    }

    ret = settings_load_subtree(HW_SETTINGS_ROOT);
    if (ret != 0) {
        return ret;
    }

    sanitize_cfg(&storage_state.active_cfg);
    sanitize_cfg(&storage_state.pending_cfg);
    return 0;
}
