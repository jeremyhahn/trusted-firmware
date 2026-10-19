# OTA — engineering

> **Status: DRAFT** · Issue: #75, #52, #48 · Scope: trusted-firmware · Last verified: 2026-10-18
> Covers the code on main. The design, the state table and the measured sizes
> are in [architecture.md](architecture.md); the decisions in the
> [ADR](adr-ota-image-format-and-trust.md).

## Files

| File | Role |
|---|---|
| `src/core/ota_stage.c`, `src/core/ota_stage.h` | Application stager: `OTA_BEGIN`, `OTA_DATA`, `OTA_END`, `OTA_STATUS` into the external store's `STAGE` area; checks the trailer, board id and SHA-256 at `OTA_END`, then writes `MARK` |
| `src/core/ota_stage_none.c` | Linked instead where no stager is built: a no-op `tfw_stage_init`, which `main.c` calls on every image |
| `src/hal/esp32/hal_ota.c` | The ESP32's stager and `OTA_INFO` on ESP-IDF's `esp_ota` (#48): the inactive app slot, ESP-IDF's image check at `OTA_END`, nothing that activates ([api.md](api.md), "ESP32") |
| `src/store/spi_nor.c` | The store driver the board's `OTA_STORE` names |
| `src/core/commands.def` | The four `TFW_CMD_ENTRY` rows, under `TFW_FEATURE_OTA_STAGE` |
| `src/proto/link_eth.c` | `tfw_link_admits`: the four ids are on the Ethernet allow-list |
| `boot/boot_stage.c`, `boot/boot_stage_none.c` | tfw-boot reads `MARK`, checks the staged image again and copies it into the destination slot; the stub where `BOOT_STORE=0` |
| `boot/boot_swap.c`, `boot/boot_swap_none.c` | The 2560's A/B swap-move and its EEPROM journal; the stub on every other row |
| `boot/boot_trial.c` | The one trial: `PENDING`, `TRIAL`, `CONFIRMED`, `FAILED`, and the floor at settle |
| `test/model/spi_nor_model.c` | NOR part model on the mock's SPI bus, shared by the unit tests, `tfwsim` and the simavr harness |
| `sdk/go/ota.go` | `OTABegin`, `OTAData`, `OTAEnd`, `OTAStatus`, `StageImage` |

## Gating by object list

No `#if` outside the four allowed places selects any of this.

- **Application** (`mk/firmware.mk`). `OTA_STAGE` defaults to `ETH` where the
  board's `profile.mk` names an `OTA_STORE`, and to `0` otherwise. With
  `OTA_STAGE=1` the build links `ota_stage.c`, the store driver, `sha256.c` and
  `hal_bus.c`, and passes `-DTFW_FEATURE_OTA_STAGE=1`, which only
  `commands.def` reads. Otherwise it links `ota_stage_none.c`. `OTA_STAGE=1` on
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
| `make boot-test` (`test_boot_stage`, `test_boot_stage_2560`, `test_boot_swap`, `test_boot_trial`) | The copy, a cut copy, the swap and every cut point, rollback, confirm, the floor; listed in [architecture.md](architecture.md) |
| `make integration-test-host` (`sdk/go/tfwsim`, `TestAgainstFirmwareStagesAnImage`, `TestAgainstFirmwareStagerRefuses`) | `StageImage` against `tfwsim` over serial and TCP; needs `make tfwsim` |
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
