# Application SDK — architecture

> **Status: NORMATIVE** · Scope: trusted-firmware · Last verified: 2026-10-15
> Conflicts with this document are defects. Fix them here first.

## Problem

trusted-firmware is a device application framework. The firmware speaks the
wire protocol, owns the pins it is commanded to drive, measures itself and
attests. It carries no application logic, and must not: a board management
controller, a sensor node and a door controller are different products, and
none of their code belongs in this repository (L1, L2).

An application lives in its own `trusted-firmware-<application>` repository.
It needs one documented way to become part of a firmware image without editing
a line here: what it must provide, when the firmware calls it, what it may do
in each call, how it is configured, how it is built, and how it is updated.
The first consumer is the BMC on the ESP32-P4. The interface is defined and
tested on the architecture that exists today, AVR, and against the host build.

## Chosen shape

**One image, one measurement.** The application is compiled from its own
directory and linked into the firmware image. tfw-boot measures the image it
boots, so `FWHash` covers the firmware and the application together. There is
no second image, no loader and no relocation.

**Five hooks and a descriptor, resolved at link time.** `include/tfw/app.h`
declares them. The application defines all six symbols. The firmware calls the
hooks directly, by name. A missing hook is a link error, exactly as a HAL
function missing from the mock is.

```
tfw_app_descriptor      const, in flash: name, version, device profile, ABI, needs
tfw_app_init            once, at boot
tfw_app_run             every superloop pass while running
tfw_app_shutdown        before a commanded reset
tfw_app_before_update   before a reset into the bootloader for an update
tfw_app_after_update    first boot of a new image (wired by the ota row)
```

**A no-application default that costs nothing.** A build with no `APP_DIR`
links `src/core/app_runner_none.c` in place of the runner: the runner's five
entry points, empty. No runner, no descriptor, no descriptor check and no
configuration code are in that image, and under `-flto` the empty calls inline
away, so it is byte for byte the size it was before applications existed. The
firmware's call sites never change, and nothing in `src/` is conditional on
whether an application exists. That is the rule CLAUDE.md states: a feature is
a file in or out of the object list, never an `#if`.

**Applications target boards with room.** The runner, the descriptor and the
application's own code are flash the smallest tiers do not have: the
ATmega328P and ATmega32U4 with the ATECC608A sit within a few hundred bytes of
their budget without an application. An application build that does not fit
one of those is expected, and the budgets are not raised for it. The
ATmega2560 and the larger parts that follow it are what applications target.

**A runner the firmware owns.** `src/core/app_runner.c` sits between the
firmware and the hooks. It checks the descriptor before the first call,
records the application's state, and is the only caller of any hook. An
application can never be called in a state the runner did not put it in.

**Fail to firmware-only, never to a brick.** An application that is
incompatible with the image, or whose `init` fails, is never called again
until the next reset. The firmware keeps answering the wire, keeps attesting
and keeps accepting an update. A broken application is replaced over the air,
not with a programmer.

## Rejected alternatives

| Alternative | Why not |
|---|---|
| A table of function pointers in the descriptor | A vtable. It costs RAM or flash per entry, makes every entry a live reference so `--gc-sections` drops nothing, and is a writable or patchable indirection on a part with no memory protection. CLAUDE.md forbids it for the HAL for the same reasons. |
| Weak default hooks the application overrides | A misspelt hook links silently against the default and the application never runs. A link error names the missing symbol. The mock's weak I2C symbols exist to be overridden by a model, not to hide an omission. |
| A separately built and loaded application image | No MMU, no MPU and no position-independent code on classic AVR. Two images would need two measurements, two update paths and a loader inside the trusted computing base. One image keeps one `FWHash` and the update path that exists. |
| `#if TFW_APP` in `main.c` | Forbidden outside the four permitted places. It would also mean the image with an application and the image without one are different code paths that are never both tested. |
| Threads or an RTOS task per application | Classic AVR has neither the RAM for a second stack nor an RTOS. The firmware is a cooperative superloop, and the application is one more cooperative step in it. A later architecture with an RTOS keeps this interface: `run` becomes the body of a task loop. |
| Application-defined wire commands | Command ids are one global namespace that extends and never renumbers. Giving applications a range is a wire-protocol design of its own, with vectors, and is proposed as a follow-up rather than guessed at here. |
| Enforcing the run budget by preemption | Needs a timer interrupt that can abandon a call. C cannot unwind it safely, and the only blunt tool, a watchdog reset, turns a slow application into a reset loop. The runner measures and counts overruns instead. |

## Boundaries

This project owns the hook contract, the descriptor layout, the runner, the
build hook and the no-application default. It owns the shape of the
application's evidence claims inside the firmware's own evidence, once that
lands (see [Evidence claims](#evidence-claims)).

It does not own:

- **any application.** The BMC and every later one live in their own
  repositories.
- **appraisal of an application's claims.** That belongs to go-xkms, which
  verifies UCCS and EAT evidence and defines the plug-in point a policy
  registers at.
- **fleet policy**, for example which application versions a device may run.
  That belongs to go-trusted-platform.
- **the update store and the image manifest.** Those belong to go-quicbaas,
  reached through go-trusted-platform (the `ota` row).

A gap in any of these is filed against the owning project, not worked around
in an application or here.

## Data flow

```
reset
  └─ startup()                       src/main.c (tools/tfwsim/main.c mirrors it)
       ├─ HAL, bus, identity, crypto, scheduler, auth, transport init
       └─ tfw_app_runner_start()     check descriptor ─► tfw_app_init()
  └─ interrupts on, BOOT event
  └─ superloop
       ├─ transport poll, scheduler poll, sys poll
       │     └─ sys poll, commanded reset pending and reply drained:
       │           tfw_app_runner_stop(for_update)
       │             ├─ tfw_app_before_update()   (RESET mode 1 only)
       │             └─ tfw_app_shutdown()
       │           then reset
       └─ tfw_app_runner_poll()      ─► tfw_app_run(now_ms)

first boot of a new image (ota row, not wired yet)
  └─ after init returned OK: tfw_app_after_update() ─► confirm, or roll back
```

Configuration flows in from the host through the existing `EEPROM_WRITE`
command, into a region the descriptor declares. The application reads it
with `tfw_app_config_read`. Nothing new crosses the wire.

## Trust boundaries

- **The application is inside the image's trusted computing base.** It runs in
  the same address space, at the same privilege, as the firmware. AVR offers
  no isolation, and this design does not pretend to. What makes this
  acceptable is that the application is measured: a verifier that appraises
  `FWHash` appraises the application with it, and a device running an
  application nobody approved fails attestation.
- **The descriptor is not a permission.** `required_caps` is a compatibility
  check that stops an application from starting on an image that lacks what it
  needs. It grants nothing and restricts nothing.
- **An application cannot block a security update.** `before_update` is a
  notification. It has no return value, and the reset into the bootloader
  happens after it returns whatever it did. A compromised or buggy application
  can delay an update by its budget, and no more, unless it never returns.
  A hook that never returns stops the superloop. No cooperative design can
  prevent that, and recovering the device then takes physical access. That is
  why the budgets are part of the contract, and why an application runs on
  tfwsim before it ships.
- **Secrets never pass through the application.** The application has no
  interface to the UDS, the CDI or any key. The runtime configuration region
  is plain EEPROM, readable by any host with wire access; secrets do not go
  there.
- **The host is not trusted by the application more than by the firmware.**
  The configuration region is written by whatever host can write EEPROM. An
  application validates what it reads.

## What it does not do

- It does not isolate the application from the firmware, or from the pins the
  host commands. There is no pin reservation yet: an application and a host
  that drive the same pin conflict. A follow-up proposes one.
- It does not let an application add wire commands, report its state over the
  wire or add evidence claims. Each is a proposed follow-up, and each is a
  wire or evidence change with vectors.
- It does not call `after_update`. The confirm step it belongs to is the `ota`
  row's, and the call site is fixed here so that row has nothing to design.
- It does not support more than one application per image. A product that is
  two applications is one application with two modules, in its own
  repository.
- It does not port anything. The interface is architecture-neutral. The
  ESP32-P4 port is its own work.

## Evidence claims

Designed here, implemented by a follow-up. An application will be able to
contribute claims to the firmware's UCCS evidence as one EAT submodule:

- **Claim key.** `submods`, key **266** (RFC 9711 §4.2.18), in the claims set
  the alias certificate's `tcg-dice-UCCS-evidence` extension carries.
- **Submodule name.** The descriptor's `name`, as a text string.
- **Submodule value.** A claims set, not a nested token, so the alias
  signature covers it. It holds `swname` (key **270**) set to the descriptor
  name, `swversion` (key **271**) set to `["<major>.<minor>.<patch>"]`, and
  whatever the application adds.
- **Application claim keys.** Private-use labels only, integers below
  **-65536** (the CWT claims registry's private-use range), so an application
  can never collide with a registered claim.
- **Encoding.** Core deterministic CBOR (RFC 8949 §4.2.1), as the rest of the
  claims set already is. Positive keys sort before negative ones, so the
  firmware writes 270 and 271 first and the application's pairs follow.
- **Hook.** A sixth hook, `tfw_app_claims`, will hand the firmware the
  application's encoded pairs. It is not declared yet: declaring it now would
  make every application define a function nothing calls.
- **Seam to go-xkms.** go-xkms parses the UCCS. It needs to recognise
  `submods`, find the submodule named for the application, and pass that
  claims set to the appraisal policy registered for that application name.
  This project emits the claims and never interprets them. Anything go-xkms
  lacks for this is filed against go-xkms.

The claims set is today a fixed 151 bytes inside a fixed 383-byte TBS
template, with `_Static_assert`s on both lengths. A submodule makes both
lengths variable, which is why this is a follow-up with its own vectors and
not part of this interface.
