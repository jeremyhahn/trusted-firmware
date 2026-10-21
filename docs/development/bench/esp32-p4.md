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
The part has 32 MB of flash and the board profile builds for 32 MB
(`boards/esp32p4-poe/sdkconfig.defaults`); its partitions stay in the first
16 MB ("Pins" below says why).

| | |
|---|---|
| Board | Waveshare ESP32-P4-WIFI6-POE-ETH ("Pins" below says how this is known): ESP32-P4 PoE Ethernet board with an ESP32-C6 co-processor, USB through a WCH `1a86` "USB Single Serial" bridge, `/dev/ttyACM1` on the bench host |
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

## Pins

Source: Waveshare's *ESP32-P4-WIFI6-POE-ETH Schematic*, listed by title and
URL in [`../../reference/README.md`](../../reference/README.md) ("Board
sources"; no licence is published, so no copy). The board is that model by
inference, because the record did not note the silkscreen. The schematic's
flash is a GD25Q256 (32 MB, JEDEC `0xC8` `0x4019`, as read above), its USB
bridge a CH343P (WCH `1a86`), its PHY an IP101GRI, its co-processor an
ESP32-C6-MINI-1U, and the vendor's ESP-Brookesia demo is the factory app.
The ESP32-P4-NANO, its smaller sibling, has a GD25Q128 (16 MB): that does not
match this part, and is the likely origin of the profile's old 16 MB. The
vendor's wiki lists "16MB Nor Flash" in its feature list and 32 MB in its
hardware description; the schematic and the read agree on 32 MB.

`U11` is the IP101GRI, `P6` the 40-pin header.

| GPIO | Profile | Schematic |
|---|---|---|
| 28, 29, 30 | RMII `CRS_DV`, `RXD0`, `RXD1` | `U11` pins 19, 18, 17 |
| 34, 35 | RMII `TXD0`, `TXD1` | `U11` pins 9, 8; 35 is also the BOOT key (a strap) |
| 49 | RMII `TX_EN` | `U11` pin 5 |
| 50 | RMII clock in | `U11` pins 10 and 14 (`50M_CLKI`, `50M_CLKO`): the PHY makes 50 MHz from its 25 MHz crystal `Y3`, so the EMAC's clock is external |
| 31, 52 | MDC, MDIO | `U11` pins 22, 23 |
| 51 | PHY reset | `U11` pin 32 |
| — | PHY address 1 | `PHY_AD0` (`U11` pin 11) pulled up through `R75`, `PHY_AD3` (pin 12) down through `R72`, the other straps fitted NC |
| 54 | C6 enable | `R54` to the C6's `CHIP_PU` |
| 14–19 | C6 SDIO (reserved) | C6 module pins 26, 27, 28, 29, 25, 24 |
| 37, 38 | UART0 console (reserved) | the CH343P and header `H2` |
| 7, 8 | I2C SDA, SCL | `P6` pins 4, 6; also the ES8311 codec, header `H1` and the camera and display connectors |
| 46, 47, 48 | SPI MOSI, MISO, SCK | `P6` pins 35, 38, 34 |
| 2, 3, 4, 5 | LEDC 0–3 | `P6` pins 22, 20, 17, 15 |
| 6 | digital, tone | `P6` pin 16; also `R52` to the C6's `IO2` (the C6 is held in reset) |
| 20, 21, 22, 23 | ADC1 4–7 | `P6` pins 14, 12, 11, 8 |
| 32, 33 | digital | `P6` pins 25, 30 |
| 53 | digital | `P6` pin 36; also `R71` to `PA_CTRL`, the NS4150B speaker amplifier's enable |

Also on the board, and so reserved: the ES8311's I2S (9–13), the SD card
(39–44, and 45 its power switch `Q1`), the 32 kHz crystal (0, 1), the strap 36,
and USB full-speed 24–27, which `P6` brings out but the profile keeps. The
board has no user LED.

The vendor's own Ethernet example (`waveshareteam/ESP32-P4-Platform`,
`examples/esp-idf/11_ethernetbasic`, its `ethernet_init` Kconfig) defaults to
the same MDC 31, MDIO 52, reset 51 and PHY address 1.

Not confirmed here: the model, until the silkscreen is read; PHY address 1,
which follows from the straps but needs the IP101GRI's strap table, not in
this repository, so the first link-up at HIL-0 confirms it; the ADC1 channel
of each GPIO, which is the chip's map, not the board's.

**Flash size.** The profile builds for 32 MB (`CONFIG_ESPTOOLPY_FLASHSIZE_32MB`).
In ESP-IDF v5.5.5 that turns on the bootloader's 32-bit flash addressing
(`BOOTLOADER_FLASH_32BIT_ADDR`, `components/bootloader/Kconfig.projbuild`);
mapping an app through the cache above 16 MB is a separate, experimental
option (`BOOTLOADER_CACHE_32BIT_ADDR_QUAD_FLASH`) left off. So
`partitions.csv` is unchanged: the table at `0x10000`, the slots ending at
`0xC20000`. The v3.2 chip needs no revision override: v5.5.5's P4 defaults
accept v3.1 to v3.99 (`components/esp_hw_support/port/esp32p4/Kconfig.hw_support`).

## HIL-0

The first boot of this firmware on the board. It writes flash only and **no
eFuse**: the overlay turns off the two ESP-IDF features whose first boot burns
one, layer 0 burns none (with no `HMAC_UP` key it hands off tier `none`),
`esptool write-flash` never touches an eFuse, and `espefuse` is run for
`summary` only. The lane names its serial port as `ESP_PORT`.

**Gate.** Design slice 2 (trailer v1, layer 0's ES256 check and the key pin
baked into `tfw_plat`) is on this branch, so the image step 3 writes has its
signature checked at every boot, as every tfw-boot row must (CA m453). Step 3
now waits for two things only: the coordinator's board time, and the network.
The network half is met once the bench host reaches the board's network
directly, which is where step 2's static address puts the board (step 4).
Steps 1 and 2 need neither the network nor a write.

The overlay is [`boards/esp32p4-poe/sdkconfig.hil0`](../../../boards/esp32p4-poe/sdkconfig.hil0):
`CONFIG_SECURE_BOOT=n`, `CONFIG_SECURE_FLASH_ENC_ENABLED=n`, and
`CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT=y` with the RSA scheme. The last
keeps the app built signed and with the `--secure-pad-v2` zero segment that
`l0_seal.py` grows for trailer v1 (it refuses an image without it) while
nothing on the part enables secure boot; the bootloader does not check the
app's RSA signature (`CONFIG_SECURE_SIGNED_ON_BOOT_NO_SECURE_BOOT=n`). Layer
0's ES256 check is not a Kconfig setting, and the file leaves it on. The tier is
`CRYPTO=none` on the build line.

1. **Re-read and compare.** Into a fresh directory outside the repository:

   ```sh
   esptool --port $ESP_PORT chip-id
   esptool --port $ESP_PORT flash-id
   espefuse --port $ESP_PORT summary
   esptool --port $ESP_PORT -b 921600 read-flash 0 ALL p4-flash-before.bin
   sha256sum p4-flash-before.bin
   ```

   Revision v3.2, flash `0xC8` `0x4019`, every row of "Security eFuses"
   unchanged, and the SHA-256 equal to the record's. Any difference stops the
   lane before a write.

2. **Build.**

   ```sh
   make esp32-build BOARD=esp32p4-poe CRYPTO=none \
     ESP_SDKCONFIG_EXTRA="boards/esp32p4-poe/sdkconfig.hil0 $HOME/p4-static.defaults"
   ```

   `ESP_SDKCONFIG_EXTRA` takes a space-separated list, applied in the order
   given after the board's `sdkconfig.defaults`; a file that does not exist
   stops the build with its name. The second file is the board's
   static-address overlay. It lives in `$HOME` on the bench host and never
   enters this tree. Its form, with placeholders for the values
   (`mk/arch/esp32/main/Kconfig.projbuild` declares the keys):

   ```
   CONFIG_TFW_ETH_STATIC_IP=y
   CONFIG_TFW_ETH_IP_ADDR="<the board's address>"
   CONFIG_TFW_ETH_NETMASK="<the bench network's netmask>"
   ```

   `CONFIG_TFW_ETH_GATEWAY` is left unset. The board then keeps the Kconfig
   default, a documentation address that nothing is routed through, because
   the bench host is on the board's network. An empty string would not do:
   `hal_eth.c` refuses an address it cannot parse, and the link never comes up.

   Output in `build/esp32/esp32p4-poe-esp32p4-none/`. Its `sdkconfig` must say
   secure boot and flash encryption are not set, `CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT=y`,
   and `CONFIG_ESPTOOLPY_FLASHSIZE_32MB=y`.

3. **Flash these four, erase the four data sectors, nothing else.** From
   that build directory:

   ```sh
   esptool --chip esp32p4 --port $ESP_PORT erase-region 0x12000 0x5000
   esptool --chip esp32p4 --port $ESP_PORT write-flash \
     0x2000  idf/bootloader/bootloader.bin \
     0x10000 idf/partition_table/partition-table.bin \
     0x11000 tfw_plat.bin \
     0x20000 trusted-firmware.bin
   ```

   `tfw_plat.bin` and `trusted-firmware.bin` are both written by step 2's
   `esp32-build` (`mk/arch/esp32-targets.mk`). `tfw_plat.bin` is the platform
   block `tfwprov --bake-eeprom` bakes with the pin of the image key
   `l0_seal.py keyslot` compiles into the bootloader, and with platform-bake's
   neutral test serial unless `SERIAL=` is given (never a device's in this
   tree). `trusted-firmware.bin` here, not `idf/trusted-firmware.bin`, is the
   app `l0_seal.py seal` sealed with trailer v1 and the SUIT envelope; the one
   under `idf/` has no trailer, and layer 0 refuses it.

   `tfw_auth` (`0x16000`) is not written: it holds the record of an
   `OTA_AUTH`, and layer 0 reads it only for an app on trial after an
   `OTA_ACTIVATE` (`boot/hal/esp32/boot_l0.c`). A write-flashed app is not on
   trial. The erase covers it with `tfw_id`, `tfw_eeprom` and `otadata`
   (`0x12000` to `0x16FFF`) because in the factory layout those sectors lie
   inside `nvsfactory` and still hold its bytes. With `otadata` erased the
   bootloader boots `ota_0`; with `tfw_auth` erased there is no stale record;
   with `tfw_eeprom` erased the EEPROM capability reads blank. The restore in
   step 5 writes the whole factory image back over all of it.

4. **Checks.** The console (UART0, through the CH343P) shows layer 0's line
   that the part has no UDS, then the IP101 link up with the address the
   board took (`link up, address …, wire on tcp/21574`). That address is the
   one in step 2's static-address overlay, and the line is the check that it
   took: a different address, or no link-up line, stops the lane. The bench
   host reaches the board's network directly, with no gateway between them.
   Take the address from the overlay, and never write it into this tree:

   ```sh
   P4_ADDR=<CONFIG_TFW_ETH_IP_ADDR from the overlay, as on the link-up line>
   cd test/integration/esp32 && go build -o ../../../build/esp32/wirecheck ./wirecheck && cd -
   build/esp32/wirecheck -addr $P4_ADDR:21574 -board 0x80 -tier 0
   ```

   Over the Ethernet link `wirecheck` round-trips a PING, checks CAPS (board
   id `0x80`, tier 0, `none`) and reads SYS_INFO; it exits non-zero on any
   failure.

5. **Restore and re-read.**

   ```sh
   make board-restore BOARD=esp32p4-poe ESP_PORT=$ESP_PORT BOARD_RESTORE_IMAGE=<record dir>/p4-flash-factory.bin
   esptool --port $ESP_PORT -b 921600 read-flash 0 ALL p4-flash-after.bin
   sha256sum p4-flash-after.bin
   espefuse --port $ESP_PORT summary
   ```

   The SHA-256 equals the record's, and the eFuses are still as in step 1.

### Runs

**2026-10-18, step 1 only: positive.** Read only, `ESP_PORT=/dev/ttyACM1`,
esptool / espefuse v5.6.0 (pip, in a venv outside the repository). Nothing
was written or erased, and no `espefuse` command other than `summary` was run.
The outputs are kept in the bench host's record directory for the run, not in
this tree. The board matches the factory record on every point step 1 checks.

| Command | Result |
|---|---|
| `esptool --port $ESP_PORT chip-id` | `Chip type: ESP32-P4 (revision v3.2)`, `Dual Core + LP Core, 400MHz`, `Crystal frequency: 40MHz` |
| `esptool --port $ESP_PORT flash-id` | `Manufacturer: c8`, `Device: 4019`, `Detected flash size: 32MB` |
| `espefuse --port $ESP_PORT summary` | every row of "Security eFuses" unchanged: `SECURE_BOOT_EN` False; `SPI_BOOT_CRYPT_CNT` `Disable (0b000)`; `KEY_PURPOSE_0`–`5` `USER`, `BLOCK_KEY0`–`5` all zero, R/W; `SECURE_BOOT_KEY_REVOKE0`–`2` False; `DIS_DOWNLOAD_MODE`, `ENABLE_SECURITY_DOWNLOAD` False; `DIS_PAD_JTAG`, `DIS_USB_JTAG` False, `SOFT_DIS_JTAG` 0; `WR_DIS` 0, `RD_DIS` 0; `SECURE_VERSION` 0; `BLOCK_USR_DATA`, `CUSTOM_MAC` all zero |
| `esptool --port $ESP_PORT -b 921600 read-flash 0 ALL p4-flash-before.bin` | `Read 33554432 bytes from 0x00000000 in 499.2 seconds (537.8 kbit/s)` |
| `sha256sum p4-flash-before.bin` | `c2d4ac0f25246b053d4d25f62580578a5f2b2a37002f7cda7a9d3a548702fde0`, equal to the record's |

The SHA-256 of each command's output as kept (the summary and the chip reads
carry the part's MAC, so they stay outside the tree):

| Output | SHA-256 |
|---|---|
| `chip-id` | `6eea01f4be669e42ab8d3a0ef40a7babd73b225f1fc6aa007d54d27987226324` |
| `flash-id` | `4c4b0c1416a28bbfcb7b2723ec7c0204ecec96ecdbbc33f4f3cafd3ddc84b805` |
| `espefuse summary` | `279d40b49e4969fabe75b9ce01c34f1771b36099ec3bbf8c73affc4f9a62831d` |

Steps 2 to 5 were not run.

**2026-10-19, steps 1 and 2: positive; the network gate closed, so nothing
was written.** `ESP_PORT=/dev/ttyACM1`, esptool / espefuse v5.6.0 (pip, in a
venv outside the repository). Nothing was written or erased, and no `espefuse`
command other than `summary` was run, so step 5 had nothing to restore. The
outputs are kept in the bench host's record directory for the run, not in this
tree.

| Step | Command | Result |
|---|---|---|
| 1 | `esptool --port $ESP_PORT chip-id` | `Chip type: ESP32-P4 (revision v3.2)`; output byte-identical to 2026-10-18's, SHA-256 `6eea01f4be669e42ab8d3a0ef40a7babd73b225f1fc6aa007d54d27987226324` |
| 1 | `esptool --port $ESP_PORT flash-id` | `Manufacturer: c8`, `Device: 4019`, `Detected flash size: 32MB`; byte-identical, `4c4b0c1416a28bbfcb7b2723ec7c0204ecec96ecdbbc33f4f3cafd3ddc84b805` |
| 1 | `espefuse --port $ESP_PORT summary` | byte-identical to 2026-10-18's, `279d40b49e4969fabe75b9ce01c34f1771b36099ec3bbf8c73affc4f9a62831d`, so every row of "Security eFuses" is unchanged |
| 1 | `esptool --port $ESP_PORT -b 921600 read-flash 0 ALL p4-flash-before.bin` | `Read 33554432 bytes from 0x00000000 in 506.7 seconds (529.8 kbit/s)` |
| 1 | `sha256sum p4-flash-before.bin` | `c2d4ac0f25246b053d4d25f62580578a5f2b2a37002f7cda7a9d3a548702fde0`, equal to the record's; kept as the restore image |
| 2 | ESP-IDF `v5.5.5` from Espressif's tag, `install.sh esp32p4`, then `idf_tools.py install cmake ninja` | `idf.py --version`: `ESP-IDF v5.5.5`; `cmake version 3.30.2`. On Linux `install.sh` leaves cmake and ninja to the host, and `esp32-build` stops with `"cmake" must be available on the PATH` until they are installed |
| 2 | `make esp32-build BOARD=esp32p4-poe CRYPTO=none ESP_SDKCONFIG_EXTRA=boards/esp32p4-poe/sdkconfig.hil0` (no static-address overlay: the gate was closed) | `sealed …/trusted-firmware.bin: board 0x80, version 1, image_len 524272`; `sdkconfig`: `# CONFIG_SECURE_BOOT is not set`, `# CONFIG_SECURE_FLASH_ENC_ENABLED is not set`, `CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT=y`, `CONFIG_ESPTOOLPY_FLASHSIZE_32MB=y`, `# CONFIG_TFW_ETH_STATIC_IP is not set` |

The build's artefacts. The keys are generated per build directory and never
committed, so another build has other hashes for all but the partition table.

| Artefact | Bytes | SHA-256 |
|---|---|---|
| `idf/bootloader/bootloader.bin` | 33 008 | `96b174828fbc7c0de114a32ca4b4f4071a0733ac2e717bac6ca58cb713bdae6a` |
| `idf/partition_table/partition-table.bin` | 3 072 | `73543a3838a312385e15d62ebcd0e52b7e870a58c715fc3a9829a0fbae158689` |
| `tfw_plat.bin` | 192 | `c8225d015d2dbadd386d9690d589891d72eeba101ac3e1ac1722a4c2ba3a8588` |
| `trusted-firmware.bin` (sealed) | 532 480 | `9d4a9f73f24b04738200d2174dd89b1c152cd53516f95ee167b0a675a01c6322` |
| `idf/trusted-firmware.bin` (no trailer) | 528 384 | `0205688429b6d4bde619fb417eb97f0ded1ad9c2c64938d8ba987daf8bafe919` |

**The network gate was closed.** The lane runs in a container on the bench
host with its own network namespace on the container bridge, and the image
carries no `ip`. Read through `/proc/net` instead, the namespace has one
interface besides loopback, a route to the bridge's own subnet and a default
route through the bridge. So the lane could not read the bench host's own
interfaces and routes, and has no direct route or interface to the board's
network. The neighbour table holds the bridge's gateway only, with no entry
for the board's MAC, so the board's address was not found. No network was
scanned. Steps 3 to 5 were not run. Opening the gate takes a run on the bench
host's own network namespace, where its `ip -br addr`, `ip route` and
neighbour table show the board's network and the board, or the board's
address and the bench network's netmask handed to the lane.

**2026-10-21, steps 1 to 5: positive; the board is restored.**
`ESP_PORT=/dev/ttyACM1`, esptool / espefuse v5.6.0 (pip, in a venv outside the
repository), on branch `test/p4-hil0-net` at `7868ca31`. Writes were the one
erase region and the four images of step 3, then the restore; no `espefuse`
command other than `summary` was run, and no eFuse was written. The static-address
overlay held the board's address on the bench's own network block (a /24 with
no gateway); this page writes the documentation address `192.0.2.24` in its
place. The lane ran
in a container on the bench host's default bridge, whose outbound TCP and ICMP
to that block is forwarded (Infrastructure, #171). The outputs are kept in the
bench host's record directory for the run, not in this tree.

| Step | Command | Result |
|---|---|---|
| 1 | `esptool --port $ESP_PORT chip-id` | `Chip type: ESP32-P4 (revision v3.2)`; byte-identical to 2026-10-18's, `6eea01f4be669e42ab8d3a0ef40a7babd73b225f1fc6aa007d54d27987226324` |
| 1 | `esptool --port $ESP_PORT flash-id` | `Manufacturer: c8`, `Device: 4019`, `Detected flash size: 32MB`; byte-identical, `4c4b0c1416a28bbfcb7b2723ec7c0204ecec96ecdbbc33f4f3cafd3ddc84b805` |
| 1 | `espefuse --port $ESP_PORT summary` | byte-identical to 2026-10-18's, `279d40b49e4969fabe75b9ce01c34f1771b36099ec3bbf8c73affc4f9a62831d`, so every row of "Security eFuses" is unchanged |
| 1 | `esptool --port $ESP_PORT -b 921600 read-flash 0 ALL p4-flash-before.bin` | `Read 33554432 bytes from 0x00000000 in 504.9 seconds (531.6 kbit/s)` |
| 1 | `sha256sum p4-flash-before.bin` | `c2d4ac0f25246b053d4d25f62580578a5f2b2a37002f7cda7a9d3a548702fde0`, equal to the record's; kept as the restore image |
| 2 | ESP-IDF `v5.5.5` from Espressif's tag, `install.sh esp32p4`, then `idf_tools.py install cmake ninja` | `idf.py --version`: `ESP-IDF v5.5.5`; `cmake version 3.30.2`, ninja `1.12.1`. The lane image's Python has no `ensurepip`, so `python3 -m venv` fails, for the esptool venv and for `install.sh`'s own; each was made `--without-pip` and given pip with `python3 -m pip --python <venv>/bin/python install pip`, after which `install.sh` completed |
| 2 | `make esp32-build BOARD=esp32p4-poe CRYPTO=none ESP_SDKCONFIG_EXTRA="boards/esp32p4-poe/sdkconfig.hil0 $HOME/p4-static.defaults"` | `sealed …/trusted-firmware.bin: board 0x80, version 1, image_len 524272`; `sdkconfig`: `# CONFIG_SECURE_BOOT is not set`, `# CONFIG_SECURE_FLASH_ENC_ENABLED is not set`, `CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT=y`, `CONFIG_ESPTOOLPY_FLASHSIZE_32MB=y`, `CONFIG_TFW_ETH_STATIC_IP=y`, `CONFIG_TFW_ETH_IP_ADDR="192.0.2.24"`, `CONFIG_TFW_ETH_NETMASK="255.255.255.0"`, `CONFIG_TFW_ETH_GATEWAY` the Kconfig default `"192.0.2.1"` |
| 3 | `esptool --chip esp32p4 --port $ESP_PORT erase-region 0x12000 0x5000` | `Flash memory region erased successfully in 0.1 seconds.` |
| 3 | `esptool --chip esp32p4 --port $ESP_PORT write-flash 0x2000 … 0x10000 … 0x11000 tfw_plat.bin 0x20000 trusted-firmware.bin` | `Wrote 33168 bytes … at 0x00002000`, `Wrote 3072 bytes … at 0x00010000`, `Wrote 192 bytes … at 0x00011000`, `Wrote 532480 bytes … at 0x00020000`; `Hash of data verified.` after each |
| 4 | console, UART0 at 115200 for 40 s after one reset | `boot: ESP-IDF v5.5.5 2nd stage bootloader`, `chip revision: v3.2`, `SPI Flash Size : 32MB`, the seven-partition table from `0x11000`; `tfw-l0: app at 0x20000 verified, measurement 99398ff53f7f97b845ebfa2921adb2e36f7bb8a02d32a6bdb031082f5a34cba3`; `tfw-l0: no HMAC_UP key in eFuse: this part has no UDS`; `boot: Loaded app from partition at offset 0x20000`; `App version: 7868ca31`; `eth ip: 192.0.2.24, mask: 255.255.255.0, gw: 192.0.2.1`; `tfw-eth: link up, address 192.0.2.24, wire on tcp/21574`, the overlay's address, so PHY address 1 is confirmed |
| 4 | `ping -c 3 192.0.2.24` | the lane image has no `ping`; the same three ICMP echoes were sent from the container through an unprivileged ICMP socket (`ping_group_range` admits the lane's group): `3 packets transmitted, 3 received`, 1.61, 0.68 and 0.47 ms |
| 4 | `build/esp32/wirecheck -addr 192.0.2.24:21574 -board 0x80 -tier 0` | `ping echoed over tcp`; `caps proto 2.0 fw 0.1.0 board 0x80 pins 17 tier 0 features 0x000007ff`; `sysinfo uptime 55.284s free-ram 538172 reset-cause 1`; `none tier: no P-256 in CAPS, ATTEST refused (unsupported)`; `wirecheck: ok`, exit 0 |
| 5 | `make board-restore BOARD=esp32p4-poe ESP_PORT=$ESP_PORT BOARD_RESTORE_IMAGE=<record dir>/p4-flash-before.bin` | `Wrote 33554432 bytes (5135340 compressed) at 0x00000000 in 517.4 seconds`, `Hash of data verified.`; `board-restore: esp32p4-poe has its factory flash image back, SHA-256 c2d4ac0f…fde0` |
| 5 | `esptool --port $ESP_PORT -b 921600 read-flash 0 ALL p4-flash-after.bin` | `Read 33554432 bytes from 0x00000000 in 506.7 seconds (529.7 kbit/s)` |
| 5 | `sha256sum p4-flash-after.bin` | `c2d4ac0f25246b053d4d25f62580578a5f2b2a37002f7cda7a9d3a548702fde0`, equal to the record's |
| 5 | `espefuse --port $ESP_PORT summary` | `279d40b49e4969fabe75b9ce01c34f1771b36099ec3bbf8c73affc4f9a62831d`, byte-identical to step 1's and to the record's: no eFuse moved |

The build's artefacts, as written in step 3. The keys are generated per build
directory and never committed, so these differ from 2026-10-19's for all but
the partition table, and the static-address overlay moves the app too.

| Artefact | Bytes | SHA-256 |
|---|---|---|
| `idf/bootloader/bootloader.bin` | 33 168 | `2855d8b9d794cb95c73b1edeee892d4d8c183f017b6c9396c977da15c445e038` |
| `idf/partition_table/partition-table.bin` | 3 072 | `73543a3838a312385e15d62ebcd0e52b7e870a58c715fc3a9829a0fbae158689` |
| `tfw_plat.bin` | 192 | `fe843dc8d4442695591264bbad42f13f4ad42b41a6ab9713bcf3525839ef426b` |
| `trusted-firmware.bin` (sealed) | 532 480 | `b30e1c47258cc8c7f7bac0fd90ea91ac09aecb45dbd2d8b79faddc2cf8e93cd2` |
| `idf/trusted-firmware.bin` (no trailer, not written) | 528 384 | `1992ae0b739366dcf9997979fbb466c62ad9fd1a8bbe0e956cb50158b8436f11` |

The restore:

| | SHA-256 |
|---|---|
| The record (`FACTORY_FLASH_SHA256`) | `c2d4ac0f25246b053d4d25f62580578a5f2b2a37002f7cda7a9d3a548702fde0` |
| `p4-flash-before.bin`, the image restored | `c2d4ac0f25246b053d4d25f62580578a5f2b2a37002f7cda7a9d3a548702fde0` |
| `p4-flash-after.bin` | `c2d4ac0f25246b053d4d25f62580578a5f2b2a37002f7cda7a9d3a548702fde0` |
| `espefuse summary`, step 1 and step 5 | `279d40b49e4969fabe75b9ce01c34f1771b36099ec3bbf8c73affc4f9a62831d` |

Nothing blocked a step. Not checked by these five steps: the C6 held in
reset, which step 4 does not observe.

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
boot key digest, the UDS in the `HMAC_UP` key block, flash encryption) stays,
and an image that does not match those eFuses will not boot. Restore before
burning any of them, or record here what was burned.
