/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * boot/hal/cortex-m/recovery_halt.c, unchanged (#628): with BOOT_OTA=0 a
 * refusing Cortex-M tfw-boot masks interrupts, sends the reason as one EVENT
 * frame, then sends the same EVENT again for every whole frame that arrives,
 * never answers one as a command, and never returns. A requested session
 * returns at once, touching nothing.
 *
 * test/model/cortex-m/cm_regs.h sends tfw_cm_irq_off() to
 * tfw_imx95_model_irq_mask(), which this suite defines to record the mask;
 * tfw_imx95_model_reg() fails the case, as the halt touches no register. The
 * boot UART is this suite's too: tx appends to a capture (failing if the mask
 * is not yet set), and rx hands out a scripted byte list, then leaves the
 * endless loop by longjmp, the only way out a test has.
 */
#include <setjmp.h>
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "frame.h"
#include "boot_hal.h"
#include "boot_recovery.h"

#define EVENT_LEN (3u + TFW_FRAME_OVERHEAD)

static uint32_t primask;
static unsigned mask_calls;
static uint8_t  cap[256];
static unsigned ncap;
static uint8_t  script[128];
static unsigned nscript, script_at;
static jmp_buf  out;

volatile uint32_t *tfw_imx95_model_reg(uintptr_t addr)
{
    static volatile uint32_t sink;

    (void)addr;
    TEST_FAIL_MESSAGE("recovery_halt.c touched a register");
    return &sink;
}

uint32_t tfw_imx95_model_irq_save(void)
{
    TEST_FAIL_MESSAGE("recovery_halt.c saved the mask");
    return 0u;
}

void tfw_imx95_model_irq_restore(uint32_t was)
{
    (void)was;
    TEST_FAIL_MESSAGE("recovery_halt.c restored the mask");
}

void tfw_imx95_model_irq_mask(uint32_t masked)
{
    primask = masked;
    mask_calls++;
}

void tfw_boot_uart_tx(uint8_t byte)
{
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, primask, "a byte went out with interrupts unmasked");
    TEST_ASSERT_TRUE_MESSAGE(ncap < sizeof cap, "capture full");
    cap[ncap++] = byte;
}

uint8_t tfw_boot_uart_rx(uint8_t *b)
{
    if (script_at == nscript) {
        longjmp(out, 1);
    }
    *b = script[script_at++];
    return 1u;
}

static void feed(const uint8_t *b, unsigned n)
{
    TEST_ASSERT_TRUE(nscript + n <= sizeof script);
    memcpy(&script[nscript], b, n);
    nscript += n;
}

static void feed_ping(uint8_t seq)
{
    uint8_t  f[16];
    uint16_t n = tfw_frame_encode_request(f, (uint16_t)sizeof f, seq, TFW_CMD_PING, 0, 0u);

    TEST_ASSERT_NOT_EQUAL(0u, n);
    feed(f, n);
}

static void halt(tfw_status_t reason)
{
    tfw_boot_platform_t plat;

    memset(&plat, 0, sizeof plat);
    if (setjmp(out) == 0) {
        tfw_boot_recovery(&plat, reason);
    }
    TEST_ASSERT_EQUAL_UINT_MESSAGE(nscript, script_at, "the halt stopped reading");
}

/* Decodes the capture; every frame in it must be an EVENT carrying reason. */
static unsigned events_in_capture(tfw_status_t reason)
{
    tfw_framer_t f;
    unsigned     i, n = 0u;

    tfw_framer_init(&f);
    for (i = 0u; i < ncap; i++) {
        tfw_frame_event_t e = tfw_framer_push(&f, cap[i]);

        TEST_ASSERT_TRUE_MESSAGE(e == TFW_FRAME_NONE || e == TFW_FRAME_READY, "capture is not clean frames");
        if (e == TFW_FRAME_READY) {
            TEST_ASSERT_EQUAL_HEX8_MESSAGE(TFW_OP_EVENT, tfw_frame_op(&f), "a frame other than an EVENT");
            TEST_ASSERT_EQUAL_INT(reason, tfw_frame_status(&f));
            TEST_ASSERT_EQUAL_UINT8(0u, tfw_frame_payload_len(&f));
            n++;
        }
    }
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0u, tfw_framer_in_frame(&f), "a partial frame at the end");
    return n;
}

/* Every EVENT is the same bytes as the first. */
static void assert_identical_events(unsigned n)
{
    unsigned i;

    TEST_ASSERT_EQUAL_UINT(n * EVENT_LEN, ncap);
    for (i = 1u; i < n; i++) {
        TEST_ASSERT_EQUAL_HEX8_ARRAY(cap, &cap[i * EVENT_LEN], EVENT_LEN);
    }
}

void setUp(void)
{
    primask = 0u;
    mask_calls = 0u;
    ncap = nscript = script_at = 0u;
    memset(cap, 0, sizeof cap);
}

void tearDown(void) {}

/* (1) Entry with no input sends one EVENT whose status is the reason. */
static void test_entry_sends_one_event_with_the_reason(void)
{
    halt(TFW_ERR_CRYPTO);
    TEST_ASSERT_EQUAL_UINT(1u, events_in_capture(TFW_ERR_CRYPTO));
    assert_identical_events(1u);

    setUp();
    halt(TFW_ERR_NOT_PROVISIONED);
    TEST_ASSERT_EQUAL_UINT(1u, events_in_capture(TFW_ERR_NOT_PROVISIONED));
    assert_identical_events(1u);
}

/* (2) Interrupts are masked, once, before the first byte goes out (tx checks
 * the mask on every byte), and never unmasked. */
static void test_mask_is_set_before_the_first_byte(void)
{
    halt(TFW_ERR_CRYPTO);
    TEST_ASSERT_EQUAL_UINT(1u, mask_calls);
    TEST_ASSERT_EQUAL_UINT32(1u, primask);
    TEST_ASSERT_NOT_EQUAL(0u, ncap);
}

/* (3) Each whole request frame adds one identical EVENT, never a response:
 * one PING, one more; two PINGs, two more. */
static void test_each_whole_frame_adds_one_identical_event(void)
{
    feed_ping(0x11u);
    halt(TFW_ERR_CRYPTO);
    TEST_ASSERT_EQUAL_UINT(2u, events_in_capture(TFW_ERR_CRYPTO));
    assert_identical_events(2u);

    setUp();
    feed_ping(0x11u);
    feed_ping(0x12u);
    halt(TFW_ERR_NOT_PROVISIONED);
    TEST_ASSERT_EQUAL_UINT(3u, events_in_capture(TFW_ERR_NOT_PROVISIONED));
    assert_identical_events(3u);
}

/* (4) Noise and a frame with a bad CRC add nothing; a good frame after them
 * is still heard. */
static void test_noise_and_a_bad_crc_add_nothing(void)
{
    static const uint8_t noise[] = { 0x00u, 0x55u, 0xFFu, 0x13u, 0xC3u };
    uint8_t  f[16];
    uint16_t n = tfw_frame_encode_request(f, (uint16_t)sizeof f, 0x21u, TFW_CMD_PING, 0, 0u);

    TEST_ASSERT_NOT_EQUAL(0u, n);
    f[n - 1u] ^= 0x5Au;
    feed(noise, sizeof noise);
    feed(f, n);
    halt(TFW_ERR_CRYPTO);
    TEST_ASSERT_EQUAL_UINT(1u, events_in_capture(TFW_ERR_CRYPTO));
    assert_identical_events(1u);

    setUp();
    feed(noise, sizeof noise);
    feed(f, n);
    feed_ping(0x22u);
    halt(TFW_ERR_CRYPTO);
    TEST_ASSERT_EQUAL_UINT(2u, events_in_capture(TFW_ERR_CRYPTO));
}

/* (5) A requested session returns at once: nothing sent, the mask alone. */
static void test_session_open_returns_at_once(void)
{
    tfw_boot_platform_t             plat;
    const tfw_boot_session_limits_t lim = { 1u, 2u, 3u, 4u };

    memset(&plat, 0xA5, sizeof plat);
    tfw_boot_session_open(&plat, &lim);
    tfw_boot_session_open(NULL, NULL);
    TEST_ASSERT_EQUAL_UINT(0u, ncap);
    TEST_ASSERT_EQUAL_UINT(0u, mask_calls);
    TEST_ASSERT_EQUAL_UINT32(0u, primask);
    TEST_ASSERT_EACH_EQUAL_HEX8(0xA5u, &plat, sizeof plat);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_entry_sends_one_event_with_the_reason);
    RUN_TEST(test_mask_is_set_before_the_first_byte);
    RUN_TEST(test_each_whole_frame_adds_one_identical_event);
    RUN_TEST(test_noise_and_a_bad_crc_add_nothing);
    RUN_TEST(test_session_open_returns_at_once);
    return UNITY_END();
}
