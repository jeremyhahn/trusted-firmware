# ATtiny85

> **Status: INFORMATIVE** · Scope: `boards/attiny85/`, `src/hal/avr/hal_uart_soft.c`,
> `src/crypto/none/` · Last verified: 2026-09-23, avr-gcc 14.2.0, `-Os -flto`
>
> Every number here was measured, and each is reproducible by the command beside
> it. The normative contract is [`adding-a-board.md`](adding-a-board.md); this is
> the record of what adding one to a part four times smaller than the next
> smallest actually cost.

**It fits, with room.** 5 338 bytes of flash against a 6 144 budget and 170 bytes
of statics against 256 — but only as a build that carries no crypto provider at
all, and only after four capabilities were turned off because the silicon cannot
perform them. The interesting part of this page is not the total; it is which of
those four are facts about the part and which are facts about this tree.

```sh
make firmware BOARD=attiny85     # 5338 / 6144 flash, 170 / 256 ram
make board-sweep                 # the descriptor, against this board's own profile
```

## The measurement

| | flash | of 6 144 | of 8 192 | statics | of 256 | of 512 |
|---|---:|---:|---:|---:|---:|---:|
| **as shipped** | **5 338** | 87% | 65% | **170** | 66% | 33% |

Headroom against the budget: **806 bytes of flash, 86 bytes of statics.** The
remaining 2 854 bytes of flash and 342 bytes of SRAM are the half of the part
the budget deliberately does not spend — flash for a future feature, SRAM for
the stack, which nothing measures and which `avr-size` does not count.

## What each gate saved

Measured by turning the gate back on and rebuilding. Where a gate cannot be
turned on here — because the code does not compile for this part at all — the
figure is from the ATmega328P and is labelled as such. Cross-part figures are
indicative only: `avr25` and `avr5` are different instruction sets, and the
tiny's code is typically a few percent larger for the same source.

| gate | saving | how it was measured |
|---|---:|---|
| **crypto provider** (`CRYPTO=none`) | **3 084** | `CRYPTO=soft` links `src/crypto/sha256.c`, and the image becomes 8 422 — 230 bytes past the end of the part |
| **`TFW_FEATURE_ATTEST 0`** | **1 058** flash, **198** RAM | on top of the above: attest + the soft tier is 9 480 flash, 368 RAM |
| I2C + SPI + 1-Wire | ~1 712 *(328P)* | 11 850 → 10 138 with all three off |
| — of which 1-Wire alone | ~942 *(328P)* | 10 138 → 11 080 with 1-Wire back on and the other two off |
| — of which I2C + SPI | ~770 *(328P)* | the remainder |
| **`TFW_FEATURE_PWM 0`** | **298** | with PWM on, the image is 5 636 |
| **`TFW_FEATURE_TONE 0`** | ~334 *(328P)* | 11 850 → 11 516 |
| **sizing** (payload 32, 4 slots, 16-byte rings) | **124** flash, **184** RAM | the tree's defaults give 5 462 flash and **354 RAM — 98 over the budget** |

The sizing row is the one that decides whether the part works at all. Flash was
never the binding constraint: **SRAM was.** At the tree's default sizing the
statics come to 354 bytes of a 512-byte part, and the stack that is left is not
enough for a dispatch into a capability handler.

## What survived, and what a host is told

`CAPS` reports the compiled feature bitmap, so every row below is something a
host learns before it tries.

| | | |
|---|---|---|
| GPIO | **yes** | six pins, one port |
| ADC | **yes** | three usable channels — ADC1, ADC2, ADC3 |
| scheduler | **yes** | 4 slots |
| pulse in / capture | **yes** | the DHT22 and HC-SR04 primitive, unchanged |
| EEPROM | **yes** | 512 bytes |
| PWM | no | every compare unit is spent; see below |
| tone | no | the part has no Timer2 |
| I2C, SPI | no | the part has a USI, which is neither |
| 1-Wire | no | **and this one is our fault, not the part's** |
| attestation | no | there is nowhere on this part to keep a key |
| crypto tier | `TFW_CRYPTO_NONE` | tier 0, empty algorithm bitmap |

Payload is **32 bytes**, not 64. `TFW_MAX_PAYLOAD` is reported in `CAPS`, so a
host sizes its `EEPROM_WRITE` and `PING` from the device's answer rather than
from a constant — but a host that hard-coded 64 will get `TFW_ERR_BAD_ARGC` here,
and that is the first thing that will surprise anyone porting a script.

## Where the part broke an assumption the other three boards share

Five, and the first two are the reason this took real work rather than a fourth
copy of a board directory.

### 1. There is no USART

Not a second one, not one that is spelled differently — none. Every other board
pastes its register names from `TFW_UART_N` in `src/hal/avr/avr_usart_regs.h`.

`src/hal/avr/hal_uart_soft.c` implements the same four HAL functions with a
timer and two interrupts. It is selected by file selection off `TFW_UART_SOFT 1`
in the profile, so one implementation or the other is linked and never both.

**One timer serves both directions**, which is what makes it fit: Timer1
free-runs in CTC with `OCR1C` as TOP and a period of exactly one bit. `OCR1A` is
the transmitter's clock, whose phase does not matter. `OCR1B` is the receiver's,
whose phase does — so the pin-change ISR that sees the start edge sets
`OCR1B = TCNT1 + half a bit`, and every compare after that is one bit later by
construction. A design that restarted the timer on each start bit would have had
to stop the transmitter to do it.

### 2. 115200 is not reachable, and 19200 is the honest answer

At 8 MHz a bit at 115200 is **69 CPU cycles**. An ISR entry, a sample and a
return do not fit in 69 cycles with the millisecond tick also running, so 115200
here is not a tight link — it is a link that does not work.

**The chosen baud is 19200 and the measured bit-time error is −0.160%.** Timer1
at CK/2 runs at 4 MHz; a bit wants 208.333 ticks and gets 208, so the bit time is
52.000 µs against 52.0833 µs nominal. The figure is recomputed in
`test/unit/test_board_attiny85.c` from the same three numbers the firmware
derives its timer from, and `hal_uart_soft.c` asserts the bound at compile time,
so a board that changes `F_CPU` finds out at build time rather than on a scope.

The full error budget, against a ±50% sampling window, at the middle of the stop
bit (9.5 bit times from the start edge):

| | worst case | note |
|---|---:|---|
| divisor | 1.5% of a bit | −0.160% × 9.5 |
| internal RC oscillator | **19% of a bit** | ±2% over voltage and temperature after factory calibration |
| ISR jitter | ~10% of a bit | the tick ISR or the TX ISR delaying a sample, ~40 cycles |
| | **~30%** | |

**The oscillator dominates, and it is baud-independent.** That is why picking a
baud with a smaller divisor error would buy nothing, and why the baud is chosen
against the *jitter* term, which does scale: at 38400 the same sum is about 40%,
which works on a bench and not over temperature. A board with a crystal has no
oscillator term and can raise this; a board on the internal RC should not.

### 3. Timer1 is not a mega timer, and it is spent anyway

`tfw_hal_pwm_write` sets `WGM00` and a COM bit in `TCCRnA`. This part's Timer1
control register is `TCCR1`, whose bit 0 is `CS10` and which needs `PWM1A`
rather than a waveform-mode bit — so a PWM descriptor pointing at it would
compile, link, start the timer at clk/1 and connect nothing.

It is moot in this build: Timer0 is the millisecond tick and Timer1 is the UART's
bit clock, so **every output-compare unit on the part is already doing something
the firmware cannot do without.** The four pins that have one are marked
`TFW_PWM_RSVD` rather than `TFW_PWM_NONE`, so a host is told the fix is a
different build rather than a different pin — and on this part there is no other
pin, which is exactly the case that distinction exists for.

### 4. The ADC reference encoding is inverted

On every mega part `REFS1:REFS0 = 01` selects AVcc and `00` selects the external
AREF pin. On the ATtiny25/45/85 the field is three bits and the assignments are
not an extension of the mega's — they are different:

```
000  Vcc          001  external AREF
010  internal 1.1V     110/111  internal 2.56V
```

So **the mega's "AVcc" pattern is the tiny's "external AREF".** A board that
pasted the mega encoding would report that it is measuring against Vcc while
measuring against whatever is on PB0 — a reading that is stable, plausible and
wrong, and one that no host test could ever see. `src/hal/avr/hal_adc.c` now
selects on `REFS2`, beside the `MUX5` conditional, which is the same class of
fact about the same peripheral. The board profile asserts `REFS2 == 4`, so a
part that took that arm without having the encoding fails to build.

This is a **bug that was latent in the tree** and became reachable the moment a
part in this family was added. It is the fourth entry for the table in
`adding-a-board.md` headed "where data only stopped being true".

### 5. There is no boot section, so there is no key store

The ATtiny85 has no boot-loader section, no `BOOTRST` and no boot lock bits.
Three consequences, and none of them is about size:

- **tfw-boot cannot exist here.** `boards/attiny85/profile.mk` states no
  `BOOT_SECTION`, and `BOOT_SUPPORTED` in `mk/boot.mk` does not name the board.
- **The software crypto tier would be a lie.** That tier's definition is "the
  identity key lives in flash behind lock bits", and there is nowhere here to put
  one. An image carrying it would report `TFW_CRYPTO_SOFTWARE` to a host deciding
  what the device's evidence is worth.
- **The element tier is impossible too** — an ATECC608A is an I2C device on a
  part with no I2C.

`TFW_CRYPTO_NONE` has been in `tfw_crypto_tier_t` since the protocol was written,
described there as "no provider: unattested". `src/crypto/none/provider.c` is
the implementation of it. `CAPS` reports tier 0 and an empty algorithm bitmap,
`ATTEST` is not in the command table at all, and there is no path by which a
device on this tier produces something that could be mistaken for evidence.

That it also saves 3 084 bytes is a consequence, not the reason.

## What this cost the other three boards

**Six to eight bytes of flash each, and nothing else.**

| | before | after |
|---|---:|---:|
| atmega328p, soft | 11 844 | 11 850 |
| atmega32u4, soft | 11 588 | 11 596 |

`tfw_hal_tick_init` used to *assign* `_BV(OCIE0A)` to the timer interrupt mask.
That is safe on a part with one mask register per timer, and this part has one
mask register for both — `TIMSK` also carries `OCIE1A` and `OCIE1B`, which here
are the software UART's bit clock. The assignment became an OR, which is a
read-modify-write and costs an instruction on parts that did not need it.

It is paid because the alternative is a firmware whose UART works only while
`main()` happens to call `tfw_hal_tick_init` before `tfw_hal_uart_init`, and fails
silently and completely if anyone ever swaps two lines. The register reads zero
at reset, so the OR is identical in effect everywhere.

## The one capability lost for a reason that is ours

**1-Wire.** It is bit-banged on an ordinary GPIO and would work on this part
unchanged. It is off because it shares a translation unit — `src/hal/avr/hal_bus.c`
— with the TWI and SPI drivers, and the gate is whole-file selection. The file
does not compile here, so all three go.

Splitting `hal_bus.c` into its three drivers is the change that would give it
back. **Before anyone does that work, note the second number:** 1-Wire costs
about 942 bytes on the 328P, and there are 806 bytes of headroom here — and the
tiny's `avr25` codegen is the larger of the two. It very likely would not fit
even after the split. That is worth knowing first.

Until then `CAPS` reports 1-Wire absent, which is the truth about this build even
though it is not the truth about the part.

## What this board is for

A peripheral behind something that can attest, not a device that attests. It
answers `CAPS`, `PING`, `SYS_INFO`, the GPIO commands, `ANALOG_READ`, the three
timer commands, `PULSE_IN`/`PULSE_CAPTURE` and the two EEPROM commands, over a
software serial link at 19200 baud, in 5 338 bytes. For 6 pins and 8 KB that is
the whole useful surface of the part.

## Programming it

There is no serial bootloader and there cannot be one. `make flash
BOARD=attiny85` drives `avrdude` with `-c usbasp`; any ISP works.

Two fuse facts, and the first is the opposite of what one expects:

- **`CKDIV8` does not matter.** A part leaves the factory with it programmed and
  boots at 1 MHz — but `tfw_hal_init` writes `CLKPR = 0` as its first act, and
  that undoes the fuse in software before `tfw_hal_uart_init` ever programs the
  bit clock. The existing HAL already did the right thing here; it simply had
  never met a part where the fuse was set by default.
- **`CKSEL` does.** The profile declares `F_CPU = 8000000`, which is the factory
  default internal RC. A part fused to the 16 MHz PLL instead runs every bit
  twice as fast as the profile believes, and the link says nothing intelligible
  — with no build-time check able to see it, because `F_CPU` is what the build
  was *told*.
- **`RSTDISBL` should stay unprogrammed.** Pin 5 is `/RESET`; programming
  `RSTDISBL` makes it an ordinary pin and ends ISP programming. The descriptor
  marks it `TFW_PIN_RESERVED` and does not advertise it as digital.

## What is not verified

**The bit timing has never been run.** `make ci`'s cross-implementation suite
talks to `tfwsim`, the firmware built for the host, which uses stdio rather than
either UART implementation; the simavr suite covers the three mega boards. So
the software UART's timing is checked by a compile-time assertion on the divisor
and by the descriptor test's recomputation of it, and by nothing that executes
instructions. A simavr target for this board is the gap, and it is the one thing
that would move this board from "measured" to "demonstrated".
