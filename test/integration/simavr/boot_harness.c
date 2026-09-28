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
 * ag-boot.elf into a real ATmega model, puts the real application image and a
 * real trailer in the emulated flash, a real provisioned region in the emulated
 * EEPROM and a real ATECC608A on the emulated TWI bus, and proves the thing
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <simavr/sim_avr.h>
#include <simavr/sim_elf.h>
#include <simavr/sim_io.h>
#include <simavr/avr_uart.h>
#include <simavr/avr_eeprom.h>
#include <simavr/avr_twi.h>

#include "ag/status.h"
#include "ag/protocol.h"
#include "ag/boot.h"
#include "boot_image.h"
#include "boot_platform.h"
#include "sha256.h"
#include "crc16.h"

/* The element's CRC, from the driver rather than from a second copy of it.
 * Declared rather than included, because atecc.h also declares the transport
 * this file exists to replace. */
uint16_t ag_atecc_crc16(const uint8_t *data, uint8_t len);

/*
 * The element driver is linked for that one function, and a C object cannot be
 * linked a function at a time. These three resolve the references its other
 * functions make -- ag_hal_i2c_init joined the set when ag_atecc_wake()
 * started reasserting the element's required bus rate itself, rather than
 * trusting whatever the generic I2C_CONFIG command last left it at. Reaching
 * any of them is a wiring mistake and not a case to handle: this harness sits
 * BELOW the HAL -- it is the wire -- so anything in this process calling into
 * the HAL has bypassed the emulated part entirely and whatever it then proved
 * would be about nothing.
 */
ag_status_t ag_hal_i2c_xfer(uint8_t addr7, const uint8_t *wr, uint8_t wlen,
                            uint8_t *rd, uint8_t rlen);
ag_status_t ag_hal_i2c_init(uint32_t hz);
void        ag_hal_delay_us(uint16_t us);

ag_status_t ag_hal_i2c_xfer(uint8_t addr7, const uint8_t *wr, uint8_t wlen,
                            uint8_t *rd, uint8_t rlen)
{
    (void)addr7; (void)wr; (void)wlen; (void)rd; (void)rlen;
    fprintf(stderr, "the harness called the AVR I2C HAL; it is the wire, not a host\n");
    abort();
}

ag_status_t ag_hal_i2c_init(uint32_t hz)
{
    (void)hz;
    fprintf(stderr, "the harness called the AVR I2C HAL; it is the wire, not a host\n");
    abort();
}

void ag_hal_delay_us(uint16_t us)
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
static int run_until(int (*done)(void), long budget)
{
    for (long i = 0; i < budget; i++) {
        if (done()) { return 1; }
        avr_run(avr);
    }
    return done();
}

/* A whole boot: measure ~12 KB sixty-four bytes at a time through the element,
 * verify, derive the CDI, advance the counter and jump. Measured at 8.4 M
 * cycles; the budget is loose because it bounds a hang, not a performance
 * target, and a tight one would fail on a slower emulator for no reason. */
#define BUDGET_BOOT     40000000L
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

/* --------------------------------------------- an ATECC608A on the wire */
/*
 * A SECOND MODEL OF THE SAME PART, AND THAT IS DELIBERATE.
 *
 * test/model/atecc608a.c models the element at the ag_hal_i2c_xfer seam, which
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
 * bootloader never reads or writes them; provisioning is agprov's, and
 * test/model/atecc608a.c is where that is exercised. And it is not timing: the
 * part is modelled as busy for exactly one read after each command, which
 * exercises the driver's NACK-poll without spending emulator cycles on a wait
 * that is not the thing under test.
 */

#define EL_ADDR             0x60u
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
    ag_sha256(buf, 64u, sig);

    memcpy(buf, digest, 32);
    memcpy(&buf[32], EL_SIGNING_KEY, 32);
    ag_sha256(buf, 64u, &sig[32]);
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
    ag_sha256_t sha;
    int         sha_active;
    uint32_t    counter;

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
    crc = ag_atecc_crc16(el.rsp, (uint8_t)(n + 1u));
    el.rsp[n + 1u] = (uint8_t)(crc & 0xFFu);
    el.rsp[n + 2u] = (uint8_t)(crc >> 8);
    el.rsp_n = (unsigned)n + 3u;
}

static void el_status(uint8_t code)
{
    uint16_t crc;
    el.rsp[0] = 4u;
    el.rsp[1] = code;
    crc = ag_atecc_crc16(el.rsp, 2u);
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
    crc = ag_atecc_crc16(&p[1], (uint8_t)(count - 2u));
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
                   * nobody could identify. */
        body[0] = 0x00u; body[1] = 0x00u; body[2] = 0x60u; body[3] = 0x02u;
        el_respond(body, 4u);
        return;

    case 0x47u:   /* SHA */
        if (p1 == 0x00u) {                        /* SHA256 Start */
            ag_sha256_init(&el.sha);
            el.sha_active = 1;
            el_status(0x00u);
            return;
        }
        if (p1 == 0x01u && el.sha_active) {       /* SHA256 Update */
            /* Whole blocks only, which is what the part accepts. A driver that
             * sent a short Update would pass against a lenient model and fail
             * on silicon, so the refusal is worth more than the tolerance. */
            if (p2 != 64u || dlen != 64u) { el_status(0x03u); return; }
            ag_sha256_update(&el.sha, &p[6], 64u);
            el.sha_blocks++;
            el_status(0x00u);
            return;
        }
        if (p1 == 0x02u && el.sha_active) {       /* SHA256 End */
            uint8_t tail = (uint8_t)p2;
            if (tail != dlen) { el_status(0x03u); return; }
            if (tail) { ag_sha256_update(&el.sha, &p[6], tail); }
            ag_sha256_final(&el.sha, body);
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
            ag_hmac_sha256(EL_UDS, 32u, &p[6], p2, body);
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
 * The part wakes on SDA held low for at least 60 us, which ag_atecc_wake
 * produces as the low period of a byte written to address zero -- nine bit
 * times. At 100 kHz that is 90 us and the part wakes; above it the pulse is too
 * short and the part stays asleep, which is why src/crypto/atecc608a/atecc.c
 * says the element's bus must be at or below 100 kHz.
 *
 * Whether it is depends on TWBR, and TWBR is a register. A model at the
 * ag_hal_i2c_xfer seam never sees one -- test/model/atecc608a.c's
 * ag_hal_i2c_init takes the rate and returns AG_OK either way, because above
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
     * with. It matters because ag_atecc_wake reads the token ONCE and does not
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
 * THE SIGNING TOOL, WRITTEN OUT IN A TEST, exactly as
 * test/unit/boot/test_boot_policy.c writes it out on the host.
 *
 * The point is that the two halves are run against each other: this composes a
 * trailer with the host's ag_boot_trailer_serialise and an x86 compiler, and
 * the device parses it with avr-gcc's copy of ag_boot_trailer_parse out of real
 * program memory. A test that hand-wrote the bytes would only prove the parser
 * reads what the test wrote.
 *
 * THE APPLICATION IS THE REAL ONE. `image` holds the firmware .elf this same CI
 * run built, with a trailer at AG_BOOT_TRAILER_ADDR and erased flash between --
 * two images in one flash, which is the arrangement on a real part. It has to
 * be the real one: "a correctly signed image boots" is observed by the
 * application answering CAPS after the jump, and only code that runs can do
 * that.
 */
static uint8_t image[AG_BOOT_APP_END];
static uint8_t eeprom_image[AG_BOOT_EE_LEN];
static uint32_t image_len;

/* ag_sha256 takes a uint16_t length and an application region does not fit in
 * one. Chunked here rather than widened there: the device hashes 64 bytes at a
 * time through the element and never needs the wider call. */
static void sha256_span(const uint8_t *p, uint32_t n, uint8_t out[32])
{
    ag_sha256_t ctx;
    uint32_t    off = 0u;

    ag_sha256_init(&ctx);
    while (off < n) {
        uint32_t left = n - off;
        uint16_t take = left > 32768uL ? (uint16_t)32768u : (uint16_t)left;
        ag_sha256_update(&ctx, &p[off], take);
        off += take;
    }
    ag_sha256_final(&ctx, out);
}

static void build_image(const elf_firmware_t *app, uint32_t version, long tamper_at)
{
    ag_boot_trailer_t tr;
    uint8_t           raw[AG_BOOT_TRAILER_LEN];
    uint8_t           prefix_digest[32];

    memset(image, 0xFF, sizeof image);            /* erased flash, not zeros */
    memcpy(image, app->flash, app->flashsize);
    image_len = app->flashsize;

    memset(&tr, 0, sizeof tr);
    tr.image_version = version;
    tr.image_len     = image_len;
    sha256_span(image, image_len, tr.measurement);

    /* The signature covers the trailer PREFIX -- magic, layout version, image
     * version, image length and the measurement -- not the measurement alone.
     * Signing the measurement by itself would leave the version
     * unauthenticated and hand an attacker anti-rollback for a four-byte edit;
     * see include/ag/boot.h. */
    ag_boot_trailer_serialise(&tr, raw);
    ag_sha256(raw, AG_BOOT_TR_SIGNED_LEN, prefix_digest);
    element_sign(prefix_digest, &raw[AG_BOOT_TR_SIGNATURE]);

    memcpy(&image[AG_BOOT_TRAILER_ADDR], raw, AG_BOOT_TRAILER_LEN);

    /* The tamper goes in LAST, so the trailer still claims the measurement of
     * the image nobody edited -- which is the situation a real attacker leaves
     * behind and the one the bootloader has to notice. */
    if (tamper_at >= 0) {
        image[tamper_at] ^= 0x01u;
    }
}

static void build_eeprom(void)
{
    ag_boot_platform_t plat;

    memset(eeprom_image, 0xFF, sizeof eeprom_image);
    memset(&plat, 0, sizeof plat);
    /*
     * pubkey_hash is left ZERO on purpose and the device still boots.
     *
     * On the element tier there is no key for the bootloader to hash: it lives
     * in a locked element slot and is named by number, so ag_boot_pubkey
     * refuses and the EEPROM pin is not this bootloader's to check. Filling
     * this field with something plausible would hide the fact that nothing
     * reads it, and would make a regression that started reading it look like
     * a pass.
     */
    plat.boot_state = AG_BOOT_STATE_GOOD;
    ag_boot_platform_serialise(&plat, eeprom_image);
}

static void install(elf_firmware_t *boot, uint32_t counter)
{
    avr = avr_make_mcu_by_name(AG_SIMAVR_MCU);
    if (!avr) { fprintf(stderr, "no simavr core for %s\n", AG_SIMAVR_MCU); exit(2); }
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
    if (avr->pc == 0u) {
        fprintf(stderr, "ag-boot.elf has no boot-section entry point\n");
        exit(2);
    }

    /* The application image goes straight into the emulated program memory,
     * BELOW the bootloader the .elf just placed at AG_BOOT_APP_END. */
    memcpy(avr->flash, image, AG_BOOT_APP_END);

    {
        avr_eeprom_desc_t d = { .ee = eeprom_image, .offset = AG_BOOT_EE_BASE,
                                .size = AG_BOOT_EE_LEN };
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
    check(el.counter == 3u,
          "the rollback floor advanced in the element's counter, not in EEPROM");

    from_device_n = 0;
    n = frame_request(wire, 0x31, AG_CMD_CAPS, NULL, 0);
    send(wire, n);
    run_cycles(BUDGET_EXCHANGE);
    check(find_response(0x31, &status, out, &outlen) && status == AG_OK,
          "CAPS answers, so the APPLICATION is running");
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
    check(find_event(&status) && status == AG_ERR_CRYPTO,
          "refused with AG_ERR_CRYPTO");

    /* WHERE it refused, and this is the part a status byte cannot tell you. The
     * measurement is compared before the signature is checked, so a tampered
     * image must cost the element a full hash and no Verify at all -- if Verify
     * ran, the ordering in ag_boot_evaluate has changed and the expensive check
     * is now happening on images that never needed it. */
    check(el.sha_blocks > 0, "the element was asked to measure the image");
    check(el.verify == 0,
          "and was never asked to verify: the measurement caught it first");

    {
        uint8_t wire[64], out[64], outlen;
        unsigned n;
        from_device_n = 0;
        n = frame_request(wire, 0x32, AG_CMD_CAPS, NULL, 0);
        send(wire, n);
        run_cycles(BUDGET_EXCHANGE);
        check(find_response(0x32, &status, out, &outlen)
              && status == AG_ERR_UNKNOWN_CMD,
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
    check(find_event(&status) && status == AG_ERR_PROTECTED_REGION,
          "refused with AG_ERR_PROTECTED_REGION");
    check(el.sha_blocks == 0,
          "and refused before measuring: the cheap check came first");
    check(el.counter == 5u, "the floor did not move for a refused image");
}

int main(int argc, char **argv)
{
    elf_firmware_t boot, app;
    const char    *boot_elf = argc > 1 ? argv[1] : 0;
    const char    *app_elf  = argc > 2 ? argv[2] : 0;

    if (!boot_elf || !app_elf) {
        fprintf(stderr, "usage: boot_harness <ag-boot.elf> <application.elf>\n");
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
    if (app.flashsize == 0u || app.flashsize > AG_BOOT_MAX_IMAGE_LEN) {
        fprintf(stderr, "%s does not fit the application region\n", app_elf);
        return 2;
    }
    /* The .elf carries no clock speed, so it is supplied here -- and it must
     * match the -DF_CPU the firmware was built with, or every baud divisor and
     * every TWI bit rate in the image is computed against a different clock
     * than the one it runs on. */
    boot.frequency = F_CPU_HZ;
    strcpy(boot.mmcu, AG_SIMAVR_MCU);

    printf("simavr ag-boot integration: %s + %s on %s\n",
           boot_elf, app_elf, AG_SIMAVR_MCU);

    test_signed_image_boots(&boot, &app);
    test_tampered_image_refuses(&boot, &app);
    test_downgrade_refuses(&boot, &app);

    printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
