# The ISP route on the bench

Issues #7 and #55. The bench host has no ISP programmer, and two parts need one:

- The Nano has no serial bootloader that answers
  ([the probe](wiznet-probe.md#arduino-nano-atmega328p-ch340-devttyusb0)), so
  Optiboot goes in by ISP before anything can be flashed over USB.
- `tfw-boot` can only be installed by ISP
  ([secure boot](../secure-boot/user.md#prerequisites)), and the Mega needs it
  for `ATTEST` evidence ([the Mega run](../ethernet-transport/test-matrix.md#the-mega-run-2026-10-15),
  item 7).

Each board takes its turn as the other's programmer, running the ArduinoISP
sketch through its own serial bootloader:

1. **The Mega programs the Nano.** The Mega's stock STK500v2 bootloader works,
   so the Mega runs ArduinoISP and burns Optiboot into the Nano. This page
   records that step.
2. **The Nano programs the Mega.** With Optiboot in place, the Nano runs
   ArduinoISP and writes `tfw-boot` into the Mega's boot section with
   `make boot-flash`. That is the next step; what it needs is under
   [the next step](#the-next-step-the-nano-programs-the-mega).

Nothing here runs in CI, and nothing is installed on the bench host. The
programmer is a sketch in flash that the stock bootloader replaces at the end
of the run.

On 2026-10-15 the owner chose a USBasp instead, and the chain above became the
fallback, not taken. [The USBasp path](#the-usbasp-path-taken-owner-2026-10-15)
records what ran. The ArduinoISP sections after it are left as they were
written.

## Toolchain and sources

The toolchain is the one [the probe records](wiznet-probe.md#toolchain): the
same Debian 13 package versions, fetched with `apt-get download` into
user-owned `Dir::State` and `Dir::Cache` and unpacked with `dpkg -x` into
`$HOME/avr`. The ArduinoISP sketch, the Arduino AVR core it builds against
and the Optiboot image come from two more Debian 13 packages, unpacked into
`$HOME/arduino`.

| Package | Version | SHA-256 of the `.deb` |
|---|---|---|
| gcc-avr | 1:14.2.0-2 | `e1c9985199b9329bcb3efd7a720e9f0b82b8705e80efc883945e0f4be6a915fd` |
| binutils-avr | 2.43.50.20250108-1 | `d1b81c9a828371037a22a6daa75d1c88ac22941df2f9d729b2305b57cc940854` |
| avr-libc | 1:2.2.1-1 | `7a5574cd3dad522d72de1304c4e9c15b730b217a7d6a5182c2d7e296ba6b528f` |
| avrdude | 7.1+dfsg-3+b2 | `ec1b94afc3310f864a89be44d314223736747a3375919430e6675b72a14c37dd` |
| libftdi1 | 0.20-4+b2 | `0bf09d33ea3cfe2a1aaf33727006da4384f1065da465cee73c6789dd44451732` |
| libusb-0.1-4 | 2:0.1.12-35+b1 | `dffa6c8ff5cd6d26827110556d5dd2e2461bf3a67823f12da52a3d7b616125b9` |
| libhidapi-libusb0 | 0.14.0-1+b2 | `8c93a8c22bbbe7d6179505372db237aeed5748dfc6b1a9e2f645161d5facb530` |
| libusb-1.0-0 | 2:1.0.28-1 | `bb6467a002915c4ac464aa1b22204907951a30e43bcb86596ebd3539e84458e9` |
| arduino | 2:1.8.19+dfsg1-3 | `43d1fcff40bca92c90a75358bd85eef81d6130946541abe8e4c2fd0bb6c2be5d` |
| arduino-core-avr | 1.8.7+dfsg-1~deb13u1 | `928fdd3bf51582fbf5f78b6e84fd3ccea8b39d10088621a00cc795fc66b2e223` |

| File | From | SHA-256 |
|---|---|---|
| `usr/share/doc/arduino/examples/11.ArduinoISP/ArduinoISP/ArduinoISP.ino` (version 1.18, `SWMAJ`/`SWMIN`) | arduino | `473cc1327885ae1d121ce00ae50b06fcba8254524683ce5bf052cde156bbb3e9` |
| `usr/share/arduino/hardware/arduino/avr/bootloaders/optiboot/optiboot_atmega328.hex` (Optiboot 4.4, `0x7E00`–`0x7FFF`, 510 bytes) | arduino-core-avr | `08a2c8a41e72b567234f1461c0588e203340f2aff50da9090fd06ac150f77786` |

### The fuses Optiboot gives for the ATmega328P at 16 MHz

The package ships no `boards.txt` of Optiboot's own. Optiboot's own fuse values
for the part are its Makefile's `atmega328_isp` target,
`usr/share/arduino/hardware/arduino/avr/bootloaders/optiboot/Makefile`:

| Fuse | Value | Line | Makefile comment |
|---|---|---|---|
| high | `0xDE` | 231 | 512 byte boot, SPIEN |
| low | `0xFF` | 233 | Low power xtal (16MHz) 16KCK/14CK+65ms |
| extended | `0x05` | 235 | 2.7V brownout |

The Arduino core's `boards.txt` entry for the Uno, whose
`bootloader.file` is this same `optiboot/optiboot_atmega328.hex` (line 100),
agrees: `0xFF`, `0xDE`, `0xFD` at lines 95–97. The ATmega328P implements only
bits 2:0 of the extended fuse, so `0x05` and `0xFD` are the same setting.

The core's Nano entry (lines 220–222) gives a high fuse of `0xDA` instead: a
2 KB boot section for the same 512-byte image, the Nano's historical size.
That is not Optiboot's value, and this route does not use it.

The route writes no lock bits. The Makefile's and `boards.txt`'s `lock_bits`
(`0x0F`, which stops SPM from writing the boot section) are left out on
purpose: the next image for the Nano's boot section may be `tfw-boot`.

## The USBasp path (taken, owner 2026-10-15)

The programmer is a USBasp clone on the bench host's USB: vendor `16c0`,
product `05dc`, strings `USBasp` and `www.fischl.de`. The Company Agent's udev
rule gives it group `dialout`, mode `0660`. avrdude is the 7.1 in the table
above. Every run prints `cannot set sck period; please check for usbasp
firmware update`, because the clone's firmware predates that request. It then
programs at its own default clock, and every read below completed through it.

### The Mega, 2026-10-16

The USBasp is on the Mega's ICSP header, and the Mega is on USB as
`/dev/ttyACM0`. Nothing was written to the Mega. The run stopped at
`make boot-flash`, which does not build (step 3).

**0. Signature and fuses.**

```sh
avrdude -c usbasp -P usb -p m2560 -v \
        -U lfuse:r:-:h -U hfuse:r:-:h -U efuse:r:-:h -U lock:r:-:h
```

| | Read | The Company Agent's reading |
|---|---|---|
| Signature | `0x1E9801` (ATmega2560) | the same |
| Low fuse | `0xFF` | the same |
| High fuse | `0xD8` | the same |
| Extended fuse | `0xFD` | the same |
| Lock | `0xCF` | the same |

The high fuse `0xD8` has `BOOTRST` programmed and `BOOTSZ` `00`, which is the
8 KB section at `0x3E000` (`BOOT_BASE` in `boards/atmega2560/profile.mk`).
`EESAVE` is unprogrammed.

**1. The EEPROM, read out before anything is written.**

```sh
avrdude -c usbasp -P usb -p m2560 -U eeprom:r:m2560-eeprom-pre.hex:i
```

All 4096 bytes read `0xFF`.

| | SHA-256 |
|---|---|
| `m2560-eeprom-pre.hex` (Intel hex) | `e68043379dd6686b8a327c9fe63829307ae42a5663794a693a629d26bd1a1e2f` |
| The same 4096 bytes, binary | `f47a8ec3e9aff2318d896942282ad4fe37d6391c82914f54a5da8a37de1300c6` |

The file is kept in `$HOME/isp/`, outside the repository.

It does not match the last run. On 2026-10-15, before ArduinoISP went in,
`tfwctl eeprom-read` dumped `0x000`–`0x0FF` (see
[Flash and check](#flash-and-check-2026-10-15)):

- the platform block at `0x000`, with serial
  `54465700000000000000000000002560` at `0x034` and its CRC at `0x0BE`;
- the network block at `0x0C0`, with MAC `5a:ed:09:10:bf:59`;
- `0xFF` from `0x0E0`.

Every byte of those two blocks that was not `0xFF` is `0xFF` now.

The application is gone as well:

- Over ISP, flash reads `0xFF` from `0x00000`.
- The boot section at `0x3E000` begins with the STK500v2 bootloader's vector
  table (`0d 94 89 f1 0d 94 b2 f1`).
- The stock bootloader, read with `avrdude -c wiring` in terminal mode, shows
  the same flash and the same EEPROM.

So between the ArduinoISP check on 2026-10-15 and this run, the flash and the
EEPROM were erased, and the bootloader and the fuses were left as they were. A
chip erase followed by a bootloader write would leave exactly that. This run
did not see what did it. The platform and network blocks now exist only in the
2026-10-15 dump, also in `$HOME/isp/`.

`make boot-flash` writes no fuse (step 2), so it does not set `EESAVE`. The
read-out is kept regardless.

**2. What `make boot-flash` writes.**

- `mk/boot.mk` lines 424–425: `boot-flash: $(BOOT_TARGET).hex` runs
  `$(ARCH_BOOT_FLASH)`.
- `mk/arch/avr.mk` lines 110–114 set `ISP_PROG ?= usbasp` and
  `ISP_PORT ?= usb`. `ARCH_BOOT_FLASH` there is
  `avrdude -v -p$(AVRDUDE_PART) -c$(ISP_PROG) -P$(ISP_PORT) -Uflash:w:$<:i`.
- `boards/atmega2560/profile.mk` gives the boot section's size and base, and
  no fuse or lock value.

So it writes no fuse and no lock bit. It passes no `-D`, so avrdude erases the
chip first. That erases the application, the EEPROM (`EESAVE` is
unprogrammed) and the lock bits.

**3. `make boot-flash` does not build.**

```sh
make boot-flash BOARD=atmega2560 ISP_PROG=usbasp ISP_PORT=usb
```

The command exits 2 before avrdude runs. With no `BOOT_*` variables it builds
`make bootloader`'s default row:

- `BOOT_TIER` soft (`mk/boot.mk` line 32);
- `BOOT_SIG` ed25519-donna (line 51);
- `BOOT_MEASURE` soft (line 74);
- `BOOT_OTA` 1 (line 91).

The link fails at line 175, because the core alone is 9152 of the 8192 bytes,
before any signature backend. That row is not in `BOOT_SUPPORTED` (lines
217–221).

Read again over ISP afterwards, the signature and all four fuse bytes were
unchanged. The EEPROM's SHA-256 was `e6804337…bd1a1e2f`, the same as before.

| Row (`BOOT_TIER`:`BOOT_MEASURE`:`BOOT_SIG`:`BOOT_OTA`) | Where it comes from | Result | SHA-256 of the `.hex` |
|---|---|---|---|
| soft:soft:ed25519-donna:1 | the default, and the command above | does not link: core 9152 / 8192 | — |
| atecc608a:soft:ed25519-donna:1 | [hardware enrolment](hardware-enrollment.md) line 50, which sets `BOOT_TIER` only | does not link: core 10254 / 8192 | — |
| atecc608a:atecc608a:atecc608a:1 | `BOOT_SUPPORTED` | builds: 6840 / 8192 flash, 774 / 8192 RAM | `692055b275466c56a4963e6a4a61e4b3a54d58e6d854cf8c8176c0c68ad68dcf` |

Since 5342430 (2026-10-16), `make bootloader` and `make boot-flash` refuse the
two soft-signature rows at parse time, before anything is built:

```text
mk/boot.mk:520: *** boot-flash: atmega2560:soft:soft:ed25519-donna:1:0 (BOARD:BOOT_TIER:BOOT_MEASURE:BOOT_SIG:BOOT_OTA:BOOT_STORE) is not a supported configuration. For atmega2560 the supported rows, which fit, are: atmega2560:atecc608a:atecc608a:atecc608a:0:0 atmega2560:atecc608a:atecc608a:atecc608a:1:0 atmega2560:atecc608a:atecc608a:atecc608a:0:1. See BOOT_SUPPORTED in mk/boot.mk and docs/secure-boot/cli.md.  Stop.
```

Measured that day, the two refused rows' cores are 10 004 and 11 242 of 8192
(`make boot-core-size`), and the supported row builds to 7832 / 8192 flash and
1037 / 8192 RAM.

The supported row was not flashed, for three reasons:

- It needs the element. The ATECC608C is not fitted
  ([the matrix](../ethernet-transport/test-matrix.md#matrix)), and until the
  owner chooses its slot map it answers `NOT_PROVISIONED`.
- The EEPROM holds no provisioning, and "a bootloader flashed before
  provisioning has nothing to verify against and stays in recovery"
  ([secure boot](../secure-boot/user.md#build-and-flash-a-bootloader)).
- `boot-flash` replaces the STK500v2 bootloader that every application flash
  on the bench has gone through.

**4. Not run.** These are the EEPROM write-back, the application install,
`make hw-test`, the identity read-back and the `tfw-boot` evidence. This run
erased nothing, and no `tfw-boot` went in. `make hw-test` needs an
application, and the Mega has none now.

**Gaps found.** These are recorded, not fixed:

1. **`make boot-flash` rebuilds instead of flashing what was built.**
   [cli.md](../secure-boot/cli.md#make-boot-flash--write-tfw-boot-over-isp)
   line 91 and [user.md](../secure-boot/user.md#build-and-flash-a-bootloader)
   line 24 run it without the `BOOT_*` variables. Its prerequisite is
   `$(BOOT_TARGET).hex`, and the path of that file names the row
   (`BOOT_DIR_OUT`, `mk/boot.mk` line 109). So it rebuilds the default row
   instead of using the one `make bootloader` built a line earlier, and on the
   ATmega2560 the default row does not fit. The command needs the same four
   variables as `make bootloader`.
2. **[Hardware enrolment](hardware-enrollment.md) cannot run as written.**
   Line 50 passes `BOOT_TIER` alone, which builds a row that does not fit.
   Line 52 then flashes the application with `make flash` over
   `/dev/ttyACM0`, which talks to the STK500v2 bootloader that `boot-flash`
   has just replaced.
3. **The application has no documented route once `tfw-boot` is in.**
   [user.md](../secure-boot/user.md#build-and-flash-a-bootloader) lines 34–36
   say to flash the application "the normal way" by
   [flashing.md](flashing.md). That is the same serial route, and it has the
   same problem.

### The Mega re-provisioned over ISP, 2026-10-16

The Company Agent found what erased the part (m233): an Arduino IDE "Burn
Bootloader" over the USBasp. That is a chip erase with `EESAVE` unprogrammed,
then the stock bootloader. The Mega was treated as a blank stock board, and
its two blocks were written back over ISP with the values of the 2026-10-15
dump. No chip erase, no fuse, no lock bit, and no flash write over ISP.

**The image.** The two blocks are built by the project's encoders. No byte was
set by hand.

- **The platform block, `0x000`–`0x0BF`**, from `tfwprov`, built by
  `mk/platform-bake.mk`'s `build/tools/tfwprov` rule:

  ```sh
  tfwprov --bake-eeprom --serial 54465700000000000000000000002560 --out m2560-platform.eep
  ```

  The pubkey hash and the operator key are not given, so `tfwprov` bakes its
  documented all-zero default for both. That is what the 2026-10-15 block held
  ([the Mega run](../ethernet-transport/test-matrix.md#the-mega-run-2026-10-15)).
  The `.eep`'s SHA-256 is
  `1f1d2e349937a2f436f240d2069356e3beb08e10ce7116d0d4e50dd5aeed8c29`, the same
  file as that run's.
- **The network block, `0x0C0`–`0x0DF`**, from `sdk/go`'s
  `NetConfig.MarshalBinary`:
  - MAC `5a:ed:09:10:bf:59`;
  - IP `198.51.100.22`, mask `255.255.255.0`;
  - gateway `0.0.0.0`;
  - endpoint `198.51.100.1:7020`.

  These are the dump's values. No command emits this block as a file, so a
  helper outside the repository calls `MarshalBinary` and prints its 32 bytes
  as Intel HEX records at `tfw.NetConfigAddr`. The helper is
  `$HOME/isp/scripts/netblock`, a module whose `replace` points at this clone's
  `sdk/go`.

The two files are joined into one, `m2560-provision.hex`, which covers 224
bytes from `0x000` to `0x0DF`. Its SHA-256 is
`ebc30e37c5a2ad919ecf184b7ea54eff4284243a79e142698058a89e943289c3`. Over
`0x000`–`0x0FF` it equals the 2026-10-15 dump byte for byte, and the dump holds
only `0xFF` beyond `0x0DF`.

**The avrdude commands, as run:**

```sh
avrdude -c usbasp -P usb -p m2560 -U eeprom:r:m2560-eeprom-prewrite.hex:i
avrdude -c usbasp -P usb -p m2560 -D -U eeprom:w:m2560-provision.hex:i
avrdude -c usbasp -P usb -p m2560 -U eeprom:r:m2560-eeprom-post-provision.hex:i \
        -U lfuse:r:-:h -U hfuse:r:-:h -U efuse:r:-:h -U lock:r:-:h
```

The read before the write was all `0xFF`, the same SHA-256 as
[step 1](#the-mega-2026-10-16) (`e6804337…bd1a1e2f`). avrdude wrote and
verified 224 bytes. The fuses read `0xFF`, `0xD8`, `0xFD` and lock `0xCF`
after the write, as before it.

**The read-back**, all 4096 bytes. The Intel hex has SHA-256 `9d50fdf4…97e8a062`,
and the binary `9860d5e8…c8581cff`. Diffed three ways:

| Against | Bytes that differ |
|---|---|
| The image written | none in `0x000`–`0x0DF`; `0x0E0`–`0xFFF` all `0xFF` |
| The 2026-10-15 dump (`0x000`–`0x0FF`) | none |
| The read before the write | `0x000`–`0x0CF` and `0x0D3`–`0x0DF`: every byte of the two blocks that is not `0xFF` (`0x0D0`–`0x0D2` are the mask's `0xFF`). None outside the blocks |

**Through the firmware.**
[Secure boot](../secure-boot/user.md#build-and-flash-a-bootloader) sends the
application to [flashing](flashing.md). On a board with its stock bootloader,
that is `make flash`, which passes `-D` and writes flash only.

```sh
make flash BOARD=atmega2560 CRYPTO=soft ETH=1 AVRDUDE_PORT=/dev/ttyACM0
```

The command ran `avrdude -v -pm2560 -cwiring -P/dev/ttyACM0 -b115200 -D
-Uflash:w:build/atmega2560/soft-eth-w5100/trusted-firmware.hex:i`. It wrote and
verified 15262 bytes. The `.hex` is `923646184b…b0b26bb`, the second-pass image
of 2026-10-15.

- `tfwctl /dev/ttyACM0 caps` answers board `0x02` (atmega2560), protocol 2.0,
  root of trust `software`.
- `tfwctl eeprom-read`, in 28 reads of 8 bytes, reads `0x000`–`0x0DF` back
  equal to the image. That includes serial `54465700000000000000000000002560`
  at `0x034` and MAC `5a:ed:09:10:bf:59` at `0x0C6`.
- An ISP read after the application ran gives the same SHA-256 as the
  read-back above.

`make hw-test` and `ATTEST` evidence were not run. Behind a stock bootloader
the software tier has no UDS, so `ATTEST` answers "no provisioned identity"
([item 7](../ethernet-transport/test-matrix.md#the-mega-run-2026-10-15)).

**A gap, recorded, not fixed.** The network block has no offline encoder. It
can be written over serial with `WriteNetConfig`, or by a program that calls
`MarshalBinary`. `tfwprov --bake-eeprom` writes only the platform block, so a
part provisioned by ISP alone needs code outside the repository for its
network block.

### The Nano, next

This is for the owner, before the Nano slice:

1. **Move the USBasp to the Nano's ICSP header.** Pin 1 (MISO) is marked by a
   dot or a square pad, at the end opposite the USB socket.
2. **Take the Nano off its Ethernet base.** While the USBasp holds the 328P in
   reset, the Nano's D10 floats, and D10 is the base's Ethernet chip select.
   The chip can then be selected and drive MISO against the 328P. It also
   loads the SPI lines.
3. **Take the USBasp's target-power jumper off** while the Nano is on USB.
   With it on, the USBasp's 5 V would join the Nano's USB supply. The jumper's
   label differs between clones.
4. **Keep the Nano on USB as `/dev/ttyUSB0`.** It powers the Nano, and it is
   the port the check in step 5 flashes over.
5. **The slice itself:**
   - Read the signature and fuses.
   - Erase, and write `optiboot_atmega328.hex` (Optiboot 4.4, SHA-256
     `08a2c8a4…f77786`) with low fuse `0xFF`, high fuse `0xDE` and extended
     fuse `0xFD`. On the 328P, `0xFD` is the Makefile's `0x05` in the
     implemented bits 2:0.
   - Write no lock bit.
   - Check the result by flashing over `/dev/ttyUSB0` with `make flash
     BOARD=atmega328p`. That uses the `arduino` programmer at 115200, from
     `boards/atmega328p/profile.mk`.

## The programmer: ArduinoISP on the Mega — fallback path, not taken (owner, 2026-10-15)

### The sketch, changed for the Ethernet shield

The Mega keeps its Ethernet shield. The shield's W5100 chip select is D10,
which is also ArduinoISP's default target reset, and its SD card's chip select
is D4. The shield's SPI lines come from the Mega's ICSP header, which is the
same bus (D50–D52) that ArduinoISP drives to the target. So the sketch is
changed in two ways:

- The target reset moves from D10 to **D22**. No shield uses it, and it is on
  the Mega's double-row header beyond the shield.
- `setup()` drives D10 and D4 high for good, so the W5100 and the SD card stay
  deselected and never drive MISO while the target is programmed.

The Arduino IDE generates prototypes for a sketch. The build below uses plain
`avr-g++`, so the patch declares the three functions the sketch uses before
defining them. It adds no conditional.

```diff
--- ArduinoISP.ino
+++ ArduinoISP.ino
@@ -70,7 +70,11 @@
 // The standard pin configuration.
 #ifndef ARDUINO_HOODLOADER2
 
-  #define RESET     10 // Use pin 10 to reset the target rather than SS
+  // trusted-firmware bench: D10 is the Ethernet shield's W5100 chip select and
+  // D4 its SD card's, so the target reset moves to D22, which no shield uses.
+  #define RESET     22
+  #define SHIELD_CS_ETH 10
+  #define SHIELD_CS_SD  4
   #define LED_HB    9
   #define LED_ERR   8
   #define LED_PMODE 7
@@ -157,6 +161,9 @@
 #define CRC_EOP     0x20 //ok it is a space...
 
 void pulse(int pin, int times);
+void avrisp();
+uint8_t write_flash_pages(int length);
+uint8_t write_eeprom_chunk(unsigned int start, unsigned int length);
 
 #ifdef USE_HARDWARE_SPI
 #include "SPI.h"
@@ -222,6 +229,13 @@
 #endif
 
 void setup() {
+  // Deselect the shield's W5100 and SD card for good, so neither drives MISO
+  // while the target is programmed over the same SPI bus.
+  digitalWrite(SHIELD_CS_ETH, HIGH);
+  pinMode(SHIELD_CS_ETH, OUTPUT);
+  digitalWrite(SHIELD_CS_SD, HIGH);
+  pinMode(SHIELD_CS_SD, OUTPUT);
+
   SERIAL.begin(BAUDRATE);
 
   pinMode(LED_PMODE, OUTPUT);
```

The patch applies with `patch -l`, because the sketch's whitespace mixes tabs
and spaces.

### Build

The sketch and the Arduino core (`cores/arduino`, `variants/mega`,
`libraries/SPI`) are compiled for the Mega with `-Os` and per-function
sections, without LTO:

```sh
cp ArduinoISP.ino ArduinoISP.cpp && patch -l ArduinoISP.cpp arduinoisp-mega.patch
DEFS="-mmcu=atmega2560 -DF_CPU=16000000L -DARDUINO=10819 -DARDUINO_AVR_MEGA2560 -DARDUINO_ARCH_AVR"
INC="-I$AVR/cores/arduino -I$AVR/variants/mega -I$AVR/libraries/SPI/src"
# every cores/arduino/*.c:   avr-gcc -std=gnu11 -Os -g -ffunction-sections -fdata-sections $DEFS $INC -c
# every cores/arduino/*.cpp, SPI.cpp and ArduinoISP.cpp:
#   avr-g++ -std=gnu++11 -fno-exceptions -fno-threadsafe-statics -fpermissive (same flags) -c
# every cores/arduino/*.S:   avr-gcc -x assembler-with-cpp $DEFS $INC -c
avr-gcc -mmcu=atmega2560 -Os -Wl,--gc-sections *.o -o ArduinoISP.elf -lm
avr-objcopy -O ihex -R .eeprom ArduinoISP.elf ArduinoISP.hex
```

`$AVR` is `$HOME/arduino/usr/share/arduino/hardware/arduino/avr`. The sketch's
`SPI_CLOCK` (166 kHz) is above `F_CPU / 128`, so it uses the hardware SPI on
D50 (MISO), D51 (MOSI) and D52 (SCK), at 125 kHz.

| Artifact | Size (text / data / bss) | SHA-256 |
|---|---|---|
| `ArduinoISP.cpp`, patched | | `4151e5e00c8f338daf702e38bec03f4128dfb0c3283c039a792ee3554e888a6a` |
| `ArduinoISP.hex` for the ATmega2560 | 8216 / 46 / 934 bytes | `e279c6d8e1ac2da8882f5b985665733fd78e65ea2efcec96c24d4d151ea2ef1e` |

### Flash and check, 2026-10-15

Flashed through the Mega's stock bootloader, which avrdude verified:

```sh
avrdude -v -c wiring -p m2560 -b 115200 -P /dev/ttyACM0 -D -U flash:w:ArduinoISP.hex:i
```

Before the flash, the Mega ran the `CRYPTO=soft ETH=1` image. Its EEPROM
`0x000`–`0x0FF`, read with `tfwctl eeprom-read`, held the platform block
(serial `54465700000000000000000000002560`) and the network block at `0x0C0`
(MAC `5a:ed:09:10:bf:59`). A bootloader flash with `-D` does not touch the
EEPROM; the restore at the end of the run checks that.

Then the programmer check, with no target wired:

```sh
avrdude -c stk500v1 -b 19200 -P /dev/ttyACM0 -p m328p
```

1. **On a freshly opened port: a sync error,** `not in sync: resp=0x00` ten
   times. Opening the port makes the 16U2 pulse the Mega's reset, and the
   Mega's bootloader, not ArduinoISP, is running when avrdude sends its sync
   bytes.
2. **After `stty -F /dev/ttyACM0 -hupcl` and a 3-second wait: the programmer
   answers.** avrdude reported `AVR device initialized and ready to accept
   instructions`, then `device signature = 0x000000` and `Invalid device
   signature`, which is what no target looks like. `-hupcl` leaves DTR
   asserted when a program closes the port, so the next open makes no DTR
   edge and the Mega is not reset. The `stty` call itself resets the Mega
   once; the wait lets ArduinoISP start.

So the auto-reset is real, and `-hupcl` clears it without hardware. The 10 µF
capacitor on the Mega's RESET is **not** fitted. It would also stop the Mega's
own bootloader from being reached, which the restore at the end of the run
needs.

## Wiring: the Mega programs the Nano

Part of the ArduinoISP route: a fallback path, not taken (owner, 2026-10-15).

The owner wires this with both boards on their USB cables. Nothing is written
to the Nano before it is done.

1. **Mega D50 (MISO) → Nano ICSP pin 1 (MISO).** The same net as the Nano's D12.
2. **Mega D51 (MOSI) → Nano ICSP pin 4 (MOSI).** The same net as the Nano's D11.
3. **Mega D52 (SCK) → Nano ICSP pin 3 (SCK).** The same net as the Nano's D13.
4. **Mega D22 (target reset) → Nano ICSP pin 5 (RESET).** The same net as the
   Nano's RST pin.
5. **Mega GND → Nano ICSP pin 6 (GND).**
6. **5 V: not connected.** The Nano stays on its USB cable, which powers it, and
   a wire from the Mega's 5 V would join two USB supplies. If the Nano is ever
   taken off USB for this, connect Mega 5V → Nano ICSP pin 2 (VCC) instead.
7. **No capacitor** on the Mega's RESET. Step 2 above showed the auto-reset is
   handled in software.
8. **The Mega's Ethernet shield stays on.** The patched sketch holds the W5100
   (D10) and the SD card (D4) deselected and resets the target from D22. The
   Mega's ICSP header is under the shield, so take D50, D51, D52, D22 and GND
   from the Mega's double-row header beyond the shield: D22 is at the 5 V end,
   D50–D52 and GND are at the far end.
9. **The Nano comes off its Ethernet base.** While the Nano is held in reset
   its D10 floats, so the base's Ethernet chip can be selected and drive MISO
   against the 328P during programming, and the chip loads the SPI lines. The
   Nano stays plugged into USB as `/dev/ttyUSB0`.

The Nano's ICSP header is the 2×3 header at the end opposite the USB socket.
Pin 1 (MISO) is marked with a dot or a square pad; the labelled pins in each
item carry the same nets and are the safer choice if the marking is unclear.

## Burning Optiboot into the Nano

Part of the ArduinoISP route: a fallback path, not taken (owner, 2026-10-15).

Pending the wiring. The planned commands, each preceded by
`stty -F /dev/ttyACM0 -hupcl` and a 3-second wait:

```sh
avrdude -c stk500v1 -b 19200 -P /dev/ttyACM0 -p m328p \
        -U signature:r:-:h -U lfuse:r:-:h -U hfuse:r:-:h -U efuse:r:-:h -U lock:r:-:h
avrdude -c stk500v1 -b 19200 -P /dev/ttyACM0 -p m328p -e \
        -U lfuse:w:0xFF:m -U hfuse:w:0xDE:m -U efuse:w:0x05:m \
        -U flash:w:optiboot_atmega328.hex:i
```

`-e` erases the flash, clears any lock bits the part holds (it writes none),
and erases the EEPROM unless the part's current high fuse has EESAVE
programmed. The Nano's EEPROM holds the old application's data, which the
owner freed.

## The next step: the Nano programs the Mega

Part of the ArduinoISP route: a fallback path, not taken (owner, 2026-10-15).

Recorded here so the next run can check it before it starts:

- **Wiring.** The Nano, off its base and on USB, runs the stock ArduinoISP
  (reset on D10, which is free once the base is off). The Mega's Ethernet
  shield comes off so its ICSP header is reachable and its W5100 is off the
  bus while the Mega is held in reset (its D10 floats then). Nano D12 → Mega
  ICSP pin 1 (MISO), D11 → pin 4 (MOSI), D13 → pin 3 (SCK), D10 → pin 5
  (RESET), GND → pin 6; no 5 V wire while both boards are on USB. The Nano's
  CH340 resets it on open the same way, and `stty -hupcl` clears it the same
  way.
- **`make boot-flash` passes no baud.** `ARCH_BOOT_FLASH` in `mk/arch/avr.mk`
  is `-c$(ISP_PROG) -P$(ISP_PORT)` with no `-b`, and ArduinoISP talks at
  19200, not avrdude's 115200 default. Either the target gains an `ISP_BAUD`
  or the run passes `ISP_PROG='stk500v1 -b19200'`.
- **`make boot-flash` erases the Mega's EEPROM.** It writes flash without
  `-D`, so avrdude erases the chip first, and the Mega's stock high fuse
  (`0xD8`) leaves EESAVE unprogrammed. The platform block at `0x000` and the
  network block at `0x0C0` would be lost. Read the EEPROM out over ISP before
  the flash and write it back after, or program EESAVE first: a fuse decision
  for that run.
