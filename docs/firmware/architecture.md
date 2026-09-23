# Firmware architecture

> **Status: NORMATIVE** · Scope: `src/`, `include/ag/` · Last verified: 2026-09-22
>
> The layering below is enforced by what each file may include, and by a CI
> grep. A violation is a defect, not a style preference.

## Layers

```
L5  TRANSPORT      src/proto/transport.c    HAL uart + frame, nothing else
L4  FRAMING        src/proto/frame.c        a pure function of bytes; no I/O
L3  DISPATCH       src/core/dispatch.c      reader/writer + capability entry points
L2  CAPABILITIES   src/caps/*.c             board resolve + HAL + crypto provider
L1b BOARD          src/core/board_lookup.c         lookups; boards/*/board.c is pure data
L1a HAL / CRYPTO   src/hal/avr/*.c, src/crypto/*/
L0  REGISTERS      <avr/io.h>
```

**L3 never calls the HAL.** Dispatch reads arguments, calls a capability, writes
a reply; it does not know a pin exists. That is what lets the whole protocol
layer be tested on x86 with no mock of the dispatcher itself.

**L1b sits above the HAL, not inside it.** The board descriptor contains no
register access — only addresses held as numbers — so it compiles and runs on
the host, and the *real* pin table of a real part is unit-tested on x86. A wrong
port bit for pin 13 is otherwise a class of bug findable only by wiring up a
board and watching the wrong LED.

## Substitution is at link time

Both the HAL and the crypto provider are plain `extern` functions with one
implementation linked per image.

A vtable would cost RAM — 68 bytes on an ATtiny85, which is a fifth of its SRAM
— and every slot would be a live reference, so `--gc-sections` could no longer
drop an unused peripheral. With link-time substitution, `-flto` inlines
`ag_hal_gpio_write` to a two-instruction `sbi` at its call site, and the contract
is enforced by the linker: add a function to `include/ag/hal.h`, forget the
mock, and `make test` fails to link.

It also means an image built for the software crypto tier **cannot be talked
into** using an element it does not have. There is no runtime selection to
subvert.

## Features are gated by file selection

A feature is included or excluded as a whole translation unit. It is never
`#if`-ed out inside a file.

The cost of `#if` scattered through logic is that no single build is ever the
one that was tested: every combination is a different program and the test suite
exercised one of them. So `AG_FEATURE_TONE 0` on the ATmega32U4 keeps
`src/caps/tone.c` and `src/hal/avr/hal_tone.c` out of the object list entirely,
and `CAPS` reports tone absent.

Preprocessor conditionals are permitted in exactly four places —
`include/ag/config.h`, a board profile, `src/core/commands.def`, and
`src/hal/avr/` — plus the toolchain shim in `include/ag/progmem.h`. `make lint`
greps `src/core`, `src/proto` and `src/caps` and fails on anything else.

Defaults live in `config.h`, exactly once. A default defined next to its use is
a default a board profile **cannot** override, because `config.h` has already
been included by the time the `.c` file reaches it — which is not a theoretical
concern; two of them were found that way.

## The main loop

```c
for (;;) { ag_transport_poll(); ag_sched_poll(); ag_pulse_poll(); ag_sys_poll(); }
```

**There is no `delay()` and there must never be one.** `ag_hal_delay_us` exists
for bus bit-banging, where the delay *is* the protocol.

`RESET` does not reset. It latches the request and returns normally, so the
response is framed and queued like any other; `ag_sys_poll()` then waits for the
UART to go idle and only then triggers the watchdog. The firmware this replaced
called `delay(100)` and hoped — a guess that is too long on every healthy link
and too short on a busy one.

## The tick and the scheduler

A **true 1 ms** CTC tick, `OCR0A = 249` at 16 MHz with clk/64 — not Arduino's
1.024 ms. A valve told to run 600 s should run 600 s, not 614.

The ISR does the tick and nothing else. The sweep runs in `main()`, so the slot
array has exactly one writer and needs no locking. The only shared state is the
tick counter, read through `ag_hal_millis()`, which is the sole `ATOMIC_BLOCK`
in the tree and is inside the HAL — so `sched.c` compiles on x86 with no
`volatile` anywhere.

Rollover uses a signed difference, `(int32_t)(now - deadline) >= 0`, and the
interval is bounded at arm time so the failure is a clean `AG_ERR_RANGE` rather
than a valve that stays open for seven weeks.

## Lock-free by construction

The UART rings are single-producer/single-consumer with power-of-two sizes and
`uint8_t` indices. Each index is written by one side and read by the other, and
a byte-wide load or store on AVR is a single instruction.

That last clause is load-bearing, and there is a `_Static_assert` on it: at 512
the indices become two bytes, the single-instruction property evaporates, and a
reader can observe a half-updated index — a corruption that appears under load,
looks like line noise, and is very hard to attribute.

## AVR traps handled explicitly

- **`MCUSR` is cleared and the watchdog disarmed in the first instructions**,
  from a naked `.init3` function before the C runtime. Otherwise a watchdog
  reset loops forever.
- **Reading `ADCL` before `ADCH`**, because reading `ADCL` latches `ADCH` and the
  other order returns halves of two different conversions.
- **`MUX5` for ADC channels above 7.** Channel 8 is bank 1 channel 0, not
  `MUX3`, which selects a differential pair. Getting it wrong does not fail: it
  silently converts a different pin.
- **The element's bus is 100 kHz**, because the ATECC608A wake pulse is the low
  period of a byte written to address zero and at 400 kHz that is 22 µs against
  the 60 µs the part requires.

## What this deliberately does not do

- **No dynamic allocation.** No `malloc`, no `new`, nothing with a heap. Every
  buffer is static and sized by `config.h`, so the RAM figure at the end of a
  build is the RAM figure, not a lower bound.
- **No C++.** Not a style preference: the Arduino core plus `String` and the
  usual libraries consume around 900 bytes of a Nano's 2 KB before Ethernet is
  counted, and that is the entire reason this project exists.
- **No floating point.** The soft-float library is large and nothing on the
  device needs it. VPD and heat-index arithmetic belong on the host.
- **No sensor drivers.** The device exposes primitives — `PULSE_CAPTURE` returns
  intervals, and a host turns 26 µs and 70 µs into DHT22 bits. A device that
  decoded sensors would need a new build for every sensor.
