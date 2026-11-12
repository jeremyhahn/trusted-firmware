# OTA — engineering

> **Status: DRAFT** · Issue: #75, #77, #52, #48 · Scope: trusted-firmware · Last verified: 2026-10-22
> Covers the code on main. The design, the state table and the measured sizes
> are in [architecture.md](architecture.md); the decisions in the
> [ADR](adr-ota-image-format-and-trust.md).

## The row on main

The requirements of SPEC row `ota`, as met or unmet by the code on main, and
the file that meets each. The AVR rows first; the composed platforms after.

| Requirement | On main | Where |
|---|---|---|
| Every image carries trailer v1 with its board id; tfw-boot refuses another board's | met | `boot/boot_image.c` (parse, `tfw_boot_binding_permit`), `boot/boot_policy.c` |
| The build writes the trailer: the signed span with byte 6, then the sealed image | met | `tools/image-seal/`, `make image-tbs` and `make image-seal` (`mk/suit.mk`) |
| ES256 verification of the trailer on every tfw-boot row, soft and element | met | `boot/verify/`, `boot/boot_policy.c` |
| One trial boot within the per-board timeout, the 8 s watchdog armed before the jump | met | `boot/boot_trial.c`, `src/core/ota_trial.c`; through `boot/boot_main.c` in `test/unit/boot/test_boot_main.c` |
| A failed trial rolls back by itself; recovery is another OTA round | met | `boot/boot_trial.c`, `boot/boot_prev.c`, `boot/boot_swap.c` (2560); through `boot/boot_main.c`, with a reset before each write, in `test/unit/boot/test_boot_main.c` |
| An anti-rollback floor, advanced at confirm | met | `boot/boot_image.c` (`tfw_boot_rollback_permit`), `boot/boot_trial.c`, `src/core/ota_floor_*.c`; under the floor refused and the floor never lowered, in `test/unit/boot/test_boot_main.c` |
| A status code for the wrong OTA state | met | `include/tfw/status.h` (`TFW_ERR_BAD_STATE`, 19) |
| `OTA_INFO`, `OTA_ACTIVATE`, `OTA_CONFIRM` (0x6C to 0x6E) over serial, the last two behind `OTA_AUTH` | met | `src/core/ota_ctl.c`, `src/core/ota_activate.c`, `src/core/ota_auth.c`, `sdk/go/cmd/tfwctl/e2e_ota_test.go` |
| tfw-boot checks the `OTA_ACTIVATE` authorization at reset (#137) | met | `boot/boot_cose.c`, `boot/boot_trial.c` |
| On the 2560 tfw-boot confirms from the confirm request and advances the floor | met | `boot/boot_request.c`, `boot/boot_trial.c` |
| The application stages into the external store; tfw-boot checks it again and copies it | met | `src/core/ota_stage.c`, `boot/boot_stage.c` |
| The serial stage path end to end under simavr | met | `test/integration/simavr/boot_harness.c`, `sdk/go/cmd/tfwctl/ota_stage_simavr_test.go` |
| The network leg (W5100) end to end under simavr | met | `src/proto/link_eth.c`, `test/integration/simavr/boot_harness.c` |
| The 2560's A/B slots by swap-move | met | `boot/boot_swap.c` |
| The A/B on the generic Cortex-M port (`mps2-an505`, `mps2-an500`), the same swap-move | met | `boot/boot_swap.c`, `boot/hal/cortex-m/boot_factory.c`; under QEMU in `sdk/go/cmd/tfwctl/ota_ab_qemu_test.go` (CI) |
| Its slot geometry as boot-HAL data (ADR, "Cortex-M `tfw-boot` ports") | met | `boot/hal/cortex-m/boot_slots.h` derives the secondary and the write end from the primary, the application's link base (`APP_BASE` in `profile.mk`, `TFW_BOOT_APP_BASE`); `boot/hal/avr/boot_slots.h` names the 2560's values unchanged. `boot/boot_swap.c` and `boot_factory.c` read the slots only there; pinned by `test/unit/boot/test_boot_slots_mps2.c`, every `tfw-boot.hex` byte-identical |
| The store MARK cleared at fallback (item 56) | met | `boot/boot_stage.c` |
| The ATmega32U4 restore from the store, on a board or under simavr (the ATmega328P has none: owner queue 176, option (a), no boot-time rollback, a failed activation is recovered by reflash) | unmet | written, not run: see "What the simavr harness lacks for the 328P and 32U4" below. The row's boot image does not fit: 1 724 bytes over the 4 096-byte section, measured with `make bootloader-candidate` on the CI image, avr-gcc 14.2.0, at `9a338a82`. Whether the 32U4 keeps a restore goes to the owner (#52) |
| The SUIT-subset manifest checked before activation (ruling 3, amended by ruling 121): the ATmega2560 application, element tier | met | `src/core/ota_manifest.c` at `OTA_ACTIVATE`, before the authorization is written, through `tfw_suit_gate` (`src/core/suit.c`); `test/unit/test_ota_manifest.c`. The reader and the check cost 3 772 B (rec., against the ADR's 3 086 B estimate): app 28 802 of 65 536 with the check, 25 030 without ([README.md](README.md), "Measurements") |
| The build writes the envelope where `OTA_ACTIVATE` reads it: after the image, a u16 length then the envelope (ruling 109, 2026-10-22) | met | `tools/image-seal/` (`make image-seal` takes `make suit-envelope`'s `.suit` and refuses to run without it on a board that checks); `test/unit/test_image_seal.c` |
| The same, ATmega328P | unmet | the reader and the check cost 3 772 B on the ATmega2560 (rec., README.md "Measurements"; the ADR estimated 3 086 B); the reader alone puts the app at 22 153 against the 16 384 policy line (rec., ADR 6.3); the 328P stays trailer-only (ruling 3). Ruled off, not pending (owner queue 176, option (a)): `make` refuses `OTA_MANIFEST=1` on it (`mk/firmware.mk`; `scripts/test_manifest_refused.py`) |
| The same, ATmega32U4 | unmet | as the 328P: 3 772 B as measured on the ATmega2560 against a 14 336 budget the stager alone has not been measured under (the W5100 is named in `boards/atmega32u4/profile.mk`; the `ETH=1` image is not measured). Ruled off, not pending (owner queue 176, option (a)): `make` refuses `OTA_MANIFEST=1` on it |
| The same, soft tier (every board) | unmet | no stored key verifies the manifest's ES256 on the soft tier; it keeps the full trailer gate in tfw-boot (ruling 121). The reader and the check would add about 3 772 B (rec. on the ATmega2560 element tier, README.md "Measurements") besides a key home |
| `OTA_ACTIVATE` and `OTA_CONFIRM` over TCP | unmet | serial only; not on the Ethernet allow-list until a ruling admits them (owner item 48). The session scope is decided: a TCP session's authorization ends with its connection (ADR open question 8, settled under ruling 109, 2026-10-26), pinned by `test/unit/test_ota_hold_link.c` and `TestE2EOTAAuthSessionEndsWithItsConnection`. Open question below |
| tfw-boot checks the `OTA_CONFIRM` authorization at reset | met on the 2560 | `boot/boot_cose.c`, `boot/boot_request.c`: the 75-byte confirm request at EEPROM `0x0EA`–`0x134` (#231) |
| Every board reflashed with an image that carries its board id | unmet | unmet until all three are done. The ATmega2560: done, reflashed with a board-id trailer image ([`docs/development/bench/mega2560.md`](../development/bench/mega2560.md); the record is on a sibling branch in the same train). The ESP32-P4: flashed, trailer `TFIM` v1 with board id `0x80` verified by layer 0, no eFuse burned; the wire check (`caps`) waits on the board's bench address (the record is on a sibling branch in the same train). The Nano: not reflashed; no factory record and no answering serial bootloader ([`docs/development/bench/nano.md`](../development/bench/nano.md)), and the ISP read waits on the owner's cabling question ([`docs/development/isp-route.md`](../development/isp-route.md)) |
| ESP32: session, activate authorization at the trial boot, rollback at 60 s | met | `src/hal/esp32/hal_ota.c`, `boot/hal/esp32/boot_l0.c` |

### Open question: `OTA_ACTIVATE` and `OTA_CONFIRM` over TCP

Nothing is built for this until a ruling admits it.

- **What the allow-list says.** `tfw_link_admits` (`src/proto/link_eth.c`)
  admits, over TCP, `CAPS`, `APP_INFO`, the three attestation reads and the four
  staging ids `OTA_BEGIN` to `OTA_STATUS` (0x68 to 0x6B). The ADR's decision 8
  narrowed it this way (owner item 48 on #55). `OTA_AUTH` (0x61, 0x62),
  `OTA_ACTIVATE` (0x6D) and `OTA_CONFIRM` (0x6E) are serial only: over TCP they
  answer `TFW_ERR_UNKNOWN_CMD`, which `TestE2EOTAControlIsSerialOnly` pins. The
  harness's network leg stages over TCP and activates over the UART
  (`net_activate`).
- **What admitting them would change.**
  - `tfw_link_admits` gains three cases: `OTA_AUTH`, `OTA_ACTIVATE` and
    `OTA_CONFIRM`. Either both `OTA_AUTH` ids are admitted, or the authorization
    travels inside the activate and confirm frames, which is a wire change.
  - A TCP session's authorization ends when its connection closes (the ADR's
    open question 8, settled under ruling 109 on 2026-10-26). Otherwise a token
    taken on one connection could be spent on another. No auth state opens on
    a TCP connection today, so a close has nothing to clear;
    `test/unit/test_ota_hold_link.c` and
    `TestE2EOTAAuthSessionEndsWithItsConnection` pin that.
  - `TestE2EOTAControlIsSerialOnly` flips, and the harness's network leg could
    activate over TCP instead of the UART. New cases are needed: an activate
    over TCP refused without a session, and a session lost at connection
    close.
  - No command id and no status code changes. tfw-boot gains no byte, because
    it reads the same request record whichever link wrote it.
  - The ADR's requirement that every board update over the network (it names
    the 328P) is then met over Ethernet with no serial step.
- **Recommendation.** Admit all three, with a session bound to its connection
  (open question 8, now settled). Until then, a fleet update over
  Ethernet still needs a serial step on every device, and the network leg
  exists only to stage.

### What the simavr harness lacks for the 328P and 32U4

`boot_harness.c`'s `prev` mode is written: an unconfirmed trial restores `PREV`,
a refused pending image restores it, and a corrupt `PREV` halts. The harness now
takes from the board profile it is built with what used to be fixed to the 2560:

- **The run.** `make integration-test-boot-prev BOARD=<board>` runs the `prev`
  row for the board named. It refuses, before building, a board with no `PREV`
  in a store (`BOOT_AB=1`, no `OTA_STORE`, or `BOOT_PREV=0`, which is the 328P's
  by owner queue 176, option (a): no boot-time rollback there). It is not in `ci`.
  `scripts/test_boot_prev_target.py` pins it.
- **The store's chip select.** `attach_store` listens on `TFW_OTA_STORE_CS_IO`
  (`boot_harness_io.h`): PH5 on the 2560, PB0 on the 328P, PB4 on the 32U4.
- **The UART.** The harness listens on `TFW_UART_N` and paces on that USART's
  `UCSRnA`: USART0 at 0xC0 on the 2560 and the 328P, USART1 at 0xC8 on the 32U4.

`test/unit/boot/test_boot_harness_io.c`, built under each of the three profiles,
pins the select and the UART.

One thing is left, on the 32U4 (the 328P keeps no restore, so its row has no
`prev` run; its store row, without the restore, is not in `BOOT_SUPPORTED`:
at `cb104cf4`, `make bootloader-check` (avr-gcc 14.2.0) found it overflows its
4096-byte boot section (the part's largest `BOOTSZ`) by 1498 bytes, while the
base row fits at 3712 / 4096; activation on the 328P goes back to the owner
(#52)):

1. **No tfw-boot to load.** The 32U4's `BOOT_STORE=1` row is not in
   `BOOT_SUPPORTED`, because its boot image does not fit: it is 1 724 bytes
   over the 4 096-byte section, measured with `make bootloader-candidate` on
   the CI image, avr-gcc 14.2.0, at `9a338a82` ([architecture.md](architecture.md)).
   The row stays out of `BOOT_SUPPORTED`, and `integration-test-boot-prev`
   stays stopped at the boot budget gate. The CI image measures a
   candidate row, without adding it, with

   ```sh
   make bootloader-candidate BOOT_CANDIDATE=atmega32u4:atecc608a:atecc608a:atecc608a:0:1
   ```

   It prints the image against the 4 096-byte section, or ld's overflow and
   `boot-core-size`, and exits 0 only when the row fits.
   `scripts/test_boot_candidate_target.py` pins its arguments. A row that fits
   joins `BOOT_SUPPORTED` once `make bootloader-check` passes it; one that does
   not is recorded with its number and stays out. Until then
   `integration-test-boot-prev` stops at the boot budget gate.

The restore logic itself is covered on the host by
`test/unit/boot/test_boot_prev.c` (328P profile).

### The split gate, measured per row (ruling 121)

Owner ruling 121 (decision 106, option (b), CA m957, 2026-10-21) lets the
application check the manifest fields at `OTA_ACTIVATE` on a row where boot
cannot fit them. tfw-boot keeps the P-256 verify over image and trailer on every
row, and the soft row keeps the full gate in boot. The ruling asks for each
row's byte split before any code is written. This is that measurement.

**What "the field checks" are on AVR.** tfw-boot on AVR parses no SUIT
envelope; the application's manifest reader does (`src/core/suit.c`). The field
checks in boot are the trailer's own fields, in `boot/boot_image.c`:

- envelope: magic and layout version, in `tfw_boot_trailer_parse`;
- sequence: `image_version` not 0, in `tfw_boot_trailer_parse`;
- board id and image flags: `tfw_boot_binding_permit`;
- anti-rollback: `tfw_boot_rollback_permit` against the floor.

They run in `tfw_boot_evaluate_at` (`boot/boot_policy.c`) on every row, and again
in `boot/boot_stage.c` on the `BOOT_STORE=1` rows. The `image_len` bound in
`tfw_boot_trailer_parse` is not a field check: the measurement and the verify
depend on it, so it stays in boot on every row. Each check is a byte or
32-bit compare and a branch. With LTO they are inlined into their callers, so
the linked ELF has no symbol of their own.

**How each number was got.** "rec." is a figure recorded in the tree or
measured, with its source. The field-check column is measured at `48d85486`
with avr-gcc 14.2.0: the sum of the `avr-nm --size-sort -S` sizes of
`tfw_boot_binding_permit` (30), `tfw_boot_rollback_permit` (32) and
`tfw_boot_trailer_parse` (238; 242 on the soft row) from each row's
`make bootloader-budget` build, the command after the table. That is 300 B per
call site (304 on the soft row), times the number of call sites. It is an upper
bound: `tfw_boot_trailer_parse` also holds the `image_len` bound, which stays
in boot, and inlining removes the call overhead. The verify-only and headroom
columns are the full-gate figure less that bound, so they are bounds too. The
application column is still an estimate, "est.": `bootloader-budget` does not
measure the application.

| Row (`BOARD:TIER:MEASURE:SIG:OTA:STORE`) | Boot, full gate | Field checks in boot | Boot, verify only | Headroom vs BOOTSZ, verify only | Application: field checks at `OTA_ACTIVATE`, headroom vs `TFW_FLASH_BUDGET` | Gate |
|---|---|---|---|---|---|---|
| `atmega328p:atecc608a:atecc608a:atecc608a:0:0` | 3 746 / 4 096, rec. (`mk/boot.mk`, avr-gcc 14.2.0); `architecture.md` records 3 924 | ≤ 300, rec. (`48d85486`, avr-gcc 14.2.0), one call site | ≥ 3 446 | ≤ 650 | no OTA in boot, nothing to stage: n/a | full, fits |
| `atmega32u4:atecc608a:atecc608a:atecc608a:0:0` | 3 816 / 4 096, rec. (`mk/boot.mk`) | ≤ 300, rec. (`48d85486`, avr-gcc 14.2.0) | ≥ 3 516 | ≤ 580 | n/a, as the 328P | full, fits |
| `atmega2560:atecc608a:atecc608a:atecc608a:0:0` | 3 906 / 8 192, rec. (`mk/boot.mk`) | ≤ 300, rec. (`48d85486`, avr-gcc 14.2.0) | ≥ 3 606 | ≤ 4 586 | n/a | full, fits |
| `atmega2560:atecc608a:atecc608a:atecc608a:1:0` | 7 954 / 8 192, rec. (`architecture.md`, lane-verify, with the confirm request) | ≤ 300, rec. (`48d85486`, avr-gcc 14.2.0) | ≥ 7 654 | ≤ 538 | ≤ 100, est.; app about 24 445 / 65 536 rec. (`architecture.md`) | full, fits |
| `atmega2560:atecc608a:atecc608a:atecc608a:0:1` | 6 492 / 8 192, rec. (`architecture.md`, lane-verify) | ≤ 600, rec. (`48d85486`, avr-gcc 14.2.0), two call sites | ≥ 5 892 | ≤ 2 300 | ≤ 100, est.; app as above | full, fits |
| `atmega2560:soft:soft:p256-soft:0:0` | 8 064 / 8 192, rec. (`mk/boot.mk`) | ≤ 304, rec. (`48d85486`, avr-gcc 14.2.0) | not built: the soft row keeps the full gate | 128 with the full gate, rec. | n/a | full, by ruling 121 |
| `mps2-an505:soft:soft:p256-soft:0:0` | 32 KB region, not measured here | not measured | not built: soft row | not measured | n/a | full, by ruling 121 |
| `atmega328p:atecc608a:atecc608a:atecc608a:0:1` (with the `PREV` restore, now dropped: the row links no restore, is not in `BOOT_SUPPORTED`, and measured 1498 bytes over without it) | 5 814 / 4 096: 1 718 over, rec. (`make bootloader-candidate` on the CI image, slice 6) | ≤ 600, rec. (`48d85486`, avr-gcc 14.2.0, the `-noprev` build), two call sites | ≥ 5 214 | ≥ 1 118 over | ≤ 100, est.; app about 19 267 with `ETH=1` and the stager, already over the 16 384 policy line, rec. (`architecture.md`) | does not fit either way |
| `atmega32u4:atecc608a:atecc608a:atecc608a:0:1` (candidate) | 5 804 / 4 096: 1 708 over, rec. (as above) | ≤ 600, rec. (`48d85486`, avr-gcc 14.2.0), two call sites | ≥ 5 204 | ≥ 1 108 over | ≤ 100, est.; app not measured (`ETH=1` builds with the W5100 `profile.mk` names; not measured), budget 14 336 | does not fit either way; not in `BOOT_SUPPORTED` |

**The result.** No row needs the split:

- Every `BOOT_SUPPORTED` row already fits with the full gate.
- The two rows that do not fit, the 328P and 32U4 store rows with the restore,
  are 1 718 and 1 708 bytes over (the 32U4's, 1 724 at `9a338a82`; the 328P's restore is dropped since, owner
  queue 176, option (a)), and the 328P store row without it measured 1498 bytes
  over at `cb104cf4`. Moving the field checks out recovers 600 bytes at
  most, measured (two call sites of 300), so both stay more than 1 100 bytes
  over, and the 328P store row without the restore about 900.

So no row splits, and no code was written for the split. Ruling 121's split
gives the 4 KB parts about a third of what they lack, at most. Restoring from
`PREV` on those parts needs a different answer, and that is the owner's to give.

A split would also leave boot unable to refuse a downgrade on its own. Without
the floor check in boot, an image that reached `MARK` without going through
`OTA_ACTIVATE`, written by the application to the store, is installed whatever
its version, provided its signature verifies. If a later row ever needs the
split, that trade-off goes to the owner along with the numbers.

**Exact numbers, on the CI image.** For each row, set its six fields and run:

```sh
make bootloader-budget BOARD=<b> BOOT_TIER=<t> BOOT_MEASURE=<m> BOOT_SIG=<g> BOOT_OTA=<o> BOOT_STORE=<s>
avr-nm --size-sort -S build/<b>/boot/<t>-<m>-<g>-ota<o>-store<s>/measure/boot_boot_image.c.o
```

`bootloader-budget` compiles `boot/boot_image.c` without LTO and with
`-ffunction-sections`. `avr-nm` then prints `tfw_boot_binding_permit`,
`tfw_boot_rollback_permit` and `tfw_boot_trailer_parse` at their own sizes,
which bound the inlined cost from above. The 328P store row builds into
`ota0-store1-noprev`. The full-gate column comes from
`make bootloader-check`, and for the two candidates from
`make bootloader-candidate BOOT_CANDIDATE=<row>`. The application column can be
measured only once checks exist at `OTA_ACTIVATE`: it is the difference in
`make firmware BOARD=<b> ETH=1` (`size-check`) between the two commits.
Slice s8 put them there on the 2560's element tier (`src/core/ota_manifest.c`),
so the 2560 figure is now `make firmware BOARD=atmega2560 CRYPTO=atecc608a ETH=1`
before and after its feature commit, on the CI image; the "≤ 200, est." stands
until then.

## Files

| File | Role |
|---|---|
| `src/core/ota_stage.c`, `src/core/ota_stage.h` | Application stager: `OTA_BEGIN`, `OTA_DATA`, `OTA_END`, `OTA_STATUS` into the external store's `STAGE` area; checks the trailer, board id and SHA-256 at `OTA_END`, then writes `MARK` |
| `src/core/ota_manifest.c`, `src/core/ota_manifest_none.c` | `OTA_ACTIVATE`'s manifest check (ruling 121): reads the envelope after the staged image and holds it to the staged trailer through `tfw_suit_gate`. The stub, which answers `TFW_OK`, on every stager that does not link the reader |
| `src/core/ota_stage_none.c` | Linked instead where no stager is built: a no-op `tfw_stage_init`, which `main.c` calls on every image |
| `src/hal/esp32/hal_ota.c` | The ESP32's stager and `OTA_INFO` on ESP-IDF's `esp_ota` (#48): the inactive app slot, ESP-IDF's image check at `OTA_END`, nothing that activates ([api.md](api.md), "ESP32") |
| `src/store/spi_nor.c` | The store driver the board's `OTA_STORE` names |
| `src/core/commands.def` | The four `TFW_CMD_ENTRY` rows, under `TFW_FEATURE_OTA_STAGE` |
| `src/proto/link_eth.c` | `tfw_link_admits`: the four ids are on the Ethernet allow-list |
| `boot/boot_stage.c`, `boot/boot_stage_none.c` | tfw-boot reads `MARK`, checks the staged image again and copies it into the destination slot; the stub where `BOOT_STORE=0` |
| `boot/boot_swap.c`, `boot/boot_swap_none.c` | The 2560's A/B swap-move and its EEPROM journal; the stub on every other row |
| `boot/boot_trial.c` | The one trial: `PENDING`, `TRIAL`, `CONFIRMED`, `FAILED`, and the floor at settle |
| `test/model/spi_nor_model.c` | NOR part model on the mock's SPI bus, shared by the unit tests, `tfwsim` and the simavr harness |
| `tools/image-seal/` | The build's sealer for tfw-boot rows, built per board: emits trailer v1's 48-byte signed span (board id from the board's profile) for go-xkms to sign, then wraps the signature into the trailer and writes the stream `tfwctl ota-stage` sends and the region tfw-boot's serial session writes. On a board that checks the manifest the stream carries `make suit-envelope`'s envelope after the image, a u16 length first. Encodes and reads back through `boot/boot_image.c`; holds no key |
| `sdk/go/ota.go` | `OTABegin`, `OTAData`, `OTAEnd`, `OTAStatus`, `StageImage` |

## Gating by object list

No `#if` outside the four allowed places selects any of this.

- **Application** (`mk/firmware.mk`). `OTA_STAGE` defaults to `ETH` where the
  board's `profile.mk` names an `OTA_STORE`, and to `0` otherwise. With
  `OTA_STAGE=1` the build links `ota_stage.c`, the store driver, `sha256.c` and
  `hal_bus.c`, and passes `-DTFW_FEATURE_OTA_STAGE=1`, which only
  `commands.def` reads. Every stager links `ota_activate.c` and, where the
  board links the SUIT reader (`OTA_MANIFEST := 1` on the element tier, the
  2560 today), `ota_manifest.c`; elsewhere `ota_manifest_none.c`.
  Otherwise it links `ota_stage_none.c`. `OTA_STAGE=1` on
  a board with no `OTA_STORE` is a build error. A serial image with the stager
  gets the `-stage` suffix; an Ethernet image without it gets `-nostage`.
  An architecture whose update path is its own names it in `ARCH_OTA_SRCS`
  (`mk/arch/esp32.mk`: `src/hal/esp32/hal_ota.c`). That file is then the
  stager on every image, in place of `ota_stage.c` and the store, with
  `-DTFW_FEATURE_OTA_STAGE=1 -DTFW_FEATURE_OTA_INFO=1` and no
  `TFW_FEATURE_OTA_ACTIVATE`.
- **Boot** (`mk/boot.mk`). `BOOT_STORE` (default `0`) links `boot_stage.c` or
  `boot_stage_none.c`. `boot_swap.c` is linked where the board has the slots
  (`BOOT_AB` in `profile.mk`) and the row is `BOOT_OTA=1` or `BOOT_STORE=1`.
  `boot_trial.c` is linked on every row. `BOOT_STORE` is the sixth field of a
  `BOOT_SUPPORTED` row ([secure-boot engineering](../secure-boot/engineering.md)),
  and `make bootloader` refuses `BOOT_OTA=1 BOOT_STORE=1` on the 2560.

## Tests

| Target | Covers |
|---|---|
| `make test` (`test_ota_stage`) | The stager against the NOR model: begin, data, end, status, and each refusal |
| `make boot-test` (`test_boot_stage`, `test_boot_stage_2560`, `test_boot_swap`, `test_boot_trial`, `test_boot_main`) | The copy, a cut copy, the swap and every cut point, rollback, confirm, the floor; listed in [architecture.md](architecture.md). `test_boot_main` runs tfw-boot's own `boot/boot_main.c`, its `main` renamed by the link, on the host board's in-place row: arm before the watchdog and the jump, an unconfirmed trial failed at the next reset, a settle, the floor refusals, and a reset before each write |
| `make integration-test-host` (`sdk/go/cmd/tfwctl`, `TestEthernetStagesAnImage`, `TestEthernetStagerRefusesALengthNotWholePages`, `TestE2EOTAStageResumesAfterALostFrame`) | `tfwctl ota-stage` (`Upload`) against `tfwsim` over serial and TCP, and its going on from `OTA_STATUS`'s count after a lost `OTA_DATA`, or on a board with `make hw-test HW_ETH=`; needs `make tfwsim` |
| `make boot-test` (`test_boot_prev`, `test_boot_settle`), `make host-test` (`test_ota_stage`) | The 328P/32U4 fall-back writes `TFW_BOOT_EE_FLAG_REFUSED` after a refusal and clears it after an unconfirmed trial; the flags byte stays inside the block's CRC; `OTA_INFO` reports it as flags bit 1 beside `FAILED` only (no 328P/32U4 image links `OTA_INFO` yet, and the 2560 does not write the bit) |
| `make host-test` (`test_image_seal`) | The sealer against the 2560 tfw-boot with p256-soft: the span is the wire vector's layout, a sealed region boots, the floor and a changed byte refuse it, the stream installs as the region, the largest image boots, and a span or signature for another image is refused; the envelope's length bytes and bytes after the image, as literals, and the page it can add |
| `make host-test` (`test_ota_manifest`) | `OTA_ACTIVATE`'s manifest check on a 2560 element stager, from `testdata/suit_vectors.h`: a good envelope marks; no envelope, a length of zero or past the padding, a flipped signature byte, another board's class id, an image the manifest does not name and a sequence that is not the trailer's version each leave the store unmarked |
| `make test` (`test_esp_ota`) | The ESP32 session against a mock `esp_ota` (`test/mock/esp_ota_mock.c`): every accept path and refusal, `OTA_INFO`, and nothing that activates |
| `make esp32-qemu-test` (`test/integration/esp32/qemu_test.py`) | Over Ethernet in QEMU: a tampered app refused at `OTA_END`, the app written to `ota_1`, `ota_0` and `otadata` untouched, and the next boot still on `ota_0` |
| `make integration-test-boot` | The real tfw-boot and application under simavr on two 2560 rows, `BOOT_OTA=1` and the store row; the cases are in [architecture.md, "The OTA round under simavr"](architecture.md#the-ota-round-under-simavr-75-77) |

The simavr suite needs simavr and avr-gcc. It runs one fixed row per board
(`BOOT_INT_BOARD`, `BOOT_INT_ROW` in `mk/boot.mk`) whatever `BOARD` the caller
set.

## Sizes

The boot sizes come from `make bootloader-check`, which builds every
`BOOT_SUPPORTED` row and checks it against the boot section. The application
sizes come from `make firmware`'s size check. `make bootloader-budget` breaks
tfw-boot down by part. The current numbers are in
[architecture.md](architecture.md) and
[secure-boot/cli.md](../secure-boot/cli.md); this page does not copy them.
