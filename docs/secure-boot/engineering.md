# ag-boot — engineering

> **Status: NORMATIVE** · Scope: arduino-generic · Last verified: 2026-09-23
> Conflicts with this document are defects. Fix them here first.

See [architecture.md](architecture.md) for component boundaries and data
flow. This is the contract: real names, grepped from `boot/` and
`include/ag/boot.h`.

## Exported surface

Decision (`boot/boot_policy.h`, `boot/boot_image.h`):

```c
ag_status_t ag_boot_evaluate(uint32_t floor, ag_boot_verdict_t *out);
ag_status_t ag_boot_check_pubkey_pin(const uint8_t pinned[AG_BOOT_MEASURE_LEN],
                                      uint8_t pubkey_out[AG_BOOT_PUBKEY_LEN]);
uint8_t     ag_boot_equal(const uint8_t *a, const uint8_t *b, uint8_t n);
ag_status_t ag_boot_trailer_parse(const uint8_t *raw, ag_boot_trailer_t *out);
void        ag_boot_trailer_serialise(const ag_boot_trailer_t *tr, uint8_t *raw);
ag_status_t ag_boot_rollback_permit(uint32_t version, uint32_t floor);
ag_status_t ag_boot_floor_advance(uint32_t version, uint32_t floor, uint32_t *out);
```

Backend seams, one implementation of each linked per image (`mk/boot.mk`):

```c
// boot/boot_measure.h
ag_status_t ag_boot_measure(uint32_t len, uint8_t out[AG_BOOT_MEASURE_LEN]);
void        ag_boot_hash(const uint8_t *data, uint16_t len, uint8_t out[AG_BOOT_MEASURE_LEN]);

// boot/boot_verify.h — VERIFY ONLY, no sign entry point exists on this contract
ag_status_t ag_boot_verify(const uint8_t *msg, uint16_t msglen,
                            const uint8_t sig[AG_BOOT_SIG_LEN],
                            const uint8_t pub[AG_BOOT_PUBKEY_LEN]);
ag_status_t ag_boot_pubkey(uint8_t out[AG_BOOT_PUBKEY_LEN]);

// boot/boot_cdi.h
ag_status_t ag_boot_cdi_init(void);
uint8_t     ag_boot_cdi_tier(void);
ag_status_t ag_boot_cdi_derive(const uint8_t measurement[AG_BOOT_MEASURE_LEN],
                                uint8_t cdi[AG_BOOT_MEASURE_LEN]);
ag_status_t ag_boot_floor_read(uint32_t *out);
ag_status_t ag_boot_floor_write(uint32_t value);

// boot/boot_recovery.h — never returns
void ag_boot_recovery(ag_boot_platform_t *plat, ag_status_t reason);
void ag_boot_hand_over(const ag_boot_verdict_t *v);
```

OTA state machine and session (`boot/boot_ota.h`, `boot/boot_session.h`, linked
only when `BOOT_OTA=1`):

```c
ag_status_t ag_ota_init(ag_ota_t *o, ag_ota_page_write_fn write_page, uint16_t page_size);
ag_status_t ag_ota_begin(ag_ota_t *o, uint32_t total_len);
ag_status_t ag_ota_data(ag_ota_t *o, uint32_t offset, const uint8_t *data, uint8_t len);
ag_status_t ag_ota_end(ag_ota_t *o);
void        ag_ota_abort(ag_ota_t *o);
uint8_t     ag_ota_state(const ag_ota_t *o);
uint32_t    ag_ota_received(const ag_ota_t *o);

ag_status_t ag_boot_session_handle(ag_ota_t *ota, uint8_t cmd,
                                    const uint8_t *payload, uint8_t plen,
                                    uint8_t *resp, uint8_t *rlen,
                                    ag_boot_session_action_t *action);
```

HAL contract (`boot/boot_hal.h`; `boot/hal/avr/boot_hal_avr.c` for the device,
`test/mock/boot_hal_mock.c` for the host — a function added here without a
mock update fails to link, not silently):

```c
void        ag_boot_flash_read(uint32_t addr, uint8_t *buf, uint8_t len);
ag_status_t ag_boot_flash_write_page(uint32_t addr, const uint8_t *data);
uint16_t    ag_boot_flash_page_size(void);
ag_status_t ag_boot_eeprom_read(uint16_t addr, uint8_t *buf, uint8_t len);
ag_status_t ag_boot_eeprom_write(uint16_t addr, const uint8_t *buf, uint8_t len);
void        ag_boot_uart_init(uint32_t baud);
uint8_t     ag_boot_uart_rx(uint8_t *out);      // never blocks
void        ag_boot_uart_tx(uint8_t byte);       // blocks
void        ag_boot_handoff_write(const uint8_t *block, uint8_t len);
void        ag_boot_jump_application(void);       // does not return
void        ag_boot_reset(void);                  // does not return
```

Types (`boot/boot_policy.h`, `include/ag/boot.h`, `boot/boot_ota.h`,
`boot/boot_session.h`):

```c
typedef struct { ag_boot_trailer_t trailer; uint8_t measurement[AG_BOOT_MEASURE_LEN]; } ag_boot_verdict_t;
typedef struct { uint32_t image_version, image_len; uint8_t measurement[32], signature[64]; } ag_boot_trailer_t;
typedef struct { uint8_t pubkey_hash[32]; uint32_t rollback_floor, boot_count;
                 uint8_t boot_state, ota_state; uint16_t fail_count; uint8_t serial[16]; } ag_boot_platform_t;
typedef enum { AG_OTA_IDLE, AG_OTA_RECEIVING, AG_OTA_COMPLETE, AG_OTA_FAILED } ag_ota_state_t;
typedef enum { AG_BOOT_SESSION_CONTINUE, AG_BOOT_SESSION_COMMIT, AG_BOOT_SESSION_RESET } ag_boot_session_action_t;
```

## Invariants

- **Link-time substitution, never a vtable.** `BOOT_TIER`, `BOOT_MEASURE`,
  `BOOT_SIG` and the recovery choice implied by `BOOT_OTA` each resolve to
  exactly one translation unit at link time. A function-pointer table would
  keep every backend's code live for `--gc-sections`, and none of the
  configurations fits with more than one signature backend linked.
- **Evaluation order in `ag_boot_evaluate` is fixed and load-bearing**: trailer
  parse, then rollback check, then measurement, then signature. The rollback
  check runs first because it is cheap and reads a version that sits inside
  the span the signature covers — a forged version can only make the function
  refuse, never accept, so checking it before the signature is safe. The
  measurement runs before the signature so a tampered image is refused
  without ever invoking the most expensive operation the bootloader performs.
- **The signed span is bytes `[0, AG_BOOT_TR_SIGNED_LEN)` of the trailer — the
  version included, not the measurement alone.** Signing the measurement only
  would leave `image_version` unauthenticated, letting an attacker relabel a
  genuine old image with a high version and defeat anti-rollback with a
  four-byte edit and no key.
- **The floor only advances after a verified *boot*, never after a verified
  update that did not run.** `ag_boot_floor_advance` is called from
  `ag_boot_hand_over`'s caller in `boot_main.c` and from `recovery_ota.c`'s
  commit path — both only after `ag_boot_evaluate` returned `AG_OK`.
- **No boot-section callable is exposed to the application.** The CDI crosses
  in `.noinit` SRAM, written once by `ag_boot_handoff_write` and read once by
  the application (`src/core/handoff.c`); there is no trampoline, because one
  BLS-callable entry point would void the BLB mode 3 read barrier that keeps
  the UDS out of the application's reach from the inside. The same block
  also carries the `MCUSR` value ag-boot's `.init3` observed before clearing
  it (handoff version 2, issue #18) — the one record of the reset cause that
  survives a bootloader running at all.
- **`.init3` runs before any C runtime work, in both images, from one shared
  file** (`src/hal/avr/hal_early.c`). It reads and clears `MCUSR` and disarms
  the watchdog before `.bss` is zeroed and long before `main()`. A watchdog
  reset leaves `WDE` set and the shortest prescaler on every classic AVR; a
  build that disarms it later never reaches the code that would, and the
  device bootloops until someone reaches it with ISP.
- **`AG_BOOT_FLOOR_MAX_STEP` (16) bounds one update's rollback advance.** On
  the element tier the floor is a monotonic counter with roughly 2,000,000
  increments for the device's life; an unbounded jump from one signed image
  could exhaust the whole budget in one update and leave the floor stuck with
  no way back.
- **`ag_boot_flash_write_page` refuses any page whose end reaches
  `AG_BOOT_APP_END`.** BLB0 mode 3 stops the *application* writing the boot
  section; it does not stop the boot section writing itself, since SPM
  executes from the boot section. The range check is the only thing that
  stops an over-long OTA image from overwriting the bootloader.
- **The rollback comparison and the floor arithmetic never subtract unsigned
  values to detect a downgrade.** `ag_boot_rollback_permit` compares
  `version < floor` directly — a difference wraps on any downgrade and reads
  as a huge positive number, which is the classic way this check passes its
  own happy-path test while being wrong.

## Error semantics — fail closed

`ag_boot_evaluate` returns `AG_OK` or the first reason it refuses, and every
non-`AG_OK` return reaches `ag_boot_recovery`, which never returns to the boot
path:

| Status | Source | Meaning |
|---|---|---|
| `AG_ERR_UNSUPPORTED` | `ag_boot_trailer_parse` | no trailer magic, or a trailer/EEPROM layout version this build does not know |
| `AG_ERR_RANGE` | `ag_boot_trailer_parse` | `image_len` is zero or reaches into the trailer |
| `AG_ERR_PROTECTED_REGION` | `ag_boot_rollback_permit` | `image_version` is below the floor — a downgrade |
| `AG_ERR_CRYPTO` | `ag_boot_evaluate` (measurement compare), `ag_boot_trailer_parse` (`image_version == 0`), `ag_boot_verify` | the flash does not hash to the recorded measurement, or the signature does not verify |
| `AG_ERR_NOT_PROVISIONED` | `ag_boot_check_pubkey_pin` | the bootloader's key is not the one reserved EEPROM pins, or (`ag_boot_platform_load`) the region was never provisioned |

`ag_boot_check_pubkey_pin` returning `AG_ERR_UNSUPPORTED` is **not** a
refusal reaching `ag_boot_evaluate`'s caller as an error — it means the
backend (the element, in stored mode) keeps its key and hands nothing over to
pin; `ag_boot_evaluate` reads that as a tier capability answer, passes `key =
NULL` to `ag_boot_verify`, and the element backend refuses a caller-supplied
key so a future caller cannot reintroduce one silently. The same shape
applies to `ag_boot_floor_read` on the software tier, which has no counter of
its own.

An unverified boot's only other path is `boot_state == AG_BOOT_STATE_STAGED`
in `boot_main.c` — an OTA that started and never finished — which is refused
with `AG_ERR_CRYPTO` before `ag_boot_evaluate` even runs, because jumping into
a half-erased application region (`0xFF`, which decodes as a long run of
`SBRS` on AVR) is not a decision `ag_boot_evaluate` needs to make twice.

There is no status meaning "boot anyway." Every reachable failure ends in
`ag_boot_recovery`, and both its backends (`recovery_halt.c`,
`recovery_ota.c`) never return.

## Configuration

Selected in `mk/boot.mk`, each resolving to exactly one linked file set:

| Variable | Values | Default | Selects |
|---|---|---|---|
| `BOOT_TIER` | `soft`, `atecc608a` | `soft` | `boot/tier/cdi_{soft,atecc}.c` |
| `BOOT_MEASURE` | `soft`, `atecc608a` | `soft` | `boot/measure/measure_{soft,atecc608a}.c` |
| `BOOT_SIG` | `ed25519-donna`, `atecc608a` | `ed25519-donna` | `boot/verify/verify_{ed25519_donna,atecc608a}.c` |
| `BOOT_OTA` | `0`, `1` | `1` | `recovery_ota.c` + the OTA/session sources, or `recovery_halt.c` alone |
| `BOARD` | a `boards/*/profile.mk` name | — | `BOOT_SECTION`, `BOOT_BUDGET`, `FLASH_TOTAL`, `RAM_TOTAL`, MCU |

`BOOT_BUDGET` equals `BOOT_SECTION` on every board profile
(`boards/*/profile.mk`) — the bootloader's flash budget is the whole boot
section, not a fraction of it, and it is **not raised to meet an image**: on
the ATmega328P and ATmega32U4, `BOOTSZ` at 4096 bytes is the part's largest
setting, so there is nothing to raise it to.

`mk/boot.mk`'s `BOOT_SUPPORTED` is the only combination set that fits and is
built by `make bootloader-check`:

```
atmega328p:atecc608a:atecc608a:atecc608a:0
atmega32u4:atecc608a:atecc608a:atecc608a:0
atmega2560:atecc608a:atecc608a:atecc608a:0
atmega2560:atecc608a:atecc608a:atecc608a:1
```

`ed25519-donna` never appears in a supported row: its verify path alone is
72,930 bytes against every boot section this family has. It stays buildable
and tested so that claim is a measurement, not an assertion — see
[cli.md](cli.md).

## Test strategy

Unit (`test/unit/boot/test_boot_{image,platform,measure,ota,ed25519,session,
policy,element}.c`, run by `make boot-test`) against
`test/mock/boot_hal_mock.c` on the host — no MCU, no flash, no EEPROM:

- `test_boot_policy.c` links the real Ed25519 primitive and a real RFC 8032
  key/signature over a real flash array: accept-signed, refuse-tampered,
  refuse-downgrade, against the actual decision function, not a description
  of it. It is deliberately excluded from the shared `BOOT_LOGIC_SRCS` link
  set because it is the one unit that calls the verify seam.
- `test_boot_element.c` links the software element model
  (`test/model/atecc608a.c`) and asserts the element-backed hash and verify
  paths, including that a caller-supplied key is refused.
- `test_boot_ed25519.c` asserts the primitive itself against RFC 8032 §7.1,
  valid and tampered.
- `test_boot_ota.c` covers the state machine's failure cases: a truncated
  transfer, a failing page write, an out-of-order offset.
- `test_boot_image.c` includes `test_a_relabelled_version_is_refused` — a
  genuine old image relabelled with a high version, refused because the
  version sits inside the signed span.

Integration (`test/integration/simavr/boot_harness.c`, `make
integration-test-boot`, part of `make ci`): the real `ag-boot.elf` and the
real application `.elf` on emulated `atmega2560` silicon with `BOOT_OTA=1`,
with an ATECC608A modelled below the driver on the emulated TWI bus — so the
element protocol arrives through `src/hal/avr/hal_bus.c`'s real state
machine rather than through a seam. It asserts things no status byte carries:
that a tampered image cost a full measurement and no `Verify` command, and
that a downgrade was refused before the element was asked to hash anything.

Reproducibility (`make bootloader-reproducible`, `make reproducible-check`):
two builds in different directories, byte-identical — stronger a requirement
here than for the application, because the bootloader is what a verifier must
be able to identify.

Size gate (`make boot-size-check`, a prerequisite of `make bootloader`; `make
bootloader-budget` and `make boot-core-size` for the breakdown): every
`BOOT_SUPPORTED` row is built and measured by `make bootloader-check`, part
of `make ci`, so a row that stops fitting is a CI failure rather than a
surprise at flash time.

## Observability

`ag-boot` has no logging and no metrics — a 4 KB image has no budget for
either. Its only outward signal is the wire protocol: `recovery_ota.c` emits
an `EVENT` frame carrying the entry reason (the `ag_status_t` that sent it to
recovery) once on entry, and another `EVENT` after every `OTA_END` commit
attempt carrying the result of `ag_boot_evaluate`. `recovery_halt.c` emits
nothing and is deliberately silent — on a 4 KB part with no OTA path, a
tampered image and a downgrade are indistinguishable from outside, which is
recorded as a cost in [architecture.md](architecture.md), not hidden.

`ag_boot_platform_t.boot_count`, `.boot_state` and `.fail_count` in reserved
EEPROM are the durable record a host can read back after the fact (through
provisioning tooling, not a runtime command — see
[../device-provisioning/user.md](../device-provisioning/user.md)).
