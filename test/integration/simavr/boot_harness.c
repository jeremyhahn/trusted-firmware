/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * ag-boot on emulated silicon: the three behaviours that matter.
 *
 *   a correctly signed image boots
 *   a tampered image refuses
 *   a downgrade is refused
 *
 * Modelled on harness.c, and asking a different question from
 * test/unit/boot/test_boot_policy.c. That suite runs the decision logic on x86
 * against a flash array and proves the DECISION is right. This loads the real
 * ag-boot.elf into a real ATmega328P model, puts a real signed image in the
 * emulated flash and a real provisioned region in the emulated EEPROM, and
 * proves the thing that ships works -- LPM against the actual program memory,
 * the actual EEPROM peripheral, the polled USART at the actual baud divisor,
 * and the jump landing where the application is.
 *
 * ------------------------------------------------------------------------
 * THIS SUITE IS NOT IN `make ci`, AND THAT IS NOT A SKIP GUARD.
 *
 * There is nothing to run. `make bootloader BOARD=atmega328p` does not produce
 * an ag-boot.elf, because the only signature backend in the tree --
 * ed25519-donna -- is 72 930 bytes against a 4 096-byte boot section and needs
 * a 2 950-byte stack frame on a part with 2 048 bytes of SRAM. The link fails;
 * there is no image; this harness has no input. See boot/boot_verify.h for the
 * measurement and docs/security/bootloader.md for what the options are.
 *
 * A skip guard would report a pass for a test that did not run. This reports a
 * FAILURE and says exactly why, which is what a gate that cannot be satisfied
 * should do -- and the day a backend that fits is chosen, adding this to `ci`
 * is a one-line change and the assertions below are already written.
 * ------------------------------------------------------------------------
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <simavr/sim_avr.h>
#include <simavr/sim_elf.h>
#include <simavr/avr_uart.h>
#include <simavr/avr_eeprom.h>

#include "ag/status.h"
#include "ag/protocol.h"
#include "ag/boot.h"
#include "crc16.h"

/* ------------------------------------------------------------- plumbing */

static avr_t *avr;

#define RXCAP 4096
static uint8_t  from_device[RXCAP];
static unsigned from_device_n;

static void on_device_tx(struct avr_irq_t *irq, uint32_t value, void *param)
{
    (void)irq; (void)param;
    if (from_device_n < RXCAP) {
        from_device[from_device_n++] = (uint8_t)value;
    }
}

static avr_irq_t *uart_in;

static void run_cycles(long n)
{
    for (long i = 0; i < n; i++) { avr_run(avr); }
}

static void send(const uint8_t *b, unsigned n)
{
    for (unsigned i = 0; i < n; i++) {
        avr_raise_irq(uart_in, b[i]);
        /* One byte per ~87 us at 115200. Feeding a whole frame in one instant
         * would test a device that never exists -- and the bootloader POLLS the
         * USART rather than taking an interrupt, so a byte that arrives before
         * the previous one was read is a byte that is genuinely lost. */
        run_cycles(20000);
    }
}

static int failures;

static void check(int cond, const char *what)
{
    printf("  %-62s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond) { failures++; }
}

/* --------------------------------------------------------------- framing */

static unsigned frame_request(uint8_t *out, uint8_t seq, uint8_t cmd,
                              const uint8_t *payload, uint8_t plen)
{
    unsigned i = 0;
    uint16_t crc;
    out[i++] = AG_SOF;
    out[i++] = (uint8_t)(2u + plen);
    out[i++] = seq;
    out[i++] = cmd;
    for (uint8_t k = 0; k < plen; k++) { out[i++] = payload[k]; }
    crc = ag_crc16(&out[1], (uint16_t)(i - 1u));
    out[i++] = (uint8_t)(crc & 0xFFu);
    out[i++] = (uint8_t)(crc >> 8);
    return i;
}

/* The bootloader announces why it is in the OTA loop with an EVENT frame whose
 * STATUS carries the reason. That is how a host learns "tampered" from
 * "downgrade" without a second round trip, and it is what this harness reads to
 * tell the two refusals apart. */
static int find_event(uint8_t *status)
{
    for (unsigned i = 0; i + 6 <= from_device_n; i++) {
        if (from_device[i] != AG_SOF) { continue; }
        unsigned len = from_device[i + 1];
        if (len < 3 || len > 200) { continue; }
        unsigned total = len + 4;
        if (i + total > from_device_n) { continue; }
        uint16_t want = ag_crc16(&from_device[i + 1], (uint16_t)(len + 1));
        uint16_t got  = (uint16_t)(from_device[i + total - 2]
                                   | (from_device[i + total - 1] << 8));
        if (want != got) { continue; }
        if (from_device[i + 3] != AG_OP_EVENT) { continue; }
        *status = from_device[i + 4];
        return 1;
    }
    return 0;
}

static int find_response(uint8_t seq, uint8_t *status, uint8_t *payload, uint8_t *plen)
{
    for (unsigned i = 0; i + 6 <= from_device_n; i++) {
        if (from_device[i] != AG_SOF) { continue; }
        unsigned len = from_device[i + 1];
        if (len < 3 || len > 200) { continue; }
        unsigned total = len + 4;
        if (i + total > from_device_n) { continue; }
        uint16_t want = ag_crc16(&from_device[i + 1], (uint16_t)(len + 1));
        uint16_t got  = (uint16_t)(from_device[i + total - 2]
                                   | (from_device[i + total - 1] << 8));
        if (want != got) { continue; }
        if (from_device[i + 2] != seq) { continue; }
        if (!(from_device[i + 3] & AG_OP_RESPONSE_BIT)) { continue; }
        if (from_device[i + 3] == AG_OP_EVENT) { continue; }
        *status = from_device[i + 4];
        *plen   = (uint8_t)(len - 3u);
        memcpy(payload, &from_device[i + 5], *plen);
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------- image fixtures */

/*
 * The signed image and the provisioned EEPROM come from a fixture the host
 * suite generates, not from this file.
 *
 * Signing here would mean linking ed25519-donna into the harness and producing
 * the signature with the same code that verifies it -- which proves the two
 * halves agree with each other and nothing about whether either is right. The
 * fixture is written once by tools and read verbatim here, exactly as
 * testdata/vectors.h is a literal rather than a computation.
 */
static uint8_t image[AG_BOOT_APP_END];
static uint8_t eeprom_image[AG_BOOT_EE_LEN];

static int load_fixture(const char *path, uint8_t *buf, size_t cap, size_t *got)
{
    FILE *f = fopen(path, "rb");
    if (!f) { return 0; }
    *got = fread(buf, 1, cap, f);
    fclose(f);
    return 1;
}

static void install(elf_firmware_t *f, const char *elf)
{
    memset(f, 0, sizeof *f);
    if (elf_read_firmware(elf, f) != 0) {
        fprintf(stderr, "cannot read %s\n", elf);
        exit(2);
    }
    f->frequency = 16000000;
    strcpy(f->mmcu, "atmega328p");

    avr = avr_make_mcu_by_name(f->mmcu);
    if (!avr) { fprintf(stderr, "no core for %s\n", f->mmcu); exit(2); }
    avr_init(avr);
    avr_load_firmware(avr, f);

    /* The application image goes straight into the emulated program memory,
     * BELOW the bootloader the .elf just placed at AG_BOOT_APP_END. Two images
     * in one flash, which is the arrangement on a real part. */
    memcpy(avr->flash, image, AG_BOOT_APP_END);

    {
        avr_eeprom_desc_t d = { .ee = eeprom_image, .offset = AG_BOOT_EE_BASE,
                                .size = AG_BOOT_EE_LEN };
        avr_ioctl(avr, AVR_IOCTL_EEPROM_SET, &d);
    }

    uart_in = avr_io_getirq(avr, AVR_IOCTL_UART_GETIRQ('0'), UART_IRQ_INPUT);
    avr_irq_register_notify(
        avr_io_getirq(avr, AVR_IOCTL_UART_GETIRQ('0'), UART_IRQ_OUTPUT),
        on_device_tx, NULL);

    from_device_n = 0;
}

/* ---------------------------------------------------------------- tests */

/*
 * A booted application is observed by what it does, not by a flag: the
 * application announces itself with AG_EVENT_BOOT, and a PING it answers proves
 * the jump landed on running code. A bootloader that stayed in its OTA loop
 * answers PING too -- so the discriminator is CAPS, which only the application
 * implements.
 */
static void test_signed_image_boots(const char *elf)
{
    elf_firmware_t f;
    uint8_t status, out[64], outlen;
    uint8_t wire[64];
    size_t  n;

    if (!load_fixture("build/fixtures/signed.bin", image, sizeof image, &n) ||
        !load_fixture("build/fixtures/signed.eep", eeprom_image,
                      sizeof eeprom_image, &n)) {
        fprintf(stderr, "missing fixture under build/fixtures/\n");
        exit(2);
    }
    install(&f, elf);

    run_cycles(8000000);          /* measure 2 KB, verify, jump, announce */
    check(from_device_n > 0, "a signed image: the device transmits after the jump");

    from_device_n = 0;
    n = frame_request(wire, 0x31, AG_CMD_CAPS, NULL, 0);
    send(wire, (unsigned)n);
    run_cycles(400000);
    check(find_response(0x31, &status, out, &outlen) && status == AG_OK,
          "a signed image: CAPS answers, so the APPLICATION is running");
}

static void test_tampered_image_refuses(const char *elf)
{
    elf_firmware_t f;
    uint8_t status;
    size_t  n;

    if (!load_fixture("build/fixtures/signed.bin", image, sizeof image, &n) ||
        !load_fixture("build/fixtures/signed.eep", eeprom_image,
                      sizeof eeprom_image, &n)) {
        exit(2);
    }
    image[1000] ^= 0x01u;          /* one bit, in the middle of the image */
    install(&f, elf);

    run_cycles(8000000);
    check(find_event(&status) && status == AG_ERR_CRYPTO,
          "a tampered image: refused with AG_ERR_CRYPTO");

    {
        uint8_t wire[64], out[64], outlen;
        from_device_n = 0;
        n = frame_request(wire, 0x32, AG_CMD_CAPS, NULL, 0);
        send(wire, (unsigned)n);
        run_cycles(400000);
        check(find_response(0x32, &status, out, &outlen)
              && status == AG_ERR_UNKNOWN_CMD,
              "a tampered image: CAPS is unknown, so it did NOT jump");
    }
}

static void test_downgrade_refuses(const char *elf)
{
    elf_firmware_t f;
    uint8_t status;
    size_t  n;

    /* The same signed image, against a device whose recorded floor is above its
     * version. Nothing about the image is altered, so a pass here cannot be the
     * measurement check firing by accident. */
    if (!load_fixture("build/fixtures/signed.bin", image, sizeof image, &n) ||
        !load_fixture("build/fixtures/downgrade.eep", eeprom_image,
                      sizeof eeprom_image, &n)) {
        exit(2);
    }
    install(&f, elf);

    run_cycles(8000000);
    check(find_event(&status) && status == AG_ERR_PROTECTED_REGION,
          "a downgrade: refused with AG_ERR_PROTECTED_REGION");
}

int main(int argc, char **argv)
{
    const char *elf = argc > 1 ? argv[1] : "build/atmega328p/boot/ag-boot.elf";
    FILE       *probe;

    printf("simavr ag-boot integration: %s\n", elf);

    /*
     * Checked first, and reported at length, because the absence of this file
     * is not a build ordering mistake -- it is the finding.
     */
    probe = fopen(elf, "rb");
    if (!probe) {
        fprintf(stderr,
            "\nFAIL: there is no ag-boot image to test.\n\n"
            "`make bootloader BOARD=atmega328p` does not link. ed25519-donna's\n"
            "verify path measures 72 930 bytes of flash against a 4 096-byte boot\n"
            "section -- which is the ATmega328P's LARGEST BOOTSZ, so there is no\n"
            "bigger setting to select -- and its ed25519_sign_open frame alone\n"
            "needs 2 950 bytes of stack on a part with 2 048 bytes of SRAM.\n\n"
            "The signed image and provisioned-EEPROM fixtures this harness reads\n"
            "are absent for the same reason: a fixture cannot be signed until a\n"
            "primitive has been chosen, and choosing one is the open decision.\n\n"
            "This is a FAILURE, not a skip. `make bootloader-budget` reproduces\n"
            "the numbers; docs/security/bootloader.md sets out the options.\n\n");
        return 1;
    }
    fclose(probe);

    test_signed_image_boots(elf);
    test_tampered_image_refuses(elf);
    test_downgrade_refuses(elf);

    printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
