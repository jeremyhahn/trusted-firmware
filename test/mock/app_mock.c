/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
#include <string.h>
#include "app_mock.h"
#include "hal_mock.h"
#include "tfw/protocol.h"

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
static tfw_status_t after_update_status;
static tfw_ms_t     run_ms;
static tfw_ms_t     last_now;
static tfw_status_t evidence_status;
static uint8_t      evidence[TFW_APP_EVIDENCE_LEN];
static tfw_status_t command_status;
static uint8_t      command_reply[TFW_MAX_PAYLOAD];
static uint8_t      command_reply_len;
static uint8_t      command_reads;
static uint8_t      command_reads_set;
static tfw_ms_t     command_ms;
static uint8_t      command_op;
static uint8_t      command_args[TFW_MAX_PAYLOAD];
static uint8_t      command_args_len;

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
    after_update_status = TFW_OK;
    run_ms      = 0u;
    last_now    = 0u;
    evidence_status = TFW_ERR_UNSUPPORTED;
    memset(evidence, 0, sizeof evidence);
    command_status    = TFW_OK;
    command_reply_len = 0u;
    command_reads     = 0u;
    command_reads_set = 0u;
    command_ms        = 0u;
    command_op        = 0u;
    command_args_len  = 0u;
    memset(command_reply, 0, sizeof command_reply);
    memset(command_args, 0, sizeof command_args);
}

tfw_app_descriptor_t *tfw_mock_app_descriptor(void) { return &desc; }
void        tfw_mock_app_set_init(tfw_status_t st)   { init_status = st; }
void        tfw_mock_app_set_after_update(tfw_status_t st) { after_update_status = st; }
void        tfw_mock_app_set_run_ms(tfw_ms_t ms)     { run_ms = ms; }
const char *tfw_mock_app_log(void)                   { return log_buf; }
tfw_ms_t    tfw_mock_app_last_now(void)              { return last_now; }

void tfw_mock_app_set_evidence(tfw_status_t st, const uint8_t ev[TFW_APP_EVIDENCE_LEN])
{
    evidence_status = st;
    memcpy(evidence, ev, sizeof evidence);
}

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
    return after_update_status;
}

/* Writes the bytes whatever it answers, so a test sees whether a refusal's
 * scribble reaches the caller. */
tfw_status_t tfw_app_evidence(uint8_t out[TFW_APP_EVIDENCE_LEN])
{
    record('e');
    memcpy(out, evidence, TFW_APP_EVIDENCE_LEN);
    return evidence_status;
}

void tfw_mock_app_set_command(tfw_status_t st, const uint8_t *reply, uint8_t len)
{
    command_status = st;
    if (len > sizeof command_reply) {
        len = (uint8_t)sizeof command_reply;
    }
    if (len != 0u) {
        memcpy(command_reply, reply, len);
    }
    command_reply_len = len;
}

void tfw_mock_app_set_command_reads(uint8_t n)
{
    command_reads     = n;
    command_reads_set = 1u;
}

void           tfw_mock_app_set_command_ms(tfw_ms_t ms) { command_ms = ms; }
uint8_t        tfw_mock_app_command_op(void)            { return command_op; }
uint8_t        tfw_mock_app_command_args_len(void)      { return command_args_len; }
const uint8_t *tfw_mock_app_command_args(void)          { return command_args; }

/* Reads its arguments (all that remain unless set_command_reads said n, which
 * may run past them: the reader's overrun is the dispatcher's to catch), then
 * writes the reply whatever it answers. */
tfw_status_t tfw_app_command(uint8_t op, tfw_reader_t *r, tfw_writer_t *w)
{
    uint8_t n = command_reads_set ? command_reads : tfw_reader_remaining(r);
    uint8_t i;

    record('c');
    command_op       = op;
    command_args_len = 0u;
    for (i = 0u; i < n && command_args_len < sizeof command_args; i++) {
        command_args[command_args_len++] = tfw_read_u8(r);
    }
    tfw_write_blob(w, command_reply, command_reply_len);
    tfw_mock_advance(command_ms);
    return command_status;
}
