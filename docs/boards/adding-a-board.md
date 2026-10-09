# Adding a board

> **Status: NORMATIVE** · Scope: `boards/`, `include/tfw/board.h`,
> `src/core/board_lookup.c` · Last verified: 2026-09-23
>
> The claim this document makes is falsifiable: adding a board touches nothing
> under `src/`. Where that stopped being true, the fix belonged in `src/`, and
> this document records what those fixes were.

A board is **four files and no code** — as long as the part has the peripherals
every other part in the tree has. Where it does not, the missing peripheral costs
a file under `src/hal/avr` and nothing else; see
[a part that is missing a peripheral](#a-part-that-is-missing-a-peripheral).

The four files:

```
boards/<board>/board_profile.h   constants: what the part is
boards/<board>/board.c           three tables: where its pins are
boards/<board>/profile.mk        the toolchain's view: MCU, clock, budgets
test/unit/test_board_<short>.c   what you assert is true about the above
```

The lookups that read those tables live once, in `src/core/board_lookup.c`. They are
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
_Static_assert(TFW_ADDR_PINB   == _SFR_ADDR(PINB),   "PINB address");
_Static_assert(TFW_ADDR_TCCR1A == _SFR_ADDR(TCCR1A), "TCCR1A address");
_Static_assert(COM1A1_BIT     == COM1A1,            "COM1A1 bit position");
#endif
```

A transposed digit is a compile error on the firmware build, naming the
register. Assert every address and every bit position you introduce; the cost is
one line each and the alternative is a wrong pin on a bench.

Also state, with the reasoning in a comment: `TFW_BOARD_NAME`, a unique
`TFW_BOARD_ID`, `TFW_PIN_COUNT`, `TFW_PORT_COUNT`, `TFW_PWM_COUNT`, `TFW_ADC_COUNT`,
`TFW_EEPROM_SIZE`, `TFW_TICK_TIMER`, `TFW_SCHED_SLOTS`, the flash and RAM budgets,
the flash geometry (`TFW_FLASH_TOTAL`, `TFW_APP_REGION_BASE`/`_SIZE`,
`TFW_BOOT_REGION_BASE`/`_SIZE` and `TFW_OTA_PAGE_MAX`, which
`include/tfw/boot.h` checks for overlap and fit), and — for each peripheral the part has — `TFW_UART_N` with its two vector names,
`TFW_TONE_PIN`, and the three `TFW_SPI_BIT_*` positions. A board that states none
of the last three is saying it has none of those peripherals, and must gate the
matching feature off. Everything else takes its default from
`include/tfw/config.h`, which is where defaults live exactly once.

`TFW_ADC_COUNT` is the HAL's range-check bound, so it is **one past the highest
real channel**, not the count of channels that exist. A part whose channel space
has holes (the ATmega32U4 has no ADC2 or ADC3) needs no expression of them: no
pin claims those numbers, so no request can reach them.

## `board.c`

Three tables — `tfw_board_ports`, `tfw_board_pins`, `tfw_board_pwms` — and three
`_Static_assert`s pinning each against its declared count. Nothing else. No
functions.

The asserts are not decoration. A table one row short of its `TFW_PIN_COUNT`
zero-fills in silence, and the missing pin then resolves to port 0 bit 0 — a
real pin, on a real port, that the host was never told about.

Two flags deserve care because the difference is reported to hosts:

- **`TFW_PWM_RSVD` is not `TFW_PWM_NONE`.** The first says the pin has an
  output-compare unit that this build spent on the millisecond tick; the second
  says it has none. A host that cannot tell them apart will move a load to a
  different pin when the right fix is a different build.
- **`TFW_PORT_NONE`** is for a pin with no digital port at all — an ADC-only
  input, which can be read and never written. The 328P's A6 and A7 are the only
  instance in the tree.

**`TFW_PWM_COUNT` may be 0**, and then `tfw_board_pwms` is an empty array and the
third `_Static_assert` pins the count rather than the row count. Every pin that
has a compare unit is then `TFW_PWM_RSVD`: on a board with no PWM at all, telling
a host that a pin has none would send it looking for another pin, and the truth
is that the fix is a different build. The ATtiny85 is the instance — its two
timers are the millisecond tick and the software UART's bit clock.

## `profile.mk`

`ARCH`, `MCU`, `F_CPU`, the avrdude part and programmer, and four numbers:
`FLASH_BUDGET`, `RAM_BUDGET`, `FLASH_TOTAL`, `RAM_TOTAL`.

`ARCH := avr` is **required**, and it is how the build finds everything that is
not the board's: the toolchain, flags, programmer and linker placement come from
`mk/arch/$(ARCH).mk`, and the register-level HAL is `src/hal/$(ARCH)/` (with
`boot/hal/$(ARCH)/boot_hal_$(ARCH).c` for tfw-boot). A profile that omits it, or
names an architecture with no `mk/arch/<arch>.mk`, fails `make` with a message
that says which. Boards stay flat — `boards/<board>/`, never
`boards/<arch>/<board>/` — and `BOARD=` is unchanged.

A board on an architecture the tree already has adds nothing else. **A new
architecture** is `mk/arch/<arch>.mk` (the contract is listed at the top of
`mk/arch/avr.mk`), `src/hal/<arch>/`, `boot/hal/<arch>/` if it has a bootloader,
and one line in `.github/workflows/ci.yml` per board and tier —
`make ci-matrix-check` names the lines it is missing. No edit to `mk/firmware.mk`,
`mk/boot.mk` or `mk/release.mk`.

The budgets are **gates, not estimates** — `make firmware` fails when an image
exceeds one. Set them from what the part should reasonably spend, not from what
the build happens to produce, and if the build does not fit, report the number
rather than raising the budget to meet it.

Optionally `BOARD_TIERS`, the crypto tiers this part can carry. A board that says
nothing gets `soft atecc608a`, which every board had while every part had a TWI
peripheral and a boot section. State it when one of them is impossible: the
element tier needs I2C, and the software tier's whole definition is a key in
boot-section flash behind lock bits. `make firmware-matrix` reads this, and the
CI workflow's firmware matrix is a hand list that `make ci-matrix-check` compares
against it in both directions, so the matrix cannot queue a build that could only
ever fail, nor leave a board unbuilt.

A board with a boot section states `BOOT_BASE`, the address tfw-boot links at,
alongside `BOOT_SECTION`. It must equal `TFW_BOOT_REGION_BASE` in
`board_profile.h`. On AVR, `mk/arch/avr.mk` also checks that `BOOT_BASE` plus
`BOOT_SECTION` is `FLASH_TOTAL`, because BOOTRST puts the boot section at the
top of flash.

A board with no boot section also states no `BOOT_BASE`, `BOOT_SECTION`,
`BOOT_BUDGET` or `RAM_END`, and does not appear in `BOOT_SUPPORTED` in
`mk/boot.mk`.

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
- the pins this build uses for the UART are marked `TFW_PIN_IN_USE`

And spot-check pins whose mapping has **no arithmetic relation** to their
number — which on most parts is all of them. A table that ran a block upward
where the datasheet runs it downward works perfectly and drives the wrong eight
pins.

## Where "data only" stopped being true

Four times. The first three were a 328P fact written into code that claimed to
be generic; the fourth was an ATtiny85 one. They are listed because the pattern
repeats and because each one compiles, links and fails silently on hardware.

| What | The 328P fact | What it did elsewhere |
|---|---|---|
| `hal_uart.c` | `UDR0`, `USART_RX_vect` | The 32U4's TTL pins are USART1; the Mega spells the same vector `USART0_RX_vect`. A misspelled ISR is **not a link error** — it compiles to an ordinary function nothing calls, the vector keeps its default, and the device never answers. |
| `hal_pwm.c` tone | `DDRB \|= _BV(3)` | OC2A is PB4 on a Mega and PB3 is MISO. It set the direction of an SPI line while the timer drove a pin that was still an input. |
| `hal_bus.c` SPI | `DDRB \|= _BV(3)\|_BV(5)\|_BV(2)` | The 32U4's SPI is PB2/PB1/PB0. Three unrelated pins driven, the real lines left as inputs, and SPI simply does not work. |
| `hal_adc.c` reference | `REFS1:REFS0 = 01` is AVcc | The ATtiny85's reference field is three bits, and `_BV(REFS0)` alone selects the **external AREF pin**. The device reports Vcc and measures against whatever is on PB0 — stable, plausible and wrong. Now selected on `REFS2`, beside the `MUX5` conditional. |

The first three take their values from the board now. The fourth could not: it
is not a pin fact but a **register-encoding** fact, so it belongs in the one
file that is allowed to know about registers, and it is a conditional there for
the same reason `MUX5` is.

The lesson generalises past pins. **Every part-specific fact that the HAL owns
is a fact the HAL will eventually be wrong about**, and the ones that are not
pins are worse, because no board directory shows them and no host test can
execute them — the host has no ADC.

## A capability the part does not have

Turn it off, and gate it by **file selection**. `TFW_FEATURE_TONE 0` in the
32U4's profile keeps `src/caps/tone.c` and `src/hal/avr/hal_tone.c` out of the
image entirely, and `CAPS` then reports tone as absent so a host learns the
truth before it tries.

The rule — a feature is gated by including or excluding a whole file, never by
`#if` inside one — is what makes the build that ships the build that was tested.
A CI grep enforces it over `src/core`, `src/proto` and `src/caps`, and the four
places a preprocessor conditional is permitted are `include/tfw/config.h`, a
board profile, `src/core/commands.def` and `src/hal/avr`.

## A part that is missing a peripheral

A capability the part does not have is turned off. A **peripheral** the part does
not have is different: the HAL's implementation of it will not compile at all,
and there is nothing to turn off until something replaces it.

Two of these exist, and both are resolved the same way — a second file
implementing the same contract, chosen by file selection off the board profile,
so exactly one is ever linked and neither build carries a conditional.

| the part has no… | the profile says | and the Makefile links |
|---|---|---|
| USART | `TFW_UART_SOFT 1`, plus the pins and the timer prescaler | `src/hal/avr/hal_uart_soft.c` instead of `hal_uart.c` |
| key store | `BOARD_TIERS := none` in `profile.mk` | `src/crypto/none/provider.c`, and no SHA-256 at all |

**The software UART is not a lesser UART.** It implements the same four
functions with the same non-blocking contract — interrupt-driven both ways, so
the superloop never waits for a bit — and its translation unit is 700 bytes of
flash and 41 of SRAM, compiled for an ATtiny85 at `-Os`. What it
cannot do is 115200: a bit at that rate is 69 CPU cycles at 8 MHz, and an ISR
does not fit in 69 cycles. **A board with a software UART states its own
`TFW_UART_BAUD`, and states the measured bit-time error beside it**, because the
baud is then a property of the board's clock rather than of the protocol. See
[`attiny85.md`](attiny85.md) for the error budget and why the oscillator, not
the divisor, is what sets the ceiling.

**`TFW_CRYPTO_NONE` is a tier, not an absence.** The protocol has named it since
it was written: "no provider: unattested". A part with no boot section has
nowhere to keep a key, so linking the software tier there would make `CAPS`
report `TFW_CRYPTO_SOFTWARE` to a host deciding what the device's evidence is
worth — and the sentence that byte stands for would be false. The tier that says
so instead reports 0 and an empty algorithm bitmap, and `ATTEST` is not in the
command table at all.

Neither of these belongs to the board directory, and that is the point: the
board says *which*, and the implementation is shared with the next part that
needs it.

## Checklist

```sh
make firmware BOARD=<board>                  # size budget + the address asserts
make firmware BOARD=<board> CRYPTO=atecc608a # if BOARD_TIERS has it: a different image
make firmware-matrix                         # every board at every tier ITS PROFILE claims
make ci-matrix-check                         # the CI workflow's matrix lists exactly those
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
