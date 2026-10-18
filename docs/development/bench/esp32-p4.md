# Bench board: ESP32-P4 PoE

The factory record for the ESP32-P4 board (`boards/esp32p4-poe`), written
before the project's first write to it under the owner's ruling of 2026-10-16
(CA m346) and the record-only order of 2026-10-17 (CA m522): every bench board
is the project's to flash, provision and reconfigure once its factory state is
on its page. Nothing was written to the board for this record. The gate is
lifted in full (owner ruling 2026-10-18, CA m623). Before any write, a lane
reads the board again and compares it with this page; the steps that burn an
eFuse or enable secure boot or flash encryption are owner decisions
([`../../multi-arch/esp32-p4-design.md`](../../multi-arch/esp32-p4-design.md) §4).
The part has 32 MB of flash; the board profile builds for 16 MB
(`boards/esp32p4-poe/sdkconfig.defaults`), which bench slice 7 settles.

| | |
|---|---|
| Board | ESP32-P4 PoE Ethernet board with an ESP32-C6 co-processor, USB through a WCH `1a86` "USB Single Serial" bridge, `/dev/ttyACM1` on the bench host |
| Chip | ESP32-P4, revision v3.2, dual core + LP core, 400 MHz, 40 MHz crystal |
| Flash | 32 MB, JEDEC manufacturer `0xC8` (GigaDevice), device `0x4019` |
| PSRAM | eFuse `PSRAM_CAP` 2, `PSRAM_VENDOR` 1 |
| Tool | esptool / espefuse v5.6.0 (pip, in a venv outside the repository) |

## The reads, 2026-10-17

All read-only. The image and the eFuse dump are kept outside the repository,
in the bench host's per-board record directory, not in this tree.

```sh
esptool --port /dev/ttyACM1 chip-id
esptool --port /dev/ttyACM1 flash-id
espefuse --port /dev/ttyACM1 summary
esptool --port /dev/ttyACM1 -b 921600 read-flash 0 ALL p4-flash-factory.bin
```

| What | SHA-256 |
|---|---|
| Whole flash, `0x0000000`–`0x1FFFFFF`, 33 554 432 bytes | `c2d4ac0f25246b053d4d25f62580578a5f2b2a37002f7cda7a9d3a548702fde0` |

The read took 499 s at 921600 baud (537.7 kbit/s effective).

## Security eFuses

No security eFuse is burned. The board is as Espressif ships a bare chip:

| eFuse | State |
|---|---|
| `SECURE_BOOT_EN` | not burned |
| `SPI_BOOT_CRYPT_CNT` (flash encryption) | `0b000`, disabled |
| `KEY_PURPOSE_0`–`5` | `USER`; `BLOCK_KEY0`–`5` all zero, read- and write-enabled |
| `SECURE_BOOT_KEY_REVOKE0`–`2` | not burned |
| `DIS_DOWNLOAD_MODE`, `ENABLE_SECURITY_DOWNLOAD` | not burned |
| `DIS_PAD_JTAG`, `DIS_USB_JTAG`, `SOFT_DIS_JTAG` | not burned / 0 |
| `WR_DIS`, `RD_DIS` | 0: no eFuse is write- or read-protected |
| `SECURE_VERSION` | 0 |
| `BLOCK_USR_DATA`, `CUSTOM_MAC` | all zero |

An eFuse cannot be put back. `make board-restore` never writes one; this table
is what the board had before the project's first eFuse write, for comparison
only.

## The factory flash

### Partition table (`0x8000`)

| Name | Type | Subtype | Offset | Size |
|---|---|---|---|---|
| `nvsfactory` | data | nvs | `0x009000` | `0x032000` (200 KB) |
| `nvs` | data | nvs | `0x03B000` | `0x0D2000` (840 KB) |
| `otadata` | data | ota | `0x10D000` | `0x002000` (8 KB) |
| `phy_init` | data | phy | `0x10F000` | `0x001000` (4 KB) |
| `factory` | app | factory | `0x110000` | `0x900000` (9 MB) |
| `storage` | data | spiffs (`0x82`) | `0xA10000` | `0x500000` (5 MB) |

One application slot and no `ota_0`/`ota_1`: the factory layout cannot take an
OTA update as shipped. The restore writes this table back with the rest of the
flash, whatever layout the project's images used in between.

### The bootloader's and the application's identity

Read from the ESP-IDF descriptors in each image's first segment.

| Image | Offset | Descriptor |
|---|---|---|
| Second-stage bootloader | `0x2000` | `esp_bootloader_desc_t` version 1, ESP-IDF `v5.5.4-dirty`, built `Jun  4 2026 11:15:04` |
| Application (`factory`) | `0x110000` | `esp_app_desc_t`: project `esp-brookesia`, version `1`, ESP-IDF `v5.5.4-dirty`, built `Jun  4 2026 11:14:56` |

The vendor's demo is Espressif's ESP-Brookesia UI framework, built from a
modified (`-dirty`) ESP-IDF v5.5.4 tree. This project pins v5.5.5
(`ESP_IDF_VERSION` in `mk/arch/esp32.mk`). A `-dirty` build cannot be rebuilt
byte for byte from a published tree, so the restore uses the image read off
the board, not a rebuild.

## Restore

```sh
make board-restore BOARD=esp32p4-poe ESP_PORT=/dev/ttyACM1 BOARD_RESTORE_IMAGE=<record dir>/p4-flash-factory.bin
```

| | |
|---|---|
| Image | the whole-flash read above, 33 554 432 bytes |
| Checked | size against `FACTORY_FLASH_BYTES`, SHA-256 against `FACTORY_FLASH_SHA256`, both in `boards/esp32p4-poe/profile.mk` |
| Written | `esptool write-flash --flash-size keep 0x0 <image>`; esptool checks each region's hash after the write |
| eFuses | never touched |

The target refuses an image of the wrong size or hash before it opens the port.
It restores the flash only: anything the project burns into eFuses (a secure
boot key digest, the HMAC key for the DS peripheral, flash encryption) stays,
and an image that does not match those eFuses will not boot. Restore before
burning any of them, or record here what was burned.
