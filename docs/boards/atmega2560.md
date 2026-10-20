# ATmega2560 (Arduino Mega 2560)

> **Status: INFORMATIVE** · Scope: `boards/atmega2560/` · Last verified: 2026-10-16
>
> A summary of what the board files say, with each value cited to where the
> build reads it. The normative contract is [`adding-a-board.md`](adding-a-board.md);
> where this page and a board file disagree, the board file is right and this
> page is the defect.

One of the two AVR boards that make AVR dev complete, with the Nano
([`atmega328p.md`](atmega328p.md)): the Mega over Ethernet, with the ATECC608C
element. It is the only AVR board with A/B slots, and the only one whose element
image carries `ATTEST_CSR`.

## The part

| | Value | Source |
|---|---|---|
| Part | ATmega2560, avrdude part `m2560` | `boards/atmega2560/profile.mk:4`, `:6` |
| Clock | 16 MHz (`F_CPU`) | `boards/atmega2560/profile.mk:5` |
| Flash | 262 144 bytes | `profile.mk:16`; `board_profile.h:91` |
| SRAM | 8 192 bytes, last address `0x21FF` | `profile.mk:17`, `:25` |
| EEPROM | 4 096 bytes, asserted against avr-libc's `E2END` | `board_profile.h:42`, `:260` |
| Flash budget | 65 536 bytes (a quarter of the part) | `profile.mk:14`; `board_profile.h:133` |
| RAM budget | 2 048 bytes (a quarter) | `profile.mk:15`; `board_profile.h:134` |
| Boot section | 8 192 bytes at `0x3E000`, budget 8 192 | `profile.mk:22`–`24`; `board_profile.h:92`–`93` |

The budgets are gates (`make size-check`), not estimates; why the
fractions are what they are, and why the RAM gate moved to 2 048 (issue #55),
is at `board_profile.h:65`–`80`. The part facts agree
with Microchip's *ATmega640/V-1280/V-1281/V-2560/V-2561/V* data sheet,
DS40002211A (features page: 256 KB flash, 8 KB SRAM, 4 KB EEPROM), and the
Arduino *Mega 2560 Rev3* documentation (SKU A000067: 16 MHz, 4 KB EEPROM,
<https://docs.arduino.cc/hardware/mega-2560/>). The EESAVE fuse is high-fuse
bit 3 (`profile.mk:27`–`31`, citing the data sheet's *Memory Programming*, Fuse
High Byte table; section number not confirmed).

### Build rows

`ETH=1` is this board's default (`profile.mk:51`, owner ruling, issue #55 item
48): `make firmware BOARD=atmega2560` with no `ETH=` builds the Ethernet image,
with the OTA stager. The default tier is `CRYPTO=auto` (`profile.mk:56`,
owner ruling of 2026-10-17, #130): one image that runs the element when one
answers at init, else the soft DICE root. The shieldless bench Mega is a named
row of its own, `ETH=0`, on every tier (`profile.mk:59`–`60`).
`make firmware-matrix` builds and size-checks all six:

| Row | Settings |
|---|---|
| The runtime tier, Ethernet (the default) | `CRYPTO=auto` (`ETH=1`) |
| Software tier, Ethernet | `CRYPTO=soft` (`ETH=1`) |
| Element tier, Ethernet | `CRYPTO=atecc608a` (`ETH=1`) |
| The runtime tier, the shieldless bench Mega | `CRYPTO=auto ETH=0` |
| Software tier, the shieldless bench Mega | `CRYPTO=soft ETH=0` |
| Element tier, the shieldless bench Mega | `CRYPTO=atecc608a ETH=0` |

The boot harness's serial legs and `make bench-check` name `ETH=0`
(`mk/boot.mk`, `mk/bench.mk`). The ATECC608C is never required: with no element
on the bus the default image runs the soft DICE root, and its evidence names that
root by its algorithm, HMAC-SHA256, where the element's is ES256 (the `tfwctl`
E2E `TestE2EAutoWithNoElementAttestsOnTheSoftRoot`). The soft root's identity
is the CDI a soft tfw-boot derives from the UDS in boot-section flash under the
lock bits; behind the stock bootloader the image runs but `ATTEST` answers "no
provisioned identity".

The element tier over Ethernet measured 1 027 bytes of RAM (`.bss` 946,
`.noinit` 1, the handoff block 80), over the old 1 024-byte gate. No static
could be freed without a change of behaviour, so the gate is now 2 048 bytes, a
quarter of SRAM like the flash budget. The rest of the 8 KB is stack.

## Board id

`0x02` (`board_profile.h:19`), in the AVR range of the
[board-id registry](board-id-registry.md). The SUIT class id is
UUIDv5 `dc97b9d0-38d6-58e0-bb18-98bb2414d131` (`board_profile.h:21`–`24`).

## Pins in summary

| | Value | Source |
|---|---|---|
| Pins | 70: D0–D53 are 0–53, A0–A15 are 54–69 | `board_profile.h:26`–`29` |
| Ports | 11, A=0 … L=10, no port I; H–L in extended I/O at `0x100`+ | `board_profile.h:31`–`35`, `:143`–`159`; `board.c:9`–`24` |
| PWM | 13 units (Timers 1–5 less Timer0) | `board_profile.h:37`–`39`; `board.c:124`–`148` |
| ADC | 16 channels, A0–A7 on port F, A8–A15 on port K | `board_profile.h:41`; `board.c:106`–`121` |
| Tick | Timer0; pins 4 (PG5) and 13 (PB7) are `TFW_PWM_RSVD` | `board_profile.h:44`–`48`; `board.c:56`, `:65` |
| Tone | Timer2, pin 10 (OC2A = PB4, not pin 11) | `board_profile.h:49`, `:271`–`274` |
| Serial | USART0 on pins 0/1 (PE0/PE1), marked `TFW_PIN_IN_USE` | `board_profile.h:257`–`269`; `board.c:52`–`53` |
| LED | pin 13, PB7 | `board.c:65` |
| Scheduler | 16 slots | `board_profile.h:56`–`58` |

Arduino's page counts 15 PWM pins; the descriptor offers 13 because Timer0's
two belong to the tick. Pins 30–37 and 42–49 count *down* their ports
(`board.c:45`–`47`). Every pin has a digital port; none is ADC-only
(`board.c:48`–`49`). `test/unit/test_board_2560.c` holds the table to these
facts under `make board-sweep`.

## The Ethernet shield and the element

The pins are stated in `board_profile.h`; `board.c` flags the shield's pins
`TFW_PIN_ETH` so `CAPS` reports them taken.

| Line | Pin | Port | Source |
|---|---|---|---|
| WIZnet chip select | D10 | PB4 | `board_profile.h:103`; `board.c:62` |
| SD socket select (held high) | D4 | PG5 | `board_profile.h:104`; `board.c:56` |
| SPI MISO / MOSI / SCK / SS | D50 / D51 / D52 / D53 | PB3 / PB2 / PB1 / PB0 | `board_profile.h:276`–`283`; `board.c:102`–`105` |
| OTA store chip select (proposed, ADR open question 2) | D8 | PH5 | `board_profile.h:98`–`102`; `board.c:60` |
| Element SDA / SCL | D20 / D21 | PD1 / PD0 | `board_profile.h:297`–`298`; `board.c:72`–`73` |

The controller is the Ethernet Shield Rev3's W5100 unless the build says
`ETH_CHIP=w5500` (`profile.mk:33`–`36`); see
[`../ethernet-transport/`](../ethernet-transport/architecture.md). The element
is the ATECC608C on the MIKROE-6774 Click, TrustFLEX at `0x36`, 100 kHz
(`board_profile.h:285`–`298`). The Mega's A4/A5 are not the TWI pins, so a
Click wired as on a Nano is not on the bus (`board_profile.h:291`–`294`). The
board test holds the element to the TWI pins and clear of the shield
(`test/unit/test_board_2560.c:332`–`340`, `:344`–`355`).

## Boot section and boot rows

tfw-boot occupies the top 8 KB (`board_profile.h:81`–`96`). The application
region is the A/B primary slot, 64 KB at `0x00000`; the secondary slot follows
at `0x10100` (`board_profile.h:110`–`117`, `profile.mk:48`–`51`). OTA stages
through an external SPI NOR store (`profile.mk:42`–`46`;
`board_profile.h:105`–`108`), and the element image carries the SUIT manifest
reader (`profile.mk:38`–`40`).

The rows that ship are `BOOT_SUPPORTED` in `mk/boot.mk:264`–`266`: the element
tier three ways, plain, with OTA, and with the store. OTA and the store
together do not fit and are not a row (`mk/boot.mk:254`–`256`). The measured
sizes are in [`../secure-boot/cli.md`](../secure-boot/cli.md) and
[`../ota/`](../ota/architecture.md#atmega2560-ab-by-swap-move); this page does
not copy them.

## EEPROM regions

- The platform block, `0x000`–`0x0BF`:
  [`../secure-boot/api.md`](../secure-boot/api.md#reserved-eeprom).
- The network block, 32 bytes at `0x0C0`:
  [`../ethernet-transport/api.md`](../ethernet-transport/api.md).
- The swap journal, ten bytes at `0x0E0`–`0x0E9`, past the wire's
  `EEPROMReservedEnd` and refused to `EEPROM_WRITE` all the same
  (`board_profile.h:117`–`123`). This one is the Mega's alone.

## Flashing on the bench

The application goes in over the stock STK500v2 bootloader, through the
on-board 16U2 as an ACM device: avrdude programmer `wiring` at 115200
(`profile.mk:7`–`11`). tfw-boot goes in only by ISP; the bench board's programmer, factory fuses and
bootloader are its factory record,
[`../development/bench/mega2560.md`](../development/bench/mega2560.md),
and the runs on this board are in
[`../ethernet-transport/test-matrix.md`](../ethernet-transport/test-matrix.md).
`make boot-flash` reads EESAVE before any erase (`profile.mk:27`–`31`;
[`../development/flashing.md`](../development/flashing.md#re-provisioning-a-board-over-isp)).
