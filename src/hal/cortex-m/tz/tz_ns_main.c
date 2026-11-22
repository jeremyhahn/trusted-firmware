/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The TZ=1 non-secure image (#590): it calls the secure world through the
 * tfw_s_version veneer and prints what it returns, then reads the first word
 * of Secure SRAM, which the SAU makes a SecureFault the secure handler
 * reports (test/integration/cortex-m/qemu_tz_test.sh reads both lines).
 * Between the two it calls the DICE service's four veneers: the DeviceID
 * public key's SHA-256, the alias certificate's length, an alias signature
 * over a fixed digest verified here against the alias public key, and a
 * buffer in Secure SRAM, which the service must refuse.
 */
#include <stdint.h>
#include "tfw/tz_s_api.h"
#include "tz_console.h"
#include "../../../crypto/sha256.h"
#include "uECC.h"

extern uint32_t __stack;
extern uint32_t __data_load, __data_start, __data_end, __bss_start, __bss_end;

void tz_ns_reset(void);
void tz_ns_fault(void);

static void tz_puthexbytes(const uint8_t *b, uint32_t n)
{
    uint32_t i;

    for (i = 0; i < n; i++) {
        tz_putc("0123456789abcdef"[b[i] >> 4]);
        tz_putc("0123456789abcdef"[b[i] & 0xfu]);
    }
}

static void tz_ns_status(const char *what, tfw_status_t st)
{
    tz_puts("tz: ns: dice ");
    tz_puts(what);
    tz_puts(" failed ");
    tz_puthex((uint32_t)st);
    tz_puts("\r\n");
}

static void tz_ns_dice(void)
{
    static uint8_t       key[TFW_DICE_KEY_LEN];
    static uint8_t       sig[TFW_DICE_SIG_LEN];
    static uint8_t       buf[TFW_DICE_PAGE_MAX];
    static uint8_t       hash[32];
    static const uint8_t digest[TFW_DICE_DIGEST_LEN] = {
        't', 'z', '-', '5', '9', '0', ' ', 'd', 'i', 'c', 'e', ' ', 'd', 'i', 'g', 'e',
        's', 't', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    };
    tfw_dice_cert_page_t page;
    tfw_status_t         st;
    uint32_t             sum = 0u, i;

    st = tfw_dice_deviceid_pub(key, sizeof key);
    if (st != TFW_OK) {
        tz_ns_status("deviceid_pub", st);
        return;
    }
    tfw_sha256(key, (uint16_t)sizeof key, hash);
    tz_puts("tz: ns: dice deviceid sha256=");
    tz_puthexbytes(hash, sizeof hash);
    tz_puts("\r\n");

    for (i = 0; i < TFW_DICE_NONCE_LEN; i++)
        page.nonce[i] = (uint8_t)i;
    page.total = 1u;
    for (page.off = 0u; page.off < page.total; page.off += page.got) {
        page.out = buf;
        page.cap = sizeof buf;
        page.got = 0u;
        st = tfw_dice_alias_cert(&page);
        if (st != TFW_OK || page.got == 0u) {
            tz_ns_status("alias_cert", st);
            return;
        }
        sum += page.got;
    }
    tz_puts("tz: ns: dice alias cert len=");
    tz_puthex(sum);
    tz_puts("\r\n");

    st = tfw_dice_alias_pub(key, sizeof key);
    if (st != TFW_OK) {
        tz_ns_status("alias_pub", st);
        return;
    }
    st = tfw_dice_alias_sign(digest, sizeof digest, sig, sizeof sig);
    if (st != TFW_OK) {
        tz_ns_status("alias_sign", st);
        return;
    }
    tz_puts(uECC_verify(key, digest, sizeof digest, sig, uECC_secp256r1())
            ? "tz: ns: dice alias signature verified\r\n"
            : "tz: ns: dice alias signature did not verify\r\n");

    /* A buffer in Secure SRAM: TFW_NS_CHECK must refuse it before any write. */
    st = tfw_dice_deviceid_pub((uint8_t *)(uintptr_t)TZ_S_RAM_BASE, TFW_DICE_KEY_LEN);
    tz_puts(st == TFW_ERR_RANGE ? "tz: ns: dice secure pointer refused\r\n"
                                : "tz: ns: dice secure pointer accepted\r\n");
}

void tz_ns_reset(void)
{
    const uint32_t *src = &__data_load;
    uint32_t *dst;
    uint32_t v;

    for (dst = &__data_start; (uintptr_t)dst < (uintptr_t)&__data_end; )
        *dst++ = *src++;
    for (dst = &__bss_start; (uintptr_t)dst < (uintptr_t)&__bss_end; )
        *dst++ = 0u;

    tz_puts("tz: ns: up\r\n");
    v = tfw_s_version();
    tz_puts("tz: ns: tfw_s_version=");
    tz_puthex(v);
    tz_puts("\r\n");

    tz_ns_dice();

    tz_puts("tz: ns: reading secure SRAM\r\n");
    v = *(volatile const uint32_t *)(uintptr_t)TZ_S_RAM_BASE;
    tz_puts("tz: ns: secure SRAM read returned ");
    tz_puthex(v);
    tz_puts("\r\n");
    for (;;)
        __asm volatile("wfi");
}

void tz_ns_fault(void)
{
    tz_puts("tz: ns: fault\r\n");
    for (;;)
        __asm volatile("wfi");
}

__attribute__((section(".vectors"), used))
static void (*const tz_ns_vectors[16])(void) = {
    (void (*)(void))(uintptr_t)&__stack,
    tz_ns_reset,
    tz_ns_fault, tz_ns_fault, tz_ns_fault, tz_ns_fault, tz_ns_fault,
    0, 0, 0, 0,
    tz_ns_fault, tz_ns_fault, 0, tz_ns_fault, tz_ns_fault,
};
