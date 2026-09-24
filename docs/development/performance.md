# Performance

Scope: what the firmware costs in cycles, how it is measured, and which of the
project plan's targets it meets. Last verified 2026-09-23 against
`make bench-check`.

Run it with `make bench-check`. It is **not** part of `make ci` and not run by
any CI job (owner ruling, 2026-09-23): five of the six targets are missed by
between 3x and 11x and two are unreachable as the plan states them, so a gate
over them would be either permanently red or pinned at today's cost and green
while the firmware is that far over budget. A green `make ci` keeps meaning
what it has always meant. The debt is tracked in #11, #12 and #13, and this
document is where the numbers live in the meantime.

## What the gate is

`size-check` for time. It fails the build when the firmware gets slower, so
performance creep is caught on the commit that causes it rather than on the day
a software UART starts dropping bits and nobody can say which of two hundred
commits did it.

Every number comes from simavr's cycle counter running a real `.elf`. There is
no host-side timing and there must not be: an x86 measurement of
`ag_framer_push` says nothing about an eight-bit part with a two-cycle SRAM
access, and a budget measured against the wrong machine is worse than no budget
because it looks like one.

The budget lives in one file, `test/bench/targets.h`, with the plan's reasoning
beside each row.

## Where the numbers stand

ATmega328P and ATmega2560, 16 MHz, `CRYPTO=soft`, avr-gcc as pinned in
`docker/Dockerfile.ci`.

| | plan's target | 328P | 2560 | verdict |
|---|---|---|---|---|
| frame decode, per byte | < 40 cycles | 120.2 | 122.2 | **3x over** |
| CRC-16, per byte | < 20 cycles | 102.5 | 104.5 | **5x over** |
| `ag_sched_poll()`, worst sweep | < 200 cycles | 1066 (8 slots) | 2158 (16 slots) | **11x over** |
| tick ISR, vector to `reti` | <= 12 cycles | 58 | 59 | **5x over** |
| `DIGITAL_WRITE`, SOF to response | < 300 us | 1584 | 1596 | **5x over** |
| sustained command rate | > 400 cmd/s | 673 | 667 | **met, 1.7x margin** |

One of six is met. The five that are not are enforced at what they cost today —
see *Target vs ceiling* below.

### What dominates each miss

**CRC-16 is the root of two rows.** `src/proto/crc16.c` folds the polynomial one
bit at a time: eight iterations, each a shift, a conditional exclusive-or and a
loop test. avr-libc's `_crc_xmodem_update` computes the same polynomial with the
same output in **fourteen single-cycle instructions, branch-free**, folding a
nibble at a time with `swap`, `andi` and `eor`. The plan's premise — "~16 cycles,
ours should add nothing" — is correct; ours costs seven times that.

**Frame decode is the CRC plus eighteen cycles.** Every byte the framer stores it
also folds into the running CRC, so the frame-decode figure *contains* the CRC
figure. The state machine's own share is 18 cycles a byte, comfortably inside the
plan's 40. Fixing the CRC brings this row under budget without anyone touching
`frame.c`.

**The tick ISR is a 32-bit volatile increment.** `ticks` is an `ag_ms_t`, four
bytes, volatile: four `lds`, an `adiw` and two `adc`, four `sts` — twenty cycles
— and to do that the handler must save r24 through r27 alongside r1 and SREG,
nineteen more in push and pop. The `ticks++` in the source is one line; the
compiled ISR is twenty-seven instructions. Twelve cycles is roughly what a
one-byte counter would cost; nothing in this family increments a 32-bit volatile
in twelve.

**The scheduler sweep is over budget in every state.** The plan's 200 cycles is
"at 16 slots", which is the ATmega2560's `AG_SCHED_SLOTS`. On that part an
*empty* sweep is already 242 cycles — `ag_hal_millis` costs an atomic section and
a four-byte load, then sixteen slots each cost a load and a bit test. Sixteen
armed and not due is 546. Sixteen coming due at once is 2158, because each one
then resolves its pin through the board descriptor and writes a port register.
The gate takes the worst of the three: a host that arms sixteen timers together
is a host that will have them expire together.

**300 us is below the link's own floor.** A `DIGITAL_WRITE` exchange is eight
request bytes and seven response bytes. At 115200 8N1 a byte occupies ten bit
times, 87 us, so fifteen bytes is 1302 us of wire before any firmware runs —
1392 us as simavr clocks it, which charges eleven bit times a byte. The device
adds 186 us on top, and 186 us is the only part this repository can change. No
device can meet 300 us for this exchange at this baud rate.

What the plan's *reasoning* asks for — "dominated by the UART, not by logic" — is
checkable and is checked: the gate asserts the firmware's share stays below the
wire's. It is 186 us against 1392.

## Target vs ceiling

`test/bench/targets.h` has two columns. `target` is what the plan asked for;
`ceiling` is what the gate enforces.

They are the same wherever the firmware meets its target. Where it does not, the
plan's target stays written down exactly as stated, the ceiling is pinned at what
the code costs today, and the row prints `MISS` on every run with the size of the
gap.

That is deliberate and it is not a relaxation. Lowering the target would erase
the fact that the firmware is over budget. Enforcing a target the code has never
met would leave the gate red from the day it landed, and a gate that is red for
reasons nobody is acting on gets switched off within a week. Pinning the
measurement makes today's cost the new worst case — the code cannot get slower
without failing the build — while the `MISS` line keeps saying, every run, that
there is a debt and how big it is.

**A ceiling is only ever moved down, by making the code faster.**

A cycle count is pinned at the measured value rounded up to the next whole cycle;
it is exactly reproducible, so anything costing a cycle more fails. A wall-clock
span is pinned at the measured worst plus two percent, rounded up to 25 us,
because a round trip moves by more than that when anything in the superloop
changes size.

## How it is measured

### Two images

`build/<board>/soft/arduino-generic.elf` — the firmware that ships. The tick ISR,
the command latency and the sustained rate are measured on it, in situ, because
all three are properties of the whole running system: an ISR is entered by
hardware, and a round trip goes through the USART, the ring, the framer, the
dispatcher and back out.

`build/bench/<board>/soft/agbench.elf` — the same objects with `src/main.c`
exchanged for `test/bench/bench_main.c`, same `AVR_CFLAGS`, same `-Os -flto`,
same board profile. The per-byte and per-sweep costs are measured on it because
`-flto` inlines `ag_framer_push` and `ag_sched_poll` bodily into `main()` in the
shipped image — `avr-nm` finds no symbol for either, so there is no address to
call and no boundary to bracket. Only `ag_crc16_update` survives as a real
function.

In the benchmark image each subject is reached through a `volatile` function
pointer, which forces the compiler to emit it out of line and stops
`--gc-sections` dropping it. An out-of-line body is the compiler's general
compilation: no constant propagated in from a caller. The shipped image's inlined
copy can be faster, never slower, so **these numbers are upper bounds** on what
the firmware pays.

The claim is checked rather than asserted. `ag_crc16_update` exists in both
images; the harness calls it in each and requires the same cycle count. If the
benchmark image ever stops generating the same code as the firmware, that check
fails.

### Per-byte costs are slopes

Each per-byte figure is the difference between a run over 8 bytes and a run over
64, divided by 56. Dividing one total instead would report the fixed setup
smeared over whichever byte count the author picked, and the published number
would then move when someone changed that count for unrelated reasons. The
difference cancels every term that does not scale: the emulated call, the closing
`ret`, `ag_framer_init`, the prologue.

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
only once the previous response has cleared the wire. 673 cmd/s on the 328P
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

**This affects `test/integration/simavr/harness.c` too.** Its `send()` comment
says "a real UART delivers one byte per ~87 us at 115200"; in that simulation,
uncalibrated, it is ~187 us. Nothing that harness asserts depends on the rate, so
nothing it reports is wrong — but any timing claim taken from it would be.

A second, smaller discrepancy is left alone and reported: simavr charges eleven
bit times per byte where 8N1 occupies ten, so everything timed over the wire is
about 10% pessimistic. That is the direction a budget tolerates.

## Boards

Enforced on the ATmega328P and the ATmega2560. The 328P is the reference part and
the one the wire protocol was sized for; the 2560 is there because the plan states
the scheduler budget "at 16 slots" and sixteen is the 2560's `AG_SCHED_SLOTS` —
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
