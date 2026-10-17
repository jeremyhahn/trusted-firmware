# tfw-boot — engineering

> **Status: NORMATIVE** · Scope: trusted-firmware · Last verified: 2026-09-23
> Conflicts with this document are defects. Fix them here first.

See [architecture.md](architecture.md) for component boundaries and data
flow. This is the contract: real names, grepped from `boot/` and
`include/tfw/boot.h`.

## Exported surface

Decision (`boot/boot_policy.h`, `boot/boot_image.h`):

```c
tfw_status_t tfw_boot_evaluate(uint32_t floor, tfw_boot_verdict_t *out);
tfw_status_t tfw_boot_check_pubkey_pin(const uint8_t pinned[TFW_BOOT_MEASURE_LEN],
                                      uint8_t pubkey_out[TFW_BOOT_PUBKEY_LEN]);
uint8_t     tfw_boot_equal(const uint8_t *a, const uint8_t *b, uint8_t n);
tfw_status_t tfw_boot_trailer_parse(const uint8_t *raw, tfw_boot_trailer_t *out);
void        tfw_boot_trailer_serialise(const tfw_boot_trailer_t *tr, uint8_t *raw);
tfw_status_t tfw_boot_binding_permit(const tfw_boot_trailer_t *tr, uint8_t board_id);
tfw_status_t tfw_boot_rollback_permit(uint32_t version, uint32_t floor);
tfw_status_t tfw_boot_floor_advance(uint32_t version, uint32_t floor, uint32_t *out);
```

Backend seams, one implementation of each linked per image (`mk/boot.mk`):

```c
// boot/boot_measure.h
tfw_status_t tfw_boot_measure(uint32_t len, uint8_t out[TFW_BOOT_MEASURE_LEN]);
void        tfw_boot_hash(const uint8_t *data, uint16_t len, uint8_t out[TFW_BOOT_MEASURE_LEN]);

// boot/boot_verify.h — VERIFY ONLY, no sign entry point exists on this contract
tfw_status_t tfw_boot_verify(const uint8_t *msg, uint16_t msglen,
                            const uint8_t sig[TFW_BOOT_SIG_LEN],
                            const uint8_t pub[TFW_BOOT_PUBKEY_LEN]);
tfw_status_t tfw_boot_pubkey(uint8_t out[TFW_BOOT_PUBKEY_LEN]);

// boot/boot_cdi.h
tfw_status_t tfw_boot_cdi_init(void);
uint8_t     tfw_boot_cdi_tier(void);
tfw_status_t tfw_boot_cdi_derive(const uint8_t measurement[TFW_BOOT_MEASURE_LEN],
                                uint8_t cdi[TFW_BOOT_MEASURE_LEN]);
tfw_status_t tfw_boot_floor_read(uint32_t *out);
tfw_status_t tfw_boot_floor_write(uint32_t value);

// boot/boot_recovery.h — never returns
void tfw_boot_recovery(tfw_boot_platform_t *plat, tfw_status_t reason);
void tfw_boot_hand_over(const tfw_boot_verdict_t *v);

// Same loop, entered by the application's request, bounded: returns when any of
// idle_polls / idle_bytes (restarted by accepted OTA frames) or max_polls /
// max_bytes (never restarted) is spent, or on the first OTA_END that does not
// verify. 0 disables a bound. recovery_halt.c (BOOT_OTA=0) returns at once.
typedef struct { uint32_t idle_polls, idle_bytes, max_polls, max_bytes; }
    tfw_boot_session_limits_t;
void tfw_boot_session_open(tfw_boot_platform_t *plat,
                          const tfw_boot_session_limits_t *lim);

// boot/boot_request.h — the update-session request (include/tfw/boot.h)
void    tfw_boot_request_encode(uint8_t *region);
uint8_t tfw_boot_request_valid(const uint8_t *region, tfw_reset_cause_t cause);
uint8_t tfw_boot_request_take(tfw_reset_cause_t cause);   // clears, then reports
```

OTA state machine and session (`boot/boot_ota.h`, `boot/boot_session.h`, linked
only when `BOOT_OTA=1`):

```c
tfw_status_t tfw_ota_init(tfw_ota_t *o, tfw_ota_page_write_fn write_page, uint16_t page_size);
tfw_status_t tfw_ota_begin(tfw_ota_t *o, uint32_t total_len);
tfw_status_t tfw_ota_data(tfw_ota_t *o, uint32_t offset, const uint8_t *data, uint8_t len);
tfw_status_t tfw_ota_end(tfw_ota_t *o);
void        tfw_ota_abort(tfw_ota_t *o);
uint8_t     tfw_ota_state(const tfw_ota_t *o);
uint32_t    tfw_ota_received(const tfw_ota_t *o);

tfw_status_t tfw_boot_session_handle(tfw_ota_t *ota, uint8_t cmd,
                                    const uint8_t *payload, uint8_t plen,
                                    uint8_t *resp, uint8_t *rlen,
                                    tfw_boot_session_action_t *action);
```

The one trial (`boot/boot_trial.h`, linked in every image; pure over the
platform block, so the caller stores the result; a transition in the wrong
state answers `TFW_ERR_BAD_STATE` and changes nothing — see
[../ota/architecture.md](../ota/architecture.md#what-runs-today-77)):

```c
tfw_boot_reset_action_t tfw_boot_trial_at_reset(const tfw_boot_platform_t *p);
tfw_status_t tfw_boot_trial_arm(tfw_boot_platform_t *p);      // PENDING -> TRIAL
tfw_status_t tfw_boot_trial_fail(tfw_boot_platform_t *p);     // TRIAL or PENDING -> TRIAL, FAILED
tfw_status_t tfw_boot_trial_settle(tfw_boot_platform_t *p, uint32_t version,
                                   uint32_t floor, uint32_t *next); // CONFIRMED -> GOOD
// no wire id yet (decision 8's ids are proposed): reached from tests only
tfw_status_t tfw_boot_trial_confirm(tfw_boot_platform_t *p);  // TRIAL -> CONFIRMED
tfw_status_t tfw_boot_trial_activate(tfw_boot_platform_t *p,
                                     const uint8_t measurement[TFW_BOOT_MEASURE_LEN],
                                     const uint8_t *failed);  // VERIFIED -> PENDING
tfw_status_t tfw_boot_trial_begin_permit(const tfw_boot_platform_t *p);
```

HAL contract (`boot/boot_hal.h`; `boot/hal/avr/boot_hal_avr.c` for the device,
`test/mock/boot_hal_mock.c` for the host — a function added here without a
mock update fails to link, not silently):

```c
void        tfw_boot_flash_read(uint32_t addr, uint8_t *buf, uint8_t len);
tfw_status_t tfw_boot_flash_write_page(uint32_t addr, const uint8_t *data);
uint16_t    tfw_boot_flash_page_size(void);
tfw_status_t tfw_boot_nvm_read(uint16_t addr, uint8_t *buf, uint8_t len);
tfw_status_t tfw_boot_nvm_write(uint16_t addr, const uint8_t *buf, uint8_t len);
void        tfw_boot_uart_init(uint32_t baud);
uint8_t     tfw_boot_uart_rx(uint8_t *out);      // never blocks
void        tfw_boot_uart_tx(uint8_t byte);       // blocks
void        tfw_boot_handoff_write(const uint8_t *block, uint8_t len);
void        tfw_boot_jump_application(void);       // does not return
void        tfw_boot_reset(void);                  // does not return
// and TFW_BOOT_TICK_HZ, a constant from $(BOOT_HAL_DIR)/boot_tick.h
```

Types (`boot/boot_policy.h`, `include/tfw/boot.h`, `boot/boot_ota.h`,
`boot/boot_session.h`):

```c
typedef struct { tfw_boot_trailer_t trailer; uint8_t measurement[TFW_BOOT_MEASURE_LEN]; } tfw_boot_verdict_t;
typedef struct { uint32_t image_version, image_len; uint8_t measurement[32], signature[64]; } tfw_boot_trailer_t;
typedef struct { uint8_t pubkey_hash[32]; uint32_t rollback_floor, boot_count;
                 uint8_t boot_state, ota_state; uint16_t fail_count; uint8_t serial[16]; } tfw_boot_platform_t;
typedef enum { TFW_OTA_IDLE, TFW_OTA_RECEIVING, TFW_OTA_COMPLETE, TFW_OTA_FAILED } tfw_ota_state_t;
typedef enum { TFW_BOOT_SESSION_CONTINUE, TFW_BOOT_SESSION_COMMIT, TFW_BOOT_SESSION_RESET } tfw_boot_session_action_t;
```

## Invariants

- **Link-time substitution, never a vtable.** `BOOT_TIER`, `BOOT_MEASURE`,
  `BOOT_SIG` and the recovery choice implied by `BOOT_OTA` each resolve to
  exactly one translation unit at link time. A function-pointer table would
  keep every backend's code live for `--gc-sections`, and none of the
  configurations fits with more than one signature backend linked.
- **Evaluation order in `tfw_boot_evaluate` is fixed and load-bearing**: trailer
  parse, then rollback check, then measurement, then signature. The rollback
  check runs first because it is cheap and reads a version that sits inside
  the span the signature covers — a forged version can only make the function
  refuse, never accept, so checking it before the signature is safe. The
  measurement runs before the signature so a tampered image is refused
  without ever invoking the most expensive operation the bootloader performs.
- **The signed span is bytes `[0, TFW_BOOT_TR_SIGNED_LEN)` of the trailer — the
  version included, not the measurement alone.** Signing the measurement only
  would leave `image_version` unauthenticated, letting an attacker relabel a
  genuine old image with a high version and defeat anti-rollback with a
  four-byte edit and no key.
- **The floor only advances after a confirm, never at staging or on trial**
  (ADR decision 5). `tfw_boot_floor_advance` is called only from
  `tfw_boot_trial_settle`, which `boot_main.c` runs through `tfw_boot_settle`
  at the reset that finds `CONFIRMED`, after `tfw_boot_evaluate` returned
  `TFW_OK` for the image that confirmed. A verified `OTA_END` arms the trial and
  leaves the floor where it was.
- **`GOOD` is written only once the floor's authority holds the new value.**
  `tfw_boot_floor_store` returns `TFW_OK` only then: from the element, or from
  EEPROM when the element answers `TFW_ERR_UNSUPPORTED` (there is none) and the
  EEPROM copy was written. A block this boot did not load is refused with
  `TFW_ERR_BAD_STATE`, a failed EEPROM write is returned, and the block's
  in-RAM floor stays the old one (#75). Any
  other element error is returned and EEPROM is not written, so
  `tfw_boot_settle` leaves the block `CONFIRMED`, opens no update session, and
  the next reset retries the advance (#82).
- **Nothing is verified against a floor that is not the authority's.**
  `tfw_boot_floor_load` has the store's contract: the EEPROM copy only when the
  element answers `TFW_ERR_UNSUPPORTED`, the element's error otherwise, and the
  element tier reports any failed read -- the part's `0x0F` included, which the
  driver maps to `TFW_ERR_UNSUPPORTED` -- as `TFW_ERR_CRYPTO`. On that error
  `main()` goes to recovery with it, unverified and without writing the block,
  and the recovery `OTA_END` refuses the image with it rather than evaluating
  (#82).
- **No transfer starts over an unsettled confirm.** `tfw_boot_trial_begin_permit`
  refuses `OTA_BEGIN` with `TFW_ERR_BAD_STATE` while the block is `CONFIRMED`,
  as it does while an image is pending. Recovery holds `CONFIRMED` when the
  floor read failed before the settle; the first page write would replace it
  with `STAGED` and lose the advance, so an older signed image could pass
  `OTA_END` once the read recovered. A reset retries the settle, and a transfer
  is accepted after it has written `GOOD` (#82).
- **A platform block that did not load is never written.** `main()` hands a
  zeroed block to recovery when the stored one is torn or unprovisioned;
  `tfw_boot_platform_store` refuses to write it back, so recovery cannot turn a
  CRC failure into a block of zeros.
- **No boot-section callable is exposed to the application.** The CDI crosses
  in `.noinit` SRAM, written once by `tfw_boot_handoff_write` and read once by
  the application (`src/core/handoff.c`); there is no trampoline, because one
  BLS-callable entry point would void the BLB mode 3 read barrier that keeps
  the UDS out of the application's reach from the inside. The same block
  also carries the reset cause tfw-boot's `.init3` observed before clearing
  `MCUSR`, already decoded to `tfw_reset_cause_t` by the HAL (handoff version 3,
  issues #18 and #10) — the one record of the reset cause that
  survives a bootloader running at all.
- **`.init3` runs before any C runtime work, in both images, from one shared
  file** (`src/hal/avr/hal_early.c`). It reads and clears `MCUSR` and disarms
  the watchdog before `.bss` is zeroed and long before `main()`. A watchdog
  reset leaves `WDE` set and the shortest prescaler on every classic AVR; a
  build that disarms it later never reaches the code that would, and the
  device bootloops until someone reaches it with ISP.
- **`TFW_BOOT_FLOOR_MAX_STEP` (16) bounds one update's rollback advance.** On
  the element tier the floor is a monotonic counter with roughly 2,000,000
  increments for the device's life; an unbounded jump from one signed image
  could exhaust the whole budget in one update and leave the floor stuck with
  no way back.
- **`tfw_boot_flash_write_page` refuses any page whose end reaches
  `TFW_BOOT_APP_END`.** BLB0 mode 3 stops the *application* writing the boot
  section; it does not stop the boot section writing itself, since SPM
  executes from the boot section. The range check is the only thing that
  stops an over-long OTA image from overwriting the bootloader.
- **The rollback comparison and the floor arithmetic never subtract unsigned
  values to detect a downgrade.** `tfw_boot_rollback_permit` compares
  `version < floor` directly — a difference wraps on any downgrade and reads
  as a huge positive number, which is the classic way this check passes its
  own happy-path test while being wrong.

## Error semantics — fail closed

`tfw_boot_evaluate` returns `TFW_OK` or the first reason it refuses, and every
non-`TFW_OK` return reaches `tfw_boot_recovery`, which never returns to the boot
path:

| Status | Source | Meaning |
|---|---|---|
| `TFW_ERR_UNSUPPORTED` | `tfw_boot_trailer_parse` | no trailer magic, or a trailer/EEPROM layout version this build does not know |
| `TFW_ERR_UNSUPPORTED` | `tfw_boot_binding_permit` | the trailer's `board_id` is 0 or not this build's `TFW_BOARD_ID`, or `image_flags` sets a reserved bit — checked before the floor, so nothing is written |
| `TFW_ERR_RANGE` | `tfw_boot_trailer_parse` | `image_len` is zero or reaches into the trailer |
| `TFW_ERR_PROTECTED_REGION` | `tfw_boot_rollback_permit` | `image_version` is below the floor — a downgrade |
| `TFW_ERR_CRYPTO` | `tfw_boot_evaluate` (measurement compare), `tfw_boot_trailer_parse` (`image_version == 0`), `tfw_boot_verify` | the flash does not hash to the recorded measurement, or the signature does not verify |
| `TFW_ERR_NOT_PROVISIONED` | `tfw_boot_check_pubkey_pin` | the bootloader's key is not the one reserved EEPROM pins, or (`tfw_boot_platform_load`) the region was never provisioned |
| `TFW_ERR_BAD_STATE` | `boot_main.c` (`tfw_boot_trial_at_reset`) | a reset found the trial still running: the image never confirmed and does not run again; recovery is another OTA round |
| `TFW_ERR_BAD_STATE` | `recovery_ota.c` (`tfw_boot_trial_begin_permit`), as an `OTA_BEGIN` response | an image is pending its trial |

`tfw_boot_check_pubkey_pin` returning `TFW_ERR_UNSUPPORTED` is **not** a
refusal reaching `tfw_boot_evaluate`'s caller as an error — it means the
backend (the element, in stored mode) keeps its key and hands nothing over to
pin; `tfw_boot_evaluate` reads that as a tier capability answer, passes `key =
NULL` to `tfw_boot_verify`, and the element backend refuses a caller-supplied
key so a future caller cannot reintroduce one silently. The same shape
applies to `tfw_boot_floor_read` on the software tier, which has no counter of
its own.

An unverified boot's only other path is `boot_state == TFW_BOOT_STATE_STAGED`
in `boot_main.c` — an OTA that started and was never armed for its trial,
whether it stopped mid-transfer or after the last page — which is refused
with `TFW_ERR_CRYPTO` before `tfw_boot_evaluate` even runs, because jumping into
a half-erased application region (`0xFF`, which decodes as a long run of
`SBRS` on AVR) is not a decision `tfw_boot_evaluate` needs to make twice.

There is no status meaning "boot anyway." Every reachable failure ends in
`tfw_boot_recovery`, and both its backends (`recovery_halt.c`,
`recovery_ota.c`) never return.

## Configuration

Selected in `mk/boot.mk`, each resolving to exactly one linked file set:

| Variable | Values | Default | Selects |
|---|---|---|---|
| `BOOT_TIER` | `soft`, `atecc608a` | `soft` | `boot/tier/cdi_{soft,atecc}.c` |
| `BOOT_MEASURE` | `soft`, `atecc608a` | `soft` | `boot/measure/measure_{soft,atecc608a}.c` |
| `BOOT_SIG` | `ed25519-donna`, `atecc608a` | `ed25519-donna` | `boot/verify/verify_{ed25519_donna,atecc608a}.c` |
| `BOOT_OTA` | `0`, `1` | `1` | `recovery_ota.c` + the OTA/session sources, or `recovery_halt.c` alone |
| `BOARD` | a `boards/*/profile.mk` name | — | `BOOT_BASE`, `BOOT_SECTION`, `BOOT_BUDGET`, `FLASH_TOTAL`, `RAM_TOTAL`, MCU |

`BOOT_BUDGET` equals `BOOT_SECTION` on every board profile
(`boards/*/profile.mk`) — the bootloader's flash budget is the whole boot
section, not a fraction of it, and it is **not raised to meet an image**: on
the ATmega328P and ATmega32U4, `BOOTSZ` at 4096 bytes is the part's largest
setting, so there is nothing to raise it to.

`mk/boot.mk`'s `BOOT_SUPPORTED` is the only combination set that fits and is
built by `make bootloader-check` (`BOARD:BOOT_TIER:BOOT_MEASURE:BOOT_SIG:BOOT_OTA:BOOT_STORE`):

```
atmega328p:atecc608a:atecc608a:atecc608a:0:0
atmega32u4:atecc608a:atecc608a:atecc608a:0:0
atmega2560:atecc608a:atecc608a:atecc608a:0:0
atmega2560:atecc608a:atecc608a:atecc608a:1:0
atmega2560:atecc608a:atecc608a:atecc608a:0:1
```

`BOOT_STORE=1` links the external store's read, check and copy (issue #75);
it is off by default and in no 4 KB row.

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
integration-test-boot`, part of `make ci`): the real `tfw-boot.elf` and the
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

`tfw-boot` has no logging and no metrics — a 4 KB image has no budget for
either. Its only outward signal is the wire protocol: `recovery_ota.c` emits
an `EVENT` frame carrying the entry reason (the `tfw_status_t` that sent it to
recovery) once on entry, and another `EVENT` after every `OTA_END` commit
attempt carrying the result of `tfw_boot_evaluate`. `recovery_halt.c` emits
nothing and is deliberately silent — on a 4 KB part with no OTA path, a
tampered image and a downgrade are indistinguishable from outside, which is
recorded as a cost in [architecture.md](architecture.md), not hidden.

`tfw_boot_platform_t.boot_count`, `.boot_state` and `.fail_count` in reserved
EEPROM are the durable record a host can read back after the fact (through
provisioning tooling, not a runtime command — see
[../device-provisioning/user.md](../device-provisioning/user.md)).
