/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
#include <string.h>
#include "app_mock.h"
#include "hal_mock.h"

#define LOG_CAP 64u

/* The runner reads tfw_app_descriptor as const, which is right for every real
 * application and leaves a test no way to vary it. So the symbol is an alias of
 * a writable object: the runner's view is const, the storage is not, and no
 * const object is ever written. GCC and Clang on ELF, which is the host build. */
static tfw_app_descriptor_t desc;
extern const tfw_app_descriptor_t tfw_app_descriptor __attribute__((alias("desc")));

static char         log_buf[LOG_CAP + 1u];
static unsigned     log_len;
static tfw_status_t init_status;
static tfw_ms_t     run_ms;
static tfw_ms_t     last_now;

static void record(char c)
{
    if (log_len < LOG_CAP) {
        log_buf[log_len++] = c;
        log_buf[log_len]   = '\0';
    }
}

void tfw_mock_app_reset(void)
{
    memset(&desc, 0, sizeof desc);
    desc.magic      = TFW_APP_MAGIC;
    desc.abi        = TFW_APP_ABI;
    desc.ota_slots  = TFW_APP_OTA_NONE;
    desc.config_len = 16u;
    memcpy(desc.name, "mock", 5u);

    memset(log_buf, 0, sizeof log_buf);
    log_len     = 0u;
    init_status = TFW_OK;
    run_ms      = 0u;
    last_now    = 0u;
}

tfw_app_descriptor_t *tfw_mock_app_descriptor(void) { return &desc; }
void        tfw_mock_app_set_init(tfw_status_t st)   { init_status = st; }
void        tfw_mock_app_set_run_ms(tfw_ms_t ms)     { run_ms = ms; }
const char *tfw_mock_app_log(void)                   { return log_buf; }
tfw_ms_t    tfw_mock_app_last_now(void)              { return last_now; }

tfw_status_t tfw_app_init(void)
{
    record('i');
    return init_status;
}

void tfw_app_run(tfw_ms_t now_ms)
{
    record('r');
    last_now = now_ms;
    tfw_mock_advance(run_ms);
}

void tfw_app_shutdown(void)      { record('s'); }
void tfw_app_before_update(void) { record('b'); }

tfw_status_t tfw_app_after_update(void)
{
    record('a');
    return TFW_OK;
}
