/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * THE BENCHMARK IMAGE'S ENTRY POINT. It replaces src/main.c and nothing else:
 * every other object in agbench.elf is the object that goes into the shipped
 * firmware, compiled from the same source with the same AVR_CFLAGS, the same
 * -Os -flto, and the same board profile.
 *
 * WHY A SECOND IMAGE AT ALL, when the point of this gate is to measure the
 * instruction stream that ships.
 *
 * -flto plus --gc-sections is what makes ag_hal_gpio_write collapse into an
 * sbi at its call site, and it does the same to ag_framer_push, ag_sched_poll
 * and ag_transport_poll: in arduino-generic.elf those four are not functions,
 * they are stretches of main(). `avr-nm` finds no symbol for any of them, so
 * there is no address for a harness to call and no boundary for a cycle counter
 * to bracket. ag_crc16_update survives as a real function, and that is the
 * exception rather than the rule.
 *
 * So the measurable ones are given an address here. Each is called through a
 * volatile function pointer, which does three things at once: the compiler must
 * emit the function out of line, --gc-sections must keep it because its address
 * is taken, and the call itself cannot be inlined or specialised to the
 * arguments this file happens to pass.
 *
 * WHAT THAT COSTS IN FIDELITY, stated plainly. An out-of-line body is the
 * compiler's general compilation of that source: no constant propagated in from
 * a caller, no register allocated across the call boundary. The shipped image's
 * inlined copy can be faster, never slower. So every per-byte and per-sweep
 * number this image produces is an UPPER BOUND on what the shipped firmware
 * pays, which is the direction a budget wants to be wrong in.
 *
 * The harness checks that claim rather than asserting it: ag_crc16_update
 * exists in both images, and bench_harness.c compares the two byte for byte. If
 * the benchmark image ever stops generating the same code as the firmware, that
 * comparison fails and says so.
 *
 * Nothing here is measured itself. The harness sets the program counter to one
 * of these functions, so main() below is only a resting state -- and the four
 * calls inside it are never executed, because ag_bench_trigger is never
 * written. They exist so the linker can see a reference.
 */

#include <avr/interrupt.h>

#include "ag/hal.h"
#include "ag/crypto.h"
#include "ag/config.h"
#include "ag/status.h"
#include "proto/frame.h"
#include "proto/crc16.h"
#include "caps/sched.h"
#include "core/reader.h"
#include "core/writer.h"

ag_status_t ag_hal_tick_init(void);

/* ------------------------------------------------------------- workspace */

/* The harness writes the bytes to decode here before it calls in. Extern so
 * the symbol survives into the .elf for the harness to find. */
uint8_t     ag_bench_buf[AG_MAX_FRAME];
ag_framer_t ag_bench_framer;

/* ------------------------------------------------------------ the subjects */

/*
 * The framer, driven exactly as src/proto/transport.c drives it: one byte at a
 * time, with the event consumed at every step. Consumed and not discarded --
 * ag_framer_push's return value decides control flow in the transport, so a
 * benchmark that threw it away would be measuring a function the compiler was
 * free to simplify.
 *
 * ag_framer_init is fixed setup and it is INSIDE the timed region on purpose:
 * the harness takes the slope between two byte counts, so anything that does
 * not scale with n subtracts itself out. Leaving it in also makes each call
 * start from the same framer state, which is what makes two runs comparable.
 */
uint8_t ag_bench_framer_run(uint8_t n)
{
    ag_frame_event_t e = AG_FRAME_NONE;
    uint8_t          i;

    ag_framer_init(&ag_bench_framer);
    for (i = 0u; i < n; i++) {
        e = ag_framer_push(&ag_bench_framer, ag_bench_buf[i]);
    }
    return (uint8_t)e;
}

/* The CRC over n bytes. ag_crc16 is the loop the encoder uses; ag_crc16_update
 * inside it is the per-byte cost the framer pays. Measuring the loop rather
 * than one update is what makes the slope a per-byte number and not a
 * per-call one. */
uint16_t ag_bench_crc_run(uint8_t n)
{
    return ag_crc16(ag_bench_buf, n);
}

/* n sweeps of the scheduler. The slope between two values of n is one sweep;
 * the intercept is the call overhead the harness needs to subtract from the
 * single-shot measurements below. */
void ag_bench_sched_run(uint8_t n)
{
    uint8_t i;

    for (i = 0u; i < n; i++) {
        ag_sched_poll();
    }
}

/*
 * Arms `count` slots through the real command handler, so the table ends up in
 * exactly the state a host would put it in -- including the pin modes and the
 * levels ag_sched_cmd_set drives on the way.
 *
 * Pins are tried in order and the ones the board refuses are skipped rather
 * than fatal: pin 0 and 1 are the UART on every board here, and which of the
 * rest are reserved is a property of the board descriptor, not something this
 * file should encode a second copy of. It returns how many it actually armed
 * and the harness insists that be the number it asked for.
 */
uint8_t ag_bench_sched_arm(uint8_t count, uint32_t ms)
{
    uint8_t     payload[6];
    uint8_t     out[4];
    ag_reader_t r;
    ag_writer_t w;
    uint8_t     pin;
    uint8_t     armed = 0u;

    ag_sched_init();
    for (pin = 0u; pin < (uint8_t)AG_PIN_COUNT && armed < count; pin++) {
        payload[0] = pin;
        payload[1] = 1u;
        payload[2] = (uint8_t)(ms & 0xFFuL);
        payload[3] = (uint8_t)((ms >> 8) & 0xFFuL);
        payload[4] = (uint8_t)((ms >> 16) & 0xFFuL);
        payload[5] = (uint8_t)((ms >> 24) & 0xFFuL);

        ag_reader_init(&r, payload, (uint8_t)sizeof payload);
        ag_writer_init(&w, out, (uint8_t)sizeof out);
        if (ag_sched_cmd_set(&r, &w) == AG_OK) {
            armed = (uint8_t)(armed + 1u);
        }
    }
    return armed;
}

/* How many slots are armed. The harness asserts on this rather than trusting
 * that "arm sixteen, wait, sweep" left the table in the state it meant to
 * measure -- a sweep over slots that quietly failed to arm is the benchmark
 * that reports the best number and means the least. */
uint8_t ag_bench_sched_active(void)
{
    return ag_sched_active();
}

/* --------------------------------------------------------------- keep-alive */

/*
 * The linker's view of the four functions above.
 *
 * volatile, so the compiler may not fold the indirection away and decide it can
 * inline the callee after all; extern, and read by main(), so --gc-sections
 * keeps the pointers and therefore keeps what they point at.
 */
uint8_t  (* volatile ag_bench_fp_framer)(uint8_t)           = ag_bench_framer_run;
uint16_t (* volatile ag_bench_fp_crc)(uint8_t)              = ag_bench_crc_run;
void     (* volatile ag_bench_fp_sched)(uint8_t)            = ag_bench_sched_run;
uint8_t  (* volatile ag_bench_fp_arm)(uint8_t, uint32_t)    = ag_bench_sched_arm;
uint8_t  (* volatile ag_bench_fp_active)(void)              = ag_bench_sched_active;

/* Never written, by anything, ever. It is the condition that makes the calls
 * below reachable as far as the compiler is concerned and unreachable in fact. */
volatile uint8_t ag_bench_trigger;
volatile uint16_t ag_bench_sink;

int main(void)
{
    /* The production bring-up order, from src/main.c. The benchmarks measure
     * code that reads the millisecond counter and writes real port registers,
     * so the peripherals behind it have to be the ones the firmware configures
     * -- a tick running at a different rate would change what "due" means to
     * the scheduler sweep. */
    ag_hal_init();
    ag_hal_tick_init();
    ag_hal_uart_init(AG_UART_BAUD);
    ag_crypto_init();
    ag_sched_init();

    sei();

    for (;;) {
        if (ag_bench_trigger) {
            ag_bench_sink = ag_bench_fp_framer(ag_bench_trigger);
            ag_bench_sink = ag_bench_fp_crc(ag_bench_trigger);
            ag_bench_fp_sched(ag_bench_trigger);
            ag_bench_sink = ag_bench_fp_arm(ag_bench_trigger, 1000uL);
            ag_bench_sink = ag_bench_fp_active();
        }
    }
}
