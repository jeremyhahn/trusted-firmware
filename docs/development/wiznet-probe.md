# The Ethernet controller probe

Issue #55. Before writing the drivers for the W5100 and W5500, we read which
controller each bench shield actually carries. The probe answers that question
and nothing more. It is a probe, not the driver, and it is not part of
`make ci` or the firmware matrix, because it needs a board and a shield on a
desk.

- Source: `test/hw/wiznet-probe/`. The detection is in `wizprobe.c` and the
  reporting in `main.c`.
- Make: `mk/wiznet-probe.mk`, which provides `make wiznet-probe` and
  `make wiznet-probe-flash`.

The probe is built on the firmware's own HAL (`tfw_hal_spi_xfer`,
`tfw_hal_gpio_*`, the UART and the millisecond tick) and on the board's pin
table, with `FW_CFLAGS` and `FW_LDFLAGS` taken by reference. So it names no
register and no pin bit, and the same source builds for every board that has a
profile. A driver can take over `wizprobe_run` unchanged.

## Method

The probe drives CS on D10 on every board. It drives the SD card's CS (D4) high
before the bus comes up, so the card cannot answer on MISO. The SPI hardware SS
pin is set as an output by `tfw_hal_spi_init`: PB0 (D53) on the Mega and PB2
(D10) on the 328P. The bus runs in mode 0 at 1 MHz (16 MHz / 16). After a 500 ms
power-up wait, the probe repeats the following every two seconds:

| Step | Frame (MOSI) | Expect |
|---|---|---|
| W5500 `VERSIONR` 0x0039, common block, read | `00 39 00 00` | `0x04` in byte 3 |
| W5200 `VERSIONR` 0x001F, read, length 1 | `00 1F 00 01 00` | `0x03` in byte 4 |
| W5100 `MR` ← `0x80` (software reset), then 10 ms | `F0 00 00 80` | |
| W5100 `MR`, `RTR` 0x0017-18, `RCR` 0x0019 | `0F hi lo 00` each | `00`, `07 D0`, `08` |
| W5100 `RTR` ← `0B B8`, read back, restore `07 D0` | `F0 ...` / `0F ...` | `0B B8` |
| ENC28J60, only when none of the above matched: soft reset, `BFS ECON1,03`, `RCR ECON1`, `RCR EREVID` (bank 3, 0x12), `BFC ECON1,03` | `FF`, `9F 03`, `1F 00`, `12 00`, `BF 03` | `ECON1` bits 1:0 = 3, `EREVID` neither 00 nor FF |

The writes in the sequence are all W5100 writes (opcode `0xF0`). A W5500 or
W5200 parses `F0 00 xx` as an address followed by a read control word, so
neither of those parts is ever written to.

The verdict is chosen in this order: W5500, then the full W5100 pattern, then
W5200, then ENC28J60. If nothing matches, the verdict is
`none (MISO idle)` when every byte read back was `00` or every byte was `FF`,
and `unknown` otherwise. The probe prints every byte clocked back, not only the
register values, at the board's `TFW_UART_BAUD` (115200 8N1).

## Toolchain

The lane container has no AVR toolchain and no root access. The toolchain came
from Debian 13 (trixie) signed packages: `apt-get update` and `apt-get download`
ran with user-owned `Dir::State` and `Dir::Cache`, and each package was unpacked
with `dpkg -x` into `$HOME/avr`, outside the clone.

| Package | Version |
|---|---|
| gcc-avr | 1:14.2.0-2 (avr-gcc 14.2.0) |
| binutils-avr | 2.43.50.20250108-1 |
| avr-libc | 1:2.2.1-1 |
| avrdude | 7.1+dfsg-3+b2 (avrdude 7.1) |
| libftdi1, libusb-0.1-4, libhidapi-libusb0, libusb-1.0-0 | 0.20-4+b2, 0.1.12-35+b1, 0.14.0-1+b2, 1.0.28-1 (avrdude's shared libraries) |

The unpacked avrdude needs `-C $HOME/avr/etc/avrdude.conf` and an
`LD_LIBRARY_PATH` pointing at the unpacked libraries. A two-line wrapper script
on `PATH` supplies both, so `$(ARCH_FLASH)` runs unchanged.

## Flashing and reading

Every board is flashed through its own bootloader. The probe never writes
fuses, lock bits, EEPROM or the bootloader.

```
make wiznet-probe-flash BOARD=atmega2560 AVRDUDE_PORT=/dev/ttyACM0
#  = avrdude -v -pm2560 -cwiring -P/dev/ttyACM0 -b115200 -D -Uflash:w:build/wiznet-probe/atmega2560/wiznet-probe.hex:i
make wiznet-probe-flash BOARD=atmega328p AVRDUDE_PORT=/dev/ttyUSB0
#  = avrdude -v -pm328p -carduino -P/dev/ttyUSB0 -b115200 -D -Uflash:w:...   (-b57600 on the old bootloader)

stty -F /dev/ttyACM0 115200 raw -echo -hupcl
timeout 6 cat /dev/ttyACM0
```

## Observed

### Arduino Mega 2560 with an Arduino Ethernet shield, `/dev/ttyACM0`, 2026-10-15

avrdude read the signature `0x1e9801`. The probe printed:

```
wiznet-probe board=atmega2560 cs=D10 sd_cs=D4(high) spi=mode0/1MHz
w5500 tx=00 39 00 00 rx= 00 01 02 00
w5200 tx=00 1F 00 01 00 rx= 00 01 02 00 00
w5100 MR<-80 tx=F0 00 00 80 rx= 00 01 02 03
w5100 RTR[0] tx=0F 00 17 00 rx= 00 01 02 07
w5100 MR= 00
w5100 RTR= 07 D0
w5100 RCR= 08
w5100 RTR<-0B B8 readback= 0B B8
verdict: W5100
```

**W5100.** After the reset, RTR reads `07 D0` and RCR reads `08`, and a written
RTR reads back unchanged. The W5500 and W5200 version registers read `00`.

The W5100 clocks back `00 01 02 03` (the byte index within the frame) on every
frame, even when the first byte is not one of its opcodes (see the W5500 and
W5200 rows). That echo alone is a cheap presence check that a driver can make
before it resets anything.

### Arduino Nano (ATmega328P, CH340), `/dev/ttyUSB0`

**Not probed: the board has no serial bootloader that answers**, so the probe
could not be flashed, and the chip is still unknown.

- `-carduino -b115200` failed with `not in sync: resp=0x4d` ×10. The
  `0x4d` is `M`, from the running sketch's `macByte1`/`MAC:` lines.
- `-b57600` failed with `resp=0x80` ×10, which is the same 115200 output read at
  the wrong baud. `-b19200` also failed to sync.
- After a DTR/RTS reset, the resident sketch sends its first byte 71 ms later:

  ```
      71ms "Initializing dosing controller..."
      75ms "\r\nmacByte1: 4\r\n"
     372ms "MAC: 420003\r\nREST service listening on: 0.0.0.0\r\n"
  ```

  Both Optiboot and the old ATmegaBOOT wait for a sync byte for about half a
  second to a second after an external reset. A 71 ms start therefore means no
  bootloader runs first: either the sketch was written by ISP over a missing
  bootloader, or BOOTRST is unprogrammed.

The board runs the owner's dosing-controller application, which already drives
the Ethernet chip. It reports `0.0.0.0`, which suggests DHCP did not complete.
The board is no longer held: the owner freed both boards for reflashing
(2026-10-15 03:40Z), so the dosing-controller sketch may be erased. Since no
serial bootloader answers, the probe still cannot go in over USB. The next step
is an ISP programmer that burns Optiboot (and with it BOOTRST and the boot
section size); after that the board takes `make wiznet-probe-flash` like the
Mega. Until that run, the Nano's controller is unknown.
