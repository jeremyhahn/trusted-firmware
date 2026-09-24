/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * THE PERFORMANCE GATE. What the firmware costs, in cycles, on an exact model
 * of the part -- and a non-zero exit when any of it gets more expensive.
 *
 * harness.c asks whether the shipped image WORKS. This asks what it SPENDS.
 * Both run the same .elf on the same emulated ATmega for the same reason: a
 * cycle count is a property of one instruction stream on one core, and an x86
 * measurement of ag_framer_push would be a number about a machine this firmware
 * never runs on. simavr keeps an exact cycle counter, avr->cycle, incremented
 * per instruction from the same tables the datasheet's timing column comes
 * from, so every figure below is countable rather than sampled -- run it twice
 * and it is the same number, which is what lets it be a gate instead of a graph.
 *
 * ---------------------------------------------------------------- the budget
 *
 * test/bench/targets.h. One table, each row carrying the project plan's target
 * and the plan's reasoning, and the reasoning is printed when a row fails.
 *
 * ------------------------------------------------------------- the two images
 *
 * arduino-generic.elf -- the firmware that ships. The tick ISR, the command
 *   latency and the sustained rate are measured on it, in situ, because all
 *   three are properties of the whole running system rather than of a function:
 *   an ISR is entered by hardware, and a round trip goes through the USART, the
 *   ring, the framer, the dispatcher and back out.
 *
 * agbench.elf -- the same objects with src/main.c exchanged for
 *   test/bench/bench_main.c. The per-byte and per-sweep costs are measured on
 *   it, because -flto inlines ag_framer_push and ag_sched_poll bodily into
 *   main() in the shipped image: `avr-nm` finds no symbol, so there is no
 *   address to call and no boundary to bracket. See the header of bench_main.c
 *   for what that costs in fidelity -- in short, an out-of-line body is the
 *   compiler's general compilation and the inlined one can only be faster, so
 *   these numbers are upper bounds.
 *
 *   The claim is CHECKED and not asserted: ag_crc16_update survives as a real
 *   function in both images, and measure_crc_identity() below calls it in each
 *   and requires the same cycle count.
 *
 * -------------------------------------------------- calling into the emulator
 *
 * The per-byte benchmarks need to invoke a function in an image whose main loop
 * is doing something else. The arguments go into r24 and downwards per the
 * avr-gcc ABI, and call_fn() then does what a `call` instruction does: it pushes
 * a return address the firmware will never otherwise contain, sets the program
 * counter and runs until the counter arrives at that address. The span it
 * returns is the function's own cycles plus its closing `ret`, and nothing else
 * -- writing registers from outside costs the emulated part no cycles at all.
 * Afterwards the whole machine goes back to the resting state snap_take()
 * recorded, which matters more than it sounds like it should.
 *
 * Interrupts are cleared for the duration. They are not noise to be averaged
 * out: the tick fires every 16 000 cycles and a two-hundred-cycle measurement
 * that catches one is fifty-eight cycles wrong, one time in eighty, which is
 * exactly the kind of number that gets called flaky and then ignored.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <simavr/sim_avr.h>
#include <simavr/sim_core.h>
#include <simavr/sim_elf.h>
#include <simavr/sim_io.h>
#include <simavr/avr_uart.h>

#include "ag/status.h"
#include "ag/protocol.h"
#include "ag/config.h"
#include "crc16.h"
#include "targets.h"

#ifndef AG_SIMAVR_MCU
#error "AG_SIMAVR_MCU must name the part -- the .elf does not carry it"
#endif
#ifndef AG_BENCH_F_CPU
#error "AG_BENCH_F_CPU must match the -DF_CPU the image was built with"
#endif
#ifndef AG_BENCH_BOARD
#error "AG_BENCH_BOARD must name the board whose ceilings apply"
#endif

/*
 * The stack pointer's data-space address. 0x5D/0x5E on every part this gate
 * runs on and on every megaAVR; the ATtiny85 puts it at 0x3D/0x3E, which is one
 * of the reasons BENCH_BOARDS does not include it. simavr keeps no accessor for
 * it -- _avr_sp_get is private to the emulator core.
 */
#define R_SPL 0x5Du
#define R_SPH 0x5Eu

/*
 * The return address call_fn() plants. Byte address 2 is the second word of the
 * reset vector -- inside the vector table, reachable by nothing, and even
 * arrival is only observed and never executed. It must be even, because an AVR
 * return address is a WORD address.
 */
#define BENCH_RET_ADDR 0x0002u

/*
 * The ATmega's interrupt response: four clocks to push the program counter and
 * fetch the vector, before the first instruction of the handler.
 *
 * simavr does not model it. That is measured and not assumed -- the emulator
 * step in which the program counter lands on a vector slot costs one or two
 * cycles, which is the interrupted instruction's own cost and nothing more. So
 * the four are added here, once, where they can be seen.
 */
#define AVR_INT_ENTRY_CYCLES 4u

/*
 * USART0's divisor and double-speed bit, by data-space address. The same three
 * addresses on the 328P, the 32U4 and the 2560; simavr keeps the avr_uart_t
 * that holds them private, so there is no accessor to ask.
 */
#define R_UCSR0A 0x00C0u
#define R_UBRR0L 0x00C4u
#define R_UBRR0H 0x00C5u
#define BIT_U2X0 0x02u

/* Nominal 8N1: a start bit, eight data bits and a stop bit. What the emulator
 * actually charges per byte is measured -- see wire_cycles below. */
#define NOMINAL_BYTE_BITS 10.0

/* One byte's occupancy of the emulated wire, in cycles, and the baud rate the
 * firmware's own divisor produces. Established by uart_calibrate() and then
 * trued up against the emulator's observed pacing before any exchange is
 * timed -- see the warm-up in main(). */
static double wire_cycles;
static double wire_baud;

/* A runaway emulated call must fail the gate, not hang the CI job. */
#define STEP_LIMIT 20000000L

/* ------------------------------------------------------------- reporting */

static int failures;

static void fatal(const char *what)
{
    printf("  %-62s %s\n", what, "FAIL");
    printf("    the benchmark could not run, which is a failure and not a skip\n");
    failures++;
}

/* ------------------------------------------------------------- plumbing */

typedef struct {
    avr_t         *avr;
    elf_firmware_t fw;
    const char    *path;

    /* The machine's RESTING STATE: what it looks like when it is doing nothing
     * that matters, and what call_fn() puts it back to. See snap_take(). */
    uint8_t         snap_regs[32];
    uint8_t         snap_sreg[8];
    uint16_t        snap_sp;
    avr_flashaddr_t snap_pc;
    int             snapped;
} image_t;

static void image_load(image_t *im, const char *path)
{
    memset(&im->fw, 0, sizeof im->fw);
    im->path = path;
    if (elf_read_firmware(path, &im->fw) != 0) {
        fprintf(stderr, "bench: cannot read %s\n", path);
        exit(2);
    }
    /* The .elf carries no clock speed, so it comes from the board profile the
     * harness was compiled against -- and it must match the -DF_CPU the image
     * was built with, or every baud divisor and timer reload in it is computed
     * against a different clock than the one it runs on. */
    im->fw.frequency = (uint32_t)AG_BENCH_F_CPU;
    strcpy(im->fw.mmcu, AG_SIMAVR_MCU);

    im->avr = avr_make_mcu_by_name(im->fw.mmcu);
    if (!im->avr) {
        fprintf(stderr, "bench: no simavr core for %s\n", im->fw.mmcu);
        exit(2);
    }
    avr_init(im->avr);
    avr_load_firmware(im->avr, &im->fw);

    /* simavr echoes everything USART0 transmits to stderr unless told not to.
     * Every frame this gate exchanges would land in the CI log as binary. */
    {
        uint32_t flags = 0;
        avr_ioctl(im->avr, AVR_IOCTL_UART_GET_FLAGS('0'), &flags);
        flags &= (uint32_t)~AVR_UART_FLAG_STDIO;
        avr_ioctl(im->avr, AVR_IOCTL_UART_SET_FLAGS('0'), &flags);
    }
}

/*
 * Flash symbols come back as byte addresses. Data symbols carry the 0x800000
 * the GNU linker adds to fake a flat address space over a Harvard part, and
 * that bit is not part of the address -- leaving it on indexes avr->data 8 MB
 * past the end of the emulated SRAM, which is a segfault in the harness rather
 * than anything about the firmware.
 */
#define SYM_NONE 0xFFFFFFFFu

static uint32_t sym_addr(const image_t *im, const char *name)
{
    uint32_t i;

    for (i = 0; i < im->fw.symbolcount; i++) {
        if (strcmp(im->fw.symbol[i]->symbol, name) == 0) {
            uint32_t a = im->fw.symbol[i]->addr;
            return (a & 0x800000u) ? (a & 0xFFFFu) : a;
        }
    }
    return SYM_NONE;
}

/*
 * The symbol AT an address, for naming the interrupt handler the run entered.
 *
 * An exact match and not the nearest preceding one. A nearest-preceding lookup
 * over this table reports __FUSE_REGION_LENGTH__ for anything in the low part of
 * flash, because an ELF symbol table holds absolute symbols -- region lengths,
 * register aliases -- whose "address" is a size and sorts below every function.
 */
static const char *sym_name_at(const image_t *im, uint32_t addr)
{
    uint32_t i;

    for (i = 0; i < im->fw.symbolcount; i++) {
        if (im->fw.symbol[i]->addr == addr && im->fw.symbol[i]->symbol[0] != '\0') {
            return im->fw.symbol[i]->symbol;
        }
    }
    return "an unnamed handler";
}

static uint16_t sp_get(const avr_t *a)
{
    return (uint16_t)((uint16_t)a->data[R_SPL] | ((uint16_t)a->data[R_SPH] << 8));
}

static void sp_set(avr_t *a, uint16_t sp)
{
    a->data[R_SPL] = (uint8_t)(sp & 0xFFu);
    a->data[R_SPH] = (uint8_t)(sp >> 8);
}

/* avr_run executes exactly one instruction and charges avr->cycle what that
 * instruction costs, so instruction counts and cycle counts are different
 * things. Everything timed below uses avr->cycle; only the settling helpers
 * count instructions, and they only need to be generous. */
static void run_instructions(avr_t *a, long n)
{
    long i;
    for (i = 0; i < n; i++) { avr_run(a); }
}

static void run_until_cycle(avr_t *a, avr_cycle_count_t target)
{
    long guard = 0;
    while (a->cycle < target) {
        avr_run(a);
        if (++guard > STEP_LIMIT) { fatal("emulator made no progress"); return; }
    }
}

/* ------------------------------------------------- calling into the image */

/*
 * The avr-gcc argument registers, left to right from r25 downwards, each
 * argument rounded up to an even number of bytes. A uint8_t therefore occupies
 * r25:r24 and is passed in r24; the next argument starts at r23.
 */
static void arg_u8(avr_t *a, unsigned reg, uint8_t v)
{
    a->data[reg] = v;
}

static void arg_u16(avr_t *a, unsigned reg, uint16_t v)
{
    a->data[reg]     = (uint8_t)(v & 0xFFu);
    a->data[reg + 1] = (uint8_t)(v >> 8);
}

static void arg_u32(avr_t *a, unsigned reg, uint32_t v)
{
    unsigned i;
    for (i = 0; i < 4u; i++) { a->data[reg + i] = (uint8_t)((v >> (8u * i)) & 0xFFu); }
}

/* r24 as the called function left it, captured before the register file is put
 * back. An AVR return value of one byte lives there; a two-byte one would need
 * r25 beside it, and nothing here returns one. */
static uint8_t call_ret_lo;

/*
 * Remember the machine's resting state, so call_fn() has somewhere to put it
 * back to.
 *
 * Taken ONCE, before any argument is written into the register file, and that
 * ordering is the whole point. Snapshotting inside call_fn() instead captures
 * the arguments the caller has just poked into r24 and downwards, and restoring
 * that afterwards leaves those values in the resting machine's registers.
 *
 * What that cost: agbench.elf's resting loop is `lds r24, ag_bench_trigger; cp
 * r24, r1; breq`, and the first benchmark leaves 8 in r24. The next time the
 * part was allowed to run freely -- to let ten milliseconds pass so a scheduler
 * deadline could arrive -- the comparison read the 8 rather than the trigger,
 * took the branch that is meant to be unreachable, and ran every benchmark
 * function once with 8 as its argument. One of them re-initialises the
 * scheduler. The measurement that followed swept a table it had just emptied
 * and reported a sweep of sixteen firing timers as costing what an empty sweep
 * costs, which is a believable number and therefore a dangerous one.
 */
static void snap_take(image_t *im)
{
    memcpy(im->snap_regs, im->avr->data, sizeof im->snap_regs);
    memcpy(im->snap_sreg, im->avr->sreg, sizeof im->snap_sreg);
    im->snap_sp = sp_get(im->avr);
    im->snap_pc = im->avr->pc;
    im->snapped = 1;
}

/*
 * Runs one function in the emulated part and returns what it cost.
 *
 * The pushed return address matches _avr_push_addr exactly: a WORD address,
 * least significant byte at the highest stack slot, so the part's own `ret`
 * pops it correctly. address_size is 2 on the 328P and 3 on the 2560, and
 * taking it from the core rather than hard-coding it is what lets the same
 * harness measure both.
 *
 * THE WHOLE MACHINE STATE IS PUT BACK, and the program counter especially.
 *
 * That is not tidiness. Leaving the program counter at the sentinel means the
 * next thing to run the part -- letting time pass so a scheduler deadline can
 * arrive, for instance -- starts executing at byte address 2, walks into the
 * interrupt vector table, reaches __bad_interrupt and RESETS the part, which
 * re-runs main() and clears everything the benchmark had just set up.
 */
static avr_cycle_count_t call_fn(image_t *im, uint32_t fn)
{
    avr_t            *a = im->avr;
    avr_cycle_count_t c0;
    uint16_t          sp = sp_get(a);
    uint32_t          w  = BENCH_RET_ADDR >> 1;
    unsigned          i;
    long              steps = 0;

    if (!im->snapped) {
        fatal("a function was called before the machine's resting state was taken");
        return 0;
    }

    for (i = 0; i < a->address_size; i++) {
        a->data[sp - i] = (uint8_t)((w >> (8u * i)) & 0xFFu);
    }
    sp_set(a, (uint16_t)(sp - a->address_size));

    /* avr_sreg_set and not a store into a->sreg: the emulator keeps a separate
     * interrupt_state alongside the flag, and writing the flag directly leaves
     * the two disagreeing -- interrupts that never come back, or a benchmark
     * whose first call silently disables the tick for the rest of the run. */
    avr_sreg_set(a, S_I, 0);   /* see the file header: a stray tick is not noise */
    a->pc        = fn;
    c0           = a->cycle;

    while (a->pc != BENCH_RET_ADDR) {
        avr_run(a);
        if (++steps > STEP_LIMIT) {
            fatal("a benchmarked call never returned");
            break;
        }
    }

    call_ret_lo = a->data[24];

    memcpy(a->data, im->snap_regs, sizeof im->snap_regs);
    memcpy(a->sreg, im->snap_sreg, sizeof im->snap_sreg);
    a->sreg[S_I] = 0;
    avr_sreg_set(a, S_I, im->snap_sreg[S_I]);
    sp_set(a, im->snap_sp);
    a->pc = im->snap_pc;
    return a->cycle - c0;
}

/* --------------------------------------------------------- the bench image */

static image_t bench;

static uint32_t a_framer_run, a_crc_run, a_sched_run, a_sched_arm;
static uint32_t a_sched_active, a_bench_buf, a_crc_update_bench;

/*
 * The per-byte slope.
 *
 * Deliberately not one total divided by its byte count. A single run's cost is
 * (fixed setup + n * per-byte), so dividing it reports the setup smeared over
 * whatever n the author happened to pick -- and the published number then
 * changes when someone changes n, for no reason connected to the code. The
 * difference between two runs cancels every term that does not scale: the
 * emulated call, the closing `ret`, ag_framer_init, the prologue.
 */
static double slope(avr_cycle_count_t lo, avr_cycle_count_t hi, unsigned dn)
{
    return (double)(hi - lo) / (double)dn;
}

#define N_LO 8u
#define N_HI 64u

/*
 * Three fill patterns, and the gate takes the worst of them.
 *
 * The CRC is not a fixed cost per byte. ag_crc16_update folds the polynomial in
 * only when the register's top bit is set, so eight conditional branches per
 * byte make the cost depend on the DATA -- a few cycles either way. One pattern
 * would report whichever end of that range the author's test vector happened to
 * land on, and the number would then move when someone changed the vector.
 * All-zeros, all-ones and a stride are cheap to state, deterministic, and
 * between them exercise both arms of the fold.
 */
#define FILLS 3u

static void fill_buf(uint8_t *buf, unsigned which)
{
    unsigned i;

    /* Bytes 0 and 1 are SOF and a LEN of AG_MAX_LEN, so every byte from index
     * two onwards is consumed by AG_FRAME_BODY -- the state that stores the
     * byte and folds it into the running CRC, and the one a real frame spends
     * all but four of its bytes in. The two runs differ only in how many body
     * bytes they push, so the slope is that state and nothing else. */
    buf[0] = AG_SOF;
    buf[1] = AG_MAX_LEN;
    for (i = 2u; i < AG_MAX_FRAME; i++) {
        buf[i] = (which == 0u) ? 0x00u
               : (which == 1u) ? 0xFFu
                               : (uint8_t)(i * 7u);
    }
}

static double measure_frame_byte(void)
{
    uint8_t  buf[AG_MAX_FRAME];
    double   worst = 0.0;
    unsigned f;

    for (f = 0; f < FILLS; f++) {
        avr_cycle_count_t c_lo, c_hi;
        double            per;

        fill_buf(buf, f);
        memcpy(&bench.avr->data[a_bench_buf], buf, sizeof buf);

        arg_u8(bench.avr, 24, N_LO);
        c_lo = call_fn(&bench, a_framer_run);
        arg_u8(bench.avr, 24, N_HI);
        c_hi = call_fn(&bench, a_framer_run);

        per = slope(c_lo, c_hi, N_HI - N_LO);
        if (per > worst) { worst = per; }
    }
    return worst;
}

static double measure_crc_byte(void)
{
    uint8_t  buf[AG_MAX_FRAME];
    double   worst = 0.0;
    unsigned f;

    for (f = 0; f < FILLS; f++) {
        avr_cycle_count_t c_lo, c_hi;
        double            per;

        fill_buf(buf, f);
        memcpy(&bench.avr->data[a_bench_buf], buf, sizeof buf);

        arg_u8(bench.avr, 24, N_LO);
        c_lo = call_fn(&bench, a_crc_run);
        arg_u8(bench.avr, 24, N_HI);
        c_hi = call_fn(&bench, a_crc_run);

        per = slope(c_lo, c_hi, N_HI - N_LO);
        if (per > worst) { worst = per; }
    }
    return worst;
}

/* Arms n slots for `ms` from now, through the real ag_sched_cmd_set. */
static uint8_t sched_arm(uint8_t n, uint32_t ms)
{
    arg_u8(bench.avr, 24, n);      /* uint8_t  -> r25:r24, value in r24 */
    arg_u32(bench.avr, 20, ms);    /* uint32_t -> r23:r20, little endian */
    (void)call_fn(&bench, a_sched_arm);
    return call_ret_lo;
}

static uint8_t sched_active(void)
{
    (void)call_fn(&bench, a_sched_active);
    return call_ret_lo;
}

/* One sweep, from the slope of n sweeps against n. */
static double sched_sweep_slope(avr_cycle_count_t *fixed_out)
{
    avr_cycle_count_t c1, c17;
    double            per;

    arg_u8(bench.avr, 24, 1u);
    c1 = call_fn(&bench, a_sched_run);
    arg_u8(bench.avr, 24, 17u);
    c17 = call_fn(&bench, a_sched_run);

    per = slope(c1, c17, 16u);
    if (fixed_out) {
        /* What one call costs that is not a sweep: the emulated call's `ret`
         * and ag_bench_sched_run's own prologue and loop setup. Needed below,
         * where a sweep that FIRES cannot be measured by repetition -- it fires
         * once and the table is then empty. */
        *fixed_out = (avr_cycle_count_t)((double)c1 - per);
    }
    return per;
}

/*
 * The scheduler, in the three states it is ever in.
 *
 * The plan's budget is one number, and one number for a loop over a table has
 * to mean the worst of them. An empty table is the common case and the cheapest;
 * a full table with nothing due is what a controller running sixteen timers
 * actually pays on every pass; a full table that all comes due at once is the
 * expensive one, because each slot then resolves a pin through the board
 * descriptor and writes a port register.
 */
static double measure_sched_sweep(void)
{
    double            empty, armed, due;
    avr_cycle_count_t fixed, c_due;
    uint8_t           n;

    (void)sched_arm(0u, 0u);                 /* also re-inits the table */
    empty = sched_sweep_slope(NULL);

    /* Far enough out that nothing is due, and inside AG_SCHED_MAX_MS. */
    n = sched_arm((uint8_t)AG_SCHED_SLOTS, 1000000uL);
    if (n != (uint8_t)AG_SCHED_SLOTS) {
        fatal("could not arm every scheduler slot");
        return -1.0;
    }
    armed = sched_sweep_slope(&fixed);

    /* Due. One millisecond, then let the tick run past it -- the interrupt is
     * the firmware's own, so this is the real deadline arriving and not a
     * counter poked from outside. */
    n = sched_arm((uint8_t)AG_SCHED_SLOTS, 1uL);
    if (n != (uint8_t)AG_SCHED_SLOTS) {
        fatal("could not arm every scheduler slot for the firing sweep");
        return -1.0;
    }
    /* Ten milliseconds of the part running on its own, so the tick ISR carries
     * the millisecond counter past the deadline. The clock that matters is the
     * firmware's, not one the harness poked into SRAM. */
    run_until_cycle(bench.avr, bench.avr->cycle + (avr_cycle_count_t)(AG_BENCH_F_CPU / 100u));

    arg_u8(bench.avr, 24, 1u);
    c_due = call_fn(&bench, a_sched_run);
    due   = (double)c_due - (double)fixed;

    if (sched_active() != 0u) {
        fatal("the firing sweep left slots armed -- it measured the wrong path");
        return -1.0;
    }

    printf("    sweep: %.0f cycles empty, %.0f with %u armed and none due, "
           "%.0f with %u firing\n",
           empty, armed, (unsigned)AG_SCHED_SLOTS, due, (unsigned)AG_SCHED_SLOTS);

    /* The gate takes the worst, because the budget has to hold on the pass
     * where every timer expires at once -- which is the pass a host that armed
     * them together will produce. */
    if (armed > empty) { empty = armed; }
    if (due   > empty) { empty = due;   }
    return empty;
}

/* -------------------------------------------------------- the shipped image */

static image_t prod;

#define RXCAP 4096
static uint8_t           tx_byte[RXCAP];
static avr_cycle_count_t tx_at[RXCAP];
static unsigned          tx_n;

static void on_device_tx(struct avr_irq_t *irq, uint32_t value, void *param)
{
    (void)irq; (void)param;
    if (tx_n < RXCAP) {
        tx_byte[tx_n] = (uint8_t)value;
        tx_at[tx_n]   = prod.avr->cycle;
        tx_n++;
    }
}

static avr_irq_t *uart_in;

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

/* The index in the TX log of the first byte of a valid response to `seq`, or
 * -1. The whole frame must be present and its CRC must check, so the timestamp
 * of the last byte is the timestamp of a complete answer. */
static int find_response(uint8_t seq, unsigned *last_index)
{
    unsigned i;

    for (i = 0; i + 6u <= tx_n; i++) {
        unsigned len, total;
        uint16_t want, got;

        if (tx_byte[i] != AG_SOF) { continue; }
        len = tx_byte[i + 1u];
        if (len < AG_LEN_MIN || len > AG_MAX_LEN) { continue; }
        total = len + AG_FRAME_OVERHEAD;
        if (i + total > tx_n) { continue; }
        want = ag_crc16(&tx_byte[i + 1u], (uint16_t)(len + 1u));
        got  = (uint16_t)(tx_byte[i + total - 2u]
                          | ((uint16_t)tx_byte[i + total - 1u] << 8));
        if (want != got) { continue; }
        if (tx_byte[i + 2u] != seq) { continue; }
        if (!(tx_byte[i + 3u] & AG_OP_RESPONSE_BIT)) { continue; }
        *last_index = i + total - 1u;
        return (int)i;
    }
    return -1;
}

/*
 * One exchange over a modelled wire.
 *
 * The bytes go in one per wire_cycles -- the rate the emulated USART actually
 * runs at -- rather than all at once. Delivering a whole frame in an instant would measure a
 * device that does not exist -- one whose framer is handed eight bytes with no
 * gaps and whose RX ring never has time to drain -- and it would hide the only
 * thing the latency budget is about, which is how much of the exchange is wire
 * and how much is firmware.
 *
 * Returns 0 on success and fills the three instants the budget is expressed in.
 */
/*
 * The four instants an exchange is measured between.
 *
 * The harness hands a byte to the emulated USART at the moment that byte's last
 * bit would have arrived, so a byte occupies the wire for the wire_cycles
 * BEFORE the instant it is handed over. sof_start therefore sits one byte time
 * ahead of the first hand-over, and the four instants then add up exactly:
 * fifteen byte times of wire plus whatever the device spends between having the
 * request and beginning to answer.
 */
typedef struct {
    avr_cycle_count_t sof_start;  /* the SOF byte's start bit enters the wire */
    avr_cycle_count_t req_done;   /* the device has the last request byte */
    avr_cycle_count_t resp_first; /* the device hands its first response byte to the USART */
    avr_cycle_count_t resp_done;  /* the last response byte has finished clocking out */
} exchange_t;

static int exchange(uint8_t seq, uint8_t cmd, const uint8_t *pay, uint8_t plen,
                    exchange_t *at)
{
    uint8_t  wire[128];
    unsigned n = frame_request(wire, seq, cmd, pay, plen);
    unsigned i, last;
    int      first;
    avr_cycle_count_t deadline;

    tx_n = 0;
    at->sof_start = prod.avr->cycle - (avr_cycle_count_t)wire_cycles;
    at->req_done  = prod.avr->cycle;
    for (i = 0; i < n; i++) {
        avr_raise_irq(uart_in, wire[i]);
        at->req_done = prod.avr->cycle;
        run_until_cycle(prod.avr,
                        prod.avr->cycle + (avr_cycle_count_t)wire_cycles);
    }

    /* Forty byte times -- nearly three whole exchanges -- is far more than a
     * stop-and-wait device may take and still be called working. Past it the
     * exchange has failed, and that is a gate failure rather than a longer
     * wait: a benchmark that hangs tells a CI job nothing. */
    deadline = prod.avr->cycle + (avr_cycle_count_t)(wire_cycles * 40.0);
    for (;;) {
        first = find_response(seq, &last);
        if (first >= 0) { break; }
        if (prod.avr->cycle > deadline) { return -1; }
        avr_run(prod.avr);
    }
    at->resp_first = tx_at[first];
    /* The IRQ fires when the byte is handed to the USART; it is on the wire for
     * a further wire_cycles after that, and an exchange is not over until the
     * host could have read it. */
    at->resp_done = tx_at[last] + (avr_cycle_count_t)wire_cycles;
    return 0;
}

static double cycles_to_us(double c)
{
    return c * 1e6 / (double)AG_BENCH_F_CPU;
}

/*
 * THE WIRE, ESTABLISHED RATHER THAN ASSUMED -- and a modelling defect in simavr
 * that silently halves the link.
 *
 * simavr recomputes a USART's cycles-per-byte when UBRRnL or UBRRnH is written,
 * and NOT when U2Xn changes. src/hal/avr/hal_uart.c writes the divisor first and
 * UCSRnA second, which is the natural order and correct on silicon -- the part
 * re-evaluates the divisor continuously and does not care. The emulator does
 * care: it captures U2X0 as it stood when UBRR0L was written, which is zero, and
 * then clocks every byte at half the configured rate for the rest of the run.
 *
 * Measured, not inferred. On the 328P the firmware programs UBRR0 = 16 with U2X0
 * set, which is 117 647 baud and 1 496 cycles a byte; left alone, simavr spaces
 * transmitted bytes 2 992 cycles apart, which is 58 823 baud exactly. Poking the
 * UBRR0L write handler again, after the firmware has set U2X0, brings it to
 * 1 496.
 *
 * So the handler is invoked once here with the value the firmware already wrote.
 * Nothing in the emulated part changes -- the same byte goes back into the same
 * register -- and the divisor the emulator uses becomes the divisor the firmware
 * programmed. Without it every latency and throughput figure below would be
 * twice its true value, which is the sort of wrong that reads as plausible.
 *
 * A second, smaller discrepancy is left alone and reported: simavr charges
 * ELEVEN bit times per byte where 8N1 occupies ten. Everything timed over the
 * wire is therefore about 10% pessimistic, which is the direction a budget
 * tolerates.
 */
static void uart_calibrate(void)
{
    unsigned io = R_UBRR0L - 32u;
    uint16_t ubrr;
    uint8_t  u2x;
    double   baud, nominal, err;

    if (!prod.avr->io[io].w.c) {
        fatal("simavr has no write handler for UBRR0L; the wire cannot be calibrated");
        return;
    }
    prod.avr->io[io].w.c(prod.avr, (avr_io_addr_t)R_UBRR0L,
                         prod.avr->data[R_UBRR0L], prod.avr->io[io].w.param);

    ubrr = (uint16_t)((uint16_t)prod.avr->data[R_UBRR0L]
                      | ((uint16_t)prod.avr->data[R_UBRR0H] << 8));
    u2x  = (uint8_t)(prod.avr->data[R_UCSR0A] & BIT_U2X0);
    baud = (double)AG_BENCH_F_CPU / ((u2x ? 8.0 : 16.0) * ((double)ubrr + 1.0));

    /*
     * The divisor the firmware chose, checked against the baud rate it claims to
     * speak. A UART tolerates a couple of percent over a ten-bit frame and not
     * much more, and a wrong divisor is the classic thing a host build cannot
     * catch -- it is arithmetic over F_CPU that only becomes wrong on the part.
     */
    nominal = (double)AG_UART_BAUD;
    err     = (baud - nominal) / nominal;
    printf("  %-62s %s\n",
           "the USART divisor produces the baud rate the firmware claims",
           (err > -0.03 && err < 0.03) ? "ok" : "FAIL");
    if (err <= -0.03 || err >= 0.03) {
        printf("    UBRR0=%u U2X0=%s gives %.0f baud against a nominal %.0f (%.2f%%)\n",
               ubrr, u2x ? "on" : "off", baud, nominal, err * 100.0);
        failures++;
    }

    wire_baud   = baud;
    wire_cycles = (double)AG_BENCH_F_CPU * NOMINAL_BYTE_BITS / baud;
    printf("    wire: UBRR0=%u U2X0=%s -> %.0f baud (%.2f%% off nominal), "
           "%.0f cycles per byte\n",
           ubrr, u2x ? "on" : "off", baud, err * 100.0, wire_cycles);
}

/* What the emulator really charges per byte, from the gaps between the bytes of
 * a response it has just sent. Compared against the wire the harness paces its
 * own input at, so the two halves of the link are known to agree. */
static double observed_byte_cycles(unsigned n, const avr_cycle_count_t *at)
{
    double   least = 1e30;
    unsigned i;

    for (i = 1u; i < n; i++) {
        double gap = (double)(at[i] - at[i - 1u]);
        if (gap < least) { least = gap; }
    }
    return least;
}

/*
 * THE TICK ISR, measured across a span that is stated rather than implied.
 *
 * Start: the cycle at which the interrupt is TAKEN. The emulator is stepped one
 * instruction at a time and the moment is identified by three facts holding at
 * once -- the global interrupt flag went from set to clear, the program counter
 * moved to a vector-table slot, and it is not where the previous instruction
 * would have fallen through to. Nothing but servicing an interrupt does all
 * three: `cli` clears the flag and does not move the program counter, and a
 * jump into the table moves the counter without clearing the flag.
 *
 * End: the cycle at which `reti` retires. Found without knowing where it is --
 * the return address the hardware just pushed is read back off the emulated
 * stack, and the span ends when the program counter arrives there. That is
 * exactly `reti` having retired, and it needs no assumption about the shape of
 * the handler or the length of the instruction that was interrupted.
 *
 * The span therefore contains the vector table's jmp, the compiler's prologue,
 * the body, the epilogue and the `reti` itself. It does NOT contain the part's
 * four-cycle interrupt response, because simavr does not model it: the step in
 * which the program counter lands on the slot costs one or two cycles, which is
 * the interrupted instruction's own cost. AVR_INT_ENTRY_CYCLES adds it back.
 *
 * Which ISR: the only interrupt armed on an idle device is TIMER0_COMPA, so the
 * first one seen is the tick -- and the run asserts every sample came from the
 * same vector, because a USART interrupt slipping into the sample would report
 * a number for a different handler entirely.
 */
static double measure_tick_isr(const char **name_out)
{
    const unsigned    want = 16u;
    unsigned          seen = 0;
    uint32_t          slot = 0;
    avr_cycle_count_t worst = 0;
    long              guard = 0;

    *name_out = "?";

    while (seen < want) {
        uint8_t  i_before = prod.avr->sreg[S_I];
        uint32_t pc_before = prod.avr->pc;

        avr_run(prod.avr);
        if (++guard > STEP_LIMIT) { fatal("no tick interrupt was ever taken"); return -1.0; }

        if (i_before && !prod.avr->sreg[S_I] &&
            prod.avr->pc < 0x200u &&
            (prod.avr->pc % prod.avr->vector_size) == 0u &&
            prod.avr->pc != pc_before + 2u) {

            avr_cycle_count_t taken = prod.avr->cycle;
            uint16_t          sp    = sp_get(prod.avr);
            uint32_t          ret   = 0;
            unsigned          k;
            int               first = (slot == 0);

            if (slot == 0) {
                slot = prod.avr->pc;
            } else if (slot != prod.avr->pc) {
                fatal("a second interrupt source fired; the sample is not the tick");
                return -1.0;
            }

            /* _avr_push_addr writes the least significant byte at the highest
             * slot, so the address reads back most significant first from
             * sp+1 -- and it is a WORD address. */
            for (k = 0; k < prod.avr->address_size; k++) {
                ret = (ret << 8) | prod.avr->data[sp + 1u + k];
            }
            ret <<= 1;

            /* The vector slot holds a jmp or an rjmp. Executing it is cheaper
             * than decoding it, and where the program counter lands is the
             * handler's own address by definition. */
            avr_run(prod.avr);
            if (first) { *name_out = sym_name_at(&prod, prod.avr->pc); }

            while (prod.avr->pc != ret) {
                avr_run(prod.avr);
                if (++guard > STEP_LIMIT) { fatal("the tick ISR never returned"); return -1.0; }
            }
            if (prod.avr->cycle - taken > worst) { worst = prod.avr->cycle - taken; }
            seen++;
        }
    }

    return (double)worst + (double)AVR_INT_ENTRY_CYCLES;
}

/* ------------------------------------------------- the cross-image check */

/*
 * ag_crc16_update is the one function under test that survives as a real
 * symbol in BOTH images -- everything else -flto inlines away in the shipped
 * one. Calling it in each and requiring the same cycle count is the evidence
 * for the claim bench_main.c makes: that agbench.elf is the same compilation of
 * the same source, and a number measured there is a number about the firmware.
 *
 * Compared by cost rather than by comparing the bytes, because the bytes after
 * the function differ between the two images and a fixed window would compare
 * the next function as well. Cost is what the budget is in anyway.
 */
static void measure_crc_identity(void)
{
    uint32_t          in_prod = sym_addr(&prod, "ag_crc16_update");
    avr_cycle_count_t c_prod, c_bench;

    if (in_prod == SYM_NONE || a_crc_update_bench == SYM_NONE) {
        fatal("ag_crc16_update is missing from one of the images");
        return;
    }

    arg_u16(prod.avr, 24, 0xFFFFu);
    arg_u8(prod.avr, 22, 0x31u);
    c_prod = call_fn(&prod, in_prod);

    arg_u16(bench.avr, 24, 0xFFFFu);
    arg_u8(bench.avr, 22, 0x31u);
    c_bench = call_fn(&bench, a_crc_update_bench);

    printf("  %-62s %s\n",
           "the benchmark image compiles ag_crc16_update as the firmware does",
           c_prod == c_bench ? "ok" : "FAIL");
    if (c_prod != c_bench) {
        printf("    firmware %" PRI_avr_cycle_count " cycles, benchmark image %"
               PRI_avr_cycle_count " -- the two images are not the same build\n",
               c_prod, c_bench);
        failures++;
    }
}

/* --------------------------------------------------------------- the gate */

static double measured[AG_BENCH_METRIC_COUNT];

static double ceiling_for(ag_bench_metric_t m, const char **why)
{
    size_t i;

    for (i = 0; i < AG_BENCH_PIN_COUNT; i++) {
        if (ag_bench_pins[i].metric == m &&
            strcmp(ag_bench_pins[i].board, AG_BENCH_BOARD) == 0) {
            *why = ag_bench_pins[i].why;
            return ag_bench_pins[i].ceiling;
        }
    }
    *why = NULL;
    return ag_bench_budget[m].target;
}

/*
 * The verdict, printed the way size-check prints a flash budget: the number and
 * the bound, every time, pass or fail -- so a figure that is drifting towards
 * its ceiling is visible on the run before the one that fails.
 */
static int verdict(void)
{
    int          over = 0;
    int          missed = 0;
    unsigned     m;

    for (m = 0; m < AG_BENCH_METRIC_COUNT; m++) {
        const ag_bench_budget_t *b = &ag_bench_budget[m];
        const char              *why;
        double                   cap = ceiling_for((ag_bench_metric_t)m, &why);
        double                   got = measured[m];
        int                      bad;

        bad = b->higher_is_better ? (got < cap) : (got > cap);

        printf("  %-12s %-32s %9.1f / %-9.1f %s%s\n",
               AG_BENCH_BOARD, b->name, got, cap,
               b->unit, bad ? "   OVER" : "");

        if (why) {
            missed++;
            printf("      MISS: the plan asks for %s%.1f %s; this build costs %.1f.\n"
                   "            %s\n"
                   "            pinned at %.1f, so it cannot get worse while it is owed.\n",
                   b->higher_is_better ? ">" : "<", b->target, b->unit, got, why, cap);
        }
        if (bad) {
            over++;
            printf("      REGRESSION: %s %.1f %s, bound %.1f\n", b->name, got, b->unit, cap);
            printf("      why the bound exists: %s\n", b->reason);
        }
    }

    if (missed) {
        printf("  %s: %d of %d metrics are over the PLAN'S target and pinned at what\n"
               "  they cost today. They cannot get worse; they are still owed.\n",
               AG_BENCH_BOARD, missed, AG_BENCH_METRIC_COUNT);
    }
    if (over) {
        printf("PERFORMANCE BUDGET EXCEEDED for %s: %d of %d metrics\n",
               AG_BENCH_BOARD, over, AG_BENCH_METRIC_COUNT);
    }
    return over;
}

/* ------------------------------------------------------------------- main */

int main(int argc, char **argv)
{
    const char *prod_path  = argc > 1 ? argv[1] : NULL;
    const char *bench_path = argc > 2 ? argv[2] : NULL;
    const char *isr_name;
    exchange_t  at;
    double      link_floor_us, device_us;

    if (!prod_path || !bench_path) {
        fprintf(stderr, "usage: bench_harness <firmware.elf> <agbench.elf>\n");
        return 2;
    }

    printf("bench: %s on %s at %lu Hz, %lu baud\n",
           AG_BENCH_BOARD, AG_SIMAVR_MCU,
           (unsigned long)AG_BENCH_F_CPU, (unsigned long)AG_UART_BAUD);

    /* ------------------------------------------------ the benchmark image */

    image_load(&bench, bench_path);
    a_framer_run       = sym_addr(&bench, "ag_bench_framer_run");
    a_crc_run          = sym_addr(&bench, "ag_bench_crc_run");
    a_sched_run        = sym_addr(&bench, "ag_bench_sched_run");
    a_sched_arm        = sym_addr(&bench, "ag_bench_sched_arm");
    a_sched_active     = sym_addr(&bench, "ag_bench_sched_active");
    a_bench_buf        = sym_addr(&bench, "ag_bench_buf");
    a_crc_update_bench = sym_addr(&bench, "ag_crc16_update");

    if (a_framer_run == SYM_NONE || a_crc_run == SYM_NONE ||
        a_sched_run  == SYM_NONE || a_sched_arm == SYM_NONE ||
        a_sched_active == SYM_NONE || a_bench_buf == SYM_NONE) {
        fprintf(stderr,
                "bench: an entry point is missing from %s.\n"
                "       --gc-sections dropped it, which means the volatile\n"
                "       pointer table in test/bench/bench_main.c stopped\n"
                "       holding it alive.\n", bench.path);
        return 2;
    }

    /* Past bring-up and into the resting loop. Generous: ag_crypto_init on the
     * software tier derives an identity before main() settles. Then the resting
     * state is taken, BEFORE any argument is written into a register -- see
     * snap_take() for what happens when that ordering is the other way round. */
    run_instructions(bench.avr, 2000000L);
    snap_take(&bench);

    measured[AG_BENCH_FRAME_BYTE]  = measure_frame_byte();
    measured[AG_BENCH_CRC_BYTE]    = measure_crc_byte();
    /* The split, because the two budgets are nested and the totals alone do not
     * say which one is in trouble: every byte the framer stores it also folds
     * into the CRC, so the frame-decode figure CONTAINS the CRC figure. */
    printf("    per received byte: %.0f cycles total = %.0f in ag_crc16_update "
           "+ %.0f in the framer's state machine\n",
           measured[AG_BENCH_FRAME_BYTE], measured[AG_BENCH_CRC_BYTE],
           measured[AG_BENCH_FRAME_BYTE] - measured[AG_BENCH_CRC_BYTE]);
    measured[AG_BENCH_SCHED_SWEEP] = measure_sched_sweep();

    /* -------------------------------------------------- the shipped image */

    image_load(&prod, prod_path);
    uart_in = avr_io_getirq(prod.avr, AVR_IOCTL_UART_GETIRQ('0'), UART_IRQ_INPUT);
    avr_irq_register_notify(
        avr_io_getirq(prod.avr, AVR_IOCTL_UART_GETIRQ('0'), UART_IRQ_OUTPUT),
        on_device_tx, NULL);

    /* Boot, then calibrate the wire against the divisor the firmware just
     * programmed, then let the boot event finish clocking out. Measuring the
     * tick while the USART still has bytes to push would sample two handlers. */
    run_instructions(prod.avr, 2000000L);
    uart_calibrate();
    tx_n = 0;
    run_until_cycle(prod.avr, prod.avr->cycle + (avr_cycle_count_t)(wire_cycles * 20.0));

    measured[AG_BENCH_TICK_ISR] = measure_tick_isr(&isr_name);
    printf("    tick ISR: %s, %.0f cycles including the part's %u-cycle "
           "interrupt response\n",
           isr_name, measured[AG_BENCH_TICK_ISR], AVR_INT_ENTRY_CYCLES);

    /* DIGITAL_WRITE. The pin is made an output first, because the budget is for
     * the command a host issues in a loop, not for the one-off that configures
     * it -- and PIN_MODE also proves the exchange path works before a number is
     * taken from it. */
    {
        const uint8_t mode_out[2] = { 13u, 1u };

        if (exchange(0x40u, AG_CMD_PIN_MODE, mode_out, 2u, &at) != 0) {
            fatal("PIN_MODE did not answer");
            return 1;
        }
        /*
         * What the emulator charges per byte, taken from the response it has
         * just sent, against the wire the harness paces its own input at. They
         * have to agree, or the two directions of a "link-bound" exchange are
         * running at different speeds and the split below means nothing.
         *
         * The tolerance is 15% and not 1%: simavr charges eleven bit times to
         * 8N1's ten, which is the discrepancy the comment on uart_calibrate()
         * describes and the reason every wire figure here is slightly
         * pessimistic. A factor of two -- the uncalibrated divisor -- is nowhere
         * near it and would fail.
         */
        {
            double obs   = observed_byte_cycles(tx_n, tx_at);
            double ratio = obs / wire_cycles;

            printf("  %-62s %s\n",
                   "both directions of the link run at the rate the divisor sets",
                   (ratio > 0.85 && ratio < 1.15) ? "ok" : "FAIL");
            printf("    the emulated USART spends %.0f cycles a byte where 8N1 at "
                   "%.0f baud is %.0f; the harness now sends at the emulator's rate "
                   "so neither direction runs ahead of the other\n",
                   obs, wire_baud, wire_cycles);
            if (ratio <= 0.85 || ratio >= 1.15) { failures++; }
            /* The emulator is the wire. Pacing the harness's own bytes at the
             * analytic rate while the device transmits at a slightly different
             * one lets the input queue build a backlog across a frame, and that
             * backlog then shows up as latency the firmware did not spend. */
            wire_cycles = obs;
        }

    }

    /*
     * DIGITAL_WRITE, twenty-four times, stop-and-wait: the next request starts
     * only once the previous response has cleared the wire, which is what a host
     * driving this protocol actually does. One loop yields both budgets --
     * the WORST round trip and the sustained rate -- because they are the same
     * traffic looked at two ways, and measuring them separately would let the
     * quoted latency come from an exchange the quoted rate never contained.
     *
     * The worst and not the first. The device is a superloop: a byte can land
     * at any point in it, so a round trip varies by wherever ag_sched_poll and
     * ag_sys_poll happened to be. One sample is a sample; a budget wants the
     * tail.
     */
    {
        const unsigned    reps = 24u;
        const uint8_t     high[2] = { 13u, 1u };
        avr_cycle_count_t t0 = prod.avr->cycle;
        avr_cycle_count_t worst = 0, best = ~(avr_cycle_count_t)0;
        avr_cycle_count_t worst_dev = 0, busy = 0;
        unsigned          k;

        for (k = 0; k < reps; k++) {
            avr_cycle_count_t total, dev;

            if (exchange((uint8_t)(0x50u + k), AG_CMD_DIGITAL_WRITE, high, 2u, &at) != 0) {
                fatal("a DIGITAL_WRITE round trip was lost");
                return 1;
            }
            total = at.resp_done - at.sof_start;
            dev   = at.resp_first - at.req_done;
            if (total > worst)   { worst = total; }
            if (total < best)    { best = total; }
            if (dev > worst_dev) { worst_dev = dev; }
            busy += dev;

            /* Stop and wait. */
            run_until_cycle(prod.avr, at.resp_done);
        }

        measured[AG_BENCH_WRITE_LATENCY] = cycles_to_us((double)worst);
        measured[AG_BENCH_CMD_RATE] =
            (double)reps * (double)AG_BENCH_F_CPU / (double)(prod.avr->cycle - t0);

        /*
         * The decomposition, which is the claim the plan was actually making.
         * Eight request bytes and seven response bytes is fifteen byte times of
         * wire that no firmware can avoid; what the device adds is the gap
         * between having the last request byte and starting to answer.
         */
        link_floor_us = cycles_to_us(wire_cycles * 15.0);
        device_us     = cycles_to_us((double)worst_dev);
        printf("    DIGITAL_WRITE round trip: worst %.0f us, best %.0f us\n",
               cycles_to_us((double)worst), cycles_to_us((double)best));
        printf("      = %.0f us of wire (15 frame bytes at %.0f baud) "
               "+ up to %.0f us the device adds\n",
               link_floor_us, wire_baud, device_us);
        printf("    sustained: %.0f cmd/s, the device busy for %.1f%% of it "
               "and the link for the rest\n",
               measured[AG_BENCH_CMD_RATE],
               100.0 * (double)busy / (double)(prod.avr->cycle - t0));

        /*
         * THE ASSERTION THE PLAN'S REASONING ACTUALLY MAKES. "Dominated by the
         * UART, not by logic" is a claim about the SPLIT, and the split is
         * checkable even where the 300 us figure is not reachable at this baud
         * rate by any device at all. If the firmware's own share ever overtakes
         * the wire, the exchange has stopped being link-bound and every other
         * number here rests on an assumption that no longer holds.
         */
        printf("  %-62s %s\n",
               "the exchange is bounded by the link and not by the firmware",
               device_us < link_floor_us ? "ok" : "FAIL");
        if (device_us >= link_floor_us) {
            printf("    the firmware's own %.0f us now exceeds the %.0f us of wire\n",
                   device_us, link_floor_us);
            failures++;
        }
    }

    /* The last thing done to the shipped image, and the only thing that calls
     * into it -- so its resting state is taken here, after every in-situ
     * measurement has finished with the running system. */
    snap_take(&prod);
    measure_crc_identity();

    printf("\n");
    if (verdict()) { failures++; }

    printf("%s: %s, %d failure(s)\n",
           failures ? "FAIL" : "PASS", AG_BENCH_BOARD, failures);
    return failures ? 1 : 0;
}
