# Application SDK — engineering

> **Status: NORMATIVE** · Scope: trusted-firmware · Last verified: 2026-10-15
> Conflicts with this document are defects. Fix them here first.

## Exported surface

Public, in `include/tfw/app.h`. The application defines the first six; the
firmware defines the rest.

| Symbol | Defined by | Purpose |
|---|---|---|
| `tfw_app_descriptor` | application | `const tfw_app_descriptor_t`, in flash (`TFW_PROGMEM`) |
| `tfw_app_init` | application | `tfw_status_t (void)`, once at boot |
| `tfw_app_run` | application | `void (tfw_ms_t now_ms)`, every superloop pass |
| `tfw_app_shutdown` | application | `void (void)`, before a commanded reset |
| `tfw_app_before_update` | application | `void (void)`, before a reset into the bootloader |
| `tfw_app_after_update` | application | `tfw_status_t (void)`, first boot of a new image |
| `tfw_app_config_read` | firmware | read the application's EEPROM region |
| `tfw_app_config_write` | firmware | write the application's EEPROM region |
| `TFW_APP_STATIC_CHECK(caps, ota)` | firmware | compile-time compatibility check, used at file scope |
| `TFW_APP_MAGIC`, `TFW_APP_ABI`, `TFW_APP_NAME_LEN`, `TFW_APP_OTA_*`, `TFW_APP_CAP(bit)`, `TFW_APP_CONFIG_BASE` | firmware | descriptor constants |

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
   more times. Then at most one stop: `before_update` (update resets only)
   followed by `shutdown`. Nothing is called after `shutdown` until the next
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
| `before_update` | from `tfw_sys_poll()`, after a `RESET` mode 1 reply has drained, before the update request is armed | shares the stop budget |
| `shutdown` | straight after `before_update`, or alone for `RESET` mode 0 | **10 ms** with `before_update`, not measured |
| `after_update` | not called yet; see [after_update](#after_update) | 20 ms with `init` |

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
5. `ota_slots` is more than the image can serve: `TFW_OTA_SLOTS` if the image
   can request an update (`tfw_update_request_supported()`), 0 otherwise;
6. `TFW_APP_CONFIG_BASE + config_len` exceeds `TFW_EEPROM_SIZE`.

FAILED and UNSUPPORTED both mean firmware-only mode. The firmware answers
every command, attests and accepts an update as if no application were
linked. The pins the application configured before failing stay as it left
them; an `init` that fails puts its outputs back to safe first.

`tfw_app_config_read` and `tfw_app_config_write` return `TFW_ERR_RANGE` when
`off + len` exceeds `config_len`, and otherwise whatever the HAL's EEPROM call
returned. They work in any state, so a FAILED application's configuration is
still readable by a later image.

`TFW_APP_STATIC_CHECK` turns checks 4 and 5 into compile errors where the
application uses it. The runtime check still runs. It is the one that holds
for an application that skipped the macro.

### after_update

Declared and required now, so the ABI does not change when it is wired. The
`ota` row will call it on the first boot of a newly installed image, after
`init` returned `TFW_OK` and before the image is confirmed. `TFW_OK` means
confirm. Anything else means do not confirm, so the bootloader rolls the
device back to the previous image on the next reset. On a single-slot board
there is no previous image; a failure there is reported and the image stays.
Until that row lands, nothing calls it.

## Configuration

**Build time** is the application's own business: its headers, and
`APP_CFLAGS` in its `app.mk` for defines. The firmware's knobs in `config.h`
and the board profile are the firmware's, and an application does not
override them.

**Run time** is a region of EEPROM:
`[TFW_APP_CONFIG_BASE, TFW_APP_CONFIG_BASE + config_len)`.
`TFW_APP_CONFIG_BASE` is `TFW_BOOT_EE_LEN` (192), the first byte above the
platform block that `EEPROM_WRITE` already refuses to touch. A host writes the
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
| `test_app` | hook order, including both stop paths through a real `RESET` command; a failing `init` leaves FAILED with no later call; each descriptor check gives UNSUPPORTED with `init` never called; overruns are counted and saturate; config read and write stay inside the region; APP_INFO reports the runner and the descriptor as written, a refused one included, and takes no payload |
| `test_app_none` | the default links with the empty runner and no application, no hook or descriptor is needed, the runner stays IDLE and a `RESET` still resets, and APP_INFO answers `TFW_ERR_UNKNOWN_CMD` |
| `test_app_example` | the example under `test/app/example`, compiled with the application flags, links into the host library and toggles its pin on time, and APP_INFO reports it byte for byte |
| `make app-sdk-check` | the example copied to a temporary directory outside the tree builds `tfwsim` with `APP_DIR` and runs it |
| `make app-firmware-check` | the same copy builds a firmware image for `BOARD`, through the size gate |
| `make tfwsim`, `make verify-vectors`, the SDK suite | the default image is unchanged on the wire, and the APP_INFO vectors agree in C and Go |
| `make sdk-test` | `Device.AppInfo` decodes what `tfwsim-app-heartbeat` sends, and the default `tfwsim` answers `TFW_ERR_UNKNOWN_CMD` |

The call-recording application, `test/mock/app_mock.c`, is linked into every
unit test through `HOST_LIB_SRCS`. Each suite that links an application of its
own filters it out. Its symbols are strong: a suite that links two
applications fails to link, which is the contract.
