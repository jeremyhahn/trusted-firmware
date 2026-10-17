/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw-boot on emulated silicon: the three behaviours that matter.
 *
 *   a correctly signed image boots
 *   a tampered image refuses
 *   a downgrade is refused
 *
 * Modelled on harness.c, and asking a different question from
 * test/unit/boot/test_boot_policy.c. That suite runs the decision logic on x86
 * against a flash array and proves the DECISION is right. This loads the real
 * tfw-boot.elf into a real ATmega model, puts the real application image and a
 * real trailer in the emulated flash, a real provisioned region in the emulated
 * EEPROM and a real ATECC608C on the emulated TWI bus, and proves the thing
 * that ships works -- LPM against the actual program memory, the actual EEPROM
 * peripheral, the actual TWI state machine in src/hal/avr/hal_bus.c, the polled
 * USART at the actual baud divisor, and the jump landing where the application
 * is.
 *
 * ------------------------------------------------------------------------
 * WHAT CHANGED SINCE THIS FILE WAS WRITTEN AND COULD NOT RUN.
 *
 * It used to say there was nothing to run, because the only signature backend
 * was ed25519-donna and its verify path is 72 930 bytes against a 4 096-byte
 * boot section. That was true and is no longer: BOOT_SIG=atecc608a verifies
 * P-256 in the element, holds no curve arithmetic at all, and every row of
 * BOOT_SUPPORTED in mk/boot.mk links. There is an image; this runs it; it is in
 * `make ci`.
 * ------------------------------------------------------------------------
 *
 * WHICH CONFIGURATION, AND WHY NOT THE 328P.
 *
 * atmega2560 with OTA in the boot section -- a row of BOOT_SUPPORTED, and the
 * only supported row whose bootloader can SAY why it refused. On a 4 KB part
 * the framed OTA path does not fit beside the boot decision, so recovery is
 * recovery_halt.c: the device stops, deliberately mute, and "tampered" and
 * "downgrade" are then indistinguishable from outside the part. Two of the
 * three assertions below would collapse into one. The 2560 also happens to be
 * the part with a three-byte program counter and a far-flash read, which is
 * where the jump and the measurement have the most room to be wrong.
 *
 * The 328P row is covered for size by `make bootloader-check` and for logic by
 * the host suites; what is NOT covered anywhere else is this file's territory,
 * and that needs a device that answers.
 */

/* dup2, fdopen, fcntl and read: the bridge mode's UART is the process's stdio. */
#define _POSIX_C_SOURCE 200809L

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <simavr/sim_avr.h>
#include <simavr/sim_elf.h>
#include <simavr/sim_io.h>
#include <simavr/avr_uart.h>
#include <simavr/avr_eeprom.h>
#include <simavr/avr_twi.h>
#include <simavr/avr_spi.h>
#include <simavr/avr_ioport.h>

#include "tfw/status.h"
#include "tfw/protocol.h"
#include "tfw/boot.h"
#include "boot_image.h"
#include "boot_platform.h"
#include "boot_request.h"
#include "sha256.h"
#include "crc16.h"
#include "spi_nor_model.h"
#include "tfw/config.h"
#include "tfw/hal.h"
#include "tfw/eth.h"
#include "w5x00_model.h"

/* The element's CRC, from the driver rather than from a second copy of it.
 * Declared rather than included, because atecc.h also declares the transport
 * this file exists to replace. */
uint16_t tfw_atecc_crc16(const uint8_t *data, uint8_t len);

/*
 * The element driver is linked for that one function, and a C object cannot be
 * linked a function at a time. These three resolve the references its other
 * functions make -- tfw_hal_i2c_init joined the set when tfw_atecc_wake()
 * started reasserting the element's required bus rate itself, rather than
 * trusting whatever the generic I2C_CONFIG command last left it at. Reaching
 * any of them is a wiring mistake and not a case to handle: this harness sits
 * BELOW the HAL -- it is the wire -- so anything in this process calling into
 * the HAL has bypassed the emulated part entirely and whatever it then proved
 * would be about nothing.
 */
tfw_status_t tfw_hal_i2c_xfer(uint8_t addr7, const uint8_t *wr, uint8_t wlen,
                            uint8_t *rd, uint8_t rlen);
tfw_status_t tfw_hal_i2c_init(uint32_t hz);
void        tfw_hal_delay_us(uint16_t us);

tfw_status_t tfw_hal_i2c_xfer(uint8_t addr7, const uint8_t *wr, uint8_t wlen,
                            uint8_t *rd, uint8_t rlen)
{
    (void)addr7; (void)wr; (void)wlen; (void)rd; (void)rlen;
    fprintf(stderr, "the harness called the AVR I2C HAL; it is the wire, not a host\n");
    abort();
}

tfw_status_t tfw_hal_i2c_init(uint32_t hz)
{
    (void)hz;
    fprintf(stderr, "the harness called the AVR I2C HAL; it is the wire, not a host\n");
    abort();
}

void tfw_hal_delay_us(uint16_t us)
{
    (void)us;
    fprintf(stderr, "the harness called the AVR delay HAL\n");
    abort();
}

/* ------------------------------------------------------------- plumbing */

static avr_t     *avr;
static avr_irq_t *uart_in;
static avr_irq_t *twi_in;

/*
 * TWBR, TWSR and TWCR by address.
 *
 * The same three addresses on every part this project supports, and simavr
 * exposes no accessor for them -- avr_twi_t is private to the emulator. They
 * are read to answer a question the TWI IRQs cannot: what bus RATE the firmware
 * configured, which is what decides whether the element wakes at all.
 */
#define R_TWBR 0xB8u
#define R_TWSR 0xB9u
#define R_TWCR 0xBCu
#define TWINT_BIT 0x80u

#define F_CPU_HZ 16000000uL

static int failures;

static void check(int cond, const char *what)
{
    printf("  %-62s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond) { failures++; }
}

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

static void run_cycles(long n)
{
    for (long i = 0; i < n; i++) { avr_run(avr); }
}

/*
 * Every wait is BOUNDED, and the bound is the point.
 *
 * A bootloader that stops answering -- spins on a TWI line nobody is driving,
 * bootloops through erased flash, sits in a loop waiting for a byte that will
 * never come -- must fail this job rather than hang it. `run_until` returns 0
 * when the budget is spent, and every caller treats that as the failure it is.
 */
static long last_run_steps;    /* the steps of the last run_until, for the store row's line */

static int run_until(int (*done)(void), long budget)
{
    for (long i = 0; i < budget; i++) {
        last_run_steps = i;
        if (done()) { return 1; }
        avr_run(avr);
    }
    last_run_steps = budget;
    return done();
}

/* A whole boot: measure ~12 KB sixty-four bytes at a time through the element,
 * verify, derive the CDI and jump. Measured at 8.4 M cycles; the reset that
 * settles a confirm evaluates twice and advances the counter. The budget is loose because it bounds a hang, not a performance
 * target, and a tight one would fail on a slower emulator for no reason. */
#define BUDGET_BOOT     40000000L
/* A store-row boot that TAKES a staged image reads the store twice: once
 * through the element to verify it, once to copy it into the slot, then swaps
 * and evaluates. Sized from the store row's own refusals (verify, 2560):
 *   floor-refused, 168 store bytes, no store hash:  7.83 M steps -- one
 *     element measurement of the primary, the CDI and the jump;
 *   hash-refused, 13 496 bytes (mark and trailer 168, image 13 328):
 *     32.03 M -- the store pass costs 24.20 M / 13 328 = 1 816 steps a byte,
 *     a 64-byte READ at F_CPU/4 and the element's SHA over TWI;
 *   taken, cut at 25 784 bytes and 40.00 M -- the copy pass's 12 288 bytes
 *     cost 7.97 M, 648 steps a byte with the page writes and the journal.
 * A taken boot: three element measurements (the store, the primary twice,
 * 24.20 + 2 x 7.83 = 39.86 M) and the copy (13 500 x 648 = 8.75 M) is
 * 48.6 M steps, about 66 M cycles. The time grows with the image, so the
 * budget is derived from its length: boot_store_budget(), below. */
#define STORE_STEPS_PASS_BYTE    1816L   /* the store pass, above */
#define STORE_STEPS_COPY_BYTE     648L   /* the copy pass, above */
/* One element measurement of the primary, CDI and jump included: the
 * floor-refused 7.83 M over its 13 328-byte image, rounded up. */
#define STORE_STEPS_MEASURE_BYTE  588L
#define STORE_MEASURES              2L   /* the secondary before the swap, the primary after */
/* What does not grow with the image (the swap, its journal, the start to the
 * BOOT event): the store row's taken boot, 62.08 M steps on the verify node
 * (verify.4, 2026-10-16), less 13 328 x 3 640 = 48.51 M of the per-byte terms,
 * rounded up. */
#define STORE_STEPS_FIXED    13600000L
/* The store row's 75 M over its 62.08 M: the margin it ran under before the
 * budget was derived, kept. */
#define STORE_MARGIN_NUM            6L
#define STORE_MARGIN_DEN            5L
/* One frame in, one frame out, at 115 200 on a polled USART. */
#define BUDGET_EXCHANGE   800000L

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

/* ------------------------------------------- simavr 1.6's TWI, corrected */
/*
 * TWO DEFECTS IN THE EMULATOR, PATCHED AT THE REGISTER, AND NAMED SO THAT
 * NOBODY MISTAKES THEM FOR SOMETHING THIS FIRMWARE DOES.
 *
 * Debian ships simavr 1.6. Its TWI master diverges from the ATmega datasheet in
 * two places, both fixed upstream after the release, and both of which make a
 * POLLED master -- which is what a bootloader must be, because it has no
 * interrupt vectors of its own -- fail immediately:
 *
 *   1. TWINT is never cleared. The cores declare the TWI vector `raise_sticky`,
 *      so avr_clear_interrupt leaves the flag alone, and the write-one-to-clear
 *      that the datasheet specifies does nothing. Every `while (!(TWCR &
 *      _BV(TWINT)))` then falls straight through and the firmware reads a
 *      status that belongs to the previous phase. Upstream now clears the bit
 *      explicitly in avr_twi_write.
 *
 *   2. SLA+W that is acknowledged reports TWI_MTX_DATA_ACK (0x28) instead of
 *      TWI_MTX_ADR_ACK (0x18), because the address branch tests TWI_COND_WRITE
 *      -- which is set by the write to TWDR that carried the address itself.
 *      Upstream tests TWI_COND_ADDR. A driver that checks for 0x18, as the
 *      datasheet says to, sees every addressed write as a NACK.
 *
 * Correcting them here rather than working around them in the firmware is the
 * only honest option: the firmware is right, and a driver bent to fit an
 * emulator is a driver that fails on silicon. Everything else about the
 * transfer -- the start and stop conditions, the addressing, the byte
 * sequencing, the ACK bit, the NACK on an unaddressed slave -- is simavr's and
 * is what this suite is here to exercise.
 */

static int addr_phase;

static void twcr_write_after(avr_t *a, uint16_t addr, uint8_t v, void *param)
{
    (void)param;
    /* Registered second, so simavr's own handler has already run and written
     * the raw value; this is the write-one-to-clear it does not do. */
    if (v & TWINT_BIT) { a->data[addr] &= (uint8_t)~TWINT_BIT; }
}

static void on_twi_status(struct avr_irq_t *irq, uint32_t value, void *param)
{
    (void)irq; (void)param;
    if (!addr_phase) { return; }
    if (value == 0x28u)      { avr->data[R_TWSR] = 0x18u; }  /* SLA+W acked  */
    else if (value == 0x30u) { avr->data[R_TWSR] = 0x20u; }  /* SLA+W nacked */
    addr_phase = 0;
}

/* --------------------------------------------- an ATECC608C on the wire */
/*
 * A SECOND MODEL OF THE SAME PART, AND THAT IS DELIBERATE.
 *
 * test/model/atecc608a.c models the element at the tfw_hal_i2c_xfer seam, which
 * is ABOVE the driver: it receives whole packets and answers with whole
 * packets. This one is a TWI slave on simavr's emulated bus, which is BELOW the
 * driver, so the bytes it sees have been through src/hal/avr/hal_bus.c's real
 * TWI state machine -- the start condition, the addressing, the per-byte ACK,
 * the final NACK that releases the line, the stop, and the NACK-poll the driver
 * uses to wait while the part is busy. None of that exists above the seam.
 *
 * Nor does this replace the host model. That one runs in `make test` in
 * milliseconds and can put the part into states a bench cannot -- a locked
 * zone, a forced status code, a part that is simply not there -- where this one
 * boots one image once. Neither subsumes the other.
 *
 * WHAT IT MODELS: Wake (including the bus-rate precondition), Info, SHA
 * Start/Update/End computing a real SHA-256, SHA HMAC for the CDI, Nonce in
 * pass-through mode, Verify in stored mode, and the monotonic counter.
 *
 * WHAT IT DOES NOT: it is not P-256. See element_sign. It is not a security
 * boundary -- a slot is unreadable here because the model refuses, not because
 * anything enforces it. It is not the config and data zones, because this
 * bootloader never reads or writes them; provisioning is tfwprov's, and
 * test/model/atecc608a.c is where that is exercised. And it is not timing: the
 * part is modelled as busy for exactly one read after each command, which
 * exercises the driver's NACK-poll without spending emulator cycles on a wait
 * that is not the thing under test.
 */

/* The ATECC608C-TFLXTLS on the MIKROE-6774 Click answers at 0x36 as shipped
 * (DS40002515B section 3.1), and boards/atmega2560/board_profile.h says so.
 * A literal, not the profile's macro: a bootloader that drifted from the part
 * on the bench has to fail here, not follow the drift. */
#define EL_ADDR             0x36u
#define EL_SLOT_IMAGE_SIGN  2u
#define EL_COUNTER_BOOT     0u

/* The element's own secrets. They exist in this process and nowhere else --
 * which is the property the tier claims and the thing the harness is standing
 * in for. */
static const uint8_t EL_SIGNING_KEY[32] = {
    0x9E,0x1C,0x4B,0x22,0x70,0xA5,0x33,0xF8,0x0D,0x61,0xB4,0x17,0xCA,0x2E,0x88,0x5D,
    0x30,0x79,0xE6,0x44,0x12,0xBB,0x57,0x9A,0xC3,0x08,0x6F,0xD1,0x25,0xAE,0x73,0x40
};
static const uint8_t EL_UDS[32] = {
    0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,0x00,
    0x0F,0x1E,0x2D,0x3C,0x4B,0x5A,0x69,0x78,0x87,0x96,0xA5,0xB4,0xC3,0xD2,0xE1,0xF0
};

/*
 * The 64 bytes the element accepts for a digest -- and it is NOT ECDSA.
 *
 * The harness does no elliptic-curve arithmetic and should not: a model that
 * implemented P-256 would become the oracle for the primitive, and a test whose
 * oracle is a second implementation of the thing under test proves that the two
 * agree and nothing else. The primitive is silicon's; it is not ours to check.
 *
 * What IS ours to check is the BINDING, and this construction checks exactly
 * that: the answer depends on the digest the bootloader loaded into TempKey
 * with Nonce and on a key that never leaves this model. Change one byte of the
 * image and the measurement moves; change one byte of the trailer prefix and
 * the signed digest moves; either way the element says no, because the digest
 * differs -- not because a flag was flipped. A model that simply returned "no"
 * would prove nothing about the bootloader at all.
 */
static void element_sign(const uint8_t digest[32], uint8_t sig[64])
{
    uint8_t buf[64];

    memcpy(buf, EL_SIGNING_KEY, 32);
    memcpy(&buf[32], digest, 32);
    tfw_sha256(buf, 64u, sig);

    memcpy(buf, digest, 32);
    memcpy(&buf[32], EL_SIGNING_KEY, 32);
    tfw_sha256(buf, 64u, &sig[32]);
}

static struct {
    int      awake;
    int      addressed;
    int      dir_read;
    unsigned busy_reads;

    uint8_t  cmd[96];
    unsigned cmd_n;
    uint8_t  rsp[96];
    unsigned rsp_n;
    unsigned rd_i;

    uint8_t     tempkey[32];
    tfw_sha256_t sha;
    int         sha_active;
    uint32_t    counter;
    /* Increments still to refuse with an execution error, before the counter
     * moves: a transient fault on the write the settle depends on. */
    unsigned    incr_refused;
    /* Counter reads still to refuse the same way: the floor read at reset. */
    unsigned    read_refused;

    /* What the element was ASKED to do. The assertions read these to say where
     * the bootloader stopped, which is evidence no UART frame carries. */
    unsigned wakes;
    unsigned wake_refused_rate;
    unsigned sha_blocks;
    unsigned nonce;
    unsigned verify;
    unsigned verify_ok;
    unsigned hmac;
} el;

static void el_respond(const uint8_t *body, uint8_t n)
{
    uint16_t crc;
    el.rsp[0] = (uint8_t)(n + 3u);
    if (n) { memcpy(&el.rsp[1], body, n); }
    crc = tfw_atecc_crc16(el.rsp, (uint8_t)(n + 1u));
    el.rsp[n + 1u] = (uint8_t)(crc & 0xFFu);
    el.rsp[n + 2u] = (uint8_t)(crc >> 8);
    el.rsp_n = (unsigned)n + 3u;
}

static void el_status(uint8_t code)
{
    uint16_t crc;
    el.rsp[0] = 4u;
    el.rsp[1] = code;
    crc = tfw_atecc_crc16(el.rsp, 2u);
    el.rsp[2] = (uint8_t)(crc & 0xFFu);
    el.rsp[3] = (uint8_t)(crc >> 8);
    el.rsp_n = 4u;
}

static void el_execute(void)
{
    const uint8_t *p = el.cmd;
    unsigned       n = el.cmd_n;
    uint8_t        count, op, p1, dlen;
    uint16_t       p2, crc;
    uint8_t        body[64];

    el.rsp_n = 0u;
    if (n < 8u || p[0] != 0x03u) { return; }      /* not a command packet */

    /* The part checks the count against what actually arrived, and the CRC over
     * the count byte onward -- NOT over the word address, which is addressing
     * rather than payload. A driver that included it gets 0xFF here, exactly as
     * it would on silicon, rather than a plausible failure somewhere later. */
    count = p[1];
    if (count != (uint8_t)(n - 1u)) { el_status(0xFFu); return; }
    crc = tfw_atecc_crc16(&p[1], (uint8_t)(count - 2u));
    if (crc != (uint16_t)(p[count - 1u] | ((uint16_t)p[count] << 8))) {
        el_status(0xFFu);
        return;
    }

    op   = p[2];
    p1   = p[3];
    p2   = (uint16_t)(p[4] | ((uint16_t)p[5] << 8));
    dlen = (uint8_t)(count - 7u);

    switch (op) {
    case 0x30u:   /* Info. Not issued by this bootloader; kept because it is the
                   * one command that tells "no part" from "wrong part", and a
                   * model that could not answer it would be modelling a part
                   * nobody could identify. 0x05 is the 608C's revision
                   * (AN5078, DS00005078A). */
        body[0] = 0x00u; body[1] = 0x00u; body[2] = 0x60u; body[3] = 0x05u;
        el_respond(body, 4u);
        return;

    case 0x47u:   /* SHA */
        if (p1 == 0x00u) {                        /* SHA256 Start */
            tfw_sha256_init(&el.sha);
            el.sha_active = 1;
            el_status(0x00u);
            return;
        }
        if (p1 == 0x01u && el.sha_active) {       /* SHA256 Update */
            /* Whole blocks only, which is what the part accepts. A driver that
             * sent a short Update would pass against a lenient model and fail
             * on silicon, so the refusal is worth more than the tolerance. */
            if (p2 != 64u || dlen != 64u) { el_status(0x03u); return; }
            tfw_sha256_update(&el.sha, &p[6], 64u);
            el.sha_blocks++;
            el_status(0x00u);
            return;
        }
        if (p1 == 0x02u && el.sha_active) {       /* SHA256 End */
            uint8_t tail = (uint8_t)p2;
            if (tail != dlen) { el_status(0x03u); return; }
            if (tail) { tfw_sha256_update(&el.sha, &p[6], tail); }
            tfw_sha256_final(&el.sha, body);
            el.sha_active = 0;
            el.sha_blocks++;
            el_respond(body, 32u);
            return;
        }
        if (p1 == 0x04u) {                        /* HMAC Start: names a slot */
            el.hmac++;
            el_status(0x00u);
            return;
        }
        if (p1 == 0x05u) {                        /* HMAC End */
            el.hmac++;
            tfw_hmac_sha256(EL_UDS, 32u, &p[6], p2, body);
            el_respond(body, 32u);
            return;
        }
        el_status(0x03u);
        return;

    case 0x16u:   /* Nonce. Pass-through loads TempKey verbatim, which is what
                   * Verify then answers about. */
        if (p1 != 0x03u || dlen != 32u) { el_status(0x03u); return; }
        memcpy(el.tempkey, &p[6], 32u);
        el.nonce++;
        el_status(0x00u);
        return;

    case 0x45u:   /* Verify */
    {
        uint8_t expect[64];
        /* Stored mode only, and only the slot this device was provisioned
         * with. External mode would take a key from the packet -- the thing
         * boot/verify/verify_atecc608a.c exists to avoid -- so a driver that
         * drifted into it is refused here rather than quietly honoured. */
        if (p1 != 0x00u)                { el_status(0x03u); return; }
        if (p2 != EL_SLOT_IMAGE_SIGN)   { el_status(0x0Fu); return; }
        if (dlen != 64u)                { el_status(0x03u); return; }
        el.verify++;
        element_sign(el.tempkey, expect);
        if (memcmp(expect, &p[6], 64u) == 0) {
            el.verify_ok++;
            el_status(0x00u);
        } else {
            el_status(0x01u);                     /* miscompare */
        }
        return;
    }

    case 0x24u:   /* Counter */
        if (p2 != EL_COUNTER_BOOT) { el_status(0x0Fu); return; }
        if (p1 == 0x01u && el.incr_refused > 0u) {
            el.incr_refused--;
            el_status(0x0Fu);
            return;
        }
        if (p1 == 0x00u && el.read_refused > 0u) {
            el.read_refused--;
            el_status(0x0Fu);
            return;
        }
        if (p1 == 0x01u) { el.counter++; }
        body[0] = (uint8_t)(el.counter & 0xFFu);
        body[1] = (uint8_t)((el.counter >> 8) & 0xFFu);
        body[2] = (uint8_t)((el.counter >> 16) & 0xFFu);
        body[3] = (uint8_t)((el.counter >> 24) & 0xFFu);
        el_respond(body, 4u);
        return;

    default:
        el_status(0x03u);                         /* an opcode this part lacks */
        return;
    }
}

/*
 * THE WAKE PULSE, AND THE PRECONDITION THE HOST MODEL CANNOT EXPRESS.
 *
 * The part wakes on SDA held low for at least 60 us, which tfw_atecc_wake
 * produces as the low period of a byte written to address zero -- nine bit
 * times. At 100 kHz that is 90 us and the part wakes; above it the pulse is too
 * short and the part stays asleep, which is why src/crypto/atecc608a/atecc.c
 * says the element's bus must be at or below 100 kHz.
 *
 * Whether it is depends on TWBR, and TWBR is a register. A model at the
 * tfw_hal_i2c_xfer seam never sees one -- test/model/atecc608a.c's
 * tfw_hal_i2c_init takes the rate and returns TFW_OK either way, because above
 * the seam there is nothing else it could do. Down here the rate is a fact
 * about the emulated part, so the refusal is real: forget to initialise the
 * bus and the element does not answer, which is precisely what happens on a
 * bench and is invisible everywhere else in this tree.
 */
static void el_wake_pulse(void)
{
    uint8_t       twbr  = avr->data[R_TWBR];
    uint8_t       twps  = (uint8_t)(avr->data[R_TWSR] & 0x03u);
    unsigned long scale = 1uL;
    unsigned long hz;

    for (uint8_t i = 0u; i < twps; i++) { scale *= 4uL; }
    hz = F_CPU_HZ / (16uL + 2uL * (unsigned long)twbr * scale);

    if (hz > 100000uL) {
        el.wake_refused_rate++;
        return;                                   /* the pulse was too short */
    }
    /* A woken part is not executing anything, so nothing is left to be busy
     * with. It matters because tfw_atecc_wake reads the token ONCE and does not
     * retry -- a leftover busy read here would fail a wake that hardware would
     * have answered. */
    el.busy_reads = 0u;
    el.awake      = 1;
    el.wakes++;
    el_status(0x11u);        /* 04 11 33 43: the token a woken part answers with */
}

static void on_twi(struct avr_irq_t *irq, uint32_t value, void *param)
{
    avr_twi_msg_irq_t m;
    (void)irq; (void)param;
    m.u.v = value;

    if (m.u.twi.msg & TWI_COND_START) {
        uint8_t sla = m.u.twi.addr;

        addr_phase   = 1;
        el.dir_read  = sla & 1;
        el.addressed = 0;

        if ((sla >> 1) == 0x00u) {
            el_wake_pulse();
            return;          /* nothing is listening at address zero: no ACK */
        }
        if ((sla >> 1) != EL_ADDR || !el.awake) { return; }
        /* A part that is still working NACKs its address, which is how the
         * driver learns to wait rather than to read a half-built answer. */
        if (el.dir_read && (el.busy_reads || el.rsp_n == 0u)) {
            if (el.busy_reads) { el.busy_reads--; }
            return;
        }
        el.addressed = 1;
        if (el.dir_read) { el.rd_i = 0u; } else { el.cmd_n = 0u; }
        avr_raise_irq(twi_in, avr_twi_irq_msg(TWI_COND_ACK, sla, 1u));
        return;
    }

    if (m.u.twi.msg & TWI_COND_STOP) {
        if (el.addressed && !el.dir_read) {
            switch (el.cmd_n ? el.cmd[0] : 0xFFu) {
            case 0x01u:                            /* Sleep */
                el.awake = 0;
                memset(el.tempkey, 0, sizeof el.tempkey);
                el.rsp_n = 0u;
                break;
            case 0x02u:                            /* Idle */
                memset(el.tempkey, 0, sizeof el.tempkey);
                break;
            case 0x03u:
                el_execute();
                el.busy_reads = 1u;
                break;
            default:
                break;
            }
        } else if (el.addressed && el.dir_read) {
            el.rsp_n = 0u;                         /* the answer has been taken */
        }
        el.addressed = 0;
        return;
    }

    if (!el.addressed) { return; }

    if (m.u.twi.msg & TWI_COND_WRITE) {
        if (el.cmd_n < sizeof el.cmd) { el.cmd[el.cmd_n++] = m.u.twi.data; }
        avr_raise_irq(twi_in, avr_twi_irq_msg(TWI_COND_ACK, m.u.twi.addr, 1u));
        return;
    }

    if (m.u.twi.msg & TWI_COND_READ) {
        /* Past the end of the answer the line floats high, which is what a
         * master reading further than the part has to give actually sees. */
        uint8_t b = el.rd_i < el.rsp_n ? el.rsp[el.rd_i++] : 0xFFu;
        avr_raise_irq(twi_in, avr_twi_irq_msg(TWI_COND_READ, m.u.twi.addr, b));
        return;
    }
}

/* --------------------------------------------------------------- framing */

static unsigned frame_request(uint8_t *out, uint8_t seq, uint8_t cmd,
                              const uint8_t *payload, uint8_t plen)
{
    unsigned i = 0;
    uint16_t crc;
    out[i++] = TFW_SOF;
    out[i++] = (uint8_t)(2u + plen);
    out[i++] = seq;
    out[i++] = cmd;
    for (uint8_t k = 0; k < plen; k++) { out[i++] = payload[k]; }
    crc = tfw_crc16(&out[1], (uint16_t)(i - 1u));
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
        if (from_device[i] != TFW_SOF) { continue; }
        unsigned len = from_device[i + 1];
        if (len < 3 || len > 200) { continue; }
        unsigned total = len + 4;
        if (i + total > from_device_n) { continue; }
        uint16_t want = tfw_crc16(&from_device[i + 1], (uint16_t)(len + 1));
        uint16_t got  = (uint16_t)(from_device[i + total - 2]
                                   | (from_device[i + total - 1] << 8));
        if (want != got) { continue; }
        if (from_device[i + 3] != TFW_OP_EVENT) { continue; }
        *status = from_device[i + 4];
        return 1;
    }
    return 0;
}

/*
 * The two EVENT frames a healthy device can emit after a reset, told apart by
 * their payload. The application's BOOT event is [status OK][TFW_EVENT_BOOT];
 * tfw-boot's announcement that it opened an update session is [status OK] with
 * no payload at all. Both are status OK, so the length is the discriminator.
 */
static int find_event_with_payload_len(uint8_t plen, uint8_t first)
{
    for (unsigned i = 0; i + 6 <= from_device_n; i++) {
        if (from_device[i] != TFW_SOF) { continue; }
        unsigned len = from_device[i + 1];
        if (len < 3 || len > 200) { continue; }
        unsigned total = len + 4;
        if (i + total > from_device_n) { continue; }
        uint16_t want = tfw_crc16(&from_device[i + 1], (uint16_t)(len + 1));
        uint16_t got  = (uint16_t)(from_device[i + total - 2]
                                   | (from_device[i + total - 1] << 8));
        if (want != got) { continue; }
        if (from_device[i + 3] != TFW_OP_EVENT || from_device[i + 4] != TFW_OK) { continue; }
        if (len - 3u != plen) { continue; }
        if (plen > 0u && from_device[i + 5] != first) { continue; }
        return 1;
    }
    return 0;
}

static int session_announced(void) { return find_event_with_payload_len(0u, 0u); }
static int app_booted(void)        { return find_event_with_payload_len(1u, TFW_EVENT_BOOT); }

static int find_response(uint8_t seq, uint8_t *status, uint8_t *payload, uint8_t *plen)
{
    for (unsigned i = 0; i + 6 <= from_device_n; i++) {
        if (from_device[i] != TFW_SOF) { continue; }
        unsigned len = from_device[i + 1];
        if (len < 3 || len > 200) { continue; }
        unsigned total = len + 4;
        if (i + total > from_device_n) { continue; }
        uint16_t want = tfw_crc16(&from_device[i + 1], (uint16_t)(len + 1));
        uint16_t got  = (uint16_t)(from_device[i + total - 2]
                                   | (from_device[i + total - 1] << 8));
        if (want != got) { continue; }
        if (from_device[i + 2] != seq) { continue; }
        if (!(from_device[i + 3] & TFW_OP_RESPONSE_BIT)) { continue; }
        if (from_device[i + 3] == TFW_OP_EVENT) { continue; }
        *status = from_device[i + 4];
        *plen   = (uint8_t)(len - 3u);
        memcpy(payload, &from_device[i + 5], *plen);
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------- image fixtures */

/*
 * THE SIGNING TOOL, WRITTEN OUT IN A TEST, exactly as
 * test/unit/boot/test_boot_policy.c writes it out on the host.
 *
 * The point is that the two halves are run against each other: this composes a
 * trailer with the host's tfw_boot_trailer_serialise and an x86 compiler, and
 * the device parses it with avr-gcc's copy of tfw_boot_trailer_parse out of real
 * program memory. A test that hand-wrote the bytes would only prove the parser
 * reads what the test wrote.
 *
 * THE APPLICATION IS THE REAL ONE. `image` holds the firmware .elf this same CI
 * run built, with a trailer at TFW_BOOT_TRAILER_ADDR and erased flash between --
 * two images in one flash, which is the arrangement on a real part. It has to
 * be the real one: "a correctly signed image boots" is observed by the
 * application answering CAPS after the jump, and only code that runs can do
 * that.
 */
/* Up to TFW_APP_WRITE_END, not the primary's end: on a board with two slots (the
 * 2560, ADR decision 7) an image is staged in the secondary at
 * TFW_OTA_STAGE_DEST and swapped into the primary by tfw-boot. On a board with
 * one slot the two ends are the same address. */
static uint8_t image[TFW_APP_WRITE_END];
static uint8_t eeprom_image[TFW_BOOT_EE_LEN];
static uint32_t image_len;

/* What one run left in the part, for a test that resets it without
 * rebuilding: the flash a swap rewrote and the EEPROM beyond the platform
 * block, where the swap record lives. Set by carry(), used once by install(). */
static uint8_t eeprom_all[TFW_EEPROM_SIZE];
static int     carried;

/* The layout, as data: a board stages in its primary or it has a secondary. */
static int two_slots(void)
{
    return (uint32_t)TFW_OTA_STAGE_DEST != TFW_BOOT_APP_BASE;
}

/* tfw_sha256 takes a uint16_t length and an application region does not fit in
 * one. Chunked here rather than widened there: the device hashes 64 bytes at a
 * time through the element and never needs the wider call. */
static void sha256_span(const uint8_t *p, uint32_t n, uint8_t out[32])
{
    tfw_sha256_t ctx;
    uint32_t    off = 0u;

    tfw_sha256_init(&ctx);
    while (off < n) {
        uint32_t left = n - off;
        uint16_t take = left > 32768uL ? (uint16_t)32768u : (uint16_t)left;
        tfw_sha256_update(&ctx, &p[off], take);
        off += take;
    }
    tfw_sha256_final(&ctx, out);
}

static void build_image_for(const elf_firmware_t *app, uint32_t version,
                            uint8_t board_id, long tamper_at)
{
    tfw_boot_trailer_t tr;
    uint8_t           raw[TFW_BOOT_TRAILER_LEN];
    uint8_t           prefix_digest[32];

    memset(image, 0xFF, sizeof image);            /* erased flash, not zeros */
    memcpy(image, app->flash, app->flashsize);
    image_len = app->flashsize;

    memset(&tr, 0, sizeof tr);
    tr.image_version = version;
    tr.image_len     = image_len;
    tr.board_id      = board_id;
    tr.image_flags   = (uint8_t)TFW_IMAGE_FLAGS;
    sha256_span(image, image_len, tr.measurement);

    /* The signature covers the trailer PREFIX -- magic, layout version, board
     * id, image flags, image version, image length and the measurement -- not
     * the measurement alone. Signing the measurement by itself would leave the
     * version unauthenticated and hand an attacker anti-rollback for a
     * four-byte edit; see include/tfw/boot.h. */
    tfw_boot_trailer_serialise(&tr, raw);
    tfw_sha256(raw, TFW_BOOT_TR_SIGNED_LEN, prefix_digest);
    element_sign(prefix_digest, &raw[TFW_BOOT_TR_SIGNATURE]);

    memcpy(&image[TFW_BOOT_TRAILER_ADDR], raw, TFW_BOOT_TRAILER_LEN);

    /* The tamper goes in LAST, so the trailer still claims the measurement of
     * the image nobody edited -- which is the situation a real attacker leaves
     * behind and the one the bootloader has to notice. */
    if (tamper_at >= 0) {
        image[tamper_at] ^= 0x01u;
    }
}

/* An image as the signer builds it for the board under test: the board
 * profile's id, the build's image_flags. */
static void build_image(const elf_firmware_t *app, uint32_t version, long tamper_at)
{
    build_image_for(app, version, (uint8_t)TFW_BOARD_ID, tamper_at);
}

/*
 * The step budget of a store-row boot, the one that takes a staged image the
 * longest: the store pass, the copy and both measurements over the image last
 * built, plus the fixed part, times the margin. Every element measurement is
 * one SHA command per 64-byte block, so a fixed cap that fits one image runs
 * out on a larger one (the network row's 17 532-byte ETH image ran out at
 * 75 M, 2.6 hashes in). Also bounds the stager's OTA_END, which hashes the
 * same image once through the same element.
 */
static long boot_store_budget(void)
{
    long per_byte = STORE_STEPS_PASS_BYTE + STORE_STEPS_COPY_BYTE
                  + STORE_MEASURES * STORE_STEPS_MEASURE_BYTE;

    return ((long)image_len * per_byte + STORE_STEPS_FIXED) * STORE_MARGIN_NUM
           / STORE_MARGIN_DEN;
}

/* A pending image where the board stages one. With one slot it replaces the
 * image in place. With two (ADR decision 7) it goes to the secondary at
 * TFW_OTA_STAGE_DEST, and the primary holds `previous`: the image tfw-boot
 * swaps out for the trial, and back in when the trial ends unconfirmed. */
static void stage_pending(const elf_firmware_t *app, uint32_t previous,
                          uint32_t version, long tamper_at)
{
    static uint8_t pending[TFW_BOOT_APP_SIZE];

    build_image(app, version, tamper_at);
    if (!two_slots()) {
        return;
    }
    memcpy(pending, image, sizeof pending);
    build_image(app, previous, -1);
    memcpy(&image[TFW_OTA_STAGE_DEST], pending, sizeof pending);
}

/* The version in the primary's trailer: which image a reset would run. */
static uint32_t primary_version(void)
{
    tfw_boot_trailer_t tr;

    if (tfw_boot_trailer_parse(&avr->flash[TFW_BOOT_TRAILER_ADDR], &tr) != TFW_OK) {
        return 0xFFFFFFFFu;
    }
    return tr.image_version;
}

/*
 * The serial `tfwprov --bake-eeprom` would have baked in before this part was
 * ever flashed -- see sdk/go/cmd/tfwprov/eeprom.go. Non-zero so this image is
 * genuinely a "provisioned platform block" and not an accidental all-zero
 * one; this harness's mismatched crypto tiers (see test_signed_image_boots)
 * mean ATTEST refuses before DeviceID would ever reach the wire here --
 * test_attest.c's test_a_baked_serial_reaches_device_id is where that flow is
 * proven, against a matching tier.
 */
static const uint8_t TEST_DEVICE_SERIAL[TFW_BOOT_SERIAL_LEN] = {
    0xD5,0xD5,0xD5,0xD5,0xD5,0xD5,0xD5,0xD5,
    0xD5,0xD5,0xD5,0xD5,0xD5,0xD5,0xD5,0xD5
};

static void build_eeprom_state(uint8_t boot_state, uint8_t ota_state)
{
    tfw_boot_platform_t plat;

    memset(eeprom_image, 0xFF, sizeof eeprom_image);
    memset(&plat, 0, sizeof plat);
    /*
     * pubkey_hash is left ZERO on purpose and the device still boots.
     *
     * On the element tier there is no key for the bootloader to hash: it lives
     * in a locked element slot and is named by number, so tfw_boot_pubkey
     * refuses and the EEPROM pin is not this bootloader's to check. Filling
     * this field with something plausible would hide the fact that nothing
     * reads it, and would make a regression that started reading it look like
     * a pass.
     */
    plat.boot_state = boot_state;
    plat.ota_state  = ota_state;
    memcpy(plat.serial, TEST_DEVICE_SERIAL, TFW_BOOT_SERIAL_LEN);
    tfw_boot_platform_serialise(&plat, eeprom_image);
}

static void build_eeprom(void)
{
    build_eeprom_state(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_IDLE);
}

/*
 * The platform block as the device left it in its EEPROM, copied into
 * eeprom_image as well, so the next install() powers the part up on exactly
 * what the last run wrote. With ee NULL simavr hands back a pointer into its
 * own EEPROM rather than copying, which every version of the ioctl does.
 */
static tfw_status_t stored_platform(tfw_boot_platform_t *p)
{
    avr_eeprom_desc_t d = { .ee = NULL, .offset = TFW_BOOT_EE_BASE,
                            .size = TFW_BOOT_EE_LEN };

    (void)avr_ioctl(avr, AVR_IOCTL_EEPROM_GET, &d);
    if (d.ee == NULL) {
        return TFW_ERR_INTERNAL;
    }
    memcpy(eeprom_image, d.ee, TFW_BOOT_EE_LEN);
    return tfw_boot_platform_parse(eeprom_image, p);
}

/* Keep what this run left in the part for the next install(): a reset, not a
 * reflash. A swap rewrites both slots and records its progress in EEPROM
 * outside the platform block, and a test that reinstalled the images it built
 * would undo the swap the device just did. The platform block still comes from
 * eeprom_image, which the test may have edited, as an application would. */
static void carry(void)
{
    avr_eeprom_desc_t d = { .ee = NULL, .offset = 0u, .size = TFW_EEPROM_SIZE };

    memcpy(image, avr->flash, sizeof image);
    (void)avr_ioctl(avr, AVR_IOCTL_EEPROM_GET, &d);
    if (d.ee == NULL) {
        fprintf(stderr, "no EEPROM to carry across the reset\n");
        exit(2);
    }
    memcpy(eeprom_all, d.ee, sizeof eeprom_all);
    carried = 1;
}

static void install(elf_firmware_t *boot, uint32_t counter)
{
    avr = avr_make_mcu_by_name(TFW_SIMAVR_MCU);
    if (!avr) { fprintf(stderr, "no simavr core for %s\n", TFW_SIMAVR_MCU); exit(2); }
    avr_init(avr);
    avr_load_firmware(avr, boot);

    /*
     * The reset vector, which simavr does not set for us.
     *
     * avr_load_firmware loads the boot section at its link address and leaves
     * the program counter at zero, so without this the APPLICATION runs and the
     * bootloader is never executed -- a suite that would pass every assertion
     * about a device that had not booted the way a real one does. On the part,
     * BOOTRST is programmed and reset enters the boot section; this is that
     * fuse.
     */
    avr->pc = boot->flashbase;
    /* BOOTRST is a fuse that applies to EVERY reset, not only the first: the
     * watchdog reset RESET(1) causes must land in the boot section as well.
     * Without this simavr restarts the application at address zero on the
     * second reset and a suite about entering the bootloader never enters it. */
    avr->reset_pc = boot->flashbase;
    if (avr->pc == 0u) {
        fprintf(stderr, "tfw-boot.elf has no boot-section entry point\n");
        exit(2);
    }

    /* The application image goes straight into the emulated program memory,
     * BELOW the bootloader the .elf just placed above TFW_APP_WRITE_END. */
    memcpy(avr->flash, image, sizeof image);

    if (carried) {
        avr_eeprom_desc_t d = { .ee = eeprom_all, .offset = 0u,
                                .size = TFW_EEPROM_SIZE };
        avr_ioctl(avr, AVR_IOCTL_EEPROM_SET, &d);
        carried = 0;
    }
    {
        avr_eeprom_desc_t d = { .ee = eeprom_image, .offset = TFW_BOOT_EE_BASE,
                                .size = TFW_BOOT_EE_LEN };
        avr_ioctl(avr, AVR_IOCTL_EEPROM_SET, &d);
    }

    memset(&el, 0, sizeof el);
    el.counter = counter;
    addr_phase = 0;

    avr_register_io_write(avr, R_TWCR, twcr_write_after, NULL);
    twi_in = avr_io_getirq(avr, AVR_IOCTL_TWI_GETIRQ(0), TWI_IRQ_INPUT);
    avr_irq_register_notify(
        avr_io_getirq(avr, AVR_IOCTL_TWI_GETIRQ(0), TWI_IRQ_OUTPUT), on_twi, NULL);
    avr_irq_register_notify(
        avr_io_getirq(avr, AVR_IOCTL_TWI_GETIRQ(0), TWI_IRQ_STATUS), on_twi_status, NULL);

    uart_in = avr_io_getirq(avr, AVR_IOCTL_UART_GETIRQ('0'), UART_IRQ_INPUT);
    avr_irq_register_notify(
        avr_io_getirq(avr, AVR_IOCTL_UART_GETIRQ('0'), UART_IRQ_OUTPUT),
        on_device_tx, NULL);

    from_device_n = 0;
}

static int device_spoke(void)   { return from_device_n > 0; }
static int event_arrived(void)  { uint8_t s; return find_event(&s); }

/* run_until's predicate has no arguments, so the sequence it waits for is a
 * static -- set immediately before the wait, exactly as every other budget
 * here bounds a single specific exchange rather than a general condition. */
static uint8_t wait_for_seq;
static int response_arrived(void)
{
    uint8_t s, plen, buf[64];
    return find_response(wait_for_seq, &s, buf, &plen);
}

/* ---------------------------------------------------------------- tests */

/*
 * A booted application is observed by what it does, not by a flag.
 *
 * The discriminator is CAPS, which only the application implements. PING would
 * not do: the bootloader answers that too, deliberately, because it is the only
 * way a host tells "in the bootloader, waiting" from "dead". A test that
 * accepted a PONG would pass on a device that never left the OTA loop.
 */
static void test_signed_image_boots(elf_firmware_t *boot, elf_firmware_t *app)
{
    uint8_t  status, out[64], outlen;
    uint8_t  wire[64];
    unsigned n;

    printf("a correctly signed image:\n");
    build_image(app, 3u, -1);
    build_eeprom();
    install(boot, 0u);

    /*
     * MCUSR, poked directly into emulated memory -- EXTRF (bit 1), an
     * external reset. This is the fact issue #18 is actually about: does the
     * value survive tfw-boot's OWN .init3 (which reads and clears the real
     * register), through the handoff, through the application's OWN .init3
     * (which reads a register already zero by then), out to SYS_INFO? A test
     * that only checked the handoff's byte layout, as test_reset_cause.c
     * does on the host, cannot catch a linker or storage-class mistake that
     * lets .bss-clearing erase the latch before anything reads it -- which is
     * exactly what shipped here, briefly, until this line existed.
     *
     * 0x54 is MCUSR's address in the AVR's unified data space (I/O address
     * 0x34 plus the 0x20 offset every classic AVR in this family uses).
     */
    avr->data[0x54] = 0x02u;   /* EXTRF */

    check(run_until(device_spoke, BUDGET_BOOT),
          "the device transmits within the cycle budget");

    /* Where it got to on the element bus, which no UART frame reports. Every
     * one of these is a command the bootloader had to frame, CRC and clock out
     * over the real TWI driver before it could be counted. */
    check(el.wakes > 0 && el.wake_refused_rate == 0,
          "the element woke, so the TWI bus was clocked at 100 kHz or below");
    /* Exactly the commands the driver should have framed: one Update per whole
     * 64-byte block of the image, one End for the tail, and one more End for
     * the 48-byte trailer prefix the signature covers -- which fits in a single
     * block, so it costs no Update at all. An off-by-one here is a driver that
     * sent a short Update or padded its own tail, and either produces a digest
     * that is not the one a verifier recomputes. */
    check(el.sha_blocks == ((image_len - 1u) / 64u) + 2u,
          "the element hashed every block of the image, and the trailer prefix");
    check(el.nonce == 1 && el.verify == 1 && el.verify_ok == 1,
          "the digest went in through Nonce and the element accepted the signature");
    check(el.hmac == 2,
          "the CDI was derived inside the element, from the UDS it holds");
    /* A plain boot moves no floor: it advances only at the reset that settles
     * a confirmed trial (ADR decision 5), which test_floor_advances_at_settle
     * drives. */
    check(el.counter == 0u,
          "a plain boot leaves the element's counter where it was");

    from_device_n = 0;
    n = frame_request(wire, 0x31, TFW_CMD_CAPS, NULL, 0);
    send(wire, n);
    run_cycles(BUDGET_EXCHANGE);
    check(find_response(0x31, &status, out, &outlen) && status == TFW_OK,
          "CAPS answers, so the APPLICATION is running");

    /*
     * ATTEST, over the wire, through the WHOLE stack -- issues #6/#7. This
     * board's bootloader is BOOT_TIER=atecc608a (the row simavr runs, and the
     * one that fits the Mega's boot section per BOOT_SUPPORTED); $(TARGET).elf
     * here is CRYPTO=soft, mk/avr.mk's own independent default -- a MISMATCH
     * this harness carries rather than resolves, because it is exactly the
     * case src/crypto/soft/provider.c must now refuse rather than sign with.
     * tfw-boot hands off a real, non-zero CDI derived from the ELEMENT's UDS,
     * tagged TFW_CRYPTO_SECURE_ELEMENT; the software tier has no way to
     * reproduce a secret from a UDS it never provisions anywhere it reads
     * from, so a verifier configured for this tier could never recompute a
     * signature made under it. Accepting the handoff anyway would be evidence
     * that verifies against nothing a real verifier could ever check.
     *
     * A test that only mocked the tier byte, as test_attest.c does on the
     * host, could not catch a REAL tfw-boot's REAL element-derived CDI
     * reaching a REAL software-tier build over the actual fixed SRAM address.
     * This is that path, and the correct answer is TFW_ERR_NOT_PROVISIONED --
     * the same status an entirely unprovisioned device returns, which is
     * exactly right: this device has no identity THIS TIER CAN USE, whatever
     * tfw-boot found.
     */
    {
        static const uint8_t nonce[TFW_ATTEST_NONCE_LEN] = {
            0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
        };
        uint8_t out2[64], outlen2;

        from_device_n = 0;
        n = frame_request(wire, 0x34, TFW_CMD_ATTEST, nonce, sizeof nonce);
        send(wire, n);
        wait_for_seq = 0x34;
        run_until(response_arrived, BUDGET_EXCHANGE);
        check(find_response(0x34, &status, out2, &outlen2)
              && status == TFW_ERR_NOT_PROVISIONED,
              "ATTEST refuses a CDI handed off for a mismatched crypto tier");
    }

    /*
     * The reset cause: the EXTRF poked into MCUSR above, having survived
     * tfw-boot's OWN .init3 (which reads and clears the real register),
     * the handoff, the application's OWN .init3 (which reads a register
     * already zero by then), and out through SYS_INFO -- issue #18.
     */
    {
        uint8_t sysinfo[64], sysinfo_len;

        from_device_n = 0;
        n = frame_request(wire, 0x36, TFW_CMD_SYS_INFO, NULL, 0u);
        send(wire, n);
        wait_for_seq = 0x36;
        run_until(response_arrived, BUDGET_EXCHANGE);
        /*
         * >= 7, not == 7: #24 appended err_crc/err_format after reset_cause,
         * which grew the reply to 11 bytes without moving reset_cause's own
         * offset -- exactly the extend-never-renumber shape SYS_INFO's own
         * versioning promises. reset_cause stays at byte 6 regardless of how
         * many bytes follow it.
         */
        check(find_response(0x36, &status, sysinfo, &sysinfo_len) && status == TFW_OK
              && sysinfo_len >= 7u && sysinfo[6] == 2u /* TFW_RESET_EXTERNAL */,
              "SYS_INFO reports the reset cause tfw-boot handed off, not unknown");
    }
}

/*
 * THE FLOOR ON THE ELEMENT TIER, ALONG THE ONE TRIAL (ADR decisions 5 and 9).
 *
 * The floor lives in the element's monotonic counter, never in EEPROM, and it
 * moves at exactly one point: the reset that finds a confirmed trial. Each
 * install() below is a power-up of the same part, carrying the EEPROM the last
 * run wrote and the counter the element kept.
 *
 * Staging is the EEPROM an activate leaves -- GOOD and PENDING, the signed
 * image already in the application region -- and the confirm is the CONFIRMED
 * that tfw_boot_trial_confirm sets. Neither has a wire id yet (decision 8's
 * ids are proposed), so the harness writes the state they would; everything
 * the bootloader does with it is the real image's.
 */
static void test_floor_advances_at_settle(elf_firmware_t *boot, elf_firmware_t *app)
{
    tfw_boot_platform_t p;
    uint8_t status;

    printf("the floor across one trial:\n");
    stage_pending(app, 2u, 3u, -1);
    build_eeprom_state(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_PENDING);
    install(boot, 0u);

    check(run_until(app_booted, BUDGET_BOOT), "a pending image boots, on trial");
    check(stored_platform(&p) == TFW_OK && p.boot_state == TFW_BOOT_STATE_TRIAL
          && p.ota_state == TFW_BOOT_OTA_IDLE,
          "TRIAL was stored before the jump");
    check(primary_version() == 3u, "the image on trial is the one in the primary");
    check(el.counter == 0u, "a trial boot leaves the element's counter where it was");

    /* The application confirms; then the settling reset. */
    p.boot_state = TFW_BOOT_STATE_CONFIRMED;
    tfw_boot_platform_serialise(&p, eeprom_image);
    carry();
    install(boot, el.counter);
    check(run_until(app_booted, BUDGET_BOOT), "the confirmed image boots at the next reset");
    check(el.counter == 3u,
          "the rollback floor advanced in the element's counter at the settling reset");
    check(stored_platform(&p) == TFW_OK && p.boot_state == TFW_BOOT_STATE_GOOD
          && p.rollback_floor == 0u,
          "settled to GOOD, and the floor in EEPROM did not move");

    carry();
    install(boot, el.counter);
    check(run_until(app_booted, BUDGET_BOOT), "the settled image boots");
    check(el.counter == 3u, "a plain boot after the settle moves the counter no further");
    check(primary_version() == 3u, "and it is the confirmed image");

    /* A trial that ends without a confirm: no second run, no floor. */
    stage_pending(app, 2u, 3u, -1);
    build_eeprom_state(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_PENDING);
    install(boot, 0u);
    check(run_until(app_booted, BUDGET_BOOT), "another pending image boots, on trial");
    check(stored_platform(&p) == TFW_OK && p.boot_state == TFW_BOOT_STATE_TRIAL,
          "TRIAL was stored before the jump");
    carry();
    install(boot, el.counter);
    if (two_slots()) {
        /* ADR decision 7: the other slot holds the image before the trial, so
         * an unconfirmed trial falls back to it rather than to recovery. */
        check(run_until(app_booted, BUDGET_BOOT),
              "the reset that ends the trial unconfirmed boots the previous image");
        check(!find_event(&status) || status == TFW_OK,
              "and reports no refusal: it is not recovery");
        check(primary_version() == 2u,
              "the image on trial is swapped out: it does not run a second time");
        check(stored_platform(&p) == TFW_OK && p.boot_state == TFW_BOOT_STATE_GOOD
              && p.ota_state == TFW_BOOT_OTA_FAILED,
              "GOOD, FAILED is stored");
    } else {
        check(run_until(event_arrived, BUDGET_BOOT),
              "the reset that ends the trial unconfirmed says why");
        check(find_event(&status) && status == TFW_ERR_BAD_STATE && !app_booted(),
              "refused with TFW_ERR_BAD_STATE: the image does not run a second time");
        check(stored_platform(&p) == TFW_OK && p.boot_state == TFW_BOOT_STATE_TRIAL
              && p.ota_state == TFW_BOOT_OTA_FAILED,
              "FAILED is stored");
    }
    check(el.counter == 0u, "an unconfirmed trial leaves the element's counter where it was");
}

/*
 * The settle when the element refuses the counter write (#82), with the status
 * the driver reads as TFW_ERR_UNSUPPORTED. The confirmed image still boots,
 * the block stays CONFIRMED, and the next reset retries the advance -- GOOD is
 * written only once the counter holds the image's version.
 */
static void test_a_refused_floor_advance_is_retried(elf_firmware_t *boot,
                                                    elf_firmware_t *app)
{
    tfw_boot_platform_t p;

    printf("a floor advance the element refuses:\n");
    build_image(app, 3u, -1);
    build_eeprom_state(TFW_BOOT_STATE_CONFIRMED, TFW_BOOT_OTA_IDLE);
    install(boot, 0u);
    el.incr_refused = 1u;

    check(run_until(app_booted, BUDGET_BOOT),
          "the confirmed image boots though the counter write was refused");
    check(el.counter == 0u, "the element's counter did not move");
    check(stored_platform(&p) == TFW_OK && p.boot_state == TFW_BOOT_STATE_CONFIRMED
          && p.rollback_floor == 0u,
          "CONFIRMED is kept and nothing is written to EEPROM in its place");

    install(boot, el.counter);
    check(run_until(app_booted, BUDGET_BOOT), "the next reset boots it again");
    check(el.counter == 3u,
          "and retried the advance: the counter holds the confirmed version");
    check(stored_platform(&p) == TFW_OK && p.boot_state == TFW_BOOT_STATE_GOOD
          && p.rollback_floor == 0u,
          "settled to GOOD, and the floor in EEPROM did not move");
}

/*
 * #82, the read side: the element answers the floor read with an execution
 * error (0x0F, which the driver maps to TFW_ERR_UNSUPPORTED -- "no element").
 * The EEPROM copy, here 0 under a counter at 5, must not stand in for it: the
 * version-3 image would pass that. Nothing is verified and nothing runs;
 * recovery says TFW_ERR_CRYPTO, and the next reset reads the counter and
 * refuses the image as below the floor.
 */
static void test_an_unreadable_floor_is_not_the_eeprom_copy(elf_firmware_t *boot,
                                                            elf_firmware_t *app)
{
    tfw_boot_platform_t p;
    uint8_t status;

    printf("a floor read the element refuses:\n");
    build_image(app, 3u, -1);
    build_eeprom_state(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_IDLE);
    install(boot, 5u);
    el.read_refused = 1u;

    check(run_until(event_arrived, BUDGET_BOOT), "the refusal says why");
    check(find_event(&status) && status == TFW_ERR_CRYPTO && !app_booted(),
          "recovery with TFW_ERR_CRYPTO, and the image did not run");
    check(stored_platform(&p) == TFW_OK && p.boot_state == TFW_BOOT_STATE_GOOD
          && p.rollback_floor == 0u,
          "the block is as it was");

    install(boot, el.counter);
    check(run_until(event_arrived, BUDGET_BOOT), "the next reset reads the counter");
    check(find_event(&status) && status == TFW_ERR_PROTECTED_REGION && !app_booted(),
          "and refuses the image as below the floor, not for the read");
}

/*
 * A pending image refused at evaluation, then a reset at which it would verify.
 *
 * The tampered bit stands in for any refusal, a transient one included: an
 * element that did not answer refuses the same way, and answers at the next
 * reset. That reset must not boot the image as accepted -- no trial ran and
 * nothing confirmed it -- so the refusal is stored as an ended trial.
 */
static void test_a_refused_pending_image_never_boots(elf_firmware_t *boot,
                                                    elf_firmware_t *app)
{
    tfw_boot_platform_t p;
    uint8_t status;

    printf("a pending image refused at evaluation:\n");
    stage_pending(app, 2u, 3u, 1000);
    build_eeprom_state(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_PENDING);
    install(boot, 0u);
    if (two_slots()) {
        /* ADR decision 7 and the swap-move in docs/ota/architecture.md: the
         * pending image is checked in the secondary before a page is written,
         * and one that is refused there is never swapped. The round ends as a
         * failed trial does, GOOD with FAILED, and the primary runs on. */
        check(run_until(app_booted, BUDGET_BOOT),
              "refused in the secondary, and the previous image boots");
        check(memcmp(avr->flash, image, sizeof image) == 0,
              "and no swap page was written: both slots are as staged");
        check(primary_version() == 2u, "the primary still holds the previous image");
        check(stored_platform(&p) == TFW_OK && p.boot_state == TFW_BOOT_STATE_GOOD
              && p.ota_state == TFW_BOOT_OTA_FAILED,
              "stored as an ended round: GOOD, FAILED");

        /* The cause clears: the same version now verifies. FAILED is not
         * PENDING, so it is not looked at again. */
        stage_pending(app, 2u, 3u, -1);
        carried = 0;
        install(boot, el.counter);
        check(run_until(app_booted, BUDGET_BOOT),
              "the reset after the refusal boots the previous image");
        check(memcmp(avr->flash, image, sizeof image) == 0 && primary_version() == 2u,
              "an image that verifies now still is not swapped in");
        check(el.verify == 1, "and only the primary was evaluated");
        check(stored_platform(&p) == TFW_OK && p.boot_state == TFW_BOOT_STATE_GOOD
              && p.ota_state == TFW_BOOT_OTA_FAILED,
              "GOOD, FAILED is kept");
    } else {
        check(run_until(event_arrived, BUDGET_BOOT), "the refusal says why");
        check(find_event(&status) && status == TFW_ERR_CRYPTO && !app_booted(),
              "refused with TFW_ERR_CRYPTO, and it did not run");
        check(stored_platform(&p) == TFW_OK && p.boot_state == TFW_BOOT_STATE_TRIAL
              && p.ota_state == TFW_BOOT_OTA_FAILED,
              "stored as an ended trial: TRIAL, FAILED");

        /* The cause clears: the same version now verifies. */
        build_image(app, 3u, -1);
        install(boot, el.counter);
        check(run_until(event_arrived, BUDGET_BOOT),
              "the reset after the refusal says why");
        check(find_event(&status) && status == TFW_ERR_BAD_STATE && !app_booted(),
              "refused with TFW_ERR_BAD_STATE: an image that verifies now still does not run");
        check(el.verify == 0, "and it was not evaluated at all");
        check(stored_platform(&p) == TFW_OK && p.boot_state == TFW_BOOT_STATE_TRIAL
              && p.ota_state == TFW_BOOT_OTA_FAILED,
              "TRIAL, FAILED is kept");
    }
    check(el.counter == 0u, "the refused image leaves the element's counter where it was");
}

static void test_tampered_image_refuses(elf_firmware_t *boot, elf_firmware_t *app)
{
    uint8_t status;

    printf("a tampered image:\n");
    build_image(app, 3u, 1000);     /* one bit, in the middle of the image */
    build_eeprom();
    install(boot, 0u);

    check(run_until(event_arrived, BUDGET_BOOT),
          "the device says why within the cycle budget");
    check(find_event(&status) && status == TFW_ERR_CRYPTO,
          "refused with TFW_ERR_CRYPTO");

    /* WHERE it refused, and this is the part a status byte cannot tell you. The
     * measurement is compared before the signature is checked, so a tampered
     * image must cost the element a full hash and no Verify at all -- if Verify
     * ran, the ordering in tfw_boot_evaluate has changed and the expensive check
     * is now happening on images that never needed it. */
    check(el.sha_blocks > 0, "the element was asked to measure the image");
    check(el.verify == 0,
          "and was never asked to verify: the measurement caught it first");

    {
        uint8_t wire[64], out[64], outlen;
        unsigned n;
        from_device_n = 0;
        n = frame_request(wire, 0x32, TFW_CMD_CAPS, NULL, 0);
        send(wire, n);
        run_cycles(BUDGET_EXCHANGE);
        check(find_response(0x32, &status, out, &outlen)
              && status == TFW_ERR_UNKNOWN_CMD,
              "CAPS is unknown, so it did NOT jump");
    }
}

static void test_downgrade_refuses(elf_firmware_t *boot, elf_firmware_t *app)
{
    uint8_t status;

    printf("a downgrade:\n");
    /* The same signed image, against a device whose recorded floor is above its
     * version. Nothing about the image is altered, so a pass here cannot be the
     * measurement check firing by accident.
     *
     * The floor is the ELEMENT'S monotonic counter and not the EEPROM field --
     * boot_store.c prefers the counter wherever there is one, because it is the
     * only copy that cannot be made to descend. Setting the EEPROM field here
     * instead would leave a device that boots and a test that passes for the
     * wrong reason. */
    build_image(app, 3u, -1);
    build_eeprom();
    install(boot, 5u);

    check(run_until(event_arrived, BUDGET_BOOT),
          "the device says why within the cycle budget");
    check(find_event(&status) && status == TFW_ERR_PROTECTED_REGION,
          "refused with TFW_ERR_PROTECTED_REGION");
    check(el.sha_blocks == 0,
          "and refused before measuring: the cheap check came first");
    check(el.counter == 5u, "the floor did not move for a refused image");
}

static void test_another_boards_image_refuses(elf_firmware_t *boot, elf_firmware_t *app)
{
    uint8_t status;

    printf("an image for another board:\n");
    /* Signed by the same key, measured correctly, above the floor: everything
     * is right except the board it names. The binding comes before the floor,
     * so nothing is hashed and nothing moves. */
    build_image_for(app, 3u, (uint8_t)(TFW_BOARD_ID + 1u), -1);
    build_eeprom();
    install(boot, 0u);

    check(run_until(event_arrived, BUDGET_BOOT),
          "the device says why within the cycle budget");
    check(find_event(&status) && status == TFW_ERR_UNSUPPORTED,
          "refused with TFW_ERR_UNSUPPORTED");
    check(el.sha_blocks == 0,
          "and refused before measuring: the binding came first");
    check(el.counter == 0u, "the floor did not move for a refused image");
}

/*
 * Issue #28: a healthy device, asked over the wire to take an update.
 *
 * RESET(1) is sent to the RUNNING APPLICATION. It leaves the request in SRAM
 * and takes a real watchdog reset; tfw-boot, entered through BOOTRST, finds the
 * request, opens the session instead of jumping, and answers PING. Left alone it
 * gives up after TFW_BOOT_SESSION_IDLE_SECONDS and boots the application it
 * always could have; told to RESET it does so at once; and in neither case does
 * the NEXT reset enter the bootloader again.
 */
static void send_and_wait(uint8_t seq, uint8_t cmd, const uint8_t *payload,
                          uint8_t plen)
{
    uint8_t  wire[64];
    unsigned n = frame_request(wire, seq, cmd, payload, plen);

    from_device_n = 0;
    send(wire, n);
    wait_for_seq = seq;
    run_until(response_arrived, BUDGET_EXCHANGE);
}

static int response_status_is(uint8_t seq, uint8_t want)
{
    uint8_t status, plen, buf[64];
    return find_response(seq, &status, buf, &plen) && status == want;
}

/*
 * Codex P1 on #82: a reset finds CONFIRMED and the element refuses the counter
 * read (0x0F), so recovery is entered before the settle. A transfer begun there
 * would replace CONFIRMED with STAGED at its first page and lose the floor
 * advance; OTA_BEGIN is refused and the block left alone. The next reset reads
 * the counter, settles, and boots the confirmed image.
 */
static void test_an_unsettled_confirm_refuses_a_transfer(elf_firmware_t *boot,
                                                         elf_firmware_t *app)
{
    const uint8_t begin[4] = { 0x00, 0x01, 0x00, 0x00 };    /* 256 bytes */
    tfw_boot_platform_t p;
    uint8_t status;

    printf("a transfer while a confirm is unsettled:\n");
    build_image(app, 3u, -1);
    build_eeprom_state(TFW_BOOT_STATE_CONFIRMED, TFW_BOOT_OTA_IDLE);
    install(boot, 0u);
    el.read_refused = 1u;

    check(run_until(event_arrived, BUDGET_BOOT), "the refusal says why");
    check(find_event(&status) && status == TFW_ERR_CRYPTO && !app_booted(),
          "recovery with TFW_ERR_CRYPTO, and the image did not run");
    send_and_wait(0x71, TFW_CMD_OTA_BEGIN, begin, sizeof begin);
    check(response_status_is(0x71, TFW_ERR_BAD_STATE),
          "OTA_BEGIN is refused while the block is CONFIRMED");
    check(stored_platform(&p) == TFW_OK && p.boot_state == TFW_BOOT_STATE_CONFIRMED,
          "and the block is still CONFIRMED");

    install(boot, el.counter);
    check(run_until(app_booted, BUDGET_BOOT), "the next reset boots the confirmed image");
    check(el.counter == 3u, "the counter holds the confirmed version");
    check(stored_platform(&p) == TFW_OK && p.boot_state == TFW_BOOT_STATE_GOOD,
          "settled to GOOD");
}

static uint64_t run_until_cycles(int (*done)(void), uint64_t budget)
{
    uint64_t start = avr->cycle;

    while (!done() && avr->cycle - start < budget) {
        avr_run(avr);
    }
    return avr->cycle - start;
}

static void test_requested_update_session(elf_firmware_t *boot, elf_firmware_t *app)
{
    const uint8_t soft[1] = { 0u }, to_boot[1] = { 1u };
    const uint8_t ping[4] = { 'u', 'p', 'd', 'a' };
    uint8_t  out[64], outlen, status;
    uint64_t elapsed;
    const uint64_t nominal = (uint64_t)TFW_BOOT_SESSION_IDLE_SECONDS * F_CPU_HZ;

    printf("a requested update session (issue #28):\n");
    build_image(app, 3u, -1);
    build_eeprom();
    install(boot, 0u);
    check(run_until(device_spoke, BUDGET_BOOT), "a healthy image boots first");

    send_and_wait(0x41, TFW_CMD_CAPS, NULL, 0u);
    check(response_status_is(0x41, TFW_OK), "CAPS answers, so the application is running");

    /* RESET(1) to the application. */
    send_and_wait(0x42, TFW_CMD_RESET, to_boot, 1u);
    check(response_status_is(0x42, TFW_OK), "RESET(1) is acknowledged by the application");

    check(run_until(session_announced, BUDGET_BOOT),
          "tfw-boot announces an update session instead of jumping");
    check(!app_booted(), "and the application did not start");

    send_and_wait(0x43, TFW_CMD_PING, ping, sizeof ping);
    check(find_response(0x43, &status, out, &outlen) && status == TFW_OK
          && outlen == sizeof ping && memcmp(out, ping, sizeof ping) == 0,
          "the bootloader answers PING");
    send_and_wait(0x44, TFW_CMD_CAPS, NULL, 0u);
    check(response_status_is(0x44, TFW_ERR_UNKNOWN_CMD),
          "and CAPS is unknown, so this is the bootloader and not the application");

    /* Left alone it times out into the normal boot. The wait is measured: it is
     * what TFW_BOOT_POLL_CYCLES (boot/hal/avr/boot_tick.h) promises, and a loop that changed cost
     * without the constant following would otherwise stretch or shrink it. */
    from_device_n = 0;
    elapsed = run_until_cycles(app_booted, nominal * 2u);
    printf("  (idle timeout: %llu cycles, nominal %llu)\n",
           (unsigned long long)elapsed, (unsigned long long)nominal);
    check(app_booted(), "after the idle timeout the application boots");
    check(elapsed >= nominal * 8u / 10u,
          "and not before 80% of the configured idle time");
    check(elapsed <= nominal * 12u / 10u + BUDGET_BOOT,
          "nor later than 120% of it plus the verify");
    send_and_wait(0x45, TFW_CMD_CAPS, NULL, 0u);
    check(response_status_is(0x45, TFW_OK), "CAPS answers: the same image, verified, is running");

    /* A RESET command inside the session ends it at once. */
    send_and_wait(0x46, TFW_CMD_RESET, to_boot, 1u);
    check(run_until(session_announced, BUDGET_BOOT), "a second request opens a second session");
    from_device_n = 0;
    send_and_wait(0x47, TFW_CMD_RESET, soft, 1u);
    elapsed = run_until_cycles(app_booted, nominal);
    check(app_booted() && elapsed < BUDGET_BOOT,
          "RESET in the session boots the application without waiting for the timeout");

    /* ONE SHOT. Nothing left over: an ordinary soft reset goes straight to the
     * application with no session in between. */
    send_and_wait(0x48, TFW_CMD_RESET, soft, 1u);
    from_device_n = 0;
    elapsed = run_until_cycles(app_booted, nominal);
    check(app_booted() && elapsed < BUDGET_BOOT && !session_announced(),
          "the next soft reset boots straight through: the request was one-shot");
}

/*
 * The same request bytes, but after a power-on reset. SRAM is not cleared by a
 * reset, so a request that was armed and then lost power -- or garbage that
 * happens to look like one -- is a real thing to meet; it must be ignored, and
 * the application must boot straight through.
 */
static void test_request_after_power_on_is_ignored(elf_firmware_t *boot, elf_firmware_t *app)
{
    uint8_t  req[TFW_BOOT_REQUEST_LEN];
    uint16_t at = (uint16_t)(avr->ramend - (TFW_BOOT_HANDOFF_LEN - 1u));

    printf("a request left in SRAM across a power cycle:\n");
    build_image(app, 3u, -1);
    build_eeprom();
    install(boot, 0u);
    tfw_boot_request_encode(req);
    memcpy(&avr->data[at], req, sizeof req);
    avr->data[0x54] = 0x01u;   /* PORF */

    check(run_until(app_booted, BUDGET_BOOT),
          "the application boots straight through");
    check(!session_announced(), "and no session was opened");
}

/*
 * A peer that never pauses. Junk at the full line rate leaves no empty receive
 * poll, so the idle-poll budget never advances; only the byte budget can end
 * the session. The application must still boot at about the configured time.
 */
static void test_junk_stream_cannot_park_the_session(elf_firmware_t *boot, elf_firmware_t *app)
{
    const uint8_t to_boot[1] = { 1u };
    /* One byte every 10 bit times at 115 200 baud and 16 MHz: the fastest the
     * wire can deliver, which is what the byte budget is derived from. */
    const unsigned cycles_per_byte = (unsigned)(F_CPU_HZ / (TFW_UART_BAUD / 10u));
    const uint64_t nominal = (uint64_t)TFW_BOOT_SESSION_IDLE_SECONDS * F_CPU_HZ;
    uint64_t start;

    printf("a continuous junk stream during a session:\n");
    build_image(app, 3u, -1);
    build_eeprom();
    install(boot, 0u);
    check(run_until(device_spoke, BUDGET_BOOT), "a healthy image boots first");
    send_and_wait(0x51, TFW_CMD_RESET, to_boot, 1u);
    check(run_until(session_announced, BUDGET_BOOT), "the session opens");

    from_device_n = 0;
    start = avr->cycle;
    while (!app_booted() && avr->cycle - start < nominal * 2u) {
        avr_raise_irq(uart_in, 0xEEu);          /* never SOF: no frame forms */
        run_cycles(cycles_per_byte);
        if (from_device_n > RXCAP - 64u) { from_device_n = 0; }
    }
    printf("  (junk-stream timeout: %llu cycles, nominal %llu)\n",
           (unsigned long long)(avr->cycle - start), (unsigned long long)nominal);
    check(app_booted(), "the byte budget ends the session and the application boots");
    check(avr->cycle - start <= nominal * 12u / 10u + BUDGET_BOOT,
          "no later than the configured time plus the verify");
}

/*
 * A peer that speaks the OTA grammar slowly. An OTA_BEGIN is accepted, restarts
 * the idle budgets, and touches no flash -- so a drip of them every 8 s, inside
 * the 10 s idle window, would hold the session open forever if only the idle
 * bounds applied. The ceiling progress cannot reset must end it, and because no
 * page was ever written the application it falls through to must still boot.
 */
static void test_valid_frame_drip_hits_the_ceiling(elf_firmware_t *boot, elf_firmware_t *app)
{
    const uint8_t to_boot[1] = { 1u };
    const uint8_t begin[4] = { 0x00, 0x10, 0x00, 0x00 };    /* 4096 bytes */
    const uint64_t ceiling = (uint64_t)TFW_BOOT_SESSION_MAX_SECONDS * F_CPU_HZ;
    const uint64_t period  = (uint64_t)(TFW_BOOT_SESSION_IDLE_SECONDS - 2u) * F_CPU_HZ;
    uint8_t  wire[64];
    unsigned n;
    uint64_t start, last_sent;

    printf("a drip of valid OTA frames during a session:\n");
    build_image(app, 3u, -1);
    build_eeprom();
    install(boot, 0u);
    check(run_until(device_spoke, BUDGET_BOOT), "a healthy image boots first");
    send_and_wait(0x61, TFW_CMD_RESET, to_boot, 1u);
    check(run_until(session_announced, BUDGET_BOOT), "the session opens");

    from_device_n = 0;
    start = last_sent = avr->cycle;
    while (!app_booted() && avr->cycle - start < ceiling * 2u) {
        if (avr->cycle - last_sent >= period) {
            last_sent = avr->cycle;
            n = frame_request(wire, 0x62, TFW_CMD_OTA_BEGIN, begin, sizeof begin);
            send(wire, n);
        }
        run_cycles(100000);
        if (from_device_n > RXCAP - 64u) { from_device_n = 0; }
    }
    printf("  (drip timeout: %llu cycles, ceiling %llu)\n",
           (unsigned long long)(avr->cycle - start), (unsigned long long)ceiling);
    check(app_booted(), "the ceiling ends the session and the application boots");
    check(avr->cycle - start >= ceiling * 8u / 10u,
          "not before 80% of the ceiling: the idle bound really was being restarted");
    check(avr->cycle - start <= ceiling * 12u / 10u + BUDGET_BOOT,
          "no later than the ceiling plus the verify");
}

/* ----------------------------------------- the BOOT_STORE=1 row (#75, #77)
 *
 * The external store on the emulated SPI bus: test/model/spi_nor_model.c, the
 * same 25-series part tfwsim and test_boot_stage use, behind its select, D8.
 * The model takes a whole transaction, the reply over the request; the bus
 * moves one byte at a time and the reply to each byte is due before the next.
 * A part's reply to byte n depends on bytes 0..n-1 only, so a read, a status
 * read and the JEDEC id are answered by replaying the transaction so far and
 * returning its last byte; they change nothing in the part, so replaying them
 * is the same transaction. Anything else is handed over once, when the select
 * rises, which is when a real part acts on a program or an erase: tfw-boot
 * sends none, and the counters say whether anything did.
 *
 * The select is read from the port and its direction both: tfw_boot_store_close
 * floats it, and a floating select is not a low one. simavr raises the pin's
 * IRQ on every PORTH and DDRH write, after the register holds the new value,
 * so that IRQ is the select's edge. The addresses are the
 * ATmega2560's register summary; the store row exists on that board alone. */
#define NOR_OP_READ    0x03u
#define NOR_OP_STATUS  0x05u
#define NOR_OP_JEDEC   0x9Fu
#define NOR_HEAD       4u                  /* the opcode and a 24-bit address */
#define M2560_DDRH     0x101u
#define M2560_PORTH    0x102u
#define STORE_CS_BIT   5u                  /* D8 = PH5, TFW_OTA_STORE_CS_IO */

static uint8_t  nor_tx[TFW_NOR_MODEL_XFER_MAX];
static uint16_t nor_n;
static int      nor_selected;
static int      nor_overrun;

static int nor_select_low(void)
{
    uint8_t bit = (uint8_t)(1u << STORE_CS_BIT);

    return (avr->data[M2560_DDRH] & bit) != 0 && (avr->data[M2560_PORTH] & bit) == 0;
}

static int nor_replayed(uint8_t op)
{
    return op == NOR_OP_READ || op == NOR_OP_STATUS || op == NOR_OP_JEDEC;
}

/* Diagnostic only (#75): what the boot did on the store bus, one line a boot.
 * It changes no check. */
enum { NC_READ, NC_PP, NC_SE, NC_WREN, NC_RDSR, NC_OTHER, NC_N };
static unsigned long nor_ops[NC_N];
static unsigned long nor_read_bytes;
static unsigned long nor_read_max;
static unsigned long nor_unselected;    /* bytes clocked with the select high */
static int           nor_attached;

static void nor_count(void)
{
    unsigned long addr;

    switch (nor_tx[0]) {
    case NOR_OP_READ:
        nor_ops[NC_READ]++;
        if (nor_n > NOR_HEAD) {
            addr = ((unsigned long)nor_tx[1] << 16) | ((unsigned long)nor_tx[2] << 8) | nor_tx[3];
            addr += (unsigned long)nor_n - NOR_HEAD - 1u;
            nor_read_bytes += (unsigned long)nor_n - NOR_HEAD;
            if (addr > nor_read_max) { nor_read_max = addr; }
        }
        break;
    case 0x02u:        nor_ops[NC_PP]++;    break;
    case 0x20u:        nor_ops[NC_SE]++;    break;
    case 0x06u:        nor_ops[NC_WREN]++;  break;
    case NOR_OP_STATUS: nor_ops[NC_RDSR]++; break;
    default:           nor_ops[NC_OTHER]++; break;
    }
}

static void store_bus_report(void)
{
    /* Every boot is a fresh core, so its cycles are the boot's alone; the
     * time is the real board's at its F_CPU (the Mega's 16 MHz where the
     * .elf does not say). */
    unsigned long hz;

    if (!nor_attached) {
        return;
    }
    nor_attached = 0;
    hz = avr->frequency ? (unsigned long)avr->frequency : 16000000UL;
    printf("  store bus: READ %lu (bytes %lu, max 0x%06lx), PP %lu, SE %lu, WREN %lu, RDSR %lu,"
           " other %lu; unselected %lu, overrun %d, open %u (op 0x%02x);"
           " boot steps %ld of a budget of %ld, cycles %llu (%llu ms at %lu Hz),"
           " cpu state %d\n",
           nor_ops[NC_READ], nor_read_bytes, nor_read_max, nor_ops[NC_PP], nor_ops[NC_SE],
           nor_ops[NC_WREN], nor_ops[NC_RDSR], nor_ops[NC_OTHER], nor_unselected, nor_overrun,
           nor_selected ? (unsigned)nor_n : 0u, nor_selected && nor_n ? nor_tx[0] : 0u,
           last_run_steps, boot_store_budget(), (unsigned long long)avr->cycle,
           (unsigned long long)(avr->cycle * 1000u / hz), hz, (int)avr->state);
}

static void nor_select_changed(void)
{
    uint8_t copy[TFW_NOR_MODEL_XFER_MAX];
    int     low = nor_select_low();

    if (nor_selected && !low && nor_n > 0u) {
        nor_count();
        if (!nor_replayed(nor_tx[0])) {
            memcpy(copy, nor_tx, nor_n);
            tfw_nor_model_xfer(copy, nor_n);
        }
    }
    if (low && !nor_selected) {
        nor_n = 0u;
    }
    nor_selected = low;
}

static void on_porth(struct avr_irq_t *irq, uint32_t value, void *param)
{
    (void)irq; (void)value; (void)param;
    nor_select_changed();
}

static void on_store_spi(struct avr_irq_t *irq, uint32_t value, void *param)
{
    uint8_t copy[TFW_NOR_MODEL_XFER_MAX];
    uint8_t reply = 0u;

    (void)irq; (void)param;
    nor_select_changed();
    if (!nor_selected) {
        nor_unselected++;
        return;
    }
    if (nor_n >= sizeof nor_tx) {
        nor_overrun++;
        return;
    }
    nor_tx[nor_n++] = (uint8_t)value;
    if (nor_replayed(nor_tx[0]) && !(nor_tx[0] == NOR_OP_READ && nor_n < NOR_HEAD)) {
        memcpy(copy, nor_tx, nor_n);
        tfw_nor_model_xfer(copy, nor_n);
        reply = copy[nor_n - 1u];
    }
    avr_raise_irq(avr_io_getirq(avr, AVR_IOCTL_SPI_GETIRQ(0), SPI_IRQ_INPUT), reply);
}

/* After install(): the part keeps its array across resets, as a real one does
 * across a power cycle, so a reset does not reload it. */
static void attach_store(void)
{
    nor_n = 0u;
    nor_selected = 0;
    memset(nor_ops, 0, sizeof nor_ops);
    nor_read_bytes = 0u;
    nor_read_max = 0u;
    nor_unselected = 0u;
    nor_attached = 1;
    avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_SPI_GETIRQ(0), SPI_IRQ_OUTPUT),
                            on_store_spi, NULL);
    avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('H'), STORE_CS_BIT),
                            on_porth, NULL);
}

static int store_untouched(void)
{
    return tfw_nor_model_erases() == 0u && tfw_nor_model_programs() == 0u
        && tfw_nor_model_violations() == 0u && nor_overrun == 0;
}

/* What the application's stager leaves for `version`: the image padded to
 * whole pages, its trailer last, and the MARK record naming the stream
 * (docs/ota/architecture.md, "The application stager"). Signed by the
 * harness's element, as every image in this suite is. Then the primary is
 * built back as `running`, which is what the device runs while it stages. */
static uint32_t stage_stream(const elf_firmware_t *app, uint32_t version, uint8_t *stream)
{
    uint32_t total, body;

    build_image(app, version, -1);
    total = (image_len + TFW_BOOT_TRAILER_LEN + 255u) & ~255u;
    body  = total - TFW_BOOT_TRAILER_LEN;
    if (total > TFW_OTA_STORE_STAGE_LEN) {
        fprintf(stderr, "the application does not fit STAGE\n");
        exit(2);
    }
    memcpy(stream, image, body);
    memcpy(&stream[body], &image[TFW_BOOT_TRAILER_ADDR], TFW_BOOT_TRAILER_LEN);
    return total;
}

static void stage_in_store(const elf_firmware_t *app, uint32_t running, uint32_t version,
                           long tamper_at)
{
    static uint8_t stream[TFW_OTA_STORE_STAGE_LEN];
    uint8_t  mark[TFW_STAGE_MARK_LEN];
    uint32_t total;

    total = stage_stream(app, version, stream);
    mark[0] = (uint8_t)TFW_STAGE_MARK_MAGIC;
    mark[1] = (uint8_t)(TFW_STAGE_MARK_MAGIC >> 8);
    mark[2] = (uint8_t)(TFW_STAGE_MARK_MAGIC >> 16);
    mark[3] = (uint8_t)(TFW_STAGE_MARK_MAGIC >> 24);
    mark[TFW_STAGE_MARK_O_LEN]      = (uint8_t)total;
    mark[TFW_STAGE_MARK_O_LEN + 1u] = (uint8_t)(total >> 8);
    mark[TFW_STAGE_MARK_O_LEN + 2u] = (uint8_t)(total >> 16);
    mark[TFW_STAGE_MARK_O_LEN + 3u] = (uint8_t)(total >> 24);
    memcpy(&mark[TFW_STAGE_MARK_O_HASH], &image[TFW_BOOT_TRAILER_ADDR + TFW_BOOT_TR_MEASUREMENT],
           TFW_BOOT_MEASURE_LEN);
    /* After the mark is made, as a store corrupted behind the stager's back. */
    if (tamper_at >= 0) {
        stream[tamper_at] ^= 0x01u;
    }

    tfw_nor_model_reset();
    nor_overrun = 0;
    tfw_nor_model_load(TFW_OTA_STORE_STAGE_ADDR, stream, total);
    tfw_nor_model_load(TFW_OTA_STORE_MARK_ADDR, mark, sizeof mark);
    build_image(app, running, -1);
}

static void boot_with_store(elf_firmware_t *boot, uint32_t counter)
{
    store_bus_report();
    install(boot, counter);
    attach_store();
}

/* Whether the block holds GOOD (when good is set) and FAILED; on a failure it
 * prints what it does hold, so the verify log says which way it went. */
static int block_failed(int good)
{
    tfw_boot_platform_t p;
    tfw_status_t        st;
    int                 ok;

    memset(&p, 0, sizeof p);
    st = stored_platform(&p);
    ok = st == TFW_OK && p.ota_state == TFW_BOOT_OTA_FAILED
                             && (!good || p.boot_state == TFW_BOOT_STATE_GOOD);

    if (!ok) {
        printf("  block: parse %d, state %u, ota %u, primary version %lu\n", (int)st,
               (unsigned)p.boot_state, (unsigned)p.ota_state,
               (unsigned long)primary_version());
    }
    return ok;
}

/* The round from the store to the floor: tfw-boot reads the staged image,
 * checks it, copies it into the secondary, swaps it in and runs its one
 * trial; a confirm settles it at the next reset and the floor advances. The
 * confirm is written into the platform block by the harness: OTA_CONFIRM is
 * not on the wire (its id is ADR decision 8's proposal), so this is the
 * state the application's command would leave, not the command. */
static void test_a_store_image_runs_its_trial_and_settles(elf_firmware_t *boot,
                                                          elf_firmware_t *app)
{
    tfw_boot_platform_t p;

    printf("store row: a staged image to its trial, confirmed:\n");
    stage_in_store(app, 2u, 3u, -1);
    build_eeprom_state(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_IDLE);
    boot_with_store(boot, 2u);
    check(run_until(app_booted, boot_store_budget()), "the staged image boots, on trial");
    check(primary_version() == 3u, "it was read from the store and swapped into the primary");
    check(stored_platform(&p) == TFW_OK && p.boot_state == TFW_BOOT_STATE_TRIAL,
          "TRIAL was stored before the jump");
    check(el.counter == 2u, "the trial leaves the floor where it was");
    check(store_untouched(), "tfw-boot only read the store, and every read was well formed");

    p.boot_state = TFW_BOOT_STATE_CONFIRMED;
    tfw_boot_platform_serialise(&p, eeprom_image);
    carry();
    boot_with_store(boot, el.counter);
    check(run_until(app_booted, boot_store_budget()), "the confirmed image boots at the next reset");
    check(el.counter == 3u, "the floor advanced to its version at the settling reset");
    check(stored_platform(&p) == TFW_OK && p.boot_state == TFW_BOOT_STATE_GOOD
          && p.ota_state == TFW_BOOT_OTA_IDLE, "settled: GOOD");

    carry();
    boot_with_store(boot, el.counter);
    check(run_until(app_booted, boot_store_budget()), "the next reset boots it again");
    check(memcmp(avr->flash, image, sizeof image) == 0,
          "and the mark still in the store copies nothing: it is installed");
    check(primary_version() == 3u && el.counter == 3u, "the settled image, the floor unmoved");
    check(store_untouched(), "and the store is as the stager left it");
}

/* The failure branch: no confirm, so the reset ends the trial, the slots swap
 * back and the round is FAILED. The mark is still in the store, and the same
 * image is not taken again: the boot side of "the image that last failed is
 * not activated again". */
static void test_an_unconfirmed_store_image_swaps_back(elf_firmware_t *boot,
                                                       elf_firmware_t *app)
{
    tfw_boot_platform_t p;

    printf("store row: a staged image whose trial is not confirmed:\n");
    stage_in_store(app, 2u, 3u, -1);
    build_eeprom_state(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_IDLE);
    boot_with_store(boot, 2u);
    check(run_until(app_booted, boot_store_budget()) && primary_version() == 3u,
          "the staged image boots");
    /* stored_platform() also refreshes eeprom_image, which install() writes
     * over the block at the next power-up: without it the reset that ends the
     * trial would find the pre-OTA GOOD/IDLE block, not this trial. */
    memset(&p, 0, sizeof p);
    check(stored_platform(&p) == TFW_OK && p.boot_state == TFW_BOOT_STATE_TRIAL,
          "on trial");

    carry();
    boot_with_store(boot, el.counter);
    check(run_until(app_booted, boot_store_budget()), "the reset that ends the trial boots");
    check(primary_version() == 2u, "the previous image, swapped back");
    check(block_failed(1), "GOOD, FAILED is stored");

    carry();
    boot_with_store(boot, el.counter);
    check(run_until(app_booted, boot_store_budget()), "and the reset after that boots");
    check(memcmp(avr->flash, image, sizeof image) == 0 && primary_version() == 2u,
          "the failed image, still marked in the store, is not copied again");
    check(block_failed(0), "FAILED is kept");
    check(el.counter == 2u, "an unconfirmed trial leaves the floor where it was");
    check(store_untouched(), "tfw-boot only read the store");
}

/*
 * The trial deadline (#77), with no reset from the harness. The application's
 * ota_trial.c counts TFW_OTA_TRIAL_TIMEOUT_S from the board profile -- the
 * board value, never a shorter one for speed -- and at expiry takes a
 * watchdog reset into tfw-boot, which finds TRIAL unconfirmed and swaps back.
 *
 * The reset is observed as the part shows it: WDRF (bit 3) in MCUSR (data
 * address 0x54), set by simavr's watchdog at the reset and cleared by
 * tfw-boot's .init3 a few instructions later, so it is polled every
 * instruction. simavr may zero avr->cycle at a reset, so the cycles are
 * summed step by step rather than subtracted.
 */
static int mcusr_wdrf(void) { return (avr->data[0x54] & 0x08u) != 0u; }

/* Runs to the reset edge -- the first instruction back at BOOTRST's entry --
 * and samples MCUSR there, before tfw-boot's .init3 can clear it. Returns the
 * cycles to the edge, or the budget if the part never reset. */
static uint64_t run_until_reset(uint64_t budget, int *wdrf)
{
    uint64_t elapsed = 0u;
    uint64_t last    = avr->cycle;

    *wdrf = 0;
    while (elapsed < budget && avr->state != cpu_Done && avr->state != cpu_Crashed) {
        avr_run(avr);
        elapsed += avr->cycle >= last ? avr->cycle - last : avr->cycle;
        last     = avr->cycle;
        if (avr->pc == avr->reset_pc) {
            *wdrf = mcusr_wdrf();
            return elapsed;
        }
    }
    return budget;
}

static void test_an_unconfirmed_store_image_rolls_back_at_the_deadline(
    elf_firmware_t *boot, elf_firmware_t *app)
{
    /* 60 s at 16 MHz is 960,000,000 cycles; the store boot before it measured
     * 85,354,510. The wait runs a second past the deadline plus one 8 s
     * watchdog period, so a late reset is measured and not cut off. */
    const uint64_t deadline = (uint64_t)TFW_OTA_TRIAL_TIMEOUT_S * F_CPU_HZ;
    const uint64_t wdt      = 8ull * F_CPU_HZ;
    tfw_boot_platform_t p;
    uint64_t            booted, to_reset;
    int                 wdrf;
    uint8_t             wire[64], sysinfo[64], sysinfo_len, status;
    unsigned            n;

    printf("store row: a trial nothing confirms, left to its deadline:\n");
    stage_in_store(app, 2u, 3u, -1);
    build_eeprom_state(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_IDLE);
    boot_with_store(boot, 2u);
    check(run_until(app_booted, boot_store_budget()) && primary_version() == 3u,
          "the staged image boots");
    booted = avr->cycle;               /* from power-on: install made the core */
    memset(&p, 0, sizeof p);
    check(stored_platform(&p) == TFW_OK && p.boot_state == TFW_BOOT_STATE_TRIAL,
          "on trial");

    to_reset = run_until_reset(deadline + wdt + F_CPU_HZ, &wdrf);
    printf("  reset %llu cycles after the boot event, deadline %llu\n",
           (unsigned long long)to_reset, (unsigned long long)deadline);
    check(to_reset < deadline + wdt + F_CPU_HZ, "the part resets by itself");
    check(wdrf, "the reset flags at the reset edge: WDRF, the watchdog");
    /* The count starts after power-on, so the reset cannot come before the
     * deadline does; nor later than one watchdog period after it. */
    check(booted + to_reset >= deadline && to_reset <= deadline + wdt,
          "the reset lands at the deadline, within one watchdog period");

    from_device_n = 0;
    check(run_until(app_booted, boot_store_budget()), "the reset boots");
    check(primary_version() == 2u, "the previous image, copied back from PREV");
    check(block_failed(1), "GOOD, FAILED is stored");
    check(el.counter == 2u, "an unconfirmed trial leaves the floor where it was");
    check(store_untouched(), "tfw-boot only read the store");

    /* The same cause, as tfw-boot latched it and handed it off. */
    from_device_n = 0;
    n = frame_request(wire, 0x37, TFW_CMD_SYS_INFO, NULL, 0u);
    send(wire, n);
    wait_for_seq = 0x37;
    run_until(response_arrived, BUDGET_EXCHANGE);
    check(find_response(0x37, &status, sysinfo, &sysinfo_len) && status == TFW_OK
          && sysinfo_len >= 7u && sysinfo[6] == 4u /* TFW_RESET_WATCHDOG */,
          "SYS_INFO reports the watchdog reset");
}

/* What the boot refuses from the store before a page of the application
 * region is written: a staged byte that does not match the measurement, and
 * an image below the floor. The primary runs on. */
static void test_a_bad_store_image_copies_nothing(elf_firmware_t *boot,
                                                  elf_firmware_t *app)
{
    tfw_boot_platform_t p;

    printf("store row: staged images the boot refuses:\n");
    stage_in_store(app, 2u, 3u, 1000);
    build_eeprom_state(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_IDLE);
    boot_with_store(boot, 2u);
    check(run_until(app_booted, boot_store_budget()), "a staged image that fails its hash: the primary boots");
    check(memcmp(avr->flash, image, sizeof image) == 0, "and no page was written");
    check(stored_platform(&p) == TFW_OK && p.boot_state == TFW_BOOT_STATE_GOOD,
          "the block still says GOOD");

    stage_in_store(app, 2u, 1u, -1);
    build_eeprom_state(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_IDLE);
    boot_with_store(boot, 2u);
    check(run_until(app_booted, boot_store_budget()), "a staged image below the floor: the primary boots");
    check(memcmp(avr->flash, image, sizeof image) == 0 && primary_version() == 2u,
          "and no page was written");
    check(el.counter == 2u && store_untouched(), "the floor and the store unmoved");
}

/* ------------------------------------------------------- the host's half (#75)
 *
 * The bridge mode: UART0 is this process's stdin and stdout, so the Go SDK's
 * (*Device).StageImage streams an image into the running application's stager,
 * which writes it to the modelled store -- the path stage_in_store stands in
 * for above. SIGUSR1 is the reset: the bridge stops, tfw-boot runs, and the
 * boot is asserted here, from the EEPROM block, as the store row asserts it;
 * the host has no wire command that reads the trial state (OTA_INFO, unbuilt).
 * Driven by sdk/go/cmd/tfwctl/ota_stage_simavr_test.go, mk/boot.mk.
 *
 * The image is this harness's, signed by its element, so it is written to
 * `out` for the host to send. One byte per 2000 steps: more than a character
 * time at 115200, a tenth of send()'s pace, because an image is many frames. */
#define BRIDGE_BYTE_STEPS 2000L

static volatile sig_atomic_t bridge_reset;

static void on_reset_signal(int sig)
{
    (void)sig;
    bridge_reset = 1;
}

static void bridge_flush(FILE *host)
{
    if (from_device_n > 0u) {
        (void)fwrite(from_device, 1u, from_device_n, host);
        (void)fflush(host);
        from_device_n = 0u;
    }
}

static void test_a_host_staged_image_runs_its_trial(elf_firmware_t *boot,
                                                    elf_firmware_t *app, const char *out)
{
    static uint8_t stream[TFW_OTA_STORE_STAGE_LEN];
    char     tmp[4096];
    uint32_t total;
    FILE    *f, *host;
    int      fd, eof = 0;
    uint8_t  b;
    tfw_boot_platform_t p;

    /* stdout is the UART from here on; the report goes to stderr. */
    fd = dup(1);
    if (fd < 0 || dup2(2, 1) < 0 || (host = fdopen(fd, "wb")) == NULL) {
        fprintf(stderr, "cannot hand stdout to the UART\n");
        exit(2);
    }
    if (signal(SIGUSR1, on_reset_signal) == SIG_ERR
        || fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK) < 0) {
        fprintf(stderr, "cannot set up the bridge\n");
        exit(2);
    }

    printf("store row, from the host: an image staged over the UART to its trial:\n");
    total = stage_stream(app, 3u, stream);
    (void)snprintf(tmp, sizeof tmp, "%s.tmp", out);
    f = fopen(tmp, "wb");
    if (f == NULL || fwrite(stream, 1u, total, f) != total || fclose(f) != 0
        || rename(tmp, out) != 0) {
        fprintf(stderr, "cannot write the image to %s\n", out);
        exit(2);
    }

    build_image(app, 2u, -1);
    tfw_nor_model_reset();
    nor_overrun = 0;
    build_eeprom_state(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_IDLE);
    boot_with_store(boot, 2u);
    check(run_until(app_booted, boot_store_budget()), "the running image boots, the store empty");
    from_device_n = 0u;

    while (!bridge_reset) {
        if (!eof) {
            ssize_t n = read(0, &b, 1u);
            if (n == 1) {
                avr_raise_irq(uart_in, b);
            } else if (n == 0) {
                eof = 1;
            }
        }
        run_cycles(BRIDGE_BYTE_STEPS);
        bridge_flush(host);
    }
    (void)fclose(host);

    printf("  reset\n");
    carry();
    boot_with_store(boot, el.counter);
    check(run_until(app_booted, boot_store_budget()), "the staged image boots, on trial");
    check(primary_version() == 3u, "it was read from the store and swapped into the primary");
    check(stored_platform(&p) == TFW_OK && p.boot_state == TFW_BOOT_STATE_TRIAL,
          "TRIAL was stored before the jump");
    check(el.counter == 2u, "the trial leaves the floor where it was");
    check(tfw_nor_model_violations() == 0u && nor_overrun == 0,
          "every store access, the stager's and tfw-boot's, was well formed");
}

/* ------------------------------------------------- the network leg (#75, #55)
 *
 * The same store row with the update arriving over TCP: the application built
 * with ETH=1 (the stager is ETH's default where the board names an OTA_STORE),
 * the W5100 on the emulated SPI bus behind D10, beside the NOR part behind D8,
 * and this harness the TCP peer the device connects out to.
 *
 * The part is test/model/w5x00_model.c, the one the driver's host tests run
 * against, and not a second model. It is written at the HAL's transfer call,
 * a whole frame at a time; the bus delivers a byte at a time, so a frame is
 * gathered under its select and handed to the model at its fourth byte, which
 * is where the part answers a read. The first three answers are the model's
 * own, taken once from a read of MR. A W5100 frame is four bytes and no more;
 * anything longer under one select is counted, not guessed at. */
#define M2560_DDRB      0x24u
#define M2560_PORTB     0x25u
#define ETH_CS_BIT      4u                  /* D10 = PB4, TFW_ETH_CS_IO */
#define W5X_FRAME_LEN   4u
#define W5X_S0_SR       0x03u               /* socket 0's status register */
#define W5X_SR_SYNSENT  0x15u
#define W5X_SR_ESTAB    0x17u
#define NET_CHUNK       (TFW_MAX_PAYLOAD - 4u)

/* Steps, as every budget here. Up is the chip's reset and the connect, which
 * the link starts on its first poll; a frame is one TCP exchange and its store
 * write. OTA_END reads the whole stream back and checks it, as tfw-boot's copy
 * does, and gets the store row's budget. */
#define BUDGET_NET_UP     5000000L
#define BUDGET_NET_FRAME  2000000L

static const tfw_pinref_t eth_cs = { .bit = ETH_CS_BIT };
static uint8_t  eth_tx[W5X_FRAME_LEN];
static unsigned eth_n;
static int      eth_selected;
static uint8_t  eth_echo[W5X_FRAME_LEN - 1u];
static int      eth_overrun;
static int      eth_accepts;

static int eth_select_low(void)
{
    uint8_t bit = (uint8_t)(1u << ETH_CS_BIT);

    return (avr->data[M2560_DDRB] & bit) != 0 && (avr->data[M2560_PORTB] & bit) == 0;
}

static void eth_select_changed(void)
{
    int low = eth_select_low();

    if (low && !eth_selected) {
        eth_n = 0u;
    }
    eth_selected = low;
}

static void on_portb(struct avr_irq_t *irq, uint32_t value, void *param)
{
    (void)irq; (void)value; (void)param;
    eth_select_changed();
}

static void on_eth_spi(struct avr_irq_t *irq, uint32_t value, void *param)
{
    uint8_t rd[W5X_FRAME_LEN];
    uint8_t reply;

    (void)irq; (void)param;
    eth_select_changed();
    if (!eth_selected) {
        return;
    }
    if (eth_n >= sizeof eth_tx) {
        eth_overrun++;
        return;
    }
    eth_tx[eth_n++] = (uint8_t)value;
    if (eth_n < W5X_FRAME_LEN) {
        reply = eth_echo[eth_n - 1u];
    } else {
        (void)tfw_hal_spi_xfer(&eth_cs, eth_tx, rd, W5X_FRAME_LEN);
        reply = rd[W5X_FRAME_LEN - 1u];
    }
    avr_raise_irq(avr_io_getirq(avr, AVR_IOCTL_SPI_GETIRQ(0), SPI_IRQ_INPUT), reply);
}

static void attach_eth(void)
{
    static const uint8_t read_mr[W5X_FRAME_LEN] = { 0x0Fu, 0x00u, 0x00u, 0x00u };
    uint8_t rd[W5X_FRAME_LEN];

    w5x_model_reset(W5X_MODEL_W5100);
    (void)tfw_hal_spi_init(0u, 0u, 0u);     /* mode 0, MSB first: what the AVR HAL sets */
    (void)tfw_hal_spi_xfer(&eth_cs, read_mr, rd, W5X_FRAME_LEN);
    memcpy(eth_echo, rd, sizeof eth_echo);
    eth_n = 0u;
    eth_selected = 0;
    eth_overrun = 0;
    eth_accepts = 0;
    avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_SPI_GETIRQ(0), SPI_IRQ_OUTPUT),
                            on_eth_spi, NULL);
    avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('B'), ETH_CS_BIT),
                            on_portb, NULL);
}

/* The network settings the link reads at TFW_NETCFG_ADDR (include/tfw/eth.h):
 * a device at 192.168.1.50 connecting out to the harness at .10, port 9000. */
static void provision_netcfg(void)
{
    uint8_t raw[TFW_NETCFG_LEN];
    uint16_t crc;
    static const uint8_t mac[6] = { 0x02u, 0x54u, 0x46u, 0x57u, 0x00u, 0x01u };
    avr_eeprom_desc_t d = { .ee = raw, .offset = TFW_NETCFG_ADDR, .size = TFW_NETCFG_LEN };

    memset(raw, 0, sizeof raw);
    raw[0] = 'T'; raw[1] = 'F'; raw[2] = 'N'; raw[3] = 'C';
    raw[4] = TFW_NETCFG_VERSION;
    memcpy(&raw[6], mac, sizeof mac);
    raw[12] = 192u; raw[13] = 168u; raw[14] = 1u;   raw[15] = 50u;
    raw[16] = 255u; raw[17] = 255u; raw[18] = 255u; raw[19] = 0u;
    raw[20] = 192u; raw[21] = 168u; raw[22] = 1u;   raw[23] = 1u;
    raw[24] = 192u; raw[25] = 168u; raw[26] = 1u;   raw[27] = 10u;
    raw[28] = (uint8_t)(9000u >> 8); raw[29] = (uint8_t)(9000u & 0xFFu);
    crc = tfw_crc16(raw, 30u);
    raw[30] = (uint8_t)(crc & 0xFFu);
    raw[31] = (uint8_t)(crc >> 8);
    avr_ioctl(avr, AVR_IOCTL_EEPROM_SET, &d);
}

/* The peer's side of the wire, run between steps: take the connect, and move
 * what the device SENT into from_device, where find_response reads frames. */
static void net_pump(void)
{
    if (w5x_model_sock(W5X_S0_SR) == W5X_SR_SYNSENT) {
        w5x_model_peer_accept();
        eth_accepts++;
    }
    if (from_device_n < RXCAP) {
        from_device_n += w5x_model_peer_recv(&from_device[from_device_n],
                                             (uint16_t)(RXCAP - from_device_n));
    }
}

static int net_up(void)
{
    net_pump();
    return eth_accepts > 0 && w5x_model_sock(W5X_S0_SR) == W5X_SR_ESTAB;
}

static int net_answered(void)
{
    uint8_t status, plen, buf[256];

    net_pump();
    return find_response(wait_for_seq, &status, buf, &plen);
}

/* One request over TCP and its answer's status, or -1 for none in budget. One
 * frame in flight at a time, so the model's 2 KB RX buffer never wraps. */
static int net_exchange(uint8_t seq, uint8_t cmd, const uint8_t *payload, uint8_t plen,
                        long budget)
{
    uint8_t  wire[TFW_MAX_PAYLOAD + 8u];
    uint8_t  status, rlen, buf[256];
    unsigned n = frame_request(wire, seq, cmd, payload, plen);

    from_device_n = 0u;
    w5x_model_peer_send(wire, (uint16_t)n);
    wait_for_seq = seq;
    if (!run_until(net_answered, budget) || !find_response(seq, &status, buf, &rlen)) {
        return -1;
    }
    return status;
}

static void put_u32(uint8_t *out, uint32_t v)
{
    out[0] = (uint8_t)v;
    out[1] = (uint8_t)(v >> 8);
    out[2] = (uint8_t)(v >> 16);
    out[3] = (uint8_t)(v >> 24);
}

/*
 * The function a PC in tfw-boot lies in: the nearest symbol at or below it,
 * looked for only inside the boot section, where the absolute symbols that
 * spoil a whole-table nearest lookup (region lengths, register aliases) are
 * not. Both the symbols and the PC are byte addresses.
 */
static const char *boot_symbol_at(const elf_firmware_t *boot, uint32_t pc, uint32_t *off)
{
    const char *name = "an unnamed address";
    uint32_t    best = 0u, i;

    for (i = 0u; i < boot->symbolcount; i++) {
        uint32_t a = boot->symbol[i]->addr;

        if (a >= boot->flashbase && a <= pc && a >= best && boot->symbol[i]->symbol[0] != '\0') {
            best = a;
            name = boot->symbol[i]->symbol;
        }
    }
    *off = pc - best;
    return name;
}

/* The swap record (boot_swap.c) as tfw-boot last wrote it: phase, then unit. */
static void print_swap_record(void)
{
    avr_eeprom_desc_t d = { .ee = NULL, .offset = TFW_SWAP_RECORD_ADDR,
                            .size = TFW_BOOT_EE_EXTRA_LEN };
    uint32_t i;

    (void)avr_ioctl(avr, AVR_IOCTL_EEPROM_GET, &d);
    printf("  swap record:");
    for (i = 0u; d.ee != NULL && i < TFW_BOOT_EE_EXTRA_LEN; i++) {
        printf(" %02x", d.ee[i]);
    }
    printf("\n");
}

static void test_a_network_staged_image_runs_its_trial(elf_firmware_t *boot,
                                                       elf_firmware_t *app)
{
    static uint8_t stream[TFW_OTA_STORE_STAGE_LEN];
    uint8_t  pl[TFW_MAX_PAYLOAD];
    uint32_t total, off, n = 0u;
    uint8_t  seq = 0u;
    int      st, booted, stored;
    tfw_boot_platform_t p;

    printf("store row, over the network: an image staged through the W5100 to its trial:\n");
    total = stage_stream(app, 3u, stream);

    build_image(app, 2u, -1);
    tfw_nor_model_reset();
    nor_overrun = 0;
    build_eeprom_state(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_IDLE);
    boot_with_store(boot, 2u);
    provision_netcfg();
    attach_eth();
    check(run_until(app_booted, boot_store_budget()), "the running image boots, the store empty");
    check(run_until(net_up, BUDGET_NET_UP),
          "it connects out through the W5100 and the harness takes the connection");

    put_u32(pl, total);
    st = net_exchange(++seq, TFW_CMD_OTA_BEGIN, pl, 4u, BUDGET_NET_FRAME);
    check(st == TFW_OK, "OTA_BEGIN over TCP is taken");
    for (off = 0u; off < total && st == TFW_OK; off += n) {
        n = total - off;
        if (n > NET_CHUNK) {
            n = NET_CHUNK;
        }
        put_u32(pl, off);
        memcpy(&pl[4], &stream[off], n);
        st = net_exchange(++seq, TFW_CMD_OTA_DATA, pl, (uint8_t)(4u + n), BUDGET_NET_FRAME);
    }
    if (st != TFW_OK) {
        printf("  OTA_DATA at offset %lu answered %d\n", (unsigned long)off, st);
    }
    check(st == TFW_OK, "every OTA_DATA over TCP is taken, in order");
    check(net_exchange(++seq, TFW_CMD_OTA_END, NULL, 0u, boot_store_budget()) == TFW_OK,
          "OTA_END over TCP checks the stream and marks the store");

    printf("  reset\n");
    carry();
    boot_with_store(boot, el.counter);
    attach_eth();
    booted = run_until(app_booted, boot_store_budget());
    check(booted, "the staged image boots, on trial");
    check(primary_version() == 3u, "it was read from the store and swapped into the primary");
    stored = stored_platform(&p) == TFW_OK;
    check(stored && p.boot_state == TFW_BOOT_STATE_TRIAL, "TRIAL was stored before the jump");
    if (!booted) {
        /* Where it stopped: a PC in the boot section is tfw-boot still at
         * work; W5100 register writes mean the application ran, since
         * tfw-boot only holds its select high. */
        printf("  after the reset: pc 0x%05lx (boot section from 0x%05lx), boot_state %u,"
               " ota_state %u, UART bytes %u, W5100 writes %lu\n",
               (unsigned long)avr->pc, (unsigned long)boot->flashbase,
               stored ? (unsigned)p.boot_state : 0xFFu, stored ? (unsigned)p.ota_state : 0xFFu,
               from_device_n, (unsigned long)w5x_model_writes());
        const char *fn = boot_symbol_at(boot, avr->pc, &off);

        /* The offset is read only once the lookup has set it: as two
         * arguments of one call the order is unspecified, and the last run
         * printed the loop's offset beside the right name. */
        printf("  in %s+0x%lx\n", fn, (unsigned long)off);
        print_swap_record();
        /* Every image hash on this tier is the element's SHA engine, one
         * command per 64-byte block, so the boot's time grows with the image:
         * how many whole-image hashes the steps went on. */
        printf("  element: %u SHA blocks (%lu per %lu-byte image), %u verifies,"
               " %lu steps\n", el.sha_blocks, (unsigned long)((image_len - 1u) / 64u + 2u),
               (unsigned long)image_len, el.verify, (unsigned long)last_run_steps);
    }
    check(el.counter == 2u, "the trial leaves the floor where it was");
    check(tfw_nor_model_violations() == 0u && nor_overrun == 0,
          "every store access, the stager's and tfw-boot's, was well formed");
    check(eth_overrun == 0, "every W5100 frame was four bytes under its select");
}

int main(int argc, char **argv)
{
    elf_firmware_t boot, app;
    const char    *boot_elf = argc > 1 ? argv[1] : 0;
    const char    *app_elf  = argc > 2 ? argv[2] : 0;

    if (!boot_elf || !app_elf) {
        fprintf(stderr, "usage: boot_harness <tfw-boot.elf> <application.elf>\n");
        return 2;
    }

    memset(&boot, 0, sizeof boot);
    memset(&app, 0, sizeof app);
    if (elf_read_firmware(boot_elf, &boot) != 0) {
        fprintf(stderr, "cannot read %s\n", boot_elf);
        return 2;
    }
    if (elf_read_firmware(app_elf, &app) != 0) {
        fprintf(stderr, "cannot read %s\n", app_elf);
        return 2;
    }
    if (app.flashsize == 0u || app.flashsize > TFW_BOOT_MAX_IMAGE_LEN) {
        fprintf(stderr, "%s does not fit the application region\n", app_elf);
        return 2;
    }
    /* The .elf carries no clock speed, so it is supplied here -- and it must
     * match the -DF_CPU the firmware was built with, or every baud divisor and
     * every TWI bit rate in the image is computed against a different clock
     * than the one it runs on. */
    boot.frequency = F_CPU_HZ;
    strcpy(boot.mmcu, TFW_SIMAVR_MCU);

    printf("simavr tfw-boot integration: %s + %s on %s\n",
           boot_elf, app_elf, TFW_SIMAVR_MCU);

    /* The BOOT_STORE=1 row has no session in the boot, so the session tests
     * are not its tests; the row is named by the caller, mk/boot.mk. */
    if (argc > 4 && strcmp(argv[3], "bridge") == 0) {
        test_a_host_staged_image_runs_its_trial(&boot, &app, argv[4]);
        store_bus_report();
        printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
        return failures ? 1 : 0;
    }
    if (argc > 3 && strcmp(argv[3], "net") == 0) {
        test_a_network_staged_image_runs_its_trial(&boot, &app);
        store_bus_report();
        printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
        return failures ? 1 : 0;
    }
    if (argc > 3 && strcmp(argv[3], "store") == 0) {
        test_a_store_image_runs_its_trial_and_settles(&boot, &app);
        test_an_unconfirmed_store_image_swaps_back(&boot, &app);
        test_an_unconfirmed_store_image_rolls_back_at_the_deadline(&boot, &app);
        test_a_bad_store_image_copies_nothing(&boot, &app);
        store_bus_report();
        printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
        return failures ? 1 : 0;
    }

    test_signed_image_boots(&boot, &app);
    test_floor_advances_at_settle(&boot, &app);
    test_a_refused_floor_advance_is_retried(&boot, &app);
    test_an_unreadable_floor_is_not_the_eeprom_copy(&boot, &app);
    test_an_unsettled_confirm_refuses_a_transfer(&boot, &app);
    test_a_refused_pending_image_never_boots(&boot, &app);
    test_tampered_image_refuses(&boot, &app);
    test_downgrade_refuses(&boot, &app);
    test_another_boards_image_refuses(&boot, &app);
    test_requested_update_session(&boot, &app);
    test_request_after_power_on_is_ignored(&boot, &app);
    test_junk_stream_cannot_park_the_session(&boot, &app);
    test_valid_frame_drip_hits_the_ceiling(&boot, &app);

    printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
