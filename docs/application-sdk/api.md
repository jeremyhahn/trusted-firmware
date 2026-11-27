# Application SDK — API

> **Status: NORMATIVE** · Scope: trusted-firmware · Last verified: 2026-10-20
> Conflicts with this document are defects. Fix them here first.

The C interface between the firmware and an application, and the build
contract. Nothing here is a wire change: the application interface adds no
command, no status code and no frame.

## `include/tfw/app.h`

```c
#define TFW_APP_MAGIC        0x41574654uL   /* "TFWA" in memory order */
#define TFW_APP_ABI          1u
#define TFW_APP_NAME_LEN     16u
#define TFW_APP_PROFILE_LEN  32u

#define TFW_APP_OTA_NONE     0u              /* needs no update path */
#define TFW_APP_OTA_SINGLE   1u              /* the image is replaced in place */
#define TFW_APP_OTA_AB       2u              /* two slots and a rollback */

#define TFW_APP_CAP(bit)     ((uint32_t)1u << (bit))   /* bit: a TFW_CAPBIT_* */
#define TFW_APP_CONFIG_BASE  (TFW_NETCFG_ADDR + TFW_NETCFG_LEN)

typedef struct {
    uint32_t magic;
    uint8_t  abi;
    uint8_t  ota_slots;
    uint8_t  version_major;
    uint8_t  version_minor;
    uint16_t version_patch;
    uint16_t config_len;
    uint32_t required_caps;
    char     name[TFW_APP_NAME_LEN];
    char     profile[TFW_APP_PROFILE_LEN];
} tfw_app_descriptor_t;                      /* 64 bytes */

/* Defined by the application. */
extern const tfw_app_descriptor_t tfw_app_descriptor;
tfw_status_t tfw_app_init(void);
void         tfw_app_run(tfw_ms_t now_ms);
void         tfw_app_shutdown(void);
void         tfw_app_before_update(void);
tfw_status_t tfw_app_after_update(void);
#define TFW_APP_EVIDENCE_LEN 32u
tfw_status_t tfw_app_evidence(uint8_t out[TFW_APP_EVIDENCE_LEN]);

/* Defined by the firmware. */
tfw_status_t tfw_app_config_read(uint16_t off, uint8_t *buf, uint8_t len);
tfw_status_t tfw_app_config_write(uint16_t off, const uint8_t *buf, uint8_t len);

/* At file scope: compile errors for an image that cannot run the application. */
#define TFW_APP_STATIC_CHECK(caps, ota) ...
```

The descriptor must be defined `TFW_PROGMEM`, in flash:

```c
const tfw_app_descriptor_t tfw_app_descriptor TFW_PROGMEM = {
    .magic         = TFW_APP_MAGIC,
    .abi           = TFW_APP_ABI,
    .ota_slots     = TFW_APP_OTA_SINGLE,
    .version_major = 1u, .version_minor = 0u, .version_patch = 0u,
    .config_len    = 8u,
    .required_caps = TFW_APP_CAP(TFW_CAPBIT_GPIO) | TFW_APP_CAP(TFW_CAPBIT_SCHED),
    .name          = "heartbeat",
    .profile       = "",             /* the platform's default device profile */
};
```

### Hooks

| Hook | Return | Meaning of the return |
|---|---|---|
| `tfw_app_init` | `tfw_status_t` | `TFW_OK`: running. Anything else: FAILED, firmware-only until the next reset; on the trial boot of a new image, the firmware resets at once instead and `tfw-boot` rolls back |
| `tfw_app_run(now_ms)` | none | `now_ms` is `tfw_hal_millis()` at the call |
| `tfw_app_shutdown` | none | |
| `tfw_app_before_update` | none | a notification; the update goes ahead |
| `tfw_app_after_update` | `tfw_status_t` | Once, on the trial boot of a new image, after `init` returned `TFW_OK`. Not asked when `init` refused or the descriptor failed its checks: the trial ends at once by the same reset. `TFW_OK`: the trial runs on, and the control plane's `OTA_CONFIRM` confirms. Anything else: the trial ends at once, as failed, and `tfw-boot` rolls back |
| `tfw_app_evidence(out)` | `tfw_status_t` | `TFW_OK`: `out` holds the application's 32 bytes of evidence. Anything else: it supplies none |
| `tfw_app_command(op, r, w)` | `tfw_status_t` | Optional, only with `APP_COMMANDS := yes`. `TFW_OK`: the bytes written to `w` are the `APP_CMD` reply. Anything else is the reply's status |

`tfw_app_evidence` is called only while the application runs, once each time
the firmware mints an alias certificate. `ATTEST_CSR` signs the value that mint
kept and does not call it, so the request matches the certificate when the
evidence has changed since ([dice-attestation/api.md](../dice-attestation/api.md#attest_csr--0x64)). The 32 bytes
are opaque to the firmware: it places them in the measurements claim (273),
where a verifier matches them against the reference registered for this
application. They are signed under the DeviceID with the rest of the claims
set. When the hook returns anything but `TFW_OK`, or no application runs, the
claim carries the firmware hash `tfw-boot` measured, never zeros. A partial
write of a refused call is discarded. The layout is in
[dice-attestation/api.md](../dice-attestation/api.md), and the host reads the
bytes with `tfw.ApplicationEvidence(uccs)` after `tfw.AliasUCCS(der)`, or
from `tfwctl -json <port> attest-cert <nonce>` as `app_evidence`.

`tfw_app_command` is called only while the application runs, once for each
`APP_CMD` request, with the request's `app_op` in `op`, the arguments after it
in `r` and the reply in `w` (`tfw/reader.h`, `tfw/writer.h`). It takes the
same budget as `run`. The application owns the 256 values of `op`; it extends
them and never renumbers. A sub-op with fixed arguments checks, when it has
read them, that `tfw_reader_remaining(r) == 0`, and answers
`TFW_ERR_BAD_ARGC` if not; the firmware checks only that `op` is there. A
sub-op that actuates may ask `tfw_app_session_ok()`, which answers whether an
authenticated session is open, and refuse with `TFW_ERR_AUTH_REQUIRED`
without one. It is a read-only view: it gives the application no key and no
way to open a session. On a tier with no auth session it answers true, as the
firmware's own gate is permissive there.

A host tells three refusals apart, with no new status code:

| Answer | Means |
|---|---|
| `TFW_ERR_UNKNOWN_CMD` | no application is linked: the image has no `APP_CMD` |
| `TFW_ERR_BAD_STATE` | an application is linked and is not running; the hook is not called |
| `TFW_ERR_UNSUPPORTED` | the application runs and has no such `op`, or it has no commands at all (the stand-in `src/core/app_cmd_none.c` answers every `op` so) |

When each is called, what it may do and its budget are in
[engineering.md](engineering.md#lifecycle-and-timing).

### Configuration access

| Call | Returns |
|---|---|
| `tfw_app_config_read(off, buf, len)` | `TFW_ERR_RANGE` if `off + len > config_len` or `TFW_APP_CONFIG_BASE + off + len` passes the end of the EEPROM; otherwise `TFW_OK` for `len == 0`, with no EEPROM access, and the HAL EEPROM read's status for the rest |
| `tfw_app_config_write(off, buf, len)` | `TFW_ERR_RANGE` if `off + len > config_len` or `TFW_APP_CONFIG_BASE + off + len` passes the end of the EEPROM; otherwise `TFW_OK` for `len == 0`, with no EEPROM access, and the HAL EEPROM write's status for the rest |

`off` is relative to the region. The host writes the same bytes with
`EEPROM_WRITE` at `TFW_APP_CONFIG_BASE + off`.

### Capability bits

`TFW_CAPBIT_*` in `include/tfw/config.h`, the CAPS `Features` bit numbers:

| Bit | Name | Bit | Name |
|---|---|---|---|
| 0 | `TFW_CAPBIT_GPIO` | 8 | `TFW_CAPBIT_SPI` |
| 1 | `TFW_CAPBIT_SCHED` | 9 | `TFW_CAPBIT_ONEWIRE` |
| 2 | `TFW_CAPBIT_ANALOG` | 10 | `TFW_CAPBIT_ATTEST` |
| 3 | `TFW_CAPBIT_PWM` | 11 | `TFW_CAPBIT_DEADMAN` |
| 4 | `TFW_CAPBIT_TONE` | 12 | `TFW_CAPBIT_PROVISION` |
| 5 | `TFW_CAPBIT_PULSE` | 13 | `TFW_CAPBIT_ALIAS_CERT` |
| 6 | `TFW_CAPBIT_EEPROM` | 14 | `TFW_CAPBIT_AUTH_SESSION` |
| 7 | `TFW_CAPBIT_I2C` | 15 | `TFW_CAPBIT_ATTEST_CSR` |

### Headers an application may include

`include/tfw/*.h` and nothing else from this repository. In practice:
`app.h`, `hal.h`, `board.h`, `types.h`, `status.h`, `config.h` and
`progmem.h`, and `reader.h` and `writer.h` for an application with
`tfw_app_command` (the request's arguments and the reply). `crypto.h` and `boot.h` are public for the firmware's own layers;
an application has no business with keys or the bootloader and does not call
them.

## Knobs, `include/tfw/config.h`

| Knob | Default | Meaning |
|---|---|---|
| `TFW_APP_RUN_BUDGET_MS` | `2u` | a `run` that takes longer counts one overrun |
| `TFW_OTA_SLOTS` | `1u` | image slots this board's update path has; a board profile with A/B raises it |

## `app.mk`, in the application's directory

| Variable | Required | Meaning |
|---|---|---|
| `APP_NAME` | yes | `[a-z0-9][a-z0-9-]*`, at most 15 characters; names the output directory. Should equal the descriptor's `name` |
| `APP_SRCS` | yes | `.c` files, relative to the application directory |
| `APP_CFLAGS` | no | extra flags for the application's own files, such as `-I$(APP_DIR)/include` or a `-D` |
| `APP_COMMANDS` | no | empty, or `yes` for an application that defines `tfw_app_command`; any other value is refused before anything is compiled. Empty links `src/core/app_cmd_none.c`. `yes` without the hook is a missing symbol, and the hook without `yes` a duplicate one: both fail at link time |

## Make targets

| Command | Output |
|---|---|
| `make firmware APP_DIR=<path> BOARD=<board> [CRYPTO=<tier>]` | `build/<board>/<tier>-app-<name>/trusted-firmware.hex` |
| `make tfwsim APP_DIR=<path>` | `build/host/tfwsim-app-<name>` |
| `make tfwsim CRYPTO=atecc608a APP_DIR=<path>` | `build/host/tfwsim-atecc608a-app-<name>` |
| `make app-sdk-check` | builds and runs the example from outside the tree |
| `make app-firmware-check` | builds the example's firmware image for `BOARD` |
| `make app-header-check` | compiles each `include/tfw/` header alone against every board, with an application's flags |

`APP_DIR` may be absolute or relative to this repository. Make runs here, in
the trusted-firmware checkout, with `-C` from anywhere else.

## APP_INFO

Command `0x06`, issue #51. It is present only in an image built with an
`APP_DIR`. Every other image answers `TFW_ERR_UNKNOWN_CMD`, so a build with no
application is unchanged on the wire. It takes no payload. It is not a CAPS
bit: the command answering is the capability.

The reply is 63 bytes, little-endian:

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | runner state: 0 idle, 1 running, 2 failed, 3 unsupported, 4 stopped |
| 1 | 2 | overrun count, saturating at `0xFFFF` |
| 3 | 1 | `abi` |
| 4 | 1 | `ota_slots` |
| 5 | 1 | `version_major` |
| 6 | 1 | `version_minor` |
| 7 | 2 | `version_patch` |
| 9 | 2 | `config_len` |
| 11 | 4 | `required_caps` |
| 15 | 16 | `name`, NUL-padded |
| 31 | 32 | `profile`, NUL-padded |

The descriptor fields are reported as written, in every state. A host can then
say why a runner refused an application (`unsupported`), not only that it did.
The magic is left out. Every other check the runner makes can be read off
the reply and CAPS, so an `unsupported` reply whose fields all pass means the
magic was wrong.

APP_INFO is admitted over Ethernet. `profile` is what the device is onboarded
under ([architecture.md](architecture.md)), and the reply is read-only and
holds no secret. It is not authenticated. `profile` is what the device says it
runs; the binding to the code that says it is `FWHash`.

The vectors are `TFWV_APPINFO_*` in `testdata/vectors.h`, mirrored in
`sdk/go/internal/vectors`. `Device.AppInfo` decodes the reply in `sdk/go`.

## APP_CMD

Command `0x07`, issue #635, per
[adr-application-wire-commands.md](adr-application-wire-commands.md)
(accepted, owner ruling 1243). It is present exactly when APP_INFO is. The
request is `[app_op:1][args:0..63]` (`TFW_APP_CMD_MAX_ARGS` is 63); the reply
is what `tfw_app_command` writes. The firmware's handler,
`tfw_app_cmd_command`, checks that `app_op` is there and that the runner is
`RUNNING`, then calls the hook; the refusals are in [Hooks](#hooks).

APP_CMD is serial only: it is not on the Ethernet allow-list, so over
Ethernet it answers `TFW_ERR_UNKNOWN_CMD`.

The example application answers sub-op `0x00` with its blink count, a `u16`
little-endian, and takes no arguments. The vectors are `TFWV_APPCMD_*` in
`testdata/vectors.h`, mirrored in `sdk/go/internal/vectors`.
`Device.AppCommand(ctx, op, args)` sends it from `sdk/go`, refusing more than
63 bytes of `args` before anything is sent, and `tfwctl app-cmd <op>
[hex-args]` from the command line.

## Reserved for follow-ups

Still reserved, so no application relies on them yet:

- `tfw_status_t tfw_app_claims(uint8_t *buf, uint8_t cap, uint8_t *len)`,
  the application's EAT submodule claims
  ([architecture.md](architecture.md#evidence-claims)). Its only caller is
  the `submods` claim, which changes the evidence layout, so it is declared
  with that change. `tfw_app_evidence` covers the fixed-width case today and
  stays when it lands.
- The pin reservation, so that an application and a host do not drive the
  same pin, still proposed in
  [adr-application-pin-reservation.md](adr-application-pin-reservation.md).
- The device's answer to a `0x7F` request, open question 4 of
  [adr-application-wire-commands.md](adr-application-wire-commands.md).
