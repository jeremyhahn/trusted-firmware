# ATmega328P (Arduino Nano)

> **Status: INFORMATIVE** · Scope: `boards/atmega328p/` · Last verified: 2026-10-16
>
> A summary of what the board files say, with each value cited to where the
> build reads it. The normative contract is [`adding-a-board.md`](adding-a-board.md);
> where this page and a board file disagree, the board file is right and this
> page is the defect.

The primary target, and one of the two AVR boards that make AVR dev complete,
with the Mega ([`atmega2560.md`](atmega2560.md)): the Nano over Ethernet, with
the ATECC608C element. The same profile serves the Uno and the Pro Mini
(`board_profile.h:4`); A6 and A7 exist only on the Nano's TQFP package.

## The part

| | Value | Source |
|---|---|---|
| Part | ATmega328P, avrdude part `m328p` | `boards/atmega328p/profile.mk:4`, `:6` |
| Clock | 16 MHz (`F_CPU`) | `boards/atmega328p/profile.mk:5` |
| Flash | 32 768 bytes | `profile.mk:12`; `board_profile.h:96` |
| SRAM | 2 048 bytes, last address `0x08FF` | `profile.mk:13`, `:24` |
| EEPROM | 1 024 bytes | `board_profile.h:29` |
| Flash budget | 16 384 bytes (half the part) | `profile.mk:10`; `board_profile.h:121` |
| RAM budget | 1 024 bytes (half) | `profile.mk:11`; `board_profile.h:122` |
| Boot section | 4 096 bytes at `0x7000`, budget 4 096 | `profile.mk:21`–`24`; `board_profile.h:97`–`100` |

The budgets are gates (`make size-check`), not estimates
(`board_profile.h:79`–`82`). The part facts are those of Microchip's
*ATmega328P* data sheet, DS40002061 (revision not confirmed); the EESAVE fuse
is high-fuse bit 3 (`profile.mk:26`–`30`, citing the data sheet's *Memory
Programming*, Fuse High Byte table; section number not confirmed). The Arduino
*Nano* documentation is <https://docs.arduino.cc/hardware/nano/> (SKU
A000005); its tech specs were not read for this page, so no value here is
confirmed against it.

The budget is the tightest of the element boards, and two things do not fit
in it. Each answers `TFW_ERR_UNKNOWN_CMD` with its `CAPS` bit clear:

- the operator-signature auth session (`AUTH_CHALLENGE`/`AUTH_RESPONSE`, issue
  #4), `board_profile.h:50`–`64`;
- `ATTEST_CSR` (issue #30), so the Nano cannot prove possession at ACME
  finalize, `board_profile.h:66`–`77`. The alias certificate stays on
  (`test/unit/test_board_328p.c`).

## Board id

`0x01` (`board_profile.h:15`), in the AVR range of the
[board-id registry](board-id-registry.md). The SUIT class id is
UUIDv5 `7a83e13f-8c4f-51bb-80ab-e1b82735410e` (`board_profile.h:17`–`20`).

## Pins in summary

| | Value | Source |
|---|---|---|
| Pins | 22: D0–D13 are 0–13, A0–A5 are 14–19, A6/A7 are 20/21 | `board_profile.h:22`–`25` |
| Ports | 3: B=0, C=1, D=2 | `board_profile.h:26`; `board.c:9`–`13` |
| PWM | 4 units: pins 3, 9, 10, 11 | `board_profile.h:27`; `board.c:58`–`63` |
| ADC | 8 channels; A6/A7 ADC-only, `TFW_PORT_NONE` | `board_profile.h:28`; `board.c:50`–`51` |
| Tick | Timer0; pins 5 and 6 are `TFW_PWM_RSVD` | `board_profile.h:31`–`35`; `board.c:35`–`36` |
| Tone | Timer2, pin 11 (OC2A = PB3) | `board_profile.h:36`, `:202`–`204` |
| Serial | the one USART on pins 0/1 (PD0/PD1), marked `TFW_PIN_IN_USE` | `board_profile.h:189`–`200`; `board.c:30`–`31` |
| LED | pin 13, PB5 | `board.c:43` |
| Scheduler | 8 slots | `board_profile.h:39` |

`test/unit/test_board_328p.c` holds the table to these facts under
`make board-sweep`.

## The Ethernet shield and the element

The pins are stated in `board_profile.h`; `board.c` flags the shield's pins
`TFW_PIN_ETH` so `CAPS` reports them taken.

| Line | Pin | Port | Source |
|---|---|---|---|
| WIZnet chip select | D10 | PB2 | `board_profile.h:113`; `board.c:40` |
| SD socket select (held high) | D4 | PD4 | `board_profile.h:114`; `board.c:34` |
| SPI MOSI / MISO / SCK | D11 / D12 / D13 | PB3 / PB4 / PB5 | `board_profile.h:206`–`212`; `board.c:41`–`43` |
| OTA store chip select (proposed, ADR open question 2) | D8 | PB0 | `board_profile.h:103`–`112`; `board.c:38` |
| Element SDA / SCL | A4 / A5 | PC4 / PC5 | `board_profile.h:235`–`236`; `board.c:48`–`49` |

Pin 13 carries both the LED and SCK, and pin 11 both MOSI and the tone
output (`board.c:41`, `:43`). The controller is the Ethernet Shield Rev3's
W5100 unless the build says `ETH_CHIP=w5500` (`profile.mk:32`–`35`); see
[`../ethernet-transport/`](../ethernet-transport/architecture.md). The element
is the ATECC608C on the MIKROE-6774 Click, TrustFLEX at `0x36`, 100 kHz; the
profile cites the Click schematic and Microchip DS40002515B for both
(`board_profile.h:214`–`236`). The board test holds the element to the TWI
pins and clear of the shield (`test/unit/test_board_328p.c:281`–`297`,
`:301`–`312`).

## Boot section and boot rows

tfw-boot occupies the top 4 KB, and the application region is everything below
it (`board_profile.h:83`–`101`). There is one internal slot: OTA stages into
the external SPI NOR store, which keeps a backup of the running image in its
`PREV` area (`profile.mk:37`–`41`; `board_profile.h:103`–`119`).

The row that ships is `atmega328p:atecc608a:atecc608a:atecc608a:0:0` in
`BOOT_SUPPORTED`, `mk/boot.mk:262`: the element tier, no boot-side OTA session
and no store driver in the bootloader. The measured sizes and margin are in
[`../secure-boot/cli.md`](../secure-boot/cli.md) and
[`../ota/`](../ota/architecture.md#atmega328p-and-atmega32u4-store-staged-one-internal-slot);
this page does not copy them.

## EEPROM regions

- The platform block, `0x000`–`0x0BF`:
  [`../secure-boot/api.md`](../secure-boot/api.md#reserved-eeprom).
- The network block, 32 bytes at `0x0C0`:
  [`../ethernet-transport/api.md`](../ethernet-transport/api.md).

## Flashing on the bench

The application goes in over Optiboot, avrdude programmer `arduino` at 115200
(`profile.mk:7`–`9`). The bench Nano has no serial bootloader that answers
([its factory record](../development/bench/nano.md#what-devttyusb0-answers-2026-10-16)),
so Optiboot goes in by ISP first, with the fuses and the steps in
[`../development/isp-route.md`](../development/isp-route.md#the-nano-next).
tfw-boot goes in only by ISP as well, and `make boot-flash` reads EESAVE before
any erase (`profile.mk:26`–`30`;
[`../development/flashing.md`](../development/flashing.md#re-provisioning-a-board-over-isp)).
The Nano must come off its Ethernet base while the programmer holds it in
reset, because D10 floats ([`isp-route.md`](../development/isp-route.md#the-nano-next), step 2).
