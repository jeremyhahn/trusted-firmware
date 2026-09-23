# Adding a board

> **Status: NORMATIVE** · Scope: `boards/`, `include/ag/board.h`,
> `src/core/board.c` · Last verified: 2026-09-22
>
> The claim this document makes is falsifiable: adding a board touches nothing
> under `src/`. Where that stopped being true, the fix belonged in `src/`, and
> this document records what those fixes were.

A board is **four files and no code**:

```
boards/<board>/board_profile.h   constants: what the part is
boards/<board>/board.c           three tables: where its pins are
boards/<board>/profile.mk        the toolchain's view: MCU, clock, budgets
test/unit/test_board_<short>.c   what you assert is true about the above
```

The lookups that read those tables live once, in `src/core/board.c`. They are
identical on every classic AVR — the packing is a port index and a bit in one
byte, an ADC channel, an index into the PWM table, and flags — so a board that
needed its own copy would be a second copy of logic that can drift for no reason
anyone intended.

## `board_profile.h`

Constants only, and **numeric literals for every register address**, used by the
host build and the device build alike.

This is the part that most wants to be `#ifdef __AVR__ ... &PINB ... #else ...
0x23 ... #endif`, and that arrangement was here and was wrong: the table the
host tests verified was then not provably the table that shipped. The two arms
could disagree and nothing would say so.

Instead the number is the only definition and **avr-libc checks it**:

```c
#ifdef __AVR__
#include <avr/io.h>
_Static_assert(AG_ADDR_PINB   == _SFR_ADDR(PINB),   "PINB address");
_Static_assert(AG_ADDR_TCCR1A == _SFR_ADDR(TCCR1A), "TCCR1A address");
_Static_assert(COM1A1_BIT     == COM1A1,            "COM1A1 bit position");
#endif
```

A transposed digit is a compile error on the firmware build, naming the
register. Assert every address and every bit position you introduce; the cost is
one line each and the alternative is a wrong pin on a bench.

Also state, with the reasoning in a comment: `AG_BOARD_NAME`, a unique
`AG_BOARD_ID`, `AG_PIN_COUNT`, `AG_PORT_COUNT`, `AG_PWM_COUNT`, `AG_ADC_COUNT`,
`AG_EEPROM_SIZE`, `AG_TICK_TIMER`, `AG_SCHED_SLOTS`, the flash and RAM budgets,
`AG_UART_N` with its two vector names, `AG_TONE_PIN`, and the three
`AG_SPI_BIT_*` positions. Everything else takes its default from
`include/ag/config.h`, which is where defaults live exactly once.

`AG_ADC_COUNT` is the HAL's range-check bound, so it is **one past the highest
real channel**, not the count of channels that exist. A part whose channel space
has holes (the ATmega32U4 has no ADC2 or ADC3) needs no expression of them: no
pin claims those numbers, so no request can reach them.

## `board.c`

Three tables — `ag_board_ports`, `ag_board_pins`, `ag_board_pwms` — and three
`_Static_assert`s pinning each against its declared count. Nothing else. No
functions.

The asserts are not decoration. A table one row short of its `AG_PIN_COUNT`
zero-fills in silence, and the missing pin then resolves to port 0 bit 0 — a
real pin, on a real port, that the host was never told about.

Two flags deserve care because the difference is reported to hosts:

- **`AG_PWM_RSVD` is not `AG_PWM_NONE`.** The first says the pin has an
  output-compare unit that this build spent on the millisecond tick; the second
  says it has none. A host that cannot tell them apart will move a load to a
  different pin when the right fix is a different build.
- **`AG_PORT_NONE`** is for a pin with no digital port at all — an ADC-only
  input, which can be read and never written. The 328P's A6 and A7 are the only
  instance in the tree.

## `profile.mk`

`MCU`, `F_CPU`, the avrdude part and programmer, and four numbers:
`FLASH_BUDGET`, `RAM_BUDGET`, `FLASH_TOTAL`, `RAM_TOTAL`.

The budgets are **gates, not estimates** — `make firmware` fails when an image
exceeds one. Set them from what the part should reasonably spend, not from what
the build happens to produce, and if the build does not fit, report the number
rather than raising the budget to meet it.

## The test

`make board-sweep` compiles each board's descriptor suite **against that board's
own profile** and runs them all. The ordinary `make test` excludes them, because
the host build is configured for one board and compiling a Mega's test against
the 328P's header fails on a register the 328P does not have.

Assert the invariants, not just a few pins:

- no two pins share a port and bit
- no two share an ADC channel
- every pin is digital or ADC (a row that is neither is a row you forgot)
- a pin beyond the board is refused
- the tick timer's pins are reserved rather than absent
- the pins this build uses for the UART are marked `AG_PIN_IN_USE`

And spot-check pins whose mapping has **no arithmetic relation** to their
number — which on most parts is all of them. A table that ran a block upward
where the datasheet runs it downward works perfectly and drives the wrong eight
pins.

## Where "data only" stopped being true

Three times, and each was a 328P fact written into code that claimed to be
generic. They are listed because the pattern repeats and because each one
compiles, links and fails silently on hardware.

| What | The 328P fact | What it did elsewhere |
|---|---|---|
| `hal_uart.c` | `UDR0`, `USART_RX_vect` | The 32U4's TTL pins are USART1; the Mega spells the same vector `USART0_RX_vect`. A misspelled ISR is **not a link error** — it compiles to an ordinary function nothing calls, the vector keeps its default, and the device never answers. |
| `hal_pwm.c` tone | `DDRB \|= _BV(3)` | OC2A is PB4 on a Mega and PB3 is MISO. It set the direction of an SPI line while the timer drove a pin that was still an input. |
| `hal_bus.c` SPI | `DDRB \|= _BV(3)\|_BV(5)\|_BV(2)` | The 32U4's SPI is PB2/PB1/PB0. Three unrelated pins driven, the real lines left as inputs, and SPI simply does not work. |

All three now take their values from the board. The lesson is not that the HAL
needed fixing once: it is that **every pin fact belongs to the board**, and a
HAL that owns most of them will be wrong about the rest.

## A capability the part does not have

Turn it off, and gate it by **file selection**. `AG_FEATURE_TONE 0` in the
32U4's profile keeps `src/caps/tone.c` and `src/hal/avr/hal_tone.c` out of the
image entirely, and `CAPS` then reports tone as absent so a host learns the
truth before it tries.

The rule — a feature is gated by including or excluding a whole file, never by
`#if` inside one — is what makes the build that ships the build that was tested.
A CI grep enforces it over `src/core`, `src/proto` and `src/caps`, and the four
places a preprocessor conditional is permitted are `include/ag/config.h`, a
board profile, `src/core/commands.def` and `src/hal/avr`.

## Checklist

```sh
make firmware BOARD=<board>                  # size budget + the address asserts
make firmware BOARD=<board> CRYPTO=atecc608a # the attested image is a different image
make board-sweep                             # every board, each against its own profile
make reproducible-check BOARD=<board>        # two builds, byte-identical
```

## What this deliberately does not do

- **No runtime board detection.** The board is a compile-time fact; an image
  built for a Mega on a Nano is a flashing mistake, not a case to handle.
- **No pin-name strings on the device.** `CAPS` reports numbers and flags. The
  host owns the silkscreen.
- **No attempt to unify the Arduino core's pin maps.** Where a core numbers
  pins in a way that would put two descriptor rows on one port bit — the
  Leonardo's A6–A11, which are ADC channels on pins that already have numbers —
  the board carries the channel on the digital pin's number instead, and says so.
