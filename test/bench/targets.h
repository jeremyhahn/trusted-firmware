/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * THE PERFORMANCE BUDGET. One table, the way boards/<board>/profile.mk holds
 * one flash budget, and for the same reason: a number that lives in two places
 * is a number that will disagree with itself.
 *
 * Every row carries the project plan's target AND the plan's reasoning, and the
 * reasoning is printed beside a failure. A budget without its justification is
 * a number nobody can argue with correctly -- the next person either raises it
 * because it is in the way, or defends it without knowing what it was for.
 *
 * ------------------------------------------------------------------------
 * TARGET vs CEILING, and why there are two columns.
 *
 * `target` is what the plan asked for. `ceiling` is what the gate enforces.
 *
 * They are the same number wherever the firmware meets its target. TODAY THAT
 * IS ONE ROW IN SIX -- the sustained command rate, which is met with 1.7x of
 * margin. The other five are over by between 3x and 11x, and two of them are
 * unreachable as the plan states them rather than merely unmet. Where the
 * firmware does NOT meet a target, the plan's target stays
 * written down exactly as the plan stated it, the ceiling is pinned at what the
 * code actually costs today, and the row prints MISS on every single run with
 * the size of the gap. That is deliberate and it is not a relaxation: lowering
 * the target would erase the fact that the firmware is over budget, and
 * enforcing a target the code has never met would mean the gate is red from the
 * day it lands and therefore gets switched off within a week. Pinning the
 * measurement makes today's cost the new worst case -- the code cannot get
 * slower without failing the build -- while the MISS line keeps saying, on
 * every run, that there is a debt and how big it is.
 *
 * A ceiling is only ever moved DOWN, by making the code faster.
 * ------------------------------------------------------------------------
 */
#ifndef TFW_BENCH_TARGETS_H
#define TFW_BENCH_TARGETS_H

typedef enum {
    TFW_BENCH_FRAME_BYTE = 0,  /* frame decode, cycles per received byte */
    TFW_BENCH_CRC_BYTE,        /* CRC-16, cycles per byte */
    TFW_BENCH_SCHED_SWEEP,     /* tfw_sched_poll(), cycles per full sweep */
    TFW_BENCH_SCHED_PER_SLOT,  /* the same worst sweep, cycles per armed slot (#11) */
    TFW_BENCH_TICK_ISR,        /* the millisecond tick ISR, cycles */
    TFW_BENCH_WRITE_LATENCY,   /* DIGITAL_WRITE, SOF to response, microseconds */
    TFW_BENCH_WRITE_DEVICE_US, /* the same exchange, device share only (#11) */
    TFW_BENCH_CMD_RATE,        /* sustained commands per second at 115200 */
    TFW_BENCH_METRIC_COUNT
} tfw_bench_metric_t;

typedef struct {
    const char *name;
    const char *unit;
    double      target;   /* what the project plan asks for */
    int         higher_is_better;
    const char *reason;   /* the plan's justification, printed on failure */
} tfw_bench_budget_t;

static const tfw_bench_budget_t tfw_bench_budget[TFW_BENCH_METRIC_COUNT] = {
    [TFW_BENCH_FRAME_BYTE] = {
        "frame decode, per byte", "cycles", 40.0, 0,
        "a byte arrives every ~87 us at 115200; 40 cycles is 2.5 us at 16 MHz,\n"
        "        which leaves >30x headroom for ISR jitter"
    },
    [TFW_BENCH_CRC_BYTE] = {
        "CRC-16, per byte", "cycles", 20.0, 0,
        "avr-libc's _crc_xmodem_update is ~16 cycles branch-free; #12 put it\n"
        "        behind the HAL for the AVR build, which should add nothing to that"
    },
    [TFW_BENCH_SCHED_SWEEP] = {
        "tfw_sched_poll(), full sweep", "cycles", 200.0, 0,
        "it runs on every pass of the superloop and must not perturb the\n"
        "        millisecond tick"
    },
    /*
     * #11: the plan stated this "at 16 slots", which makes it a target for one
     * specific board rather than a rate -- and 200 cycles is below the cost of
     * an EMPTY sweep on that board (242, see TFW_BENCH_SCHED_SWEEP's own MISS).
     * Restated per armed slot it is comparable across the 328P's eight and the
     * 2560's sixteen, and it is what the row above's total actually decomposes
     * into: (200 cycle plan) / 16 slots = 12.5 cycles/slot is the target this
     * row checks, so the size-dependent row above is retained for regression
     * tracking and this one is where the plan's intent is actually measured.
     */
    [TFW_BENCH_SCHED_PER_SLOT] = {
        "tfw_sched_poll(), per armed slot, worst sweep", "cycles/slot", 12.5, 0,
        "the plan's 200 cycles at 16 slots restated as a rate, so it means the\n"
        "        same thing on every board's slot count"
    },
    [TFW_BENCH_TICK_ISR] = {
        "tick ISR, vector to reti", "cycles", 12.0, 0,
        "it must not disturb the bit timing of a software UART on the parts\n"
        "        that need one"
    },
    [TFW_BENCH_WRITE_LATENCY] = {
        "DIGITAL_WRITE, SOF to response", "us", 300.0, 0,
        "the exchange is dominated by the UART, not by the firmware's logic"
    },
    /*
     * #11: 300 us for the WHOLE exchange is below the link's own floor at
     * 115200 baud (fifteen frame bytes cost over 1300 us of wire before any
     * firmware runs), so no device could ever meet the row above as literally
     * stated. The plan's actual claim -- "dominated by the UART, not by the
     * firmware's logic" -- is a claim about the device's OWN share, and that
     * share is what this row checks against the same 300 us. The row above is
     * retained because a host cares about the whole round trip; this one is
     * where the plan's target is reachable and enforced.
     */
    [TFW_BENCH_WRITE_DEVICE_US] = {
        "DIGITAL_WRITE, device share only", "us", 300.0, 0,
        "the plan's 300 us, restated excluding the wire the firmware cannot\n"
        "        make faster"
    },
    [TFW_BENCH_CMD_RATE] = {
        "sustained command rate", "cmd/s", 400.0, 1,
        "a stop-and-wait bound set by the link, not by the CPU"
    },
};

/*
 * The enforced ceilings, per board, for the rows the firmware does not meet.
 *
 * A row absent from this table is enforced at its plan target. A row present
 * here is over budget: the number is what it measures today, to the cycle, so
 * any regression fails -- and the gate says MISS every run until the code comes
 * back under the plan's target and the row is deleted.
 *
 * The per-board split is not padding. A cycle count is a property of ONE
 * compiled instruction stream: the ATmega2560 has a three-byte program counter,
 * so every call and return in it costs a cycle more than the 328P's, and it
 * sweeps sixteen scheduler slots where the 328P sweeps eight.
 */
typedef struct {
    const char       *board;
    tfw_bench_metric_t metric;
    double            ceiling;
    const char       *why;
} tfw_bench_pin_t;

/*
 * THE ROUNDING RULE, so a pin is not a knife edge.
 *
 * A cycle count is pinned at the measured value rounded up to the next whole
 * cycle. It is exactly reproducible -- same source, same toolchain, same
 * emulator, same number every run -- so anything that costs a cycle more fails.
 *
 * A wall-clock span is pinned at the measured worst plus two percent, rounded up
 * to the next twenty-five microseconds. A round trip depends on where in the
 * superloop the last request byte happens to land, and tfw_sched_poll alone is
 * eight microseconds of that loop -- so anything anywhere in it changing size
 * moves this number by more than the thing being budgeted did. Pinning it to the
 * microsecond would fail the build for reasons nobody could act on, and a gate
 * that cries wolf is a gate that gets switched off. The sharp assertion on this
 * path is elsewhere and is not rounded: bench_harness.c requires the firmware's
 * share of the exchange to stay below the wire's.
 */
static const tfw_bench_pin_t tfw_bench_pins[] = {
    /*
     * ---------------------------------------------------------------------
     * CRC-16 WAS FIVE TIMES ITS BUDGET; #12 BROUGHT IT TO ABOUT ONE AND A HALF.
     *
     * The plan compares against avr-libc's _crc_xmodem_update and says ours
     * should add nothing. That comparison holds up: avr-libc's routine is
     * fourteen single-cycle instructions with no branch and no loop -- it folds
     * the 0x1021 polynomial a nibble at a time with swap, andi and eor.
     *
     * The portable, bit-serial fold in src/proto/crc16_update.c is still what
     * the host, tfwsim and the bootloader link, and it still costs what this row
     * used to be pinned at (103/105 cycles) -- that number is not wrong, it is
     * just no longer what the FIRMWARE pays. #12 put the fast form behind the
     * HAL (src/hal/avr/hal_crc16.c) for the AVR build only, exactly where the
     * file's own header said a measurement would send it. What remains here is
     * the cost of REACHING that routine through a real, out-of-line function
     * call in the benchmark image -- bench_main.c reaches every subject through
     * a volatile function pointer so `-flto` cannot inline it away, which is
     * what the file's own header says makes these numbers upper bounds. The
     * shipped firmware's `-flto` build can and does inline the one-line wrapper
     * bodily into tfw_framer_push, so its real cost is lower than this pin.
     * ---------------------------------------------------------------------
     */
    { "atmega328p", TFW_BENCH_CRC_BYTE,      31.0,
      "avr-libc's branch-free fold plus the call/return this benchmark image "
      "forces around it so -flto cannot inline the call away" },
    { "atmega2560", TFW_BENCH_CRC_BYTE,      33.0,
      "as the 328P, plus the extra byte of program counter this part pushes on "
      "every call into tfw_crc16_update" },

    /*
     * FRAME DECODE WAS THREE TIMES ITS BUDGET; NOW A LITTLE OVER ITS BUDGET
     * INSTEAD OF THREE TIMES IT, FOR THE SAME REASON THE ROW ABOVE MOVED.
     *
     * Every byte the framer stores it also folds into the running CRC, so this
     * figure CONTAINS the CRC figure above. What is left after the CRC fix is
     * the framer's own state machine plus the same forced-out-of-line call
     * overhead the row above pays -- 31 cycles on the 328P, not the 18 the
     * pre-#12 measurement attributed to the state machine alone, because the
     * two rows are measured by the same call-boundary method and the state
     * machine's share was never separated from the call overhead the CRC
     * routine happened to absorb into its own count before. Fixing the CRC
     * brought this row down by almost exactly the same fifty-eight cycles the
     * CRC row itself dropped by (from ~120/122 to 62/64).
     */
    { "atmega328p", TFW_BENCH_FRAME_BYTE,    62.0,
      "31 of it is tfw_crc16_update through the same forced call boundary; the "
      "framer's own state machine is the other 31" },
    { "atmega2560", TFW_BENCH_FRAME_BYTE,    64.0,
      "33 of it is tfw_crc16_update through the same forced call boundary; the "
      "framer's own state machine is the other 31" },

    /*
     * THE SCHEDULER SWEEP IS OVER BUDGET IN EVERY STATE, INCLUDING AN EMPTY ONE.
     *
     * The plan's 200 cycles is for sixteen slots, which is the ATmega2560's
     * TFW_SCHED_SLOTS. On that part an EMPTY sweep is already 242 cycles: reading
     * tfw_hal_millis costs an atomic section and a four-byte load, and then each
     * of sixteen slots costs a load and a bit test whether it is armed or not.
     * Sixteen armed but not due is 546. Sixteen coming due at once is 2 158,
     * because each one then resolves its pin through the board descriptor and
     * writes a port register.
     *
     * The gate gives the worst of the three, because a host that arms sixteen
     * timers together is a host that will have them expire together.
     *
     * What it costs in practice is bounded and small: 2 158 cycles is 135 us,
     * and it happens on the one pass where sixteen deadlines coincide. It does
     * not perturb the tick -- the tick is an interrupt and this is main-loop
     * code -- but it does delay the next command by that much, once.
     *
     * This row is retained for regression tracking on the total a host actually
     * waits; TFW_BENCH_SCHED_PER_SLOT below is where #11 restates the plan's
     * intent as a number both boards can be held to the same way.
     */
    { "atmega328p", TFW_BENCH_SCHED_SWEEP,   1066.0,
      "eight slots all coming due at once, each resolving a pin and writing a port" },
    { "atmega2560", TFW_BENCH_SCHED_SWEEP,   2158.0,
      "sixteen slots all coming due at once; an empty sweep on this part is "
      "already 242 cycles, so the 200 in the plan is unreachable at 16 slots" },

    /*
     * #11: THE SAME WORST SWEEP, PER ARMED SLOT.
     *
     * 1066 cycles / 8 slots = 133.25 on the 328P; 2158 / 16 = 134.875 on the
     * 2560 -- the two boards agree to within about a cycle and a quarter once
     * sized per slot, which the raw totals above do not, because they are
     * mostly the per-board fixed cost of an empty sweep (130 / 242 cycles) and
     * only partly the sixteen-versus-eight slots being resolved. Neither is
     * rounded to a whole cycle: each is an exact quotient of two reproducible
     * integers, not a sampled cycle count, so the fraction is part of what
     * repeats.
     */
    { "atmega328p", TFW_BENCH_SCHED_PER_SLOT, 133.25,
      "1066 cycles (the worst full sweep) / 8 armed slots" },
    { "atmega2560", TFW_BENCH_SCHED_PER_SLOT, 134.875,
      "2158 cycles (the worst full sweep) / 16 armed slots" },

    /*
     * THE TICK ISR IS FIVE TIMES ITS BUDGET, and the cause is not the ISR body
     * anyone wrote -- it is the type of the counter.
     *
     * `ticks` is an tfw_ms_t, four bytes, and volatile. Incrementing it is four
     * lds, an adiw and two adc, four sts -- twenty cycles -- and to do that the
     * ISR must first save r24 through r27 alongside r1 and SREG, which is
     * nineteen more in push and pop. The `ticks++` in the source is one line;
     * the compiled ISR is twenty-seven instructions.
     *
     * 12 cycles is roughly what a one-byte counter would cost. Nothing in this
     * family can increment a 32-bit volatile in twelve, and the comment above
     * that ISR in src/hal/avr/hal_tick.c used to claim ten -- corrected by #13
     * to the measured figure below.
     */
    { "atmega328p", TFW_BENCH_TICK_ISR,      58.0,
      "a 32-bit volatile increment plus the register bank it forces the ISR to save" },
    { "atmega2560", TFW_BENCH_TICK_ISR,      59.0,
      "as the 328P; this part's wider program counter is paid on entry, not in "
      "the handler" },

    /*
     * SOF TO RESPONSE IS FIVE TIMES ITS BUDGET, AND THE PLAN'S OWN REASONING
     * SAYS WHY: the exchange is dominated by the UART. It is dominated by the
     * UART so completely that no device can meet 300 us at this baud rate.
     *
     * A DIGITAL_WRITE exchange is eight request bytes and seven response bytes.
     * At 115200 8N1 a byte occupies ten bit times, 87 us, so fifteen bytes is
     * 1302 us of wire before any firmware runs at all -- roughly 1370-1400 us as
     * the emulator clocks it once calibrated to the divisor the firmware itself
     * programmed (see uart_calibrate() in bench_harness.c). The device adds
     * under 200 us on top of that, which is the only part this repository can
     * change -- and it went down, not up, once the CRC fix (#12) landed, though
     * only slightly: most of a DIGITAL_WRITE exchange's device time is
     * dispatch, GPIO and the USART ISR, not the two small CRC computations in
     * an eight-byte request and a seven-byte reply.
     *
     * This row is retained for regression tracking on the total a host actually
     * waits; TFW_BENCH_WRITE_DEVICE_US below is where #11 restates the plan's
     * 300 us against the part of the exchange this repository can actually
     * move.
     */
    { "atmega328p", TFW_BENCH_WRITE_LATENCY, 1600.0,
      "1402 us of it is fifteen frame bytes on the wire; the device adds 169" },
    { "atmega2560", TFW_BENCH_WRITE_LATENCY, 1625.0,
      "1402 us of it is fifteen frame bytes on the wire; the device adds 173" },

    /*
     * #11: THE SAME EXCHANGE, DEVICE SHARE ONLY -- WHAT THE PLAN'S 300 US
     * ACTUALLY MEANT, AND THE ONE ROW THIS RESTATEMENT MOVES FROM MISS TO MET.
     *
     * 169.1 us on the 328P and 172.9 on the 2560, both comfortably under the
     * plan's 300 once restated to exclude the wire time no firmware change can
     * touch. NOT pinned, deliberately: unlike every row above, this target is
     * reachable as stated, so it needs no ceiling narrower than the plan's own
     * number -- the same reason TFW_BENCH_CMD_RATE carries no pin either. `verdict()`
     * falls through to tfw_bench_budget[m].target and the row prints as met, not
     * as MISS, which is the whole point of the restatement.
     *
     * These two numbers move a little with any change to the firmware's
     * compiled size or layout -- src/proto/transport.c's fix for a stale-
     * counter bug (see the git history for this file) shifted both boards'
     * figures by several microseconds despite touching no CRC or scheduler
     * code. The gate re-measures on every run; a stale number here is a
     * comment to update, not a regression.
     */
};

#define TFW_BENCH_PIN_COUNT (sizeof tfw_bench_pins / sizeof tfw_bench_pins[0])

#endif /* TFW_BENCH_TARGETS_H */
