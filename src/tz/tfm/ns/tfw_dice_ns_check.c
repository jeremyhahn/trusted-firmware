/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The non-secure side's first call into the DICE partition (#608 slice 3):
 * it asks for the DeviceID public key through the NS client
 * (src/tz/tz_s_client.c over psa_call) and prints one line,
 * "tfw-dice: deviceid ok" or "tfw-dice: status <n>", which
 * test/integration/cortex-m/qemu_tfm_test.sh looks for. It then fetches the
 * alias evidence (alias key, the alias certificate over a fixed nonce, an
 * alias signature over a fixed digest), prints each as hex and ends with
 * "tfw-dice: alias ok", or "tfw-dice: alias status <op> <n>" at the first
 * step that failed; the gate checks the hex with xkms/cmd/tfm-dice-check.
 *
 * It rides in tf-m-extras' example non-secure application without a change
 * to that application's source: scripts/tfm-build.sh adds this file and the
 * client to its tfm_ns target and links it with -Wl,--wrap=psa_framework_version,
 * so the application's psa_framework_version() call, made after
 * stdio_init() and its "Non-Secure system starting..." line, comes here
 * first and then goes on to the real one.
 */
#include <stdint.h>
#include <stdio.h>
#include "psa/client.h"
#include "tfw/tz_s_api.h"

uint32_t __real_psa_framework_version(void);
uint32_t __wrap_psa_framework_version(void);

/* An all-zero key is no key: the partition never answers one. */
static int key_is_set(const uint8_t pub[64])
{
    uint8_t acc = 0;
    for (size_t i = 0; i < 64; i++) {
        acc |= pub[i];
    }
    return acc != 0;
}

static void put_hex(const char *label, const uint8_t *b, size_t n)
{
    printf("tfw-dice: %s ", label);
    for (size_t i = 0; i < n; i++) {
        printf("%02x", b[i]);
    }
    printf("\r\n");
}

static void alias_status(const char *op, int st)
{
    printf("tfw-dice: alias status %s %d\r\n", op, st);
}

/* The alias certificate over nonce, page by page into cert: its length, or 0
 * after printing the failed step. The first page's total is the length; a
 * total of 0, one past the buffer, one that changes between pages or an
 * empty page is refused, so the loop always ends. */
static size_t fetch_alias_cert(const uint8_t nonce[32], uint8_t *cert, size_t cap)
{
    uint32_t off = 0, total = 0;

    do {
        size_t   got = 0;
        uint32_t t   = 0;
        int st = (int)tfw_s_dice_alias_cert(nonce, off, cert + off, cap - off, &got, &t);

        if (st != (int)TFW_OK) {
            alias_status("cert", st);
            return 0;
        }
        if (off == 0) {
            total = t;
            if (total == 0 || total > cap) {
                alias_status("cert-total", (int)total);
                return 0;
            }
        } else if (t != total) {
            alias_status("cert-total", (int)t);
            return 0;
        }
        if (got == 0 || got > total - off) {
            alias_status("cert-page", (int)got);
            return 0;
        }
        off += (uint32_t)got;
    } while (off < total);
    return total;
}

#define TFW_DICE_NS_CERT_MAX 1024u

static void tfw_dice_ns_check(void)
{
    static uint8_t cert[TFW_DICE_NS_CERT_MAX];
    uint8_t pub[64]   = {0};
    uint8_t alias[64] = {0};
    uint8_t sig[64]   = {0};
    uint8_t nonce[32];
    uint8_t digest[32];
    size_t  cert_len;
    int st = (int)tfw_s_dice_deviceid_pub(pub);

    if (st == (int)TFW_OK && key_is_set(pub)) {
        printf("tfw-dice: deviceid ok\r\n");
    } else if (st == (int)TFW_OK) {
        printf("tfw-dice: status ok, empty key\r\n");
        return;
    } else {
        printf("tfw-dice: status %d\r\n", st);
        return;
    }
    put_hex("deviceid-pub", pub, sizeof pub);

    /* The fixed challenge test/integration/cortex-m/qemu_tfm_test.sh hands
     * xkms/cmd/tfm-dice-check: nonce 00..1f, digest 20..3f. */
    for (size_t i = 0; i < sizeof nonce; i++) {
        nonce[i]  = (uint8_t)i;
        digest[i] = (uint8_t)(0x20u + i);
    }

    st = (int)tfw_s_dice_alias_pub(alias);
    if (st != (int)TFW_OK) {
        alias_status("pub", st);
        return;
    }
    put_hex("alias-pub", alias, sizeof alias);

    cert_len = fetch_alias_cert(nonce, cert, sizeof cert);
    if (cert_len == 0) {
        return;
    }
    put_hex("alias-cert", cert, cert_len);

    st = (int)tfw_s_dice_alias_sign(digest, sig);
    if (st != (int)TFW_OK) {
        alias_status("sign", st);
        return;
    }
    put_hex("alias-sig", sig, sizeof sig);
    printf("tfw-dice: alias ok\r\n");
}

uint32_t __wrap_psa_framework_version(void)
{
    static int done;

    if (!done) {
        done = 1;
        tfw_dice_ns_check();
    }
    return __real_psa_framework_version();
}
