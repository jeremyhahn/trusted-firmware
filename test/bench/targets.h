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
#ifndef AG_BENCH_TARGETS_H
#define AG_BENCH_TARGETS_H

typedef enum {
    AG_BENCH_FRAME_BYTE = 0, /* frame decode, cycles per received byte */
    AG_BENCH_CRC_BYTE,       /* CRC-16, cycles per byte */
    AG_BENCH_SCHED_SWEEP,    /* ag_sched_poll(), cycles per full sweep */
    AG_BENCH_TICK_ISR,       /* the millisecond tick ISR, cycles */
    AG_BENCH_WRITE_LATENCY,  /* DIGITAL_WRITE, SOF to response, microseconds */
    AG_BENCH_CMD_RATE,       /* sustained commands per second at 115200 */
    AG_BENCH_METRIC_COUNT
} ag_bench_metric_t;

typedef struct {
    const char *name;
    const char *unit;
    double      target;   /* what the project plan asks for */
    int         higher_is_better;
    const char *reason;   /* the plan's justification, printed on failure */
} ag_bench_budget_t;

static const ag_bench_budget_t ag_bench_budget[AG_BENCH_METRIC_COUNT] = {
    [AG_BENCH_FRAME_BYTE] = {
        "frame decode, per byte", "cycles", 40.0, 0,
        "a byte arrives every ~87 us at 115200; 40 cycles is 2.5 us at 16 MHz,\n"
        "        which leaves >30x headroom for ISR jitter"
    },
    [AG_BENCH_CRC_BYTE] = {
        "CRC-16, per byte", "cycles", 20.0, 0,
        "avr-libc's _crc_xmodem_update is ~16 cycles; our portable, table-free\n"
        "        version should add nothing to that"
    },
    [AG_BENCH_SCHED_SWEEP] = {
        "ag_sched_poll(), full sweep", "cycles", 200.0, 0,
        "it runs on every pass of the superloop and must not perturb the\n"
        "        millisecond tick"
    },
    [AG_BENCH_TICK_ISR] = {
        "tick ISR, vector to reti", "cycles", 12.0, 0,
        "it must not disturb the bit timing of a software UART on the parts\n"
        "        that need one"
    },
    [AG_BENCH_WRITE_LATENCY] = {
        "DIGITAL_WRITE, SOF to response", "us", 300.0, 0,
        "the exchange is dominated by the UART, not by the firmware's logic"
    },
    [AG_BENCH_CMD_RATE] = {
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
    ag_bench_metric_t metric;
    double            ceiling;
    const char       *why;
} ag_bench_pin_t;

/*
 * THE ROUNDING RULE, so a pin is not a knife edge.
 *
 * A cycle count is pinned at the measured value rounded up to the next whole
 * cycle. It is exactly reproducible -- same source, same toolchain, same
 * emulator, same number every run -- so anything that costs a cycle more fails.
 *
 * A wall-clock span is pinned at the measured worst plus two percent, rounded up
 * to the next twenty-five microseconds. A round trip depends on where in the
 * superloop the last request byte happens to land, and ag_sched_poll alone is
 * eight microseconds of that loop -- so anything anywhere in it changing size
 * moves this number by more than the thing being budgeted did. Pinning it to the
 * microsecond would fail the build for reasons nobody could act on, and a gate
 * that cries wolf is a gate that gets switched off. The sharp assertion on this
 * path is elsewhere and is not rounded: bench_harness.c requires the firmware's
 * share of the exchange to stay below the wire's.
 */
static const ag_bench_pin_t ag_bench_pins[] = {
    /*
     * ---------------------------------------------------------------------
     * CRC-16 IS FIVE TIMES ITS BUDGET, AND THE BUDGET'S PREMISE IS CORRECT.
     *
     * The plan compares against avr-libc's _crc_xmodem_update and says ours
     * should add nothing. That comparison holds up: avr-libc's routine is
     * fourteen single-cycle instructions with no branch and no loop -- it folds
     * the 0x1021 polynomial a nibble at a time with swap, andi and eor.
     *
     * src/proto/crc16.c walks the byte one bit at a time: eight iterations, each
     * a shift, a conditional exclusive-or and a loop test, about ninety-five
     * cycles in the body and a hundred and two by the time the caller's rcall
     * and the ret are counted. Same polynomial, same output, same table-free
     * property, seven times the cost.
     *
     * The file's own header anticipated this: "If the cycle budget in
     * docs/development ever shows this mattering, the measurement comes first
     * and the specialisation goes behind the HAL, not here." The measurement now
     * exists. Whether the branch-free form replaces the portable one, or sits
     * behind the HAL beside it, is an architecture decision and not this gate's
     * to make -- so the number is recorded and the debt is named.
     * ---------------------------------------------------------------------
     */
    { "atmega328p", AG_BENCH_CRC_BYTE,      103.0,
      "a bit-serial fold: eight conditional shifts a byte, where avr-libc does "
      "the same polynomial branch-free in fourteen instructions" },
    { "atmega2560", AG_BENCH_CRC_BYTE,      105.0,
      "as the 328P, plus the extra byte of program counter this part pushes on "
      "every call into ag_crc16_update" },

    /*
     * FRAME DECODE IS THREE TIMES ITS BUDGET, AND ALL BUT EIGHTEEN CYCLES OF IT
     * IS THE ROW ABOVE.
     *
     * Every byte the framer stores it also folds into the running CRC, so this
     * figure CONTAINS the CRC figure. The state machine's own share is eighteen
     * cycles a byte on the 328P -- comfortably inside the forty the plan asks
     * for. Fix the CRC and this row passes without anyone touching frame.c.
     *
     * The plan's reasoning survives the miss, which is worth stating plainly:
     * 120 cycles is 7.5 us at 16 MHz against a byte every 87 us, so the headroom
     * is 11x rather than the 30x the plan wanted. Tighter than intended, not
     * dangerous.
     */
    { "atmega328p", AG_BENCH_FRAME_BYTE,    121.0,
      "102 of it is ag_crc16_update; the framer's own state machine is 18" },
    { "atmega2560", AG_BENCH_FRAME_BYTE,    123.0,
      "104 of it is ag_crc16_update; the framer's own state machine is 18" },

    /*
     * THE SCHEDULER SWEEP IS OVER BUDGET IN EVERY STATE, INCLUDING AN EMPTY ONE.
     *
     * The plan's 200 cycles is for sixteen slots, which is the ATmega2560's
     * AG_SCHED_SLOTS. On that part an EMPTY sweep is already 242 cycles: reading
     * ag_hal_millis costs an atomic section and a four-byte load, and then each
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
     */
    { "atmega328p", AG_BENCH_SCHED_SWEEP,   1066.0,
      "eight slots all coming due at once, each resolving a pin and writing a port" },
    { "atmega2560", AG_BENCH_SCHED_SWEEP,   2158.0,
      "sixteen slots all coming due at once; an empty sweep on this part is "
      "already 242 cycles, so the 200 in the plan is unreachable at 16 slots" },

    /*
     * THE TICK ISR IS FIVE TIMES ITS BUDGET, and the cause is not the ISR body
     * anyone wrote -- it is the type of the counter.
     *
     * `ticks` is an ag_ms_t, four bytes, and volatile. Incrementing it is four
     * lds, an adiw and two adc, four sts -- twenty cycles -- and to do that the
     * ISR must first save r24 through r27 alongside r1 and SREG, which is
     * nineteen more in push and pop. The `ticks++` in the source is one line;
     * the compiled ISR is twenty-seven instructions.
     *
     * 12 cycles is roughly what a one-byte counter would cost. Nothing in this
     * family can increment a 32-bit volatile in twelve, and the comment above
     * that ISR in src/hal/avr/hal_tick.c claims ten.
     */
    { "atmega328p", AG_BENCH_TICK_ISR,      58.0,
      "a 32-bit volatile increment plus the register bank it forces the ISR to save" },
    { "atmega2560", AG_BENCH_TICK_ISR,      59.0,
      "as the 328P; this part's wider program counter is paid on entry, not in "
      "the handler" },

    /*
     * SOF TO RESPONSE IS FIVE TIMES ITS BUDGET, AND THE PLAN'S OWN REASONING
     * SAYS WHY: the exchange is dominated by the UART. It is dominated by the
     * UART so completely that no device can meet 300 us at this baud rate.
     *
     * A DIGITAL_WRITE exchange is eight request bytes and seven response bytes.
     * At 115200 8N1 a byte occupies ten bit times, 87 us, so fifteen bytes is
     * 1302 us of wire before any firmware runs at all -- 1392 us as the emulator
     * clocks it, which charges eleven bit times a byte. The device adds 186 us
     * on top of that, and 186 us is the only part this repository can change.
     *
     * The 300 us figure is therefore not a firmware budget at all; it is below
     * the link's own floor. What the plan's REASONING asks for is checkable and
     * is checked: bench_harness.c asserts that the firmware's share stays below
     * the wire's, and it is 186 us against 1392.
     */
    { "atmega328p", AG_BENCH_WRITE_LATENCY, 1625.0,
      "1392 us of it is fifteen frame bytes on the wire; the device adds 186" },
    { "atmega2560", AG_BENCH_WRITE_LATENCY, 1650.0,
      "1402 us of it is fifteen frame bytes on the wire; the device adds 192" },
};

#define AG_BENCH_PIN_COUNT (sizeof ag_bench_pins / sizeof ag_bench_pins[0])

#endif /* AG_BENCH_TARGETS_H */
