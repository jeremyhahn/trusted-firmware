# esp32p4-poe (ESP32-P4 PoE Ethernet)

> **Status: INFORMATIVE** · SPEC row: `esp32-p4` · Scope: `boards/esp32p4-poe/`
> · Last verified: 2026-11-17
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
| esp32-p4 | Ethernet DHCP lease | fail 2026-11-17 (#539): an image without the static overlay is refused by the board's layer-0 gate | boot console captured for 30 s across a USB-serial reset (RTS pulse; no break, no flash write): after `ethernet attached to netif` the only address lines are the got-IP event and `tfw-eth: link up, address …, wire on tcp/21574`, with no DHCP-client line, and the event's gateway is an RFC 5737 documentation address outside the board's subnet, which no DHCP server on the dev VLAN hands out and which the static-address overlay's placeholder gateway matches. The image does not log which path set the address, so the lease is not shown. The wire still answers there (`TestFirmwareCaps` passes, same date). A second run, same date: `make esp32-build` with the HIL-0 overlay only (`CONFIG_TFW_ETH_STATIC_IP` not set), the sealed app written to `ota_0` alone (the slot `otadata` boots; saved first), boot console captured for 30 s: the board's bootloader logs `Signature Check Failed` and `tfw-l0: app at 0x20000 refused by the trailer gate (status 14): no CDI`, then `no app passed the gate` and a reset loop. The build signs the trailer with its own layer-0 image key, which is not the key the board's bootloader holds, so a lane-built app cannot boot without the bootloader and table from the same build (a write outside the app partition). The saved app was written back; it verifies at layer 0, boots, and the wire answers (`TestFirmwareCaps` passes, same date). Next: a build with the bench's layer-0 image key, or a bootloader rewrite authorised for this board. MAC recorded in the bench record |
| esp32-p4 | tfw-wire over TCP | verified 2026-11-17 (#539) | `make hw-test BOARD=esp32p4-poe PORT=tcp:<board>:21574 HW_TEST_RUN='^TestFirmwareCaps$'` passes (`--- PASS: TestFirmwareCaps`): CAPS read over TCP from the bench host. `HW_TEST_RUN='CAPS'` matches no test (`no tests to run`), so the row names the test |
| esp32-p4 | GPIO | verified 2026-11-17 | rows `Write`, `Timer` pass (GPIO32/33) |
| esp32-p4 | ADC (ADC1) | fails: not measured | row `Analog` passes on GPIO20, ADC1 channel 4, but it only decodes Raw and never asserts it against a known input, so a constant result or a wrong channel passes it too; needs known input voltages on the ADC1 pins with the results recorded within tolerance; channels 0–3 (GPIO16–19) are the C6's SDIO and not in the table; issue to file |
| esp32-p4 | I2C (header 7/8) | verified 2026-11-17 | rows `I2C`, `I2CConfig` pass; bus transfer: a scan of 0x08–0x77 with `tfwctl i2c <addr> - 1` gets NACK on 110 addresses, the refusal on 0x60 (reserved), and an ACK at 0x18, whose registers 0xFD/0xFE read back `83`/`11`, the ES8311 codec's chip ID |
| esp32-p4 | SPI (46/47/48) | fails: no target on the bus | row `SPIConfig` passes, but it only records the configuration: `hal_bus.c` creates the SPI driver on the first transfer, and none was recorded; needs an SPI target or a MOSI-MISO loopback on GPIO 46/47/48, issue #569 |
| esp32-p4 | 1-Wire, pulse-in | fails: not measured | row `OneWireAndPulseIn` passes, but it does not check the presence value and only sees pulse-in time out on a silent pin, so a detector that always answers "absent" and a receiver that never captures an edge pass it too; needs a 1-Wire device on the pin answering presence, and a known pulse on the pulse-in pin measured within tolerance; issue to file |
| esp32-p4 | refusals of families CAPS lacks | verified 2026-11-17 | rows `RefusesTheAuthSessionItsCapsLacks`, `RefusesAttestCSRItsCapsLacks` pass |
| esp32-p4 | PWM (LEDC 0–3) | fails: not measured | rows `Pin`, `PWM` pass with the board facts `pwmPin` (GPIO2, LEDC0), `noLED` and no tick pin, but `PWM` only checks that setting duty on GPIO2 returns success and `Pin` checks descriptors: no output is observed, and LEDC channels 1, 2 and 3 are never driven, so a wrong pin route or a missing waveform passes too; needs duty and frequency measured on each LEDC channel's pin within tolerance; issue to file. The 2026-11-17 run of #547: row `PWM` passes over `PORT=tcp:<board>:21574` on the board's PWM pin, and row `Pin` fails on `pin 11 flags = digital, want digital and led`; row `Pin`: facts corrected, hardware re-run pending #561 (#574: the board has no LED on the header, so pin 11, GPIO32, is expected digital with no led flag) |
| esp32-p4 | tone (GPIO6) | verified 2026-11-17 | row `Tone` (#548), `make hw-test BOARD=esp32p4-poe PORT=tcp:<board>:21574 HW_TEST_RUN='Tone'`: 1 kHz on pin 4 read back with `pulse-capture` on the same pin, 30 edges, `1000.0 Hz measured over 28 half periods, 1000 Hz asked` (within 5%); after `notone` the capture times out |
| esp32-p4 | EEPROM (`tfw_eeprom`) | fails: test facts | row `EEPROM` stops at "crypto tier none: no reserved-read facts"; issue to file: none-tier facts |
| esp32-p4 | CAPS, I2C_CONFIG | facts corrected, hardware re-run pending #561 | rows `Caps`, `I2CConfig` knew the soft and element tiers only; they now hold none-tier facts (CAPS reports no algorithm, I2C_CONFIG applies the clock) and are in this board's row list (#576). On the image the board runs, row `Caps` passes (2026-11-17, #539, "tfw-wire over TCP" above) |
| esp32-p4 | ATTEST unprovisioned | fails: test facts | the board answers UNSUPPORTED (`0x7`), as "Building and HIL-0" says a tier with no UDS does; row `AttestRefusedUnprovisioned` wants NOT_PROVISIONED; issue to file |
| esp32-p4 | boot event on connect | not run | a `tcp:` connect resets nothing, so the rows that need a fresh boot (`PingDeliversTheBootEventWithNoBootWait`, `PingAfterAMalformedBurst`, and `Sys`, which wants a power-on reset cause and zero sticky framing counters) are out of this board's row list until it has a reset over the wire; issue to file |
| esp32-p4 | CLI CAPS through the port | not re-run | row `ElementWithoutTheCapsBitIsRefusedAsUnsupported` hung on 2026-11-17; the test's wrapper now keeps the TCP read deadline and waits for no BOOT; needs a hardware re-run |
| esp32-p4 | wire over Ethernet, board dials (`HW_ETH`) | not run | the `TestEthernet*` rows listen for the board's dial; the run's host was not on the development VLAN; issue to file |
| esp32-p4, ota | OTA write (OTA_STAGE over the wire) | fails 2026-11-17 (#562) | soft-tier image (below); row `OTAAuthLetsTheCommandPastTheGate`, `make hw-test BOARD=esp32p4-poe PORT=tcp:<board>:21574 HW_TEST_RUN='OTAAuth'`. The first run's `crypto provider failed` was the row's image: ESP-IDF's `esp_ota_write` refuses a first chunk without the app-image magic (`ESP_ERR_OTA_VALIDATE_FAILED`, which `hal_ota.c` answers as TFW_ERR_CRYPTO). The row now stages an ESP-IDF app image (`sdk/go/cmd/tfwctl/esp_image_test.go`): all 512 bytes are written, its header, app descriptor and appended hash verify, and `esp_ota_end` refuses it at the app signature check (console: `Secure boot signature verification failed ... image valid, signature bad`). The image verifies app signatures on update, so the row stages only an image signed with the build's image key, which the row does not have yet. `espefuse summary` identical before and after |
| esp32-p4, ota | OTA activate gate on the soft tier | verified 2026-11-17 | row `SoftTierOTAActivateWithoutOTAAuthIsAuthRequired` passes on the soft-tier image over `PORT=tcp:<board>:21574` |
| esp32-p4, ota | OTA_INFO | not runnable | row `OTAInfo` needs the element tier: the soft image reports tier 1, the row wants 2; the P4 has no element fitted |
| esp32-p4, ota | OTA trial, confirm, roll back | not runnable | rows `OTAActivate`, `OTAConfirm` need CAPS `auth` (`0x4000`), which no P4 image reports: the operator session is linked by the element tiers only (`mk/firmware.mk`, issue #4); soft, `none` and `hmac-p256` link `auth_session_none.c`. Trial and roll back have no board row; the simulator rows `TestE2EOTA*` cover them. Issue to file: the session on an element-less tier, or an element on the P4 |
| esp32-p4, ota | OTA on the `CRYPTO=none` image | not run | the `CRYPTO=none` image has no operator session, so every OTA row is out of its row list; needs an element- or soft-tier image signed with the board's key, the open owner question in #562 and #568 |
| esp32-p4 | secure boot v2, flash encryption | owner | each burns eFuses (CA 583) |
| esp32-p4 | UDS / HMAC DICE identity (`hmac-p256`) | owner | needs the UDS written to an eFuse key block (CA 583) |
| esp32 | the ESP-IDF port on the P4 | verified 2026-11-17 in part | the rows above; the rest as the `esp32-p4` lines |

The `esp32-p4, ota` lines are the 2026-11-17 run (row 552) on a soft-tier
image: `make esp32-build BOARD=esp32p4-poe CRYPTO=soft` with `sdkconfig.hil0`
and the static-address overlay, built from commit `aa0d0bab`, sealed
`trusted-firmware.bin` SHA-256
`782fa6c699fff1df099077414efb85d60dd42fac91316d0f81fae77b13244166`, flashed by
the bench procedure's step 3 (the data sectors erased, the bootloader, table,
`tfw_plat.bin` and app written, each `Hash of data verified.`): the build
generates its own layer-0 image key, so the app alone would be refused by the
bootloader on the board. The console then shows `tfw-eth: link up … wire on
tcp/21574`. `espefuse summary` before and after differ in no line. The board
is left on this image; the first 0xA2000 bytes as they were, the HIL-0 image,
are kept in the bench record outside the tree.

## What is not met yet

The board runs this firmware and answers the wire over its own EMAC; GPIO
and tone pass on it (Feature matrix). Not met:
PWM has no measured duty or frequency on any LEDC channel and ADC no result
checked against a known input; SPI has no recorded bus transfer (#569); 1-Wire has no device answering
presence and pulse-in no measured pulse; EEPROM has no passing row on
this board and row `Pin` has corrected facts and waits on its hardware re-run (#561); the board-dials
Ethernet rows have not run; OTA staging fails on the soft tier and trial,
confirm and roll back cannot run without an operator session; secure boot v2, flash encryption and the
identity keyed from the UDS need eFuse writes and wait for the owner.
