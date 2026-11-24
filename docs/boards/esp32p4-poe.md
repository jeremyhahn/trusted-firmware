# esp32p4-poe (ESP32-P4 PoE Ethernet)

> **Status: INFORMATIVE** · SPEC row: `esp32-p4` · Scope: `boards/esp32p4-poe/`
> · Last verified: 2026-11-21
>
> A summary of what the board files say, with each value cited to where the
> build reads it. The normative contract is
> [`adding-a-board.md`](adding-a-board.md); where this page and a board file
> disagree, the board file is right and this page is the defect.

Waveshare's ESP32-P4-WIFI6-POE-ETH on the bench, with an ESP32-C6
co-processor this firmware holds in reset — no firmware of ours runs on it and
its radio is never enabled (`profile.mk:4`–`5`; the C6's enable line at
`board_profile.h:57`–`60`, held low from the first instruction of
`tfw_hal_init`). It is the first MCU onboarded beside the Raspberry Pi nodes;
the row is `esp32-p4` of [`../SPEC.md`](../SPEC.md). The board runs this firmware since bench HIL-0,
[`../development/bench/esp32-p4.md`](../development/bench/esp32-p4.md)
(`board_profile.h:6`–`10`); what has run on it is in "Feature matrix". The
port is also exercised on Espressif's QEMU ([`esp32-qemu.md`](esp32-qemu.md)).

## The part

| | Value | Source |
|---|---|---|
| MCU | ESP32-P4, `ESP_TARGET=esp32p4` | `profile.mk:14`–`15` |
| Ethernet | the P4's own EMAC and an IP101GRI PHY over RMII, the 50 MHz clock coming in on GPIO50 from the PHY, which makes it from its own 25 MHz crystal; PHY address 1, the PHY's strapping | `board_profile.h:42`–`55`; `sdkconfig.defaults:5`, `:15`–`17` |
| Flash | 32 MB — a GD25Q256 in the vendor schematic, JEDEC `0xC8` `0x4019` read on the bench | `sdkconfig.defaults:10`–`11`, `:18`; `partitions.csv:4` |
| Flash geometry | `TFW_FLASH_TOTAL` 16 MiB, the trailer parser's bound; the part is laid out in its first 16 MB because ESP-IDF v5.5.5 maps an app through the cache only below that on quad flash | `board_profile.h:79`–`84`, `:85`; `partitions.csv:4`, `:6`–`8` |
| EEPROM | 4 096 bytes, the `tfw_eeprom` partition | `board_profile.h:34`; `partitions.csv:15` |
| Secure boot | ESP-IDF's secure boot v2 with an RSA image signature, outside our DICE chain; layer 0 verifies the ES256 trailer with the ROM's ECDSA, because micro-ecc did not fit the P4 bootloader's iram_loader_seg | `profile.mk:19`–`22` |
| Flash encryption | on, in release mode, on top of secure boot v2; release mode is permanent once the first boot burns it | `sdkconfig.defaults:4`–`8`, `:28`–`29` |

The factory state — 33 554 432 bytes, SHA-256 `c2d4ac…0fde0`, read 2026-10-17
before the project's first write — is in `profile.mk:24`–`27`;
`make board-restore` writes it back. The bench record, including the eFuse
state (no security fuse burned) and the pin table checked against the vendor
schematic, is [`../development/bench/esp32-p4.md`](../development/bench/esp32-p4.md);
the schematic is listed by title and URL in
[`../reference/README.md`](../reference/README.md) ("Board sources").

The OTA trial deadline is 60 s, as each AVR board with a boot section
(`board_profile.h:74`–`77`).

## The partitions

`partitions.csv`, the 32 MB part laid out in its first 16 MB: two OTA slots of
6 MB and ESP-IDF's OTA data, plus the four `tfw_*` data partitions this
firmware's, `tfw_plat`, `tfw_id`, `tfw_eeprom` and `tfw_auth`
(`src/hal/esp32/hal_esp32.h`). With flash encryption off, as for bench HIL-0,
ESP-IDF ignores the `encrypted` flag. The last slot ends at `0xC20000`; the
space above stays free for data (`partitions.csv:4`–`10`, `:13`–`19`).

## Board id

`0x80`, the first id in the ESP32 range of the
[board-id registry](board-id-registry.md) (`board_profile.h:23`). The SUIT
class id is UUIDv5 `b7535d26-c463-598f-a55f-ecc936e674e1`
(`board_profile.h:25`–`28`).

## Pins in summary

| | Value | Source |
|---|---|---|
| Pins | 17, the GPIOs the board brings out on its 40-pin header less the reserved ones — GPIO 2, 3, 4, 5, 6, 7, 8, 20, 21, 22, 23, 32, 33, 46, 47, 48, 53 | `board_profile.h:30`; `board.c:15`–`38` |
| Ports | 2: GPIO 0–31 and 32–54; a pin's GPIO is base plus bit | `board_profile.h:31`; `board.c:9`–`13` |
| PWM | 4 units, LEDC channels 0–3 on timer 0 (the low-speed group only); the tone generator takes the last timer and channel | `board_profile.h:32`; `board.c:40`–`47` |
| ADC | 8 channels: ADC1 channels 0–7, GPIO 16+n; four of them on the header, 20–23 | `board_profile.h:33`; `board.c:19`, `:28`–`31` |
| Tone | GPIO6, the first pin with no PWM channel, through LEDC. It also runs to the C6's IO2, harmless while the C6 is held in reset | `board_profile.h:62`–`64`; `board.c:17`, `:25` |
| Buses | I2C on the header's GPIO 7/8, shared on the board with the ES8311 codec and the camera and display connectors; SPI on 46/47/48 | `board_profile.h:66`–`72`; `board.c:26`–`27`, `:34`–`36` |
| GPIO53 | also the speaker amplifier's enable: driving it high switches the amplifier on | `board.c:18`–`19`, `:37` |

**Reserved, so absent from the pin table:** the RMII lines and the IP101's
MDC/MDIO/reset (28–31, 34, 35, 49–52; 35 is also the BOOT key), the C6's SDIO
(14–19) and its enable (54), USB (24–27, on the header but the P4's
full-speed pins), the console UART0 (37, 38, to the CH343P bridge), strap 36,
the ES8311 codec's I2S (9–13), the SD card (39–45) and the 32 kHz crystal
(0, 1) (`board_profile.h:12`–`17`).

## Building and HIL-0

Built by `make esp32-build BOARD=esp32p4-poe` — never by `make firmware`, and
never in `make ci`, whose image has no ESP-IDF (`profile.mk:5`–`6`); the
default tier is the part's crypto tier, `hmac-p256` (`profile.mk:16`–`17`).
`ATTEST` signs with the P-256 DeviceID the `hmac-p256` tier derives from the
eFuse UDS; a tier with no UDS answers it NOT_PROVISIONED or UNSUPPORTED
(`board_profile.h:36`–`40`). The bench HIL-0 image is built `CRYPTO=none` with
the overlay `boards/esp32p4-poe/sdkconfig.hil0`, which turns off secure boot
and flash encryption so first boot burns no eFuse, while the app is still
built signed with the `--secure-pad-v2` segment that `l0_seal.py` grows for the
trailer and re-signs
([`../development/bench/esp32-p4.md`](../development/bench/esp32-p4.md)
"HIL-0"; `sdkconfig.hil0:10`–`32`).

**Provisioning**: writing the bootloader, partition table and `tfw_plat`
for a new board or a new image key is its own procedure,
[`esp32p4-provisioning.md`](esp32p4-provisioning.md).

## Feature matrix

Run 2026-11-17 (row 536) on the bench HIL-0 image: `make esp32-build
BOARD=esp32p4-poe CRYPTO=none` with `sdkconfig.hil0` and a static-address
overlay, built from commit `50d9ddb7`, sealed `trusted-firmware.bin` SHA-256
`ba46a004da85c8d0be6065b29a7adf5cbb07ed74512dcd59f1dbbab1f2edab6a`, measured
by layer 0 as `649e1db9…8f6c10c`. The build's `sdkconfig` has
`CONFIG_SECURE_BOOT` and `CONFIG_SECURE_FLASH_ENC_ENABLED` not set. Each row is
`make hw-test BOARD=esp32p4-poe PORT=tcp:<board>:21574
HW_TEST_RUN='^TestFirmware<Row>$'`: the wire on this port is the TCP server of
`src/hal/esp32/hal_uart.c`, so the serial console answers no PING, and the
`tcp:` port was added for it. The whole-flash read before the first write and
`espefuse summary` before and after are in the bench record outside the tree;
the two summaries differ in no eFuse line.
The 2026-11-17 re-run (row 536, Codex on #546), `espefuse.py v4.13.0 summary`
only, before and after the I2C, SPI, pin and PWM rows: both files have SHA-256
`ea2a655c29fd0daabf56cba3471dee0e62134f1bd4564939b8bcccaead4d114e`, byte
for byte the same, so the list of differing lines is empty. Comparing the 237
lines left after removing the MAC, chip-ID/unique-ID and key-block lines
(left out here because they identify the device or its keys) also gives no
difference.

| SPEC row | Feature | Result | Evidence |
|---|---|---|---|
| esp32-p4 | boot, layer 0, Ethernet link | verified 2026-11-17 | console: `tfw-l0: app at 0x20000 verified`, `tfw-eth: link up … wire on tcp/21574` |
| esp32-p4 | wire over the P4's EMAC (board as server) | verified 2026-11-17 | rows `Ping`, `PingAfterAMalformedBurst`, `Sys` (after a fresh boot) pass over TCP |
| esp32-p4 | Ethernet DHCP lease | verified 2026-11-19 (#539, hw-568h) on the DHCP image, written to the app partition only ("The DHCP image" below): `tfw-eth: link up, address <a dev-VLAN lease>, wire on tcp/21574`, after the got-IP event 4.5 s after `ethernet attached to netif`, the gateway in the lease's own subnet; the image has `CONFIG_TFW_ETH_STATIC_IP` not set, so DHCP is its only address path. The earlier result: fail 2026-11-17 (#539): an image without the static overlay is refused by the board's layer-0 gate | boot console captured for 30 s across a USB-serial reset (RTS pulse; no break, no flash write): after `ethernet attached to netif` the only address lines are the got-IP event and `tfw-eth: link up, address …, wire on tcp/21574`, with no DHCP-client line, and the event's gateway is an RFC 5737 documentation address outside the board's subnet, which no DHCP server on the dev VLAN hands out and which the static-address overlay's placeholder gateway matches. The image does not log which path set the address, so the lease is not shown. The wire still answers there (`TestFirmwareCaps` passes, same date). A second run, same date: `make esp32-build` with the HIL-0 overlay only (`CONFIG_TFW_ETH_STATIC_IP` not set), the sealed app written to `ota_0` alone (the slot `otadata` boots; saved first), boot console captured for 30 s: the board's bootloader logs `Signature Check Failed` and `tfw-l0: app at 0x20000 refused by the trailer gate (status 14): no CDI`, then `no app passed the gate` and a reset loop. The build signs the trailer with its own layer-0 image key, which is not the key the board's bootloader holds, so a lane-built app cannot boot without the bootloader and table from the same build (a write outside the app partition). The saved app was written back; it verifies at layer 0, boots, and the wire answers (`TestFirmwareCaps` passes, same date). Next: a build with the bench's layer-0 image key, or a bootloader rewrite authorised for this board. MAC recorded in the bench record |
| esp32-p4 | tfw-wire over TCP | verified 2026-11-17 (#539) | `make hw-test BOARD=esp32p4-poe PORT=tcp:<board>:21574 HW_TEST_RUN='^TestFirmwareCaps$'` passes (`--- PASS: TestFirmwareCaps`): CAPS read over TCP from the bench host. `HW_TEST_RUN='CAPS'` matches no test (`no tests to run`), so the row names the test |
| esp32-p4 | GPIO | verified 2026-11-17 | rows `Write`, `Timer` pass (GPIO32/33) |
| esp32-p4 | ADC (ADC1) | fails: not measured | row `Analog` passes on GPIO20, ADC1 channel 4, but it only decodes Raw and never asserts it against a known input, so a constant result or a wrong channel passes it too; needs known input voltages on the ADC1 pins with the results recorded within tolerance; channels 0–3 (GPIO16–19) are the C6's SDIO and not in the table; issue to file |
| esp32-p4 | I2C (header 7/8) | verified 2026-11-17 | rows `I2C`, `I2CConfig` pass; bus transfer: a scan of 0x08–0x77 with `tfwctl i2c <addr> - 1` gets NACK on 110 addresses, the refusal on 0x60 (reserved), and an ACK at 0x18, whose registers 0xFD/0xFE read back `83`/`11`, the ES8311 codec's chip ID |
| esp32-p4 | SPI (46/47/48) | fails: no target on the bus | row `SPIConfig` passes, but it only records the configuration: `hal_bus.c` creates the SPI driver on the first transfer, and none was recorded; needs an SPI target or a MOSI-MISO loopback on GPIO 46/47/48, issue #569 |
| esp32-p4 | 1-Wire, pulse-in | fails: not measured | row `OneWireAndPulseIn` passes, but it does not check the presence value and only sees pulse-in time out on a silent pin, so a detector that always answers "absent" and a receiver that never captures an edge pass it too; needs a 1-Wire device on the pin answering presence, and a known pulse on the pulse-in pin measured within tolerance; issue to file |
| esp32-p4 | refusals of families CAPS lacks | verified 2026-11-17 | rows `RefusesTheAuthSessionItsCapsLacks`, `RefusesAttestCSRItsCapsLacks` pass |
| esp32-p4 | PWM (LEDC 0–3) | fails: not measured | rows `Pin`, `PWM` pass with the board facts `pwmPin` (GPIO2, LEDC0), `noLED` and no tick pin, but `PWM` only checks that setting duty on GPIO2 returns success and `Pin` checks descriptors: no output is observed, and LEDC channels 1, 2 and 3 are never driven, so a wrong pin route or a missing waveform passes too; needs duty and frequency measured on each LEDC channel's pin within tolerance; issue to file. The 2026-11-17 run of #547: row `PWM` passes over `PORT=tcp:<board>:21574` on the board's PWM pin, and row `Pin` fails on `pin 11 flags = digital, want digital and led`; row `Pin`: facts corrected, hardware re-run pending #561 (#574: the board has no LED on the header, so pin 11, GPIO32, is expected digital with no led flag); in the run of lane hw-564a on 2026-11-19 rows `Pin` and `PWM` pass |
| esp32-p4 | tone (GPIO6) | verified 2026-11-19 | row `Tone` (#548, hw-561b) on the image built from commit `2195e2cf`, sealed `trusted-firmware.bin` SHA-256 `20a73898aad8f209cbbad23ef60ea6c4f41eb8e483a5081a51d8240897b923fe` (\"Run 2026-11-19, hw-561b\" below), whose capture loops back the pin LEDC owns after NOTONE too, so the silence capture keeps the pad's output and reads what LEDC drives: 1 kHz on pin 4 read back with `pulse-capture` on the same pin as `1000.0 Hz measured over 28 half periods, 1000 Hz asked` (within 5%); after `notone`, 0 edges: the 4-edge, 20 ms capture answers TIMEOUT. The earlier 2026-11-19 run on `6c6a3dfb` captured after NOTONE with the pad's output off, so its silence proved nothing |
| esp32-p4 | EEPROM (`tfw_eeprom`) | verified 2026-11-19 | row `EEPROM` passes on the soft-tier image in the run of lane hw-564a (below), and on the `CRYPTO=none` image (2026-11-19, #539, "The DHCP image" below); before: row `EEPROM` stopped at "crypto tier none: no reserved-read facts"; the row now expects the reserved read answered on the none tier, as `tfw_auth_gate` gives it (#575) |
| esp32-p4 | CAPS, I2C_CONFIG | verified 2026-11-19 | rows `Caps`, `I2CConfig` pass on the soft-tier image in the run of lane hw-564a (below); on the `CRYPTO=none` image row `Caps` passes (2026-11-17, #539, "tfw-wire over TCP" above); before: rows `Caps`, `I2CConfig` knew the soft and element tiers only; they now hold none-tier facts (CAPS reports no algorithm, I2C_CONFIG applies the clock) and are in this board's row list (#576) |
| esp32-p4 | ATTEST unprovisioned | verified 2026-11-19 | row `AttestRefusedUnprovisioned` passes on the soft-tier image in the run of lane hw-564a (below); before: the board answers UNSUPPORTED (`0x7`), as "Building and HIL-0" says a tier with no UDS does; row `AttestRefusedUnprovisioned` wanted NOT_PROVISIONED, a tier's answer; on a none-tier image it now wants UNSUPPORTED (`attestRefusal`, `sdk/go/cmd/tfwctl/e2e_rows_test.go`) |
| esp32-p4 | boot event after a reset | verified 2026-11-19 with `HW_RESET` (#583); fails with `RESET_PORT` | with `HW_RESET`, row `PingDeliversTheBootEventWithNoBootWait` passes three of three ("The DHCP image" below). In the run of lane hw-564a (below), the reset by `RESET_PORT` (EN by RTS) between rows: rows `PingAfterAMalformedBurst` and `Sys` (power-on reset cause, zero sticky framing counters) pass; row `PingDeliversTheBootEventWithNoBootWait` fails, `Condition never satisfied` after 5 s: no BOOT event reached the wire within the row's wait after the reset, while the console shows the link coming up about 4.2 s after reset and the DHCP lease after it |
| esp32-p4 | CLI CAPS through the port | not re-run | row `ElementWithoutTheCapsBitIsRefusedAsUnsupported` hung on 2026-11-17; the test's wrapper now keeps the TCP read deadline and waits for no BOOT; needs a hardware re-run |
| esp32-p4 | wire over Ethernet, board dials (`HW_ETH`) | not run | the `TestEthernet*` rows listen for the board's dial; the run's host was not on the development VLAN; issue to file |
| esp32-p4, ota | OTA write (OTA_STAGE over the wire) | verified 2026-11-19 with the sealed image; row fails | the run of lane hw-564a (below). By hand, `tfwctl ota-stage --activate` over the board's wire port (a Unix socket piped to its TCP port; the CLI's `tcp://` form listens for a dialling board): the sealed image, 532480 bytes, `complete, 532480 bytes received`, three times, the last two into each slot; `esp_ota_end` takes it. Row `OTAAuthLetsTheCommandPastTheGate`, `HW_TEST_RUN='OTA'`: fails at `ota-stage` with `crypto provider failed`, as on 2026-11-17: the row stages its own ESP-IDF app image, which no image key seals, and `esp_ota_end` refuses it at the app signature check; the row needs the build's sealed image or its image key; follow-up: a soft-tier row that stages the build's sealed image, tracked in #640 |
| esp32-p4, ota | OTA activate gate on the soft tier | verified 2026-11-19 | row `SoftTierOTAActivateWithoutOTAAuthIsAuthRequired` passes in the run of lane hw-564a (below) |
| esp32-p4, ota | OTA_INFO | verified 2026-11-19 by hand; row fails | `tfwctl ota-info 0` on the soft image: `slots 2, active 0`, flags `0x00` settled, `0x04` on trial, `0x01` after the fall-back, and the running image's measurement, the one layer 0 prints; row `OTAInfo` fails with `this row needs a secure-element-tier peer`: the row wants tier 2, the soft image reports tier 1, and the P4 has no element fitted; follow-up: the row's tier requirement on a soft-tier board, tracked in #640 |
| esp32-p4, ota | OTA trial, confirm, roll back | verified 2026-11-19 by hand | the run of lane hw-564a (below), soft tier, no operator session (`auth_session_none.c`). Authorizations: the image key's ES256 signature over OTA_ACTIVATE's and OTA_CONFIRM's Sig_structure for the trailer's measurement and version 1, made on the host. Roll back: stage and activate into `ota_1`; at the reset `tfw-l0: app at 0x620000 on trial, activate authorized`, then `verified`, and OTA_INFO flags `0x04`; a reset with no confirm boots `ota_0`: `tfw-l0: app at 0x20000 verified`, `Loaded app from partition at offset 0x20000`, flags `0x01`. Trial and confirm: stage and activate into `ota_1` again, the trial boot as before, `ota-confirm` answers `confirmed; settled at the next reset` and flags go `0x04` to `0x00`; the next reset boots `0x620000` with no trial line. The same into `ota_0`: trial at `0x20000`, confirmed, settled. Rows `OTAActivate`, `OTAConfirm` are skipped (`this row needs CAPS feature auth (0x4000)`, the element tier's session); no host row drives this lifecycle on the soft tier yet, so the rows to add stage the sealed image with these authorizations. The simulator rows `TestE2EOTA*` start simulators, not the board: they are no evidence for this line |
| esp32-p4, ota | OTA on the `CRYPTO=none` image | not run | the `CRYPTO=none` image has no operator session, so every OTA row is out of its row list; needs an element- or soft-tier image signed with the board's key, the open owner question in #562 and #568 |
| esp32-p4 | secure boot v2, flash encryption | owner | each burns eFuses (CA 583) |
| esp32-p4 | UDS / HMAC DICE identity (`hmac-p256`) | owner | needs the UDS written to an eFuse key block (CA 583) |
| esp32 | the ESP-IDF port on the P4 | verified 2026-11-17 in part | the rows above; the rest as the `esp32-p4` lines |

Run 2026-11-19 (#561) on the HIL-0 image of "Building and HIL-0": `make
esp32-build BOARD=esp32p4-poe CRYPTO=none
ESP_SDKCONFIG_EXTRA=boards/esp32p4-poe/sdkconfig.hil0`, built from the merge
commit `6c6a3dfb`, sealed `trusted-firmware.bin` SHA-256
`3acc3f4a9520d3eddae87372b42c4db7e092409a560edd058f77fa885f8be341`, measured by
layer 0 as `6be4c98a…0df590e6b2a73`. Only the app partition was written
(`write-flash 0x20000`, `Hash of data verified.`); `espefuse summary` is
identical before and after. After an RTS reset the console shows `tfw-l0: app
at 0x20000 verified` and `tfw-eth: link up` with a dev-VLAN lease. The run is
`GOFLAGS=-v make hw-test BOARD=esp32p4-poe PORT=tcp:<lease>:21574
RESET_PORT=<the board's UART>` (`HW_RESET` takes only `qmp:`, and the TCP rows
reset through `RESET_PORT`, which drives EN by RTS with IO0 high):

- `Ping`: verified 2026-11-19
- `PingDeliversTheBootEventWithNoBootWait`: failed 2026-11-19: after the
  `RESET_PORT` reset the row's poll for `tcp/21574` timed out ("Condition
  never satisfied", 5.1 s); on the console the link comes up 5.2 s after reset,
  so the board is not back inside the row's window
- `PingAfterAMalformedBurst`: verified 2026-11-19
- `Caps`: verified 2026-11-19 (none-tier facts, #576)
- `Sys`: verified 2026-11-19
- `Pin`: verified 2026-11-19 (pin 11 digital with no led flag, #574)
- `Write`: verified 2026-11-19
- `Timer`: verified 2026-11-19
- `EEPROM`: verified 2026-11-19 (reserved read answered on the none tier, #575)
- `AttestRefusedUnprovisioned`: verified 2026-11-19
- `Analog`: verified 2026-11-19 (passes; not measured, as the ADC line says)
- `PWM`: verified 2026-11-19 (passes; not measured, as the PWM line says)
- `Tone`: verified 2026-11-19, 1000.0 Hz over 28 half periods for 1000 Hz
- `I2C`: verified 2026-11-19
- `I2CConfig`: verified 2026-11-19 (none-tier facts, #576)
- `SPIConfig`: verified 2026-11-19 (passes; no target, as the SPI line says)
- `OneWireAndPulseIn`: verified 2026-11-19 (passes; not measured, as its line says)
- `RefusesTheAuthSessionItsCapsLacks`: verified 2026-11-19
- `RefusesAttestCSRItsCapsLacks`: verified 2026-11-19

### Run 2026-11-19, hw-561b

The same build command (no static-address overlay; `CONFIG_SECURE_BOOT`,
`CONFIG_SECURE_FLASH_ENC_ENABLED` and `CONFIG_TFW_ETH_STATIC_IP` not set),
ESP-IDF v5.5.5, from commit `2195e2cf`, sealed with the image key of the
bootloader the board runs, sealed `trusted-firmware.bin` SHA-256
`20a73898aad8f209cbbad23ef60ea6c4f41eb8e483a5081a51d8240897b923fe`. esptool
v5.6.0 `write-flash 0x20000` alone over the CH343P console (USB id
`1a86:55d3`, checked first): `Wrote 532480 bytes`, one `Hash of data
verified.`. `espefuse summary` before and after differ only in esptool's
`Connecting...` progress dots. After an RTS reset (IO0 high): `tfw-l0: app at
0x20000 verified, measurement 53ab8cb3…5006ea2`, `App version:
xkms/v0.2.0-alpha-360-g2195e2cf`, and `tfw-eth: link up, address <a dev-VLAN
lease>`, the lease from the board's own got-IP line. `GOFLAGS=-v make hw-test
BOARD=esp32p4-poe PORT=tcp:<a dev-VLAN lease>:21574 RESET_PORT=<the board's
UART>`, first with `HW_TEST_RUN='Tone'`, then the full list:

- `Tone` (`HW_TEST_RUN='Tone'`): verified 2026-11-19, 1000.0 Hz over 28 half
  periods, 0 edges after NOTONE (TIMEOUT). The same selector also matches the
  simulator row `TestE2ELinksAnalogPWMTonePulse`, which failed because
  `build/host/tfwsim-atecc608a` was not built in this run, not on the board
- `PingDeliversTheBootEventWithNoBootWait`: failed 2026-11-19, "Condition
  never satisfied" after 5.1 s, as in the run above: the link comes up 5.2 s
  after the reset
- `Ping`, `PingAfterAMalformedBurst`, `Caps`, `Sys`, `Pin`, `Write`, `Timer`,
  `EEPROM`, `AttestRefusedUnprovisioned`, `Analog`, `PWM`, `Tone`, `I2C`,
  `I2CConfig`, `SPIConfig`, `OneWireAndPulseIn`,
  `RefusesTheAuthSessionItsCapsLacks`, `RefusesAttestCSRItsCapsLacks`: each
  verified 2026-11-19 (`--- PASS`; the rows that only decode or configure stay
  "not measured" in the matrix)

The `esp32-p4, ota` lines and the other lines dated 2026-11-19 (not the tone
line, the run above) are the run of lane hw-564a on 2026-11-19 (row 552, PR #564), on a soft-tier image: `make
esp32-build BOARD=esp32p4-poe CRYPTO=soft
ESP_SDKCONFIG_EXTRA=boards/esp32p4-poe/sdkconfig.hil0`, no static-address
overlay, built from commit `016b002e` (the merge of main `7553dea36`; the
commit after it changes the plan only), sealed with the image key of the
board's layer-0 build. Sealed `trusted-firmware.bin` SHA-256
`c206ed4ca28e12527e63aa87bc64c605880c68ac6d9afaa2d96764419282f7e3`, trailer
version 1, measurement
`adb97d94550a111b930247699ea4527702599fabd0058eba2acb4cd8f9602839`. Flashed
app-only: esptool v5.6.0 `write-flash 0x20000` of that
image, one `Hash of data verified.`, and no other esptool write; the OTA
writes are the firmware's own, over the wire. After a reset by `RESET_PORT`
the console shows `tfw-l0: app at 0x20000 verified`, `tfw-eth: link up` and a
dev-VLAN lease. Then `make hw-test BOARD=esp32p4-poe PORT=tcp:<board>:21574
RESET_PORT=<the UART> HW_TEST_RUN='OTA'`, the lifecycle by hand, and the full
`make hw-test` with the same ports: 18 board rows pass (`Ping`,
`PingAfterAMalformedBurst`, `Caps`, `Sys`, `Pin`, `Write`, `Timer`, `EEPROM`,
`AttestRefusedUnprovisioned`, `Analog`, `PWM`, `Tone`, `I2C`, `I2CConfig`,
`SPIConfig`, `OneWireAndPulseIn` and the two CAPS refusals) and
`PingDeliversTheBootEventWithNoBootWait` fails; the rows' own limits in the
lines above stand. In that run's `HW_TEST_RUN='OTA'` set, the simulator rows
fail with their simulators not built and the `TestEthernet*` rows for want of
`HW_ETH`: neither touches the board. `espefuse summary` before and after are
identical. The board is left on this image in `ota_0`, confirmed. The
2026-11-17 full-image flash (commit `aa0d0bab`) was provisioning, not
evidence for this PR.

### The DHCP image, 2026-11-19 (hw-568h)

`make esp32-build BOARD=esp32p4-poe CRYPTO=none
ESP_SDKCONFIG_EXTRA=boards/esp32p4-poe/sdkconfig.hil0` (no static-address
overlay; the build's `sdkconfig` has `CONFIG_TFW_ETH_STATIC_IP`,
`CONFIG_SECURE_BOOT` and `CONFIG_SECURE_FLASH_ENC_ENABLED` not set), ESP-IDF
v5.5.5, built from commit `74be91a5` and sealed with the image key of the
bootloader the board already runs, sealed `trusted-firmware.bin` SHA-256
`8ee12ea1b7d8290fe87e2d906a3fc8f5d9692ff1f22052da96f463d3ce42cc08`. Only the
app partition was written: `ota_0` at 0x20000 (the build's partition table
places it there), over the CH343P console port (USB id `1a86:55d3`, checked
first) with esptool v5.6.0 `write-flash 0x20000 trusted-firmware.bin`. No
bootloader, partition table, `tfw_plat` or erase was written. The board is
left on this image. Writing the bootloader, partition table and `tfw_plat`
is a provisioning procedure, documented separately,
and is not part of this evidence.

| Date | Row | Result | Evidence |
|---|---|---|---|
| 2026-11-19 | app-only flash | verified | `write-flash 0x20000` alone: `Wrote 532480 bytes … at 0x00020000`, one `Hash of data verified.`; `espefuse summary` before and after identical (the only `espefuse` command run); no serial break |
| 2026-11-19 | boot, layer 0 | verified | console after one reset (EN through RTS, IO0 high), 120 s: `tfw-l0: app at 0x20000 verified, measurement f931b517ff8b0d8362588a1153fe06abea482e2b9bace0e9ec98b5e6e05cbd80`, `boot: Loaded app from partition at offset 0x20000`, `App version: xkms/v0.2.0-alpha-377-g74be91a5` |
| 2026-11-19 | EMAC/PHY link up | verified | `ethernet attached to netif` at 0.76 s, `tfw-eth: link up, address <a dev-VLAN lease>, wire on tcp/21574` at 5.24 s; the image logs no separate PHY link line |
| 2026-11-19 | DHCP lease | verified | the got-IP event (`esp_netif_handlers: eth ip: <a dev-VLAN lease>`, gateway in the same subnet) 4.5 s after the netif attached, well within 60 s; the address was read from that line only. The image has no DHCP-client log line of its own |
| 2026-11-19 | `Ping`, `PingAfterAMalformedBurst`, `Caps`, `Sys`, `EEPROM` | verified | `make hw-test BOARD=esp32p4-poe PORT=tcp:<a dev-VLAN lease>:21574` (`GOFLAGS=-v`): each `--- PASS` |
| 2026-11-19 | `Pin`, `Write`, `Timer`, `Analog`, `PWM`, `Tone`, `I2C`, `I2CConfig`, `SPIConfig`, `OneWireAndPulseIn` | verified (row passes) | same run, each `--- PASS`. Rows that only decode or configure stay "not measured" in the matrix above |
| 2026-11-19 | `AttestRefusedUnprovisioned`, `RefusesTheAuthSessionItsCapsLacks`, `RefusesAttestCSRItsCapsLacks` | verified | same run, each `--- PASS` |
| 2026-11-19 | `PingDeliversTheBootEventWithNoBootWait` | verified (#583) | skipped in the run above (no `HW_RESET`); then `make hw-test BOARD=esp32p4-poe PORT=tcp:<a dev-VLAN lease>:21574 HW_RESET=cmd:<EN pulse through RTS, IO0 high> HW_TEST_RUN='^TestFirmwarePingDeliversTheBootEventWithNoBootWait$'` (`GOFLAGS=-v`), three of three runs `--- PASS` (5.42 s, 5.42 s, 7.46 s) within the 30 s `TCPResetBound` |

## What is not met yet

The board runs this firmware and answers the wire over its own EMAC; GPIO
and tone pass on it (Feature matrix). Not met:
PWM has no measured duty or frequency on any LEDC channel and ADC no result
checked against a known input; SPI has no recorded bus transfer (#569); 1-Wire has no device answering
presence and pulse-in no measured pulse; the BOOT event does not reach the
wire within the row's wait after a `RESET_PORT` reset (it does with
`HW_RESET`, #583); the board-dials Ethernet rows have not run; OTA write,
trial, confirm and roll back pass on the board by hand with the sealed image
(2026-11-19), but the rows `OTAAuth` and `OTAInfo` fail on their own image and
tier, and no host row drives the soft-tier lifecycle yet (#640); secure boot v2,
flash encryption and the
identity keyed from the UDS need eFuse writes and wait for the owner.
