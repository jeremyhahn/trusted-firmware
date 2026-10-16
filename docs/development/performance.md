# Performance

> **Status: INFORMATIVE** · Scope: `make bench-check`, `test/bench/` · Last verified: 2026-09-26
> What the firmware costs in cycles, how it is measured, and which of the
> project plan's targets it meets, after #12 (the CRC fix) and #11 (the ceiling
> policy and the two restated targets).

Run it with `make bench-check`. It is **not** part of `make ci` and not run by
any CI job: six of eight rows
are still over the plan's target even after #12, so a gate over them would be
either permanently red or pinned at today's cost and green while the firmware
is that far over budget. A green `make ci` keeps meaning what it has always
meant. The remaining debt is tracked in #11 and #13's follow-ons, and this
document is where the numbers live in the meantime.

## The ceiling policy (#11)

`test/bench/targets.h` carries two columns per row: `target`, the plan's own
number, kept verbatim; and `ceiling`, what the gate actually enforces. Where
the firmware meets its target the two are the same number and the row is
enforced there directly -- `sustained command rate` and, as of #11's
restatement below, `DIGITAL_WRITE, device share only`. Where it does not, the
ceiling is pinned at what the code costs today, to the cycle or, for a
wall-clock span, to the measured worst plus two percent rounded up to the next
25 us. **A ceiling only ever moves down, by making the code faster** -- never
up, and never to make a row quietly stop printing `MISS`.

This is a policy choice, not a relaxation, and it is worth restating why:
lowering a target to match the code would erase the fact that the firmware is
over budget, and enforcing a target the code has never met leaves the gate red
from the day it lands, which is the fastest way to get a gate switched off
within a week. Pinning today's cost as a ceiling makes it the new worst case --
the code cannot get slower without failing `make bench-check` -- while the
`MISS` line keeps naming the debt and its size on every single run. The
alternative the plan's own document warns about elsewhere applies here too: *a
gate that can pass vacuously is worse than no gate.* This one cannot pass
vacuously -- it enforces no-regression on eight real numbers and states the gap
on six of them, every run -- and the distinction is an explicit policy.

## What the gate is

`size-check` for time. It fails the build when the firmware gets slower, so
performance creep is caught on the commit that causes it rather than on the day
a software UART starts dropping bits and nobody can say which of two hundred
commits did it.

Every number comes from simavr's cycle counter running a real `.elf`. There is
no host-side timing and there must not be: an x86 measurement of
`tfw_framer_push` says nothing about an eight-bit part with a two-cycle SRAM
access, and a budget measured against the wrong machine is worse than no budget
because it looks like one.

The budget lives in one file, `test/bench/targets.h`, with the plan's reasoning
beside each row.

## Where the numbers stand

ATmega328P and ATmega2560, 16 MHz, `CRYPTO=soft`, avr-gcc as pinned in
`docker/Dockerfile.ci`.

| | plan's target | 328P | 2560 | verdict |
|---|---|---|---|---|
| frame decode, per byte | < 40 cycles | 62.0 | 64.0 | **over, was 3x, now ~1.6x** |
| CRC-16, per byte | < 20 cycles | 31.0 | 33.0 | **over, was 5x, now ~1.6x** |
| `tfw_sched_poll()`, worst sweep | < 200 cycles | 1066 (8 slots) | 2158 (16 slots) | **over -- see the per-slot row below** |
| `tfw_sched_poll()`, per armed slot (#11) | < 12.5 cycles/slot | 133.25 | 134.875 | **over, ~10.7x** |
| tick ISR, vector to `reti` | <= 12 cycles | 58 | 59 | **5x over** |
| `DIGITAL_WRITE`, SOF to response | < 300 us | 1572.8 | 1577.1 | **over -- see the device-only row below** |
| `DIGITAL_WRITE`, device share only (#11) | < 300 us | 169.1 | 172.9 | **met** |
| sustained command rate | > 400 cmd/s | 679.0 | 675.9 | **met, ~1.7x margin** |

Two of eight rows are met. Two of the eight are #11's restatement of targets the
plan could never have met as literally stated (the full sweep total, and the
whole round trip including the wire); one of those restatements now passes.
The remaining rows that miss are enforced at what they cost today — see
*Target vs ceiling* above.

### What dominates each miss

**#12 fixed the CRC, and that fixed most of frame decode with it.**
`src/proto/crc16_update.c` — the portable form, still what host tests, `tfwsim`
and the bootloader link — folds the polynomial one bit at a time: eight
iterations, each a shift, a conditional exclusive-or and a loop test. The AVR
firmware and bench images now link `src/hal/avr/hal_crc16.c` instead, which
calls avr-libc's `_crc_xmodem_update` — the same polynomial, same output,
fourteen single-cycle instructions, branch-free. Measured through the bench
harness's forced call boundary (see `test/bench/targets.h`'s CRC-16 row for why
that boundary, rather than the fourteen instructions alone, is what the number
contains), the cost dropped from 102.5/104.5 cycles/byte to 31.0/33.0 — about a
3.3x improvement, closing most but not all of the gap to the plan's 20. Frame
decode contains the CRC figure and dropped by almost exactly the same amount,
120.2/122.2 to 62.0/64.0.

**The tick ISR is a 32-bit volatile increment.** `ticks` is an `tfw_ms_t`, four
bytes, volatile: four `lds`, an `adiw` and two `adc`, four `sts` — twenty cycles
— and to do that the handler must save r24 through r27 alongside r1 and SREG,
nineteen more in push and pop. The `ticks++` in the source is one line; the
compiled ISR is twenty-seven instructions. Twelve cycles is roughly what a
one-byte counter would cost; nothing in this family increments a 32-bit volatile
in twelve. #13 corrected the comment in `src/hal/avr/hal_tick.c` that used to
claim ten cycles to this measured figure.

**The scheduler sweep is over budget in every state, and #11 restates why the
raw total was the wrong number to hold to one target.** The plan's 200 cycles
is "at 16 slots", which is the ATmega2560's `TFW_SCHED_SLOTS`. On that part an
*empty* sweep is already 242 cycles — `tfw_hal_millis` costs an atomic section and
a four-byte load, then sixteen slots each cost a load and a bit test. Sixteen
armed and not due is 546. Sixteen coming due at once is 2158, because each one
then resolves its pin through the board descriptor and writes a port register.
The gate takes the worst of the three: a host that arms sixteen timers together
is a host that will have them expire together. Restated per armed slot — the
worst sweep divided by the board's `TFW_SCHED_SLOTS` — the 328P and the 2560
agree to within about a cycle and a quarter (133.25 vs 134.875), which the raw
totals do not, because the totals are dominated by each board's own fixed empty-
sweep cost and only partly by the slot count. The plan's 200-cycle figure "at 16
slots" restates the same way to 12.5 cycles/slot, which is the target this new
row is held to — still missed by about 10.7x, and now comparable across boards
rather than being a number that only ever meant something on the 2560.

**300 us for the whole exchange is below the link's own floor, and #11 restates
the plan's actual claim as the row that is now reachable.** A `DIGITAL_WRITE`
exchange is eight request bytes and seven response bytes. At 115200 8N1 a byte
occupies ten bit times, 87 us, so fifteen bytes is 1302 us of wire before any
firmware runs — 1402 us as simavr clocks it once calibrated to the divisor the
firmware itself programmed. The device adds 169-173 us on top, and that is the
only part this repository can change; no device can meet 300 us for the
*whole* exchange at this baud rate. What the plan's reasoning actually
claimed — "dominated by the UART, not by logic" — is a claim about the DEVICE's
own share, and restated that way against the same 300 us, the firmware meets it:
169.1 us (328P) and 172.9 us (2560), both comfortably under. This is the one
restated row that is not merely comparable now but genuinely met, and it is not
pinned in `test/bench/targets.h` for that reason — its ceiling is the plan's own
300, unmodified, the same as the sustained-rate row.

(These numbers move a little with any change to compiled size or layout --
they were re-measured once more after a fix, unrelated to CRC or scheduling,
to a stale-counter bug in `src/proto/transport.c`. The gate re-measures on
every run, so a stale figure in prose is a documentation gap, not a
regression.)

## Target vs ceiling, in one place

See *The ceiling policy (#11)* above for the policy itself. The
rounding rule, precisely: a cycle count is pinned at the measured value rounded
up to the next whole cycle — it is exactly reproducible, so anything costing a
cycle more fails. A ratio derived from two exact integers (the per-armed-slot
row) is pinned at the exact quotient, unrounded, for the same reason: it is not
a sampled cycle count, it is arithmetic on one, and rounding it would hide a
one-cycle regression in the sweep it is computed from. A wall-clock span is
pinned at the measured worst plus two percent, rounded up to 25 us, because a
round trip moves by more than that when anything in the superloop changes
size.

## How it is measured

### Two images

`build/<board>/soft/trusted-firmware.elf` — the firmware that ships. The tick ISR,
the command latency and the sustained rate are measured on it, in situ, because
all three are properties of the whole running system: an ISR is entered by
hardware, and a round trip goes through the USART, the ring, the framer, the
dispatcher and back out.

`build/bench/<board>/soft/agbench.elf` — the same objects with `src/main.c`
exchanged for `test/bench/bench_main.c`, same `FW_CFLAGS`, same `-Os -flto`,
same board profile. The per-byte and per-sweep costs are measured on it because
`-flto` inlines `tfw_framer_push` and `tfw_sched_poll` bodily into `main()` in the
shipped image — `avr-nm` finds no symbol for either, so there is no address to
call and no boundary to bracket. Only `tfw_crc16_update` survives as a real
function.

In the benchmark image each subject is reached through a `volatile` function
pointer, which forces the compiler to emit it out of line and stops
`--gc-sections` dropping it. An out-of-line body is the compiler's general
compilation: no constant propagated in from a caller. The shipped image's inlined
copy can be faster, never slower, so **these numbers are upper bounds** on what
the firmware pays.

The claim is checked rather than asserted. `tfw_crc16_update` exists in both
images; the harness calls it in each and requires the same cycle count. If the
benchmark image ever stops generating the same code as the firmware, that check
fails.

### Per-byte costs are slopes

Each per-byte figure is the difference between a run over 8 bytes and a run over
64, divided by 56. Dividing one total instead would report the fixed setup
smeared over whichever byte count the author picked, and the published number
would then move when someone changed that count for unrelated reasons. The
difference cancels every term that does not scale: the emulated call, the closing
`ret`, `tfw_framer_init`, the prologue.

The CRC is not a fixed cost per byte — the polynomial fold is conditional, so
eight branches per byte make it depend on the data. Three fill patterns are
measured (all-zeros, all-ones, a stride) and the gate takes the worst.

### The tick ISR's span, exactly

**Start:** the cycle at which the interrupt is *taken*. The emulator is stepped
one instruction at a time and the moment is identified by three facts holding at
once — the global interrupt flag went from set to clear, the program counter
moved to a vector-table slot, and it is not where the previous instruction would
have fallen through to. Nothing but servicing an interrupt does all three: `cli`
clears the flag without moving the program counter, and a jump into the table
moves the counter without clearing the flag.

**End:** the cycle at which `reti` retires. Found without knowing where it is —
the return address the hardware just pushed is read back off the emulated stack,
and the span ends when the program counter arrives there. That is exactly `reti`
having retired, and it assumes nothing about the shape of the handler or the
length of the instruction that was interrupted.

The span therefore contains the vector table's `jmp`, the compiler's prologue,
the body, the epilogue and the `reti`. It does **not** contain the part's
four-cycle interrupt response, because simavr does not model it — the step in
which the program counter lands on the slot costs one or two cycles, which is the
interrupted instruction's own cost and nothing more. The four are added
explicitly, in `AVR_INT_ENTRY_CYCLES`, where they can be seen.

Sixteen consecutive tick interrupts are measured and the worst is taken. The run
asserts every sample came from the same vector, because a USART interrupt
slipping into the sample would report a number for a different handler.

### The sustained rate is a link bound, and the gate says so

Twenty-four `DIGITAL_WRITE` exchanges, stop-and-wait: the next request starts
only once the previous response has cleared the wire. 679.0 cmd/s on the 328P
against a target of 400, with the device busy for 12% of the elapsed time and the
link for the other 88%. The plan calls this "a stop-and-wait bound, not a CPU
bound" and the measurement agrees — the answer *is* the baud-rate limit, and the
gate asserts the device is not the bottleneck rather than merely reporting a
number.

## A simavr modelling defect worth knowing about

simavr recomputes a USART's cycles-per-byte when `UBRRnL` or `UBRRnH` is written,
and **not** when `U2Xn` changes. `src/hal/avr/hal_uart.c` writes the divisor first
and `UCSRnA` second, which is the natural order and correct on silicon — the part
re-evaluates the divisor continuously and does not care. The emulator does care:
it captures `U2X0` as it stood when `UBRR0L` was written, which is zero, and then
clocks every byte at **half** the configured rate for the rest of the run.

Measured, not inferred. On the 328P the firmware programs `UBRR0 = 16` with
`U2X0` set, which is 117647 baud and 1496 cycles a byte; left alone, simavr
spaces transmitted bytes 2992 cycles apart, which is 58823 baud exactly.

`uart_calibrate()` in the harness invokes the `UBRR0L` write handler once with
the value the firmware already wrote. Nothing in the emulated part changes, and
the divisor the emulator uses becomes the divisor the firmware programmed.
Without it every latency and throughput figure would be twice its true value —
the sort of wrong that reads as plausible.

**This affects `test/integration/simavr/harness.c` too, and #14 corrected its
comment to say so.** That harness is uncalibrated on purpose — it asserts
correctness, not timing, so `uart_calibrate()`'s machinery would be
overhead spent on a number nothing there reads — so its `send()` pacing runs at
the emulator's uncorrected ~187 us/byte rather than the ~87 us 8N1 at 115200
actually is, explained in place rather than left as a bare number. #14 also
fixed a real defect in the same file: `run_cycles()` counted calls to
`avr_run()`, one AVR *instruction* apiece, not one cycle — 1.436 cycles/call on
average for this firmware, so its one “simulated second” (16 000 000 calls) was
actually ~1.44. It is now two functions, `run_instructions()` for settling
(which only ever needed "enough") and `run_until_cycle()` for the one place that
needed an actual span of simulated time, and the tick-rate assertion's bound is
derived from that exact span rather than from the instruction count that used
to stand in for it.

A second, smaller discrepancy is left alone and reported: simavr charges eleven
bit times per byte where 8N1 occupies ten, so everything timed over the wire is
about 10% pessimistic. That is the direction a budget tolerates.

## Boards

Enforced on the ATmega328P and the ATmega2560. The 328P is the reference part and
the one the wire protocol was sized for; the 2560 is there because the plan states
the scheduler budget "at 16 slots" and sixteen is the 2560's `TFW_SCHED_SLOTS` —
the 328P has eight, so a 328P-only gate would never once have measured the number
the plan asks about.

The numbers hold across the two within a couple of percent. Everything on the
2560 is one to two cycles dearer per call, which is its three-byte program
counter being pushed and popped.

The ATmega32U4 is not in the list and neither is the ATtiny85. That is a scope,
not a skip: the 32U4 is a 328P-class core that introduces no path the two above do
not cover, and the ATtiny85 has no USART to time an exchange over and puts its
stack pointer at a different address. `BENCH_BOARDS` in `mk/bench.mk` is the whole
change required to add one.
