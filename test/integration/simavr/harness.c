/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Integration tests: the REAL firmware image, running on an emulated
 * ATmega328P, driven over an emulated UART.
 *
 * This is a different question from the unit tests. Those run the logic layers
 * on x86 against a mock HAL and prove the logic is right. This runs the
 * .elf that gets flashed -- through avr-gcc, with the real register writes,
 * the real interrupt vectors and the real timing -- and proves the thing that
 * ships actually works.
 *
 * It catches what a host build cannot: a wrong baud divisor, an ISR that never
 * fires, a peripheral configured in the wrong order, a linker script problem,
 * an interrupt vector table that does not match the handlers.
 *
 * simavr rather than qemu-system-avr. QEMU's ATmega model is documented as
 * "limited to USART & 16-bit timer devices" -- ADC, EEPROM, SPI, TWI and the
 * watchdog are unimplemented register stubs -- so it could only prove the
 * firmware boots and prints. simavr models the peripherals this firmware
 * actually uses, which is the difference between a smoke test and a test.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <simavr/sim_avr.h>
#include <simavr/sim_elf.h>
#include <simavr/avr_uart.h>
#include <simavr/avr_ioport.h>
#include <simavr/avr_adc.h>
#include <simavr/avr_spi.h>

#include "tfw/status.h"
#include "tfw/protocol.h"
#include "crc16.h"

/* ------------------------------------------------------------- plumbing */

static avr_t *avr;

#define RXCAP 4096
static uint8_t  from_device[RXCAP];
static unsigned from_device_n;

/* Every byte the firmware transmits lands here. */
static void on_device_tx(struct avr_irq_t *irq, uint32_t value, void *param)
{
    (void)irq; (void)param;
    if (from_device_n < RXCAP) {
        from_device[from_device_n++] = (uint8_t)value;
    }
}

static avr_irq_t *uart_in;

/* avr_run() executes exactly one instruction and charges avr->cycle whatever
 * that instruction costs -- 1.436 cycles per call on average for this
 * firmware, measured, not a fixed one. An instruction count is therefore not
 * a cycle count and the two must not be confused: settling only needs
 * "enough", so it stays instruction-counted and generous. The tick-rate check
 * further down needs an actual span of simulated time, so it runs until
 * avr->cycle, the emulator's real cycle counter, says the span has elapsed --
 * exactly the way bench_harness.c times everything. */
static void run_instructions(long n)
{
    for (long i = 0; i < n; i++) { avr_run(avr); }
}

/* Bounded the way bench_harness.c's run_until_cycle() is, and for the same
 * reason: if the emulated firmware halts or sleeps with no wake source ever
 * coming, avr_run() stops advancing avr->cycle and an unguarded loop here
 * would hang make integration-test until an external timeout killed it,
 * rather than failing with a reason.
 *
 * The guard is the requested span itself, not an arbitrary constant: no AVR
 * instruction costs less than one cycle, so reaching a target cycle count
 * never needs more avr_run() calls than the number of cycles between here
 * and there. Any call beyond that bound is proof nothing is advancing, not a
 * firmware that is merely slow. */
static void run_until_cycle(avr_cycle_count_t target)
{
    avr_cycle_count_t start = avr->cycle;
    avr_cycle_count_t guard = 0;

    while (avr->cycle < target) {
        avr_run(avr);
        if (++guard > target - start) {
            fprintf(stderr, "run_until_cycle: no progress after %"
                            PRI_avr_cycle_count " steps (wanted cycle %"
                            PRI_avr_cycle_count ", stuck at %"
                            PRI_avr_cycle_count ") -- the emulated firmware is "
                            "not advancing\n",
                    guard, target, avr->cycle);
            exit(2);
        }
    }
}

static void send(const uint8_t *b, unsigned n)
{
    for (unsigned i = 0; i < n; i++) {
        avr_raise_irq(uart_in, b[i]);
        /* Let the firmware consume the byte before the next arrives. Feeding a
         * whole frame in one instant would test a device that never exists.
         *
         * The 20000-call pacing is generous rather than exact, and it has to
         * be: a real UART delivers one byte per ~87 us at 115200, but simavr
         * does not. It recomputes a USART's cycles-per-byte when UBRRnL/H is
         * written and NOT when U2Xn changes, and src/hal/avr/hal_uart.c writes
         * the divisor before U2X0 -- correct on silicon, which re-evaluates
         * continuously, but the emulator captures U2X0 = 0 and clocks the
         * link at half rate from then on. Uncalibrated, that makes this one
         * byte per ~187 us, not ~87. bench_harness.c's uart_calibrate()
         * corrects it by re-driving the UBRR0L write handler once the
         * firmware has set U2X0; this harness does not assert on wire timing,
         * so it is left uncalibrated and paced generously instead. */
        run_instructions(20000);
    }
}

/* ------------------------------------------------------ ADC (issue #19/#21)
 *
 * simavr's ADC model asks for a voltage each time a conversion starts, by
 * raising ADC_IRQ_OUT_TRIGGER with the mux the firmware just selected. A
 * conversion only ever COMPLETES -- ADCSRA's ADSC bit clearing, which is what
 * tfw_hal_adc_read() polls -- if ADEN was set first; see avr_adc_write_adcsra
 * in simavr's own source. So this is not just "feed a plausible reading": on
 * the pre-fix firmware, which never called tfw_hal_adc_init(), this callback
 * would never fire and ANALOG_READ would run out its cycle bound with no
 * reply, exactly the way #19's silent zero would have been caught had this
 * existed then.
 */
static void on_adc_trigger(struct avr_irq_t *irq, uint32_t value, void *param)
{
    union { avr_adc_mux_t mux; uint32_t v; } e;

    (void)irq; (void)param;
    e.v = value;
    if (e.mux.kind == ADC_MUX_SINGLE) {
        avr_irq_t *chan = avr_io_getirq(avr, AVR_IOCTL_ADC_GETIRQ, ADC_IRQ_ADC0 + (int)e.mux.src);
        /* Millivolts, mid-scale against any reference this board's ADMUX could
         * plausibly select -- nonzero on any of them, which is the property
         * this test needs. */
        avr_raise_irq(chan, 1650u);
    }
}

/* --------------------------------------------------- SPI (issue #16/#19/#21)
 *
 * simavr sets SPIF automatically once SPCR's SPE bit is on and eight clocks
 * have elapsed (avr_spi_raise) -- it does not need a slave on the other end
 * to do that part. What it DOES need a slave for is a receive value other
 * than a loopback of what was just transmitted, so this stands in for one:
 * every byte the firmware clocks out, it clocks a fixed byte back in.
 *
 * Before the fix, SPE was never set (#19), so avr_spi_raise's own SPE check
 * means SPIF never rises, and hal_bus.c's wait was unbounded (#16): this
 * transfer would hang the firmware forever, which the bounded
 * run_instructions() in exchange() turns into a FAILING test rather than one
 * that blocks CI.
 */
#define SPI_SLAVE_BYTE 0xE7u

static void on_spi_output(struct avr_irq_t *irq, uint32_t value, void *param)
{
    (void)irq; (void)value; (void)param;
    avr_raise_irq(avr_io_getirq(avr, AVR_IOCTL_SPI_GETIRQ(0), SPI_IRQ_INPUT), SPI_SLAVE_BYTE);
}

/* ------------------------------------------------------------- framing */

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

/* Finds a complete, CRC-valid frame in what the device has said. */
static int find_response(uint8_t seq, uint8_t *status, uint8_t *payload, uint8_t *plen)
{
    for (unsigned i = 0; i + 6 <= from_device_n; i++) {
        if (from_device[i] != TFW_SOF) { continue; }
        unsigned len = from_device[i + 1];
        if (len < 2 || len > 200) { continue; }
        unsigned total = len + 4;
        if (i + total > from_device_n) { continue; }
        uint16_t want = tfw_crc16(&from_device[i + 1], (uint16_t)(len + 1));
        uint16_t got  = (uint16_t)(from_device[i + total - 2]
                                   | (from_device[i + total - 1] << 8));
        if (want != got) { continue; }
        if (from_device[i + 2] != seq) { continue; }
        if (!(from_device[i + 3] & TFW_OP_RESPONSE_BIT)) { continue; }
        *status = from_device[i + 4];
        *plen   = (uint8_t)(len - 3u);
        memcpy(payload, &from_device[i + 5], *plen);
        return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------- tests */

static int failures;

static void check(int cond, const char *what)
{
    printf("  %-62s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond) { failures++; }
}

static int exchange(uint8_t seq, uint8_t cmd, const uint8_t *pay, uint8_t plen,
                    uint8_t *status, uint8_t *out, uint8_t *outlen)
{
    uint8_t  wire[128];
    unsigned n = frame_request(wire, seq, cmd, pay, plen);

    from_device_n = 0;
    send(wire, n);
    run_instructions(400000);
    return find_response(seq, status, out, outlen);
}

int main(int argc, char **argv)
{
    elf_firmware_t f;
    memset(&f, 0, sizeof f);
    const char    *elf = argc > 1 ? argv[1] : "build/atmega328p/soft/trusted-firmware.elf";
    uint8_t        status, out[64], outlen;

    if (elf_read_firmware(elf, &f) != 0) {
        fprintf(stderr, "cannot read %s\n", elf);
        return 2;
    }
    /* The .elf carries no clock speed, so it is supplied here -- and it must
     * match the -DF_CPU the firmware was built with, or every baud divisor and
     * timer reload in the image is computed against a different clock than the
     * one it runs on. */
    f.frequency = 16000000;
    strcpy(f.mmcu, "atmega328p");

    avr = avr_make_mcu_by_name(f.mmcu);
    if (!avr) { fprintf(stderr, "no core for %s\n", f.mmcu); return 2; }
    avr_init(avr);
    avr_load_firmware(avr, &f);

    uart_in = avr_io_getirq(avr, AVR_IOCTL_UART_GETIRQ('0'), UART_IRQ_INPUT);
    avr_irq_register_notify(
        avr_io_getirq(avr, AVR_IOCTL_UART_GETIRQ('0'), UART_IRQ_OUTPUT),
        on_device_tx, NULL);
    avr_irq_register_notify(
        avr_io_getirq(avr, AVR_IOCTL_ADC_GETIRQ, ADC_IRQ_OUT_TRIGGER),
        on_adc_trigger, NULL);
    avr_irq_register_notify(
        avr_io_getirq(avr, AVR_IOCTL_SPI_GETIRQ(0), SPI_IRQ_OUTPUT),
        on_spi_output, NULL);

    printf("simavr integration: %s\n", elf);

    /* Boot. The firmware announces itself, which is also how we know the UART
     * is configured correctly before any test depends on it. */
    run_instructions(2000000);
    check(from_device_n > 0, "the firmware transmits on boot");

    /* PING proves the whole path on real silicon: the USART ISR, the ring, the
     * framer, dispatch, and back out through the transmit interrupt. */
    {
        const uint8_t msg[4] = { 'p', 'o', 'n', 'g' };
        int ok = exchange(0x21, TFW_CMD_PING, msg, 4, &status, out, &outlen);
        check(ok, "PING round-trips through the emulated UART");
        check(ok && status == TFW_OK, "PING returns TFW_OK");
        check(ok && outlen == 4 && memcmp(out, msg, 4) == 0, "PING echoes its payload");
    }

    /* CAPS: the device describes itself, and the numbers must be the ones the
     * image was actually built with. */
    {
        int ok = exchange(0x22, TFW_CMD_CAPS, NULL, 0, &status, out, &outlen);
        check(ok && status == TFW_OK, "CAPS answers");
        check(ok && out[0] == TFW_PROTO_VERSION_MAJOR, "CAPS reports protocol major 2");
        check(ok && out[6] == 22, "CAPS reports the 328P's 22 pins");
    }

    /* SYS_INFO proves the TIMER0 tick interrupt is actually firing -- something
     * no host test can show, because on the host millis() is a variable. */
    {
        uint32_t           t1, t2;
        avr_cycle_count_t  before;
        int ok = exchange(0x23, TFW_CMD_SYS_INFO, NULL, 0, &status, out, &outlen);
        check(ok && status == TFW_OK, "SYS_INFO answers");
        /* #12: the reply is 15 bytes. err_crc and err_format (bytes 7..10)
         * follow reset_cause, and the exact u32 free_ram is appended after
         * them at bytes 11..14. This is the length on real silicon, not only
         * on the host mock. A boot with no traffic yet has raised no framing
         * error. */
        check(ok && outlen == 15u, "SYS_INFO's reply carries #12's appended u32 free_ram");
        check(ok && out[7] == 0u && out[8] == 0u && out[9] == 0u && out[10] == 0u,
              "a freshly booted device has seen no framing errors");
        {
            uint32_t ram16 = (uint32_t)out[4] | ((uint32_t)out[5] << 8);
            uint32_t ram32 = (uint32_t)out[11] | ((uint32_t)out[12] << 8)
                           | ((uint32_t)out[13] << 16) | ((uint32_t)out[14] << 24);

            check(ok && outlen == 15u && ram32 != 0u,
                  "the appended u32 free_ram is non-zero");
            /* The u16 saturates at 0xFFFF; below that the two are equal. */
            check(ok && outlen == 15u
                  && ram16 == ((ram32 > 0xFFFFu) ? 0xFFFFu : ram32),
                  "the u16 free_ram is the u32 saturated at 0xFFFF");
        }
        t1 = (uint32_t)out[0] | ((uint32_t)out[1] << 8)
           | ((uint32_t)out[2] << 16) | ((uint32_t)out[3] << 24);

        /* Exactly one simulated second of AVR clock, not an instruction
         * count. f.frequency above is 16 MHz, so 16 000 000 is one second of
         * avr->cycle -- the counter simavr increments per instruction from
         * the part's own timing table. The former run_cycles(16000000) counted
         * calls to avr_run() instead, which execute one instruction each at
         * 1.436 cycles apiece for this firmware (measured): "one simulated
         * second" was actually ~1.44. */
        before = avr->cycle;
        run_until_cycle(before + 16000000u);

        ok = exchange(0x24, TFW_CMD_SYS_INFO, NULL, 0, &status, out, &outlen);
        t2 = (uint32_t)out[0] | ((uint32_t)out[1] << 8)
           | ((uint32_t)out[2] << 16) | ((uint32_t)out[3] << 24);
        check(ok && t2 > t1, "the millisecond tick ISR is running");
        /* The bound is derived from the wait above rather than guessed. The
         * wait guarantees at least 1 000 ms of tick-ISR time elapsed (16 MHz /
         * 16 000 cycles-per-tick), less at most one tick's phase slip against
         * where t1 happened to land -- hence the 990 floor. The ceiling adds
         * the second exchange() itself, which is not free: send() paces six
         * wire bytes at 20 000 instructions apiece plus a 400 000-instruction
         * settle, 520 000 instructions that cost at most a handful of cycles
         * each, i.e. well under 200 ms even at a pessimistic 5 cycles/instr. */
        check(t2 - t1 >= 990 && t2 - t1 <= 1200,
              "the tick rate is 1 kHz to a bound derived from the simulated clock");
    }

    /* GPIO on real registers: the board descriptor, the HAL's port arithmetic
     * and the actual DDR/PORT writes, observed on the emulated pin. */
    {
        const uint8_t mode_out[2] = { 13u, 1u };
        const uint8_t high[2]     = { 13u, 1u };
        int ok = exchange(0x25, TFW_CMD_PIN_MODE, mode_out, 2, &status, out, &outlen);
        check(ok && status == TFW_OK, "PIN_MODE accepted on pin 13");
        ok = exchange(0x26, TFW_CMD_DIGITAL_WRITE, high, 2, &status, out, &outlen);
        check(ok && status == TFW_OK, "DIGITAL_WRITE accepted on pin 13");

        /* Read it back through the device rather than poking the emulator:
         * that exercises the read path too. */
        const uint8_t pin13[1] = { 13u };
        ok = exchange(0x27, TFW_CMD_DIGITAL_READ, pin13, 1, &status, out, &outlen);
        check(ok && status == TFW_OK && out[0] == 1u,
              "the pin reads back high from the real port register");
    }

    /* The UART pins must be refused on real hardware too -- this is the check
     * that stops a host cutting the line it is talking over. */
    {
        const uint8_t tx_pin[2] = { 1u, 1u };
        int ok = exchange(0x28, TFW_CMD_DIGITAL_WRITE, tx_pin, 2, &status, out, &outlen);
        check(ok && status == TFW_ERR_RESERVED_PIN, "the UART pin is refused on the device");
    }

    /* An unprovisioned device must refuse to attest on real silicon, not only
     * in a host test where the mock could be lying. */
    {
        uint8_t nonce[16] = {0};
        int ok = exchange(0x29, TFW_CMD_ATTEST, nonce, 16, &status, out, &outlen);
        check(ok && status == TFW_ERR_NOT_PROVISIONED,
              "an unprovisioned device refuses to attest");
    }

    /* A corrupted frame must be answered, not ignored -- otherwise a host
     * cannot tell a bad line from a dead device. */
    {
        uint8_t  wire[64];
        unsigned n = frame_request(wire, 0x2A, TFW_CMD_PING, NULL, 0);
        wire[n - 1] ^= 0xFF;
        from_device_n = 0;
        send(wire, n);
        run_instructions(400000);
        int ok = find_response(0x2A, &status, out, &outlen);
        check(ok && status == TFW_ERR_CRC, "a corrupted frame is answered with TFW_ERR_CRC");
    }

    /* ANALOG_READ against the emulated ADC -- #19's silent-zero instance.
     * Before the fix ADEN was never set, ADSC never cleared, and this
     * exchange would exhaust its cycle bound with no reply rather than
     * return a plausible-looking zero: the same bound that turns #16's SPI
     * hang into a failure catches this one too. */
    {
        const uint8_t a0[1] = { 14u };  /* A0, ADC channel 0 */
        int ok = exchange(0x2B, TFW_CMD_ANALOG_READ, a0, 1, &status, out, &outlen);
        check(ok, "ANALOG_READ round-trips through the emulated ADC");
        check(ok && status == TFW_OK, "ANALOG_READ returns TFW_OK");
        uint16_t raw = ok ? (uint16_t)(out[0] | (out[1] << 8)) : 0u;
        check(ok && raw > 0u,
              "the ADC actually converted rather than reporting an unmeasured zero");
    }

    /* SPI_XFER against the emulated SPI peripheral -- #16's hang and #19's
     * missing init, together. Before either fix, SPE was never set, SPIF
     * never rose, and hal_bus.c's wait was unbounded: this exchange would
     * wedge the firmware permanently, exactly as it did on real hardware. */
    {
        const uint8_t cs_and_data[3] = { 10u, 0x12u, 0x34u };  /* cs, then two bytes out */
        int ok = exchange(0x2C, TFW_CMD_SPI_XFER, cs_and_data, 3, &status, out, &outlen);
        check(ok, "SPI_XFER round-trips through the emulated SPI peripheral");
        check(ok && status == TFW_OK, "SPI_XFER returns TFW_OK");
        check(ok && outlen == 2 && out[0] == SPI_SLAVE_BYTE && out[1] == SPI_SLAVE_BYTE,
              "SPI_XFER reads back what the emulated peripheral clocked in");

        /* The device must still be alive afterwards -- #16's actual failure
         * was not the one bad transfer, it was every command after it. */
        const uint8_t msg[5] = { 'a', 'l', 'i', 'v', 'e' };
        ok = exchange(0x2D, TFW_CMD_PING, msg, 5, &status, out, &outlen);
        check(ok && status == TFW_OK, "the device still answers after an SPI transfer");
    }

    /* I2C_CONFIG against the REAL TWBR register, on real F_CPU. 1 kHz is
     * inside the cap layer's generic TFW_I2C_HZ_MIN..MAX interval, and on this
     * board's 16 MHz clock it is NOT reachable by an 8-bit TWBR at prescaler
     * 1 -- exactly the gap a host build cannot see, since the mock has no
     * F_CPU and always answers TFW_OK. Before hal_bus.c's own reachability
     * check, this silently produced roughly 125 kHz -- 125 times the
     * requested rate -- with no error. */
    {
        const uint8_t too_slow[4] = { 0xE8u, 0x03u, 0x00u, 0x00u };  /* 1000 Hz, LE */
        int ok = exchange(0x2E, TFW_CMD_I2C_CONFIG, too_slow, 4, &status, out, &outlen);
        check(ok && status == TFW_ERR_RANGE,
              "I2C_CONFIG refuses a clock this board's TWBR cannot represent");

        const uint8_t reachable[4] = { 0xA0u, 0x86u, 0x01u, 0x00u };  /* 100000 Hz, LE */
        ok = exchange(0x2F, TFW_CMD_I2C_CONFIG, reachable, 4, &status, out, &outlen);
        check(ok && status == TFW_OK, "I2C_CONFIG accepts the boot default rate");
    }

    /* I2C_CONFIG must never program a FASTER bus than the one requested, only
     * the same rate or slower. 350000 Hz does not divide 16 MHz evenly, and
     * rounding the divisor DOWN -- as a naive (F_CPU/hz - 16)/2 does -- picks
     * TWBR=14, which is actually 363 636 Hz: faster than asked, and past what
     * a peripheral rated for exactly 350 kHz is specified to tolerate. TWBR=15
     * (346 774 Hz, at or below the request) is the only answer that keeps the
     * contract "the bus runs at no more than the requested rate". */
    {
#define R_TWBR 0xB8u
        const uint8_t threefifty[4] = { 0x30u, 0x57u, 0x05u, 0x00u };  /* 350000 Hz, LE */
        int ok = exchange(0x30, TFW_CMD_I2C_CONFIG, threefifty, 4, &status, out, &outlen);
        check(ok && status == TFW_OK, "I2C_CONFIG accepts 350 kHz");
        check(ok && avr->data[R_TWBR] == 15u,
              "I2C_CONFIG rounds the divisor so the real clock never exceeds the request");
#undef R_TWBR
    }

    printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
