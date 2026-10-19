#include <stdint.h>
#include <string.h>
#include "rom/ecdsa.h"
#include "uECC.h"
#include "esp_rom_ecdsa_mock.h"

/* The ROM's ECDSA verify as ESP-IDF documents it, over micro-ecc: the key is
 * X then Y and the signature r then s, each 32 bytes little-endian
 * (esp32p4/rom/secure_boot.h:100,102), and a good signature answers 1. Bytes
 * the caller left big-endian are another key and another signature here, so
 * they fail, as they would on the chip. */

static int      answer;
static int      answering;
static unsigned calls;
static int      last_curve;
static uint8_t  last_digest[ETS_DIGEST_SHA256_LEN];

void tfw_rom_ecdsa_mock_reset(void)
{
    answering  = 0;
    calls      = 0u;
    last_curve = 0;
    memset(last_digest, 0, sizeof last_digest);
}

void tfw_rom_ecdsa_mock_answer(int ret)
{
    answer    = ret;
    answering = 1;
}

unsigned tfw_rom_ecdsa_mock_calls(void) { return calls; }
int tfw_rom_ecdsa_mock_curve(void) { return last_curve; }
const uint8_t *tfw_rom_ecdsa_mock_digest(void) { return last_digest; }

/* A little-endian 32-byte number as SEC 1 writes it, most significant first. */
static void be32(uint8_t *out, const uint8_t *le)
{
    unsigned i;

    for (i = 0u; i < 32u; i++) {
        out[i] = le[31u - i];
    }
}

int ets_ecdsa_verify(const uint8_t *key, const uint8_t *sig, ECDSA_CURVE curve_id,
                     const uint8_t *digest, uint8_t *verified_digest)
{
    uint8_t pub[64];
    uint8_t rs[64];

    calls++;
    last_curve = (int)curve_id;
    memcpy(last_digest, digest, sizeof last_digest);
    if (answering) {
        return answer;
    }
    if (curve_id != ECDSA_CURVE_P256) {
        return 0;
    }
    be32(&pub[0], &key[0]);
    be32(&pub[32], &key[32]);
    be32(&rs[0], &sig[0]);
    be32(&rs[32], &sig[32]);
    if (uECC_verify(pub, digest, ETS_DIGEST_SHA256_LEN, rs, uECC_secp256r1()) != 1) {
        return 0;
    }
    memcpy(verified_digest, digest, ETS_DIGEST_SHA256_LEN);
    return 1;
}
