#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "boot_verify.h"
#include "boot_verify_mock.h"

const uint8_t tfw_boot_cose_alg = 0x26u;

static const uint8_t *want;
static uint8_t        last[128];
static uint16_t       last_len;
static uint8_t        last_keyless;
static tfw_status_t   pubkey_st = TFW_OK;

void tfw_boot_verify_mock_want(const uint8_t *sig)
{
    want = sig;
}

const uint8_t *tfw_boot_verify_mock_msg(uint16_t *len)
{
    *len = last_len;
    return last;
}

void tfw_boot_verify_mock_pubkey(tfw_status_t st)
{
    pubkey_st = st;
}

uint8_t tfw_boot_verify_mock_keyless(void)
{
    return last_keyless;
}

tfw_status_t tfw_boot_pubkey(uint8_t out[TFW_BOOT_PUBKEY_LEN])
{
    memset(out, 0, TFW_BOOT_PUBKEY_LEN);
    return pubkey_st;
}

tfw_status_t tfw_boot_verify(const uint8_t *msg, uint16_t msglen,
                           const uint8_t sig[TFW_BOOT_SIG_LEN],
                           const uint8_t pub[TFW_BOOT_PUBKEY_LEN])
{
    last_keyless = (uint8_t)(pub == NULL);
    last_len = 0u;
    if (msglen <= sizeof last) {
        memcpy(last, msg, msglen);
        last_len = msglen;
    }
    if (want != NULL && memcmp(sig, want, TFW_BOOT_SIG_LEN) != 0) {
        return TFW_ERR_CRYPTO;
    }
    return TFW_OK;
}
