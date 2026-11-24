# Application SDK — engineering

> **Status: NORMATIVE** · Scope: trusted-firmware · Last verified: 2026-11-21
> Conflicts with this document are defects. Fix them here first.

## Exported surface

Public, in `include/tfw/app.h`. The application defines the first seven; the
firmware defines the rest.

| Symbol | Defined by | Purpose |
|---|---|---|
| `tfw_app_descriptor` | application | `const tfw_app_descriptor_t`, in flash (`TFW_PROGMEM`) |
| `tfw_app_init` | application | `tfw_status_t (void)`, once at boot |
| `tfw_app_run` | application | `void (tfw_ms_t now_ms)`, every superloop pass |
| `tfw_app_shutdown` | application | `void (void)`, before a commanded reset |
| `tfw_app_before_update` | application | `void (void)`, before a reset into the bootloader |
| `tfw_app_after_update` | application | `tfw_status_t (void)`, once on the trial boot of a new image |
| `tfw_app_evidence` | application | `tfw_status_t (uint8_t out[TFW_APP_EVIDENCE_LEN])`, 32 bytes for the measurements claim (273) |
| `tfw_app_config_read` | firmware | read the application's EEPROM region |
| `tfw_app_config_write` | firmware | write the application's EEPROM region |
| `TFW_APP_STATIC_CHECK(caps, ota)` | firmware | compile-time compatibility check, used at file scope |
| `TFW_APP_MAGIC`, `TFW_APP_ABI`, `TFW_APP_NAME_LEN`, `TFW_APP_EVIDENCE_LEN`, `TFW_APP_OTA_*`, `TFW_APP_CAP(bit)`, `TFW_APP_CONFIG_BASE` | firmware | descriptor constants |

Capability bit numbers are named in `include/tfw/config.h` as
`TFW_CAPBIT_GPIO` (0) to `TFW_CAPBIT_ATTEST_CSR` (15). They are the bit numbers
`TFW_FEATURE_BITMAP` and the CAPS `Features` field already use. Naming them
changed no value.

Internal, in `src/core/app_runner.h`. Applications must not include it.

| Symbol | Caller |
|---|---|
| `tfw_app_runner_start(void)` | `startup()` in `src/main.c`, `run()` in `tools/tfwsim/main.c` |
| `tfw_app_runner_poll(void)` | the superloop, and tfwsim's `pump()` |
| `tfw_app_runner_stop(uint8_t for_update)` | `tfw_sys_poll()` in `src/caps/sys.c`, before the reset |
| `tfw_app_runner_state(void)` | tests |
| `tfw_app_runner_overruns(void)` | tests |
| `tfw_app_cmd_info(r, w)` | the dispatcher, for APP_INFO, through `src/core/commands.def` when `TFW_FEATURE_APP` is on |

Link-time selection, in `mk/app.mk`, `mk/firmware.mk` and `mk/host.mk`:

| Build | Runner | Application objects |
|---|---|---|
| `make firmware BOARD=<b>` | `src/core/app_runner_none.c` | none |
| `make firmware APP_DIR=<path> BOARD=<b>` | `src/core/app_runner.c` | `$(APP_SRCS)` from `<path>/app.mk` |
| `make tfwsim` | `src/core/app_runner_none.c` | none |
| `make tfwsim APP_DIR=<path>` | `src/core/app_runner.c` | the application's, against the mock HAL |
| unit tests (`HOST_LIB_SRCS`) | `src/core/app_runner.c` | `test/mock/app_mock.c`, a call-recording application |

A build that links `app_runner.c` also defines `TFW_FEATURE_APP=1`, which
puts APP_INFO in the command table: `APP_DEFS` in `mk/app.mk`, and
`HOST_APP_DEFS` for the unit tests. The no-application rows define nothing.

The no-application rows link no runner, no descriptor and no configuration
code. `app_runner_none.c` defines the runner's entry points empty, so under
`-flto` the calls in `startup()`, the superloop and `tfw_sys_poll()` inline to
nothing and every board's image is the size it was before this interface. An
application build costs the runner and the application. Applications target
boards with room: one that does not fit the ATmega328P or ATmega32U4 ATECC608A
tier is expected, and no budget is raised for it.

Measured with avr-gcc 14.2.0 (Debian 13, binutils-avr 2.43.50, avr-libc 2.2.1),
`make firmware BOARD=<b> CRYPTO=<t>`, sizes from `avr-size`: flash is `.text` +
`.data`, RAM is `.data` + `.bss`, in bytes. The example application is
`APP_DIR=test/app/example`, built at `main` (after #64).

| Board | Tier | No application, `9ed0af2` (before APP_INFO) | No application, `main` | Example application, `main` |
|---|---|---|---|---|
| atmega328p | soft | 12898 flash / 717 RAM | 12898 flash / 717 RAM | 13620 flash / 728 RAM |
| atmega328p | atecc608a | 16076 flash / 923 RAM | 16076 flash / 923 RAM | does not fit: 16804 flash against the 16384 limit (RAM 934) |
| atmega2560 | soft | 13290 flash / 765 RAM | 13290 flash / 765 RAM | 14016 flash / 776 RAM |
| atmega2560 | atecc608a | 18366 flash / 989 RAM | 18366 flash / 989 RAM | 19088 flash / 1000 RAM |

The `atmega328p` `soft` row was measured before owner ruling 2026-10-17 (m522),
which made the 328P element-only; `CRYPTO=soft` is refused on it now, and the
row stays as a dated record.
On the 328P's element-tier default the example application measures 16872 flash
against the 16384 limit, so it does not fit the element row the ruling left the
328P; `make build` (and so `make ci`) runs `app-firmware-check` with
`BOARD=atmega2560`.

The no-application images at `9ed0af2` and at `main` are byte-for-byte the same
size in flash and RAM on every board and tier: the "same size" claim holds. The
example application costs 722 to 726 bytes of flash and 11 of RAM.

## The descriptor

```
off  size  field            meaning
 0    4    magic            TFW_APP_MAGIC, the bytes "TFWA"
 4    1    abi              TFW_APP_ABI the application was built against; 1 today
 5    1    ota_slots        image slots the update story needs: 0 none, 1 single, 2 A/B
 6    1    version_major
 7    1    version_minor
 8    2    version_patch
10    2    config_len       bytes of EEPROM the application owns, from TFW_APP_CONFIG_BASE
12    4    required_caps    CAPS Features bits the application needs
16   16    name             ASCII, NUL-terminated and NUL-padded, 1..15 characters
32   32    profile          ASCII, NUL-terminated and NUL-padded, 0..31 characters:
                            the device profile the device onboards under
```

64 bytes, every field naturally aligned, with `_Static_assert` on the size and
on each offset. It is a `const` object in flash, so it is inside the region
tfw-boot measures and `FWHash` covers it. Its address is not fixed. Tools find
it by the symbol name in the ELF, or scan the image for the magic. A host
reads every field but the magic over the wire with APP_INFO
([api.md](api.md#app_info)).

`profile` names the device profile that go-trusted-platform onboards a device
running this application under. The namespace is go-trusted-platform's. The
firmware carries the string and never interprets it; empty means the
platform's default profile.

`abi` is the version of this interface: the hook set, their signatures and the
descriptor layout. Any change that would make an application built against the
old header misbehave raises it. Additions that an old application cannot
observe do not.

## Invariants

1. **The runner is the only caller of a hook.** Nothing else in `src/` names a
   `tfw_app_*` hook.
2. **Hooks are called in one order.** `init` exactly once. Then `run` zero or
   more times. Then at most one stop: `before_update` (update resets only:
   `RESET` mode 1, `OTA_ACTIVATE` mode 1, and `RESET` mode 0 after an
   `OTA_ACTIVATE` mode 0) followed by `shutdown`. Nothing is called after `shutdown` until the next
   reset.
3. **No hook is called unless `init` returned `TFW_OK`.**
4. **The descriptor is checked before `init`.** An application that fails the
   check is never called at all.
5. **The firmware without an application is the firmware before this
   interface.** It links `app_runner_none.c`, whose empty entry points inline
   away, so it costs no flash. The call sites are the same, so the default
   build exercises them. The wire is unchanged: it has no APP_INFO entry and
   answers `TFW_ERR_UNKNOWN_CMD` as it always did, and `make verify-vectors`
   passes with no existing vector moved.
6. **An application compiles against public headers only.** Its include path
   is `include/`, the board directory (for `board_profile.h`, which
   `config.h` pulls in) and the architecture's `hal_types.h` directory (for the
   opaque `tfw_pinref_t`). There is no `-Isrc`. An application that includes a
   firmware-internal header fails to compile.

## Lifecycle and timing

The run budget is a knob in `include/tfw/config.h`. The other budgets are
contract numbers: they are not measured, because the runner cannot see time
pass (`init`) or cannot report afterwards (the stop hooks).

| Hook | Called | Budget |
|---|---|---|
| `init` | once, at the end of `startup()`, after transport init, **before interrupts are enabled** | 20 ms, not measured: the tick does not run yet |
| `run` | once per superloop pass while RUNNING, after transport, scheduler and sys have polled | `TFW_APP_RUN_BUDGET_MS`, default **2 ms**, measured |
| `before_update` | from `tfw_sys_poll()`, after the reply has drained of a `RESET` mode 1, before the update request is armed, or of an `OTA_ACTIVATE` mode 1, whose reset installs the marked image and arms nothing, or of a `RESET` mode 0 after this session's `OTA_ACTIVATE` mode 0, whose reset installs the same way | shares the stop budget |
| `shutdown` | straight after `before_update`, or alone for a `RESET` mode 0 that installs nothing | **10 ms** with `before_update`, not measured |
| `after_update` | once, at the end of `startup()`, straight after `init` returned `TFW_OK`, on a trial boot only; see [after_update](#after_update) | 20 ms with `init` |
| `evidence` | only while RUNNING, from the command dispatcher, once per alias certificate minted, never for `ATTEST_CSR`, which signs the value the mint kept | **10 ms**, not measured |

**Why `evidence` is not measured.** It runs inside a command, between the
request and its reply, so the receive ring is not being drained: 10 ms is the
host's to wait for, not the ring's to absorb. The application should hand back
bytes it already holds, a hash it keeps current in `run`, rather than compute
one here. Its output goes through a copy, so a refusal that wrote half of
`out` leaves the claim the firmware measurement.

**Why 2 ms.** At 115 200 baud a byte arrives every 87 µs, and the receive ring
holds 63 bytes, so the ring overflows about 5.5 ms after the superloop stops
draining it. A request frame is up to 70 bytes. The millisecond tick makes a measured
2 ms anything below 3 ms real time, which leaves the transport half the ring.
The scheduler's resolution is also 1 ms, so a 2 ms `run` can delay a scheduled
pin edge by up to 2 ms. A board with a slower link or no scheduler can raise
the knob.

**Overruns are counted, not enforced.** The runner reads the tick before and
after `run`. If the difference is more than the budget it adds one to a
16-bit counter that saturates. Nothing else happens. Enforcement would need
preemption or a watchdog reset, and both are worse than a slow superloop.

**What each hook may do.**

| | `init` | `run` | `before_update`, `shutdown` |
|---|---|---|---|
| HAL GPIO, ADC, PWM, bus, EEPROM | yes | yes | yes |
| `tfw_app_config_read` | yes | yes | yes |
| `tfw_app_config_write` | yes, sparingly | one or two bytes per pass | yes, within budget |
| `tfw_hal_delay_us` | bus bit-banging only | bus bit-banging only, within budget | bus bit-banging only |
| wait on `tfw_hal_millis` | no, it does not advance | no; keep state and return | no |
| the UART, the transport, the wire | never | never | never |
| `tfw_hal_reset`, `tfw_hal_irq_enable` | never | never | never |
| crypto provider, identity, boot handoff | never | never | never |

An AVR EEPROM write takes about 3.4 ms per byte, so a multi-byte
`tfw_app_config_write` breaks the run budget. Write in `init`, or one byte per
pass.

Shutdown is best effort. It is not called on power loss, brown-out, a
watchdog reset, a crash or a hook that never returns. An application whose
outputs must be safe across those makes their reset state the safe state;
`shutdown` is for state that can only be tidied by running code.

## Error semantics

`tfw_app_runner_state()` returns one of these. APP_INFO puts them on the wire
as its first byte, so they extend and never renumber.

| Value | State | Entered when |
|---|---|---|
| 0 | IDLE | before `tfw_app_runner_start` |
| 1 | RUNNING | `init` returned `TFW_OK` |
| 2 | FAILED | `init` returned anything else |
| 3 | UNSUPPORTED | the descriptor check failed; `init` was not called |
| 4 | STOPPED | the stop hooks ran |

The descriptor check fails, in this order, when:

1. `magic` is not `TFW_APP_MAGIC`;
2. `abi` is not this firmware's `TFW_APP_ABI`;
3. `name` is empty or fills its field with no NUL, or `profile` fills its
   field with no NUL;
4. `required_caps` has a bit `TFW_FEATURE_BITMAP` lacks;
5. `ota_slots` is more than the image's update path serves
   (`tfw_update_slots()`): `TFW_OTA_SLOTS` where tfw-boot serves one, 2 on
   ESP32 (ESP-IDF's ota_0 and ota_1, answered by `src/hal/esp32/hal_ota.c`),
   0 on a board with neither;
6. `TFW_APP_CONFIG_BASE + config_len` exceeds `TFW_EEPROM_SIZE`.

FAILED and UNSUPPORTED both mean firmware-only mode. The firmware answers
every command, attests and accepts an update as if no application were
linked. On the trial boot of a newly installed image (boot_state TRIAL) it
does not serve on: `tfw_ota_trial_after_init()` (`src/core/ota_trial.c:27`)
resets into `tfw-boot` at once, which rolls back to the previous image, and
`tfw_app_after_update` is not asked (open question 1, decided A; see
[after_update](#after_update)). The pins the application configured before
failing stay as it left them; an `init` that fails puts its outputs back to
safe first.

`tfw_app_config_read` and `tfw_app_config_write` return `TFW_ERR_RANGE` when
`off + len` exceeds `config_len`, and otherwise whatever the HAL's EEPROM call
returned. They work in any state, so a FAILED application's configuration is
still readable by a later image.

`TFW_APP_STATIC_CHECK` turns checks 4 and 5 into compile errors where the
application uses it. The runtime check still runs. It is the one that holds
for an application that skipped the macro.

### after_update

Called once on the one trial boot of a newly installed image (`boot_state`
`TRIAL`, decision 9 of
[the OTA ADR](../ota/adr-ota-image-format-and-trust.md)), after `init`
returned `TFW_OK` and before anything is served. `startup()` calls
`tfw_ota_trial_after_init()` straight after the runner's start; in `TRIAL` it
asks `tfw_app_runner_after_update()`, which calls the hook only while the
application is RUNNING. It needs no RAM of its own: the trial's `armed` flag
already says `TRIAL`.

- `TFW_OK` confirms nothing. The trial runs on to its deadline, and the
  control plane's `OTA_CONFIRM`, after attestation, is what confirms the
  image. A device-local confirm is gone (ADR decision 9).
- Anything else ends the trial at once, as failed: the firmware resets into
  `tfw-boot`, which finds `TRIAL` with no confirm request and rolls back to
  the previous image. Recovery is another OTA round.
- Not called on any boot that is not a trial, nor on a board without the
  trial. When `init` refused, or the descriptor failed its checks, the trial
  ends at once without asking it, by the same reset (open question 1, decided
  A). On a boot that is not a trial a failed `init` resets nothing and the
  firmware serves on. On a board without the trial: there `src/core/ota_trial_none.c` is linked and
  asks nothing. Today the trial is linked on the ATmega2560 only (`BOOT_AB`,
  `mk/firmware.mk:316`).

## Configuration

**Build time** is the application's own business: its headers, and
`APP_CFLAGS` in its `app.mk` for defines. The firmware's knobs in `config.h`
and the board profile are the firmware's, and an application does not
override them.

**Run time** is a region of EEPROM:
`[TFW_APP_CONFIG_BASE, TFW_APP_CONFIG_BASE + config_len)`.
`TFW_APP_CONFIG_BASE` is `TFW_BOOT_EE_LEN` (`0x140`, 320), the first byte above
the refused region that `EEPROM_WRITE` already refuses to touch. A host writes the
region with `EEPROM_WRITE`, at the absolute addresses, so no command is new.
The region is the application's by declaration only. A host that keeps other
data in EEPROM keeps it outside the region.

The space is the board's EEPROM less 192 bytes: 320 on the ATtiny85, 832 on
the ATmega328P and ATmega32U4, 3 904 on the ATmega2560.

## Persistence

The region survives a reset and a firmware update, because tfw-boot writes
flash, not EEPROM. A new application version reads the old version's bytes.
The application versions its own layout, typically with a first byte, and
treats erased EEPROM (every byte `0xFF`) as unconfigured. The runner keeps no
state across a reset: the state and the overrun counter start again at IDLE
and 0.

Nothing secret goes in the region. Any host with wire access can read it.

## Build

`mk/app.mk`, included before the host and firmware rules:

- With `APP_DIR` empty, `APP_RUNNER_SRCS` is `src/core/app_runner_none.c`, no
  application object is linked, and every output path and image size is what
  it was.
- With `APP_DIR` set, `APP_RUNNER_SRCS` is `src/core/app_runner.c`.
- With `APP_DIR` set, it includes `$(APP_DIR)/app.mk`. That file sets
  `APP_NAME` (lower-case letters, digits and `-`, at most 15 characters) and
  `APP_SRCS` (paths relative to `APP_DIR`), and may set `APP_CFLAGS`. The
  build fails if `app.mk` is missing, `APP_NAME` is malformed or `APP_SRCS` is
  empty.
- Firmware objects for the application go to `$(FW_DIR)/app/` and the image
  to `build/<board>/<tier>-app-<name>/`. The host simulator is
  `build/host/tfwsim-app-<name>`.
- The application compiles with the firmware's warning set and `-Werror`, the
  architecture's machine and optimisation flags, the tier's feature defines,
  and its own `APP_CFLAGS`, so `TFW_FEATURE_BITMAP` reads the same in the
  application as in the firmware.
- `-ffile-prefix-map=<APP_DIR>=app` strips where the application was checked
  out, and `-frandom-seed` is the file name, as for the firmware. The same
  firmware commit, application commit, board and tier give the same image.
- `size-check` counts the application against the board's flash and RAM
  budgets. The ATtiny85 has about 800 bytes of flash and 86 bytes of RAM to
  spare.

## Test strategy

| Test | Proves |
|---|---|
| `test_app` | hook order, including both stop paths through a real `RESET` command and the install reset `OTA_ACTIVATE` mode 1 asks for; a failing `init` leaves FAILED with no later call; each descriptor check gives UNSUPPORTED with `init` never called; overruns are counted and saturate; config read and write stay inside the region; APP_INFO reports the runner and the descriptor as written, a refused one included, and takes no payload |
| `test_app_none` | the default links with the empty runner and no application, no hook or descriptor is needed, the runner stays IDLE and a `RESET` still resets, and APP_INFO answers `TFW_ERR_UNKNOWN_CMD` |
| `test_app_example` | the example under `test/app/example`, compiled with the application flags, links into the host library and toggles its pin on time, and APP_INFO reports it byte for byte |
| `test_ota_stage` (`before_update`) | `OTA_ACTIVATE` mode 1 on a staged image calls `before_update` then `shutdown` once its reply drains, and mode 0 stops nothing |
| `test_ota_stage` (`RESET` after mode 0) | a `RESET` mode 0 after `OTA_ACTIVATE` mode 0 calls `before_update` then `shutdown`; with nothing marked, or ended and not activated, only `shutdown`; a later `OTA_BEGIN`, a failed stage and a failed second activation clear it |
| `make app-sdk-check` | the example copied to a temporary directory outside the tree builds `tfwsim` with `APP_DIR` and runs it |
| `make app-firmware-check` | a copy at a fixed path under `build/`, refreshed on every run, builds a firmware image for `BOARD`, through the size gate |
| `make app-header-check`, run by `host-test` | every header in `include/tfw/` compiles as the whole of a translation unit, included twice, against every board under `boards/`, with an application's flags and warning set and no `src/` on the path; a header that needs another included first fails, and `src/core/app_runner.h` cannot be reached |
| `make app-header-check-selftest`, in the `security` stage | the gate fails on a planted header that needs `<stddef.h>` from its includer, and names it |
| `make tfwsim`, `make verify-vectors`, the SDK suite | the default image is unchanged on the wire, and the APP_INFO vectors agree in C and Go |
| `make sdk-test` | `Device.AppInfo` decodes what `tfwsim-app-heartbeat` sends, and the default `tfwsim` answers `TFW_ERR_UNKNOWN_CMD` |
| `test_app`, `test_app_none`, `test_app_example` (evidence) | a running application's evidence comes back through a copy; a failed, stopped or refused one is never asked; the empty runner supplies none; the example supplies `0xE0..0xFF` |
| `test_uccs`, `test_alias`, `test_csr` (evidence) | claim 273 is byte for byte `TFWV_APPEV_WITH_UCCS` with an application's evidence and `TFWV_APPEV_WITHOUT_UCCS` without, on every certificate page and in the signed `ATTEST_CSR` request |
| `ApplicationEvidence` tests, `sdk/go` | the Go decoder returns the 32 bytes from both vectors and refuses a claims set of any other shape with an `*EvidenceError` |
| `make sdk-test`, `tfwctl` E2E | `tfwctl -json attest-cert` against `tfwsim-atecc608a-app-heartbeat` prints the example's 32 bytes as `app_evidence`, the certificate carries them in 273 and verifies under the DeviceID, and `ATTEST`'s `FWHash` stays the firmware's; against `tfwsim-atecc608a`, 273 is the firmware hash |

The call-recording application, `test/mock/app_mock.c`, is linked into every
unit test through `HOST_LIB_SRCS`. Each suite that links an application of its
own filters it out. Its symbols are strong: a suite that links two
applications fails to link, which is the contract.

## Row status

The `application-sdk` row of [SPEC.md](../SPEC.md) (line 56), and the
contract this document adds to it, requirement by requirement, against `main`
at `d1282f61` (audit #461, which replaced the line numbers of the earlier
table, #51, with test names, because they had drifted). Done: in the code and
pinned by a test. Partial: in the code on some paths, or in the code with no
test that would fail without it; the rest is a slice below. Not started: not
in the code. Code is `file:line`; tests are `file` and test name, under
`test/unit/` unless a path is given.

| Requirement | Status | Implemented at | Proved by |
|---|---|---|---|
| Descriptor: name, version, device profile, required capabilities, OTA slots, configuration length; checked before `init` | done | `include/tfw/app.h:52`; `descriptor_ok` at `src/core/app_runner.c:31` | `test_app.c`: `test_a_wrong_magic_is_refused`, `test_a_newer_abi_is_refused`, `test_an_empty_name_is_refused`, `test_a_profile_with_no_terminator_is_refused`, `test_a_capability_this_image_lacks_is_refused`, `test_configuration_past_the_end_of_the_eeprom_is_refused`; `test_app_example.c`: `test_the_example_names_itself` |
| Hook `init`, once, before interrupts; a refused `init` or descriptor ends a trial at once (open question 1, A) | done | `tfw_app_runner_start` at `src/core/app_runner.c:65`, from `startup()` at `src/main.c:95`; `src/core/ota_trial.c:39` | `test_app.c`: `test_start_calls_init_and_poll_calls_run`, `test_a_failing_init_leaves_the_application_failed_and_uncalled`; `test_ota_trial.c`: `test_a_failed_init_ends_the_trial_at_once`, `test_an_unsupported_descriptor_ends_the_trial_at_once`, `test_outside_a_trial_a_failed_init_serves_on` |
| Hook `run`, every pass, overruns of `TFW_APP_RUN_BUDGET_MS` counted and not enforced | done | `tfw_app_runner_poll` at `src/core/app_runner.c:75`, from the superloop at `src/main.c:116` | `test_app.c`: `test_run_is_given_the_time_of_the_pass`, `test_a_run_over_the_budget_is_counted_and_not_stopped`, `test_the_overrun_count_saturates` |
| Hook `shutdown`, before a commanded reset, once | done | `tfw_app_runner_stop` at `src/core/app_runner.c:92`, from `tfw_sys_poll()` at `src/caps/sys.c:239` | `test_app.c`: `test_reset_stops_the_application_and_resets_the_device`, `test_a_stopped_application_is_not_run_or_stopped_again` |
| Hook `before_update`: `RESET` mode 1, `OTA_ACTIVATE` mode 1, `RESET` mode 0 after `OTA_ACTIVATE` mode 0 | done | `src/core/app_runner.c:92`; `tfw_sys_reset_soon()` at `src/caps/sys.c:61`, from `src/core/ota_activate.c:98`; `tfw_stage_install_pending()` at `src/core/ota_stage.c:281`, asked at `src/caps/sys.c:239` | `test_app.c`: `test_reset_to_bootloader_calls_before_update_then_shutdown`, `test_an_install_reset_calls_before_update_then_shutdown`; `test_ota_stage.c`: `test_activate_tells_the_application_of_the_update_it_resets_into`, `test_a_reset_after_activate_mode_0_is_an_update`, `test_a_reset_with_nothing_marked_only_shuts_down`, `test_a_failed_activate_clears_the_install`; `test_none_variants.c`: `test_stage_install_pending_is_zero` |
| Hook `after_update`, once on a trial boot after `init`; a refusal ends the trial, `TFW_OK` confirms nothing | done | declared at `include/tfw/app.h:110`; `tfw_ota_trial_after_init()` at `src/core/ota_trial.c:27`; `tfw_app_runner_after_update()` at `src/core/app_runner.c:129`; the reset at `src/core/ota_trial.c:42`; the trial is linked on the ATmega2560 only, elsewhere `src/core/ota_trial_none.c` | `test_ota_trial.c`: `test_an_accepted_after_update_leaves_the_trial_running`, `test_a_refused_after_update_ends_the_trial_at_once`, `test_outside_a_trial_after_update_is_not_asked`; `test_app.c`: `test_a_running_application_answers_the_trial`, `test_a_running_application_can_refuse_the_trial`, `test_an_application_that_is_not_running_is_not_asked_by_the_trial`; `test_app_none.c`: `test_the_empty_runner_never_refuses_a_trial`; `test_none_standins.c`: `test_ota_trial_after_init_returns` |
| Configuration: the EEPROM region from `TFW_APP_CONFIG_BASE`, bounded by `config_len` | done | `include/tfw/app.h:50`; `tfw_app_config_read` and `tfw_app_config_write` at `src/core/app_runner.c:186` and `:196` | `test_app.c`: `test_configuration_lands_after_the_boot_block`, `test_configuration_past_the_declared_length_is_out_of_range`, `test_an_empty_access_at_the_end_is_allowed_and_past_it_is_not` |
| Evidence claims: 32 bytes in claim 273 of the alias certificate and `ATTEST_CSR`, the firmware hash without an application | done | `src/core/app_runner.c:115`; `src/caps/attest.c:76`; `src/caps/uccs.c:76`; `src/caps/csr.c:232`; `ApplicationEvidence` at `sdk/go/appevidence.go:71`; vectors at `testdata/vectors.h:381` and `:382` | `test_uccs.c`: `test_application_evidence_is_the_bytes_the_with_vector_pins`, `test_no_application_evidence_is_the_bytes_the_without_vector_pins`; `test_alias.c`: `test_a_running_applications_evidence_is_the_claim_every_page_carries`; `test_csr.c`: `test_a_running_applications_evidence_is_what_the_request_signs`; `test_app.c`: `test_a_failed_application_is_not_asked_for_evidence`; `sdk/go/appevidence_test.go`: `TestApplicationEvidenceWithTheClaim`, `TestApplicationEvidenceWithoutTheClaim`; `sdk/go/cmd/tfwctl/e2e_dice_test.go`: `TestE2EAttestCertCarriesTheApplicationsEvidence` |
| OTA slots the images use | done | `TFW_APP_OTA_*` at `include/tfw/app.h:41`; checked at `src/core/app_runner.c:31` and at compile time by `TFW_APP_STATIC_CHECK` (`include/tfw/app.h:135`) | `test_app.c`: `test_more_update_slots_than_the_board_has_are_refused`, `test_the_slots_the_board_has_are_accepted` |
| APP_INFO (0x06) reports the descriptor and the runner state | done | `tfw_app_cmd_info` at `src/core/app_runner.c:153`, in the table at `src/core/commands.def:34` when an application is linked | `test_app.c`: `test_app_info_reports_the_runner_and_the_descriptor`, `test_app_info_reports_a_refused_descriptor_as_written`, `test_app_info_takes_no_payload`; `test_app_example.c`: `test_app_info_reports_the_example`; `sdk/go/appinfo_test.go`: `TestAppInfoParsesTheExample`; `sdk/go/cmd/tfwctl/e2e_app_test.go`: `TestFirmwareAppReportsItsApplication` |
| No application: no application logic, the image and wire as before | done | `src/core/app_runner_none.c`, selected at `mk/app.mk:73` | `test_app_none.c`: `test_the_empty_runner_stays_idle`, `test_app_info_is_unknown_without_an_application`; `sdk/go/cmd/tfwctl/e2e_app_test.go`: `TestFirmwareAppIsAnUnknownCommandWithoutAnApplication` |
| Inherits the TCB, attestation, OTA and boot and re-implements none: public headers only, no `-Isrc` (invariant 6) | done | `-Iinclude -I$(HAL_DIR) -I$(BOARD_DIR)` at `mk/firmware.mk:705`; `app-header-check` at `mk/app.mk:150` | `make app-header-check` (`mk/app.mk:150`); `make app-header-check-selftest` (`mk/app.mk:181`) |
| Onboarding by device profile | partial | the device side: `profile` in the descriptor (`include/tfw/app.h:52`), reported by APP_INFO (`src/core/app_runner.c:153`); enrolment by profile is go-trusted-platform's (SPEC row `device-onboarding`, future) | `test_app.c`: `test_app_info_reports_the_runner_and_the_descriptor`; `sdk/go/appinfo_test.go`: `TestAppInfoParsesTheExample`; slice 5 |
| One application per image, no dynamic loading | done | one `APP_DIR` at `mk/app.mk:30`; strong hook symbols, no weak default (`include/tfw/app.h:10`) | `make app-link-check` (`mk/app.mk`): the example linked beside `test/mock/app_mock.c` must fail on a multiple definition of a `tfw_app_` hook; `test_app_example.c` links exactly one application (the mock filtered out) |
| The runner is the only caller of a hook (invariant 1) | done | outside `src/core/app_runner.c`, `src/` names only `tfw_app_runner_*`, `tfw_app_config_*` and `tfw_app_cmd_info` | `make app-link-check` fails on a hook named in any other file of `src/`; `make app-link-check-selftest` plants a call in `main.c` and sees it named; `test_app.c`: `test_a_stopped_application_is_not_run_or_stopped_again` proves the order |
| Builds against a released SDK version | done | the interface first released in `v0.4.0-alpha` (`CHANGELOG.md:34`); `abi` checked at boot; the tag `v0.4.2-alpha` declares `tfw_app_after_update` and `tfw_app_evidence`, and its `include/tfw/app.h` differs from `main`'s in comments only (`git diff v0.4.2-alpha main -- include/tfw/app.h` touches only the trial-boot comments of `tfw_app_init` and `tfw_app_after_update`, #1043: no declaration, symbol or value), so the released hook set is `main`'s; `make app-sdk-check` at `v0.4.2-alpha` (commit `e0ae83d0`), the example built out of tree against the released headers and run on tfwsim, recorded 2026-11-24, #1043 | `make app-sdk-check` (`mk/app.mk:92`), the example built from outside the tree; `test_app.c`: `test_a_newer_abi_is_refused`; slice 1 |
| Board-independent: board as data at build time, any board the framework supports | partial | `make firmware APP_DIR=<path> BOARD=<b>` (`mk/firmware.mk:702`, `:717`) on the AVR and Cortex-M rule; on an ESP32 board `make esp32-build APP_DIR=<path>` hands ESP-IDF `APP_SRCS` and `APP_FW_CFLAGS` (`mk/arch/esp32-targets.mk`), an object library of its own in `mk/arch/esp32/main/CMakeLists.txt` | `make app-firmware-check` (`mk/app.mk`), which fails unless the image holds one of the application's symbols, in the `build` stage for `BOARD=atmega2560` and `BOARD=mps2-an505` (`mk/stages.mk`), and for `BOARD=esp32p4-poe` by the `esp32-build` rule (`mk/arch/esp32-targets.mk:61`, the check at `:133`) in CI's ESP32 Build job, in its ESP-IDF image; `BOARD=esp32-qemu`: `esp32-qemu-test` builds the example's esp32c3 image and boots it once, `wirecheck -app-name heartbeat` requiring APP_INFO to name it running (step 7 of `test/integration/esp32/qemu_test.py`); the run is in esp32-qemu-test (#1043), not yet observed; `make app-header-check` against every board; slice 2 |
| The same firmware, application, board and tier give the same image | done | `$(APP_PREFIX_MAP)` and `-frandom-seed` at `mk/firmware.mk:704` and `:712` | `make reproducible-check APP_DIR=test/app/example BOARD=atmega2560` (`mk/firmware.mk`, through `scripts/repro_build.py --app-dir`), in the `build` stage (`mk/stages.mk`), beside the one without `APP_DIR`; `scripts/test_repro_build.py`: `test_an_application_in_the_tree_is_the_committed_one_in_each_copy`, `test_an_application_outside_the_tree_is_one_directory_for_both` |

Counts: 16 done, 2 partial, 0 not started.

### Remaining slices

1. **The released hook set.** Released: `v0.4.2-alpha` carries
   `after_update` and the evidence hook, its `include/tfw/app.h` the same as
   `main`'s but for the trial-boot comments of #1043 (comment lines only, no
   declaration, symbol or value), so no release is needed. Done: `make
   app-sdk-check` (`mk/app.mk:92`) at `v0.4.2-alpha` (commit `e0ae83d0`), the
   example built out of tree against the released headers and run on tfwsim,
   recorded 2026-11-24, #1043; no hardware.
2. **Applications on every architecture.** Built and checked on all three:
   `mk/stages.mk` runs `make app-firmware-check` for the AVR and Cortex-M
   boards only (`BOARD=atmega2560` and QEMU's `BOARD=mps2-an505`) in the
   `build` stage. On the ESP32, CI's ESP32 Build job runs `make esp32-build
   BOARD=esp32p4-poe` in its ESP-IDF image, and the `esp32-build` rule in
   `mk/arch/esp32-targets.mk` passes `APP_SRCS` and `APP_FW_CFLAGS` to the
   ESP-IDF component, links the example application into an image of its
   own and runs `app-firmware-check` on it. On QEMU, `BOARD=esp32-qemu`:
   `esp32-qemu-test` keeps `ESP_APP_EXAMPLE` for its esp32c3 build, so the
   example's image is linked beside it, and `qemu_test.py` boots that image
   once and runs `wirecheck -app-name heartbeat`, which requires APP_INFO to
   name the application running. The row stays partial until that run is
   observed: the run is in esp32-qemu-test (#1043), not yet observed; no
   hardware.
3. **One application, one caller.** Done: `make app-link-check` (in
   `host-test`) and `make app-link-check-selftest` (in `security`). A gate in
   `mk/app.mk`, `app-link-check`, that links `test/app/example` beside
   `test/mock/app_mock.c` and expects the link to fail, and that fails when
   any file in `src/` but `src/core/app_runner.c` names a `tfw_app_` hook,
   proved by its selftest in the `security` stage as `app-header-check-selftest` is.
4. **Reproducible application image.** Done: `make reproducible-check
   APP_DIR=test/app/example BOARD=atmega2560` in the `build` stage, through
   `scripts/repro_build.py --app-dir`. `reproducible-check` in
   `mk/firmware.mk` takes `APP_DIR` through `scripts/repro_build.py`, and
   `mk/stages.mk` runs it with `APP_DIR=test/app/example`, proved by that
   run in the `build` stage.
5. **Onboarding by profile.** The `profile` string reaches go-trusted-platform's
   enrolment through its `device-onboarding` seam; nothing here changes until
   that row moves out of `future`. Another project (go-trusted-platform), and
   the owner's for the row.
6. **The proposed follow-ups**, not requirements of the row: application wire
   commands ([adr-application-wire-commands.md](adr-application-wire-commands.md),
   `APP_CMD` `0x07`, five open questions) and a pin reservation
   ([adr-application-pin-reservation.md](adr-application-pin-reservation.md),
   seven open questions). Each changes the wire, with vectors. Owner decision
   first; no code until the owner rules
   ([architecture.md](architecture.md#what-it-does-not-do)).

The row is not done: slices 3 and 4 are done, 2 but for its ESP32 run in CI; 1 is this project's and needs no hardware;
5 and 6 wait on another project or the owner. Open question 1 below is
decided.

## Open questions

### 1. Does a failed `init` in `TRIAL` end the trial at once?

**Decided A** (ruling 109, by the conventions, reported to Software): it is
the reading of decision 9's "if both return `TFW_OK`", on the one reset path
the trial already has. `tfw_ota_trial_after_init()` resets when `armed` and
the application is FAILED or UNSUPPORTED (`src/core/ota_trial.c:39`), pinned
by `test_ota_trial.c:187`, `:192` and `:225`. The record below is kept as it
was put to the owner.

Decision 9 of [the OTA ADR](../ota/adr-ota-image-format-and-trust.md) says
the new image "runs `init` and `tfw_app_after_update`. If both return
`TFW_OK`, it opens its connection and reports", and that
"`tfw_app_after_update` refusing ends the trial at once, as failed" (ADR lines
586-590; the state machine at `docs/ota/architecture.md:150` draws the same two
arrows). It names no arrow for a failed `init`: it does not say the trial ends
at once, and it does not say the trial runs to its deadline.

What the code did before the decision: `tfw_app_runner_start()` marks the application
FAILED (`src/core/app_runner.c:72`), and `tfw_app_runner_after_update()`
answers `TFW_OK` for any state but RUNNING (`src/core/app_runner.c:131`), so
`tfw_ota_trial_after_init()` does not reset. The firmware serves on without
the application, as it does on any boot, and the trial runs to its deadline.
`OTA_CONFIRM` does not look at the application's state, so the control plane
can confirm such an image if it does not read `APP_INFO` first.

The options:

- **A. End the trial at once.** `tfw_ota_trial_after_init()` resets into
  `tfw-boot` when `armed` and the application is FAILED, the same path a
  refused `after_update` takes. No command id, status code, wire vector or RAM
  byte: `armed` and the runner's state already say both facts. Recovery starts
  up to 68 s sooner, and an image whose application cannot start can never be
  confirmed, which is what "if both return `TFW_OK`" asks. The cost: the
  control plane loses the trial's window to read `APP_INFO` and the evidence of
  the failed image before the rollback.
- **B. Keep today's behaviour.** The trial runs to its deadline, and the
  control plane decides from `APP_INFO`'s FAILED state not to confirm. It
  keeps the diagnostic window, and leaves the "both return `TFW_OK`" condition
  to the control plane rather than the device.
- **C. Run to the deadline, but refuse the confirm.** `OTA_CONFIRM` answers
  `TFW_ERR_BAD_STATE` while the application is FAILED. It keeps the window and
  holds the condition on the device, at the cost of a new refusal in
  `OTA_CONFIRM`'s handler and its tests.

Whether UNSUPPORTED (a descriptor that fails its checks, so `init` is never
called) counts as a failed `init` here is part of the same question; A and C
would treat it as one.

**Recommendation: A.** It is the reading closest to decision 9's "if both
return `TFW_OK`", it takes the one reset path the trial already has, and it
costs no RAM, wire or vector. The diagnostic window B and C keep is worth
little on a trial that cannot be confirmed: `tfw-boot` keeps the failed
image's measurement in the `FAILED` record (`docs/ota/architecture.md:137`).
