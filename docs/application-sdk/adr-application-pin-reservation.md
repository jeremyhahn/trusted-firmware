# ADR: application pin reservation

> **Status: PROPOSED** · Row: `application-sdk` · Scope: trusted-firmware
> (device and `sdk/go`) · Last verified: 2026-10-23, against
> `include/tfw/board.h`, `include/tfw/status.h`, `src/core/board_lookup.c`,
> `src/caps/gpio.c`, `src/caps/bus.c`, `src/core/app_runner.c` and
> `test/app/example/example.c`
>
> Nothing in the code changes until the owner rules. Every decision below is a
> recommendation with its alternatives. The open questions are listed at the
> end.

## The question

The `application-sdk` row names a pin reservation as a proposed follow-up
([architecture.md](architecture.md#what-it-does-not-do)). Today an
application drives its pins through the HAL, and a host can drive the same
pins with `DIGITAL_WRITE`, `PIN_MODE`, `TIMER_SET` and the rest. The two
conflict, and neither knows. The first application, the secure BMC, drives
power and reset pins that a host must not toggle.

This ADR decides how an application declares the pins it owns, what a host
command on one of them returns, how a host learns which pins are taken, what
happens to the claim when the application does not run, and how a claim that
clashes with the board's own is refused.

No standard covers this. The pin model and the wire protocol are this
project's own (`docs/wire-protocol/`). The constraints come from CLAUDE.md:
status codes extend and never renumber, the hooks link at link time with no
vtable, and preprocessor conditionals stay in the four places.

## Context

- **The firmware already reserves pins, as data.** Each row of a board's pin
  table carries flags (`include/tfw/board.h:33-39`). `TFW_PIN_RESERVED` (0x20)
  marks `/RESET` or a pin a software peripheral claims, and `TFW_PIN_IN_USE`
  (0x40) a pin "claimed by this build: the UART pins, say". The claims are
  `const` rows in flash, chosen per build by expressions, not `#if`:
  `TFW_PIN_ETH` takes the Ethernet shield's pins only in an Ethernet image and
  `TFW_PIN_OTA_STORE` the store's chip select only in an image with the stager
  (`include/tfw/board.h:41-51`). The ATtiny85 marks `/RESET` reserved
  (`boards/attiny85/board.c:45`); every AVR board marks its UART pins in use
  (`boards/atmega2560/board.c:52-53`).
- **There is no runtime claim.** Nothing in `src/` takes or releases a pin
  while the firmware runs. `test/unit/test_pin_coord.c` tests a pin's
  coordinate, how a port and a bit reach the wire and the HAL, not a claim.
- **Eight checks refuse a claimed pin, all the same way.** Each reads
  `tfw_board_pin_flags()` (`src/core/board_lookup.c:123-129`) and answers
  `TFW_ERR_RESERVED_PIN` (13) for `RESERVED | IN_USE`: `PIN_MODE` and
  `DIGITAL_WRITE` (`src/caps/gpio.c:16-28`), `TIMER_SET`
  (`src/caps/sched.c:85`), `PWM_WRITE` (`src/caps/pwm.c:17`), `TONE`
  (`src/caps/tone.c:33`), `SPI_XFER`'s chip select (`src/caps/bus.c:145`) and
  the three 1-Wire commands (`src/caps/bus.c:192`, `:219`, `:244`). The status
  is documented as "pin is `/RESET`, or claimed by the soft UART"
  (`include/tfw/status.h:44`, `docs/wire-protocol/api.md:294`), and the Go SDK
  decodes it as `StatusErrReservedPin` (`sdk/go/status.go:30`).
- **Reads are not refused.** `DIGITAL_READ` (`src/caps/gpio.c:51-68`) and
  `PULSE_IN` (`src/caps/pulse.c:14-39`) resolve the pin without its flags.
  `PULSE_CAPTURE` does not check them either. On AVR both only sample the pin
  and never change its mode (`src/hal/avr/hal_pulse.c:25-45`, `:47-78`).
- **A host already sees the flags.** The CAPS pin page is six bytes, and byte
  1 is the row's flags: `DIGITAL 0x01, PWM 0x02, ADC 0x04, RESERVED 0x20,
  IN_USE 0x40, LED 0x80` (`docs/wire-protocol/api.md:84`), written at
  `src/core/board_lookup.c:114`. `sdk/go` names them `PinReserved` and
  `PinInUse` (`sdk/go/device.go:205`, `:208`), and prints a bit it does not
  know as hex rather than dropping it (`sdk/go/device.go:242`). Bits 0x08 and
  0x10 are unassigned.
- **`tfw_board_pin_flags()` links where no application can.**
  `src/core/board_lookup.c` is linked into the bootloader (`mk/boot.mk:55`),
  into `test_pin_coord` with no runner (`mk/host.mk:601`) and into the WIZnet
  probe (`mk/wiznet-probe.mk:26`). A call from it into the application runner
  would make each of those link a runner.
- **The example application already conflicts.** It drives pin 13, the LED
  (`test/app/example/example.c:27`, `:41`), which a host may also write. On the
  ATmega328P, pin 13 is the Ethernet shield's SCK in an Ethernet image
  (`boards/atmega328p/board.c:43`): there the application drives a pin the
  firmware itself has taken.
- **The application runs before any host command.** `startup()` calls
  `tfw_app_runner_start()`, which checks the descriptor and calls `init`
  (`src/core/app_runner.c:65-73`), before interrupts are enabled and before
  the superloop polls the transport ([architecture.md](architecture.md#data-flow)).
- **The configuration region is host-writable.** Any host with `EEPROM_WRITE`
  writes it, and the application validates what it reads
  ([architecture.md](architecture.md#trust-boundaries)). An `init` that
  refuses bad configuration is therefore a state a host can cause.
- **APP_INFO has one byte to spare.** Its reply is 63 bytes
  ([api.md](api.md#app_info)) against `TFW_MAX_PAYLOAD` 64
  (`include/tfw/config.h:409`).

So an application's claim can reuse the refusal and the report as they stand:
the status, the flag and the pin page all exist. It cannot reuse the claim
itself. The claims are rows of each board's table, and an application lives in
its own repository and is board-independent, so it cannot add a flag to a row.
One seam is needed: the application's list of pins, ORed into the flags the
eight checks and the pin page read.

## Decisions proposed

### 1. Declaration: a `const` pin list in flash, picked by the object list

```c
/* Defined by an application whose app.mk sets APP_PINS := yes. */
extern const tfw_pin_t tfw_app_pins[];
extern const uint8_t   tfw_app_pin_count;
```

Both are declared in `include/tfw/app.h` and defined `TFW_PROGMEM` by the
application, as the descriptor is. They are data, not a hook: nothing calls
the application to learn its pins, so the claim holds whatever state the
application is in (decision 4), and it is fixed when the image is linked,
before any host can send a command.

An application that drives no pin sets nothing. `mk/app.mk` then links
`src/core/app_pins_none.c`, which defines an empty list. An application that
drives pins sets `APP_PINS := yes` in its `app.mk` and defines both symbols.
Defining them without the knob is a duplicate symbol, and setting the knob
without them is a missing one. Both fail at link time, as decision 2 of
[adr-application-wire-commands.md](adr-application-wire-commands.md) does for
`APP_CMD`. The descriptor does not change and `TFW_APP_ABI` stays 1: an
existing application links as it does today and claims nothing.

The firmware reads the list through one function in the runner pair:

```c
uint8_t tfw_app_pin_flags(tfw_pin_t pin);   /* src/core/app_runner.h */
```

`src/core/app_runner.c` answers `TFW_PIN_IN_USE` for a listed pin and 0 for
any other. `src/core/app_runner_none.c` answers 0, and under `-flto` the call
inlines away, so an image with no application stays byte for byte the size it
is. The eight checks and the CAPS pin page OR it into the board's flags.
`tfw_board_pin_flags()` itself does not change: it stays the board's own
flags, which the bootloader and the clash check (decision 5) need without the
application's.

| Option | Where the claim lives | Cost |
|---|---|---|
| **A. A `const` list, a knob in `app.mk` (recommended)** | flash, fixed at link | one byte per pin, a loop over the list per checked command |
| B. A field in the descriptor | flash | the descriptor's layout and `TFW_APP_ABI` change; APP_INFO reports the descriptor as written, and has one byte to spare |
| C. A call from `init`, `tfw_app_claim_pin()`, into a RAM bitmap | RAM, `TFW_PIN_COUNT` bits (9 bytes on the ATmega2560's 70 pins) | the claim exists only once `init` has run it; an `init` that fails early leaves the pins free (decision 4) |
| D. An `app.mk` list turned into a `-D` mask ORed into every board row | flash, in the board tables | every row of every board gains a term, and a 70-pin mask is wider than any integer constant |

A is the descriptor's own pattern: data in flash, checked by the runner,
measured in `FWHash`. B breaks the descriptor and APP_INFO. C spends RAM and
makes the claim depend on code paths. D edits the board tables an
application is meant never to touch.

The flag is `TFW_PIN_IN_USE`, "claimed by this build", because the
application is part of the build. `TFW_PIN_RESERVED` describes `/RESET` and
software peripherals, and the eight checks refuse both alike, so the choice
changes only what a host is told (open question 2).

### 2. Refusal: `TFW_ERR_RESERVED_PIN` (13), unchanged

A host command that would drive or reconfigure an application's pin answers
`TFW_ERR_RESERVED_PIN`, from the check that already answers it for the UART
pins. No status code is added and none is renumbered. The status's comment
in `include/tfw/status.h:44` and its row in `docs/wire-protocol/api.md:294`
widen from "claimed by the soft UART" to "claimed by this build or its
application". Its number and its existing meaning stay.

A new status, "claimed by the application", was considered and is not
proposed: a host's response to either refusal is the same, to leave the pin
alone, and the pin page already says why (decision 3).

Reads stay unrefused, as they are for every claimed pin today. A host may
read a BMC's power-good line with `DIGITAL_READ` while the application owns
it. `PULSE_IN` and `PULSE_CAPTURE` sample without driving, but they hold the
superloop for up to their timeout, which delays the application's `run`
(open question 6).

### 3. Discovery: the CAPS pin page, with no new byte

A host learns a pin is taken from the pin page it already reads: byte 1 of an
application's pin has `IN_USE` (0x40) set. `tfwctl` and `PinInfo` already
print it as `in-use`. A host learns that an application exists from APP_INFO
answering.

| Option | What a host learns | Wire |
|---|---|---|
| **a. `IN_USE` in the pin page (recommended)** | the pin is taken; not by whom | no change |
| b. `IN_USE` and a new flag bit, "the application's" | the pin is taken, and by the application | a flag value assigned, with a vector; an older host still reads `IN_USE` and prints the new bit as hex |
| c. The list in APP_INFO | the application's pins | the reply has one byte to spare; a list does not fit |

a costs nothing on the wire. b answers "by whom" and is additive: it can
follow a without undoing it. Which bit, 0x08 or 0x10, is the owner's to
assign (open question 3).

With a, a host cannot tell an application's pin from a pin the build took for
itself. That matters for one statement in [api.md](api.md#app_info): "an
`unsupported` reply whose fields all pass means the magic was wrong". Decision
5 adds a second reason a runner refuses an application, and that sentence
widens with it.

### 4. The claim holds in every runner state, and in no image without the application

| Image | Runner state | The application's pins |
|---|---|---|
| no `APP_DIR` | none | not claimed; every pin page is what it is today |
| an application, `APP_PINS` not set | any | not claimed |
| an application with `APP_PINS := yes` | `IDLE`, `RUNNING`, `FAILED`, `UNSUPPORTED`, `STOPPED` | claimed |

The claim is in flash, so it does not follow the runner's state. That is the
recommendation, for two reasons. A host can make `init` fail by writing bad
configuration; if `FAILED` released the pins, any host with `EEPROM_WRITE`
could take a BMC's reset line by first breaking its application. And what the
device refuses is then exactly what `FWHash` measured, with no RAM state
between them.

The cost is that a failed application's pins are nobody's: the firmware does
not drive them and refuses the host. They stay as reset left them, inputs, or
as a failing `init` left them. The recovery is the one the architecture
already names for a broken application, an update over the air.

The alternative is to release the claim outside `RUNNING`, a test of the
runner's state in `tfw_app_pin_flags()`. It lets a host take over power
control from a broken BMC, at the price above (open question 4).

The tiers the row covers do not change this:

- **Crypto tier.** Soft and ATECC608A images take the same claim. No pin
  command is behind `tfw_auth_gate()` on any tier, so the element tier
  refuses a claimed pin to every host, authenticated or not.
- **Link.** The checks sit in the command handlers, after the link, so a
  claimed pin is refused alike over serial and Ethernet. Ethernet's
  allow-list (`src/proto/link_eth.c:148-150`) is unchanged.
- **Board.** Every board's pin table sits behind `src/core/board_lookup.c`,
  including the ESP32-P4, i.MX 95 and Arm boards, so the claim is
  architecture-neutral. The ATmega328P and ATmega32U4 get it as every board
  does; an application that does not fit them is expected, as before.
- **tfwsim.** `tfwsim-app-<name>` links the application's list, so an
  application's own tests on the host see its pins refused.

### 5. A clash is refused at boot, by the runner

A claim is refused when a listed pin is at or past `TFW_PIN_COUNT`, or has
`RESERVED` or `IN_USE` among the board's own flags (`tfw_board_pin_flags()`,
without the application's). `descriptor_ok()` (`src/core/app_runner.c:31-63`)
gains the loop. A clash sets `UNSUPPORTED`, and `init` is never called, as
for any other incompatibility. The claim itself still holds (decision 4):
the application's other pins stay refused to the host.

Build time cannot see the clash. The board's flags are rows of a `const`
table defined in the board's `board.c` (`include/tfw/board.h:82-84`), not
constant expressions, so a `_Static_assert` in the application cannot read
them, and which rows carry `IN_USE` depends on the image's features
(`TFW_PIN_ETH`, `TFW_PIN_OTA_STORE`). The bound alone is a constant: an
application may check `pin < TFW_PIN_COUNT` at compile time as
`TFW_APP_STATIC_CHECK` checks its capabilities (`include/tfw/app.h:127-135`),
and the runner checks it again at boot for one that does not.

Refusing the application, not the one pin, keeps the runner's rule that an
application runs only in an image that can run it. Running it without one of
its pins would leave it driving a pin the firmware owns, which is the
conflict this ADR exists to end.

The example application shows the case. It drives pin 13. If it claims pin 13,
an ATmega328P Ethernet image refuses it, because pin 13 is the shield's SCK
there. A code lane either leaves the example claiming nothing, or has it claim
pin 13 and checks that no image `make app-firmware-check` builds takes it.

The pins the hardware buses use without a pin argument are not flagged: the
ATmega2560's SDA and SCL rows carry no claim
(`boards/atmega2560/board.c:72-73`), and the hardware SPI lines carry one only
in an Ethernet image. An application that claims SDA in an image with I2C
passes the check, and a host's `I2C_XFER` still drives the line. Whether the
board tables mark those pins, or the check refuses them by feature, is open
question 7.

### 6. What the host sees

| Host action on an application's pin | Today | Proposed |
|---|---|---|
| `PIN_MODE`, `DIGITAL_WRITE`, `TIMER_SET`, `PWM_WRITE`, `TONE` | executed | `TFW_ERR_RESERVED_PIN` |
| `SPI_XFER` with it as chip select, `ONEWIRE_RESET`, `_WRITE`, `_READ` | executed | `TFW_ERR_RESERVED_PIN` |
| `DIGITAL_READ`, `PULSE_IN`, `PULSE_CAPTURE` | executed | executed |
| CAPS pin page | the board's flags | the board's flags with `IN_USE` |
| APP_INFO | the descriptor and state | unchanged; `unsupported` also for a clash |

**No wire byte changes.** No command, frame, status code, flag bit or reply
layout is added or moved. What changes is a value a given image reports: the
flags byte of a claimed pin, in an image whose application claims it. An
Ethernet image's pin pages already differ from a serial image's the same way
(`include/tfw/board.h:41-46`).

**No existing vector changes.** `TFWV_CAPS_PIN13_REPLY` is checked in the unit
link (`test/unit/test_dispatch.c:158`), whose mock application,
`test/mock/app_mock.c`, claims nothing. A code lane adds literal vectors for
the claimed case (below).

## What changes in `sdk/go`

Nothing the wire needs. `StatusErrReservedPin` and `PinInUse` already decode
the answer and the flag. The doc comment of `PinInUse`
(`sdk/go/device.go:206-208`) widens to name the application, as the status's
comment does. Option b of decision 3 would add one `PinFlags` constant and its
name.

## Tests a code lane adds

- **Unit, the claim.** A second mock application, in its own unit binary,
  claims one pin. On that pin, each of the eight checked commands answers
  `TFW_ERR_RESERVED_PIN`, and `DIGITAL_READ` answers its level. The pin page
  has `IN_USE`; an unclaimed pin's page is unchanged.
- **Unit, the states.** The claim holds with the runner in `FAILED` (an
  `init` that refuses) and in `UNSUPPORTED` (a bad descriptor), per decision
  4 as ruled.
- **Unit, the clash.** A listed pin that is a board's UART pin, one past
  `TFW_PIN_COUNT`, and, in an image with the stager, the OTA store's chip
  select: each leaves the runner `UNSUPPORTED` and `init` uncalled.
- **Unit, no application.** `test_app_none.c` pins that no pin page changes
  and `tfw_app_pin_flags()` answers 0 for every pin.
- **Vectors.** `TFWV_APPPIN_*` in `testdata/vectors.h`, mirrored in
  `sdk/go/internal/vectors`: the pin page of the example's claimed pin, and
  the `TFW_ERR_RESERVED_PIN` reply frame to `DIGITAL_WRITE` on it.
- **Cross-implementation.** A `caps_tfwsim_test.go` row against
  `simproc.App`: `DIGITAL_WRITE` on the application's pin is
  `StatusErrReservedPin`.
- **CLI end to end.** `tfwctl` against the application image, on the serial
  and the Ethernet link: `digital-write` on the claimed pin is refused, and
  the pin's `caps` line reads `in-use`.
- **Build.** `make app-sdk-check` builds the example with `APP_PINS := yes`
  from outside the tree; the size table in [engineering.md](engineering.md)
  is re-measured and shows the no-application images unchanged.

## Consequences

- The firmware adds one function to the runner pair, one `.c` file for
  applications that claim nothing, one `app.mk` knob, a loop in
  `descriptor_ok()` and the OR at eight checks and the pin page. RAM: none.
  Flash: the list and the loop, in images with an application only.
- An application's pins become part of its contract with the image, measured
  in `FWHash` with the rest of it.
- The reservation keeps the host off the application's pins. It does not keep
  the application off anyone's: an application can still resolve and drive
  any pin with `tfw_board_pin()`, as the example does. AVR has no isolation,
  and the architecture does not claim any
  ([architecture.md](architecture.md#trust-boundaries)).

## Open questions for the owner

1. Decision 1: a `const` pin list with an `app.mk` knob (A), a descriptor
   field (B), a claim call from `init` (C), or a mask in the board rows (D)?
   Recommended: A.
2. Decision 1: is an application's pin `IN_USE` (0x40) or `RESERVED` (0x20)?
   Both refuse alike. Recommended: `IN_USE`.
3. Decision 3: is "taken" enough (a), or does the pin page say "by the
   application" with a new flag bit (b)? If b, which bit, 0x08 or 0x10?
   Recommended: a now; b is additive later.
4. Decision 4: does the claim hold in every runner state, or is it released
   outside `RUNNING` so a host can take over from a failed application?
   Recommended: held.
5. Decision 5: is a clash `UNSUPPORTED` (3), with api.md's "the magic was
   wrong" sentence widened, or a new APP_INFO state value, which extends?
   Recommended: `UNSUPPORTED`.
6. Decision 2: do reads stay open on an application's pin? `PULSE_IN` and
   `PULSE_CAPTURE` hold the superloop up to their timeout. Recommended: open.
7. Decision 5: the hardware buses' fixed pins (SDA, SCL, and the SPI lines in
   a serial image) carry no claim. Do the board tables mark them in images
   that link the bus, or does the runner refuse a claim of them by feature?

Application-defined wire commands, the row's other proposed follow-up, are
decided separately, in
[adr-application-wire-commands.md](adr-application-wire-commands.md).
