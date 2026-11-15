# Bench board: NXP FRDM-IMX95-PRO (i.MX 95, row #428)

The read-only factory record of one bench FRDM-IMX95-PRO under CA ruling 583:
these boards become the HA HSM cluster, so nothing on them is programmed,
fused, closed or written without the owner. The Company Agent's own capture
(the lifecycle, the SRK fuses, OP-TEE, the console baud) came first; this
record adds the reads that capture did not hold and checks the
lifecycle against it. The record files stay on the bench host; their SHA-256
sums are listed at the end. No serial number, UID, ECID, MAC or eMMC CID is
written here.

Nothing was written to the board: no `fuse prog`/`override`/`sense`, no
`ahab_*` or `ele_message`, no `setenv`/`saveenv`, no `mmc` write, erase or
partconf, no `ums`/`uuu`/`fastboot`, SW4 unchanged (eMMC).

| | |
|---|---|
| Board | NXP FRDM-IMX95-PRO, `fsl,frdm-imx95-pro`, board rev V3.3 (U-Boot `BOARD:`) |
| SoC | i.MX9596 Rev 2.0 (B0), part MIMX9596XVZXNAC, A55 at 1800 MHz, extended industrial (-40 to 125 C) |
| DRAM | 15.8 GiB LPDDR5 |
| ELE lifecycle | `0x0010`, OEM Open (U-Boot `ele_info` LC `0x40010`, SM `ele info`); equals the CA capture |
| SRK hashes | OEM SRKH and PQC SRKH all zero: not fused, secure boot not enabled |
| ELE firmware | 2.0.6, commit 09a9290a; SSSM state 4, TRNG state 0x3, CSAL state 0x2 |
| System Manager | Build 896, commit 44f76dcb (16 Jun 2026), config `mx95frdm-pro`, mSel 0, 3 logical machines |
| Boot ROM path | Boot device MMC1 (eMMC), normal mode, primary stage, image set 0 at offset 0x8000 |
| TF-A (BL31) | v2.14.1, `lf-6.18.20-2.0.0-rc2` |
| U-Boot | 2026.04-lf_v2026.04+g6eeef838dac+p0 (SPL and proper), environment not saved (default env, bad CRC) |
| OP-TEE | 4.10 (37c7fbf84c40eb9e), `/dev/tee0` |
| Linux | 6.18.20-2.0.0, NXP i.MX Distro 6.18-wrynose (`fsl-imx-xwayland`) |
| Consoles | J22 quad UART: interface 0 is the A55 (LPUART1, 115200 8N1), interface 2 the M33 SM monitor (`>$`, 115200), interface 4 the M7 (silent) |

## Console note

Before this lane's first reboot the A55 console read cleanly only at 57600
on the host (the CA's CH9114 note). After a Linux `reboot` it read cleanly
at 115200, and every record after that is at 115200. Never send a serial
break: it fires the Linux SysRq.

## Reset behaviour

A Linux `reboot` resets the A55 logical machine only: the SM and ELE keep
running (SM `reason`: boot reason `por`; Linux: SYS reason `por`, LM reason
`sw`). The boot log therefore starts at U-Boot SPL; the ROM, ELE and SM boot
banners need a power cycle and are not recorded (open). The SM console
printed nothing during an A55 reboot.

## SCMI to the System Manager (from the Linux boot log)

SCMI v2.1 over the mailbox transport, vendor `NXP:IMX`, firmware version
0x380; max message size 104 bytes, 20 messages, rx timeout 5000 ms.
NXP vendor protocols: 0x80 LMM v1.1 (3 logical machines), 0x81 BBM v1.0
(RTC, power key), 0x82 CPU v1.0 (9 CPUs: m33p, m7p, a55c0 to a55c5, a55p),
0x84 MISC v1.1 (7 device controls, 8 board controls). 13 performance
domains. The SM's `err` log held one SCMI error, channel 5, protocol 0x11
(power domain), message 0x2, status -4.

## Fuses (U-Boot `fuse read`, through the ELE)

Banks 0 to 95 were read, 8 words each, then word by word where a bank
refused. Fully readable: banks 1 to 5, 16, 17, 41 to 51, 56 to 75.
Partly readable: 0, 6, 7, 22, 23, 39, 40. Refused (ELE response `0xbc29`):
the rest of 8 to 39, 52 to 55, 76 to 95. Every read refusal is logged as
an ELE event (`0x0397BC29`), so the ELE event log after this record holds
those events; it is volatile and a power cycle clears it. Bank 6 words 0
to 3 hold the UID and bank 1 words 4 to 7 the ECID; their values stay in
the record file.

## eMMC

| | |
|---|---|
| Device | 29.64 GiB (31826378752 bytes, 62160896 sectors), HS400 enhanced strobe, 8-bit, 1.8 V |
| Table | DOS: p1 FAT32 (LBA), bootable, sectors 16384 to 540671 (256 MiB, `/run/media/boot-mmcblk0p1`); p2 Linux ext4, sectors 540672 to 25785229 (12 GiB, `/`). The rest is unpartitioned |
| boot0, boot1 | 33030144 bytes each, read-only (`force_ro` 1), both all zero: SHA-256 `5d1f03c3d35232b4f5042ffc8133935dbcae29f701e5a71eda33057644cab80d`, the hash of 33030144 zero bytes. The boot image is in the user area at 32 KiB |
| RPMB | 4 MiB |

The full eMMC backup is open: Ethernet is down and the USB transfer waits
for the owner's cable (CA).

## Record files (SHA-256)

| File | SHA-256 |
|---|---|
| imx95-linux.txt (`/proc/version`, os-release, soc0, dmesg) | `9d2f6926941120c16b91a82535c72e8a15d24ab7f2673fa25bef3375226f61fe` |
| imx95-linux-hw.txt (`/proc/iomem`, eMMC ios, device tree) | `1fb5a6bf6d1d110bb41e7c3bc462acea0e7be460302d26452ec0a2bcadc86d88` |
| imx95-emmc-layout.txt | `39e839945e48eb5109cf25173b9f02ced9d6b1f8bd345a665bd159411a8bd784` |
| imx95-boot1-a55-57600.log (first reboot, unreadable after reset) | `35a6bdfc7e8c5928f72ed61ae8968d17c9dd0b42fd998eee0e45e88cc3496340` |
| imx95-boot-a55.log (SPL to Linux login) | `1caa690b8dedd0b5ce1b4c619d25f8069017edd68305e800d5d653327c65a741` |
| imx95-boot-sm.log (empty: the SM does not reboot) | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| imx95-uboot-boot.log (boot to the U-Boot prompt) | `3d0ccf355ca78fd0d4d53d2848f3756a5feb1bd0983a1decec1b4175515b16e1` |
| imx95-uboot.txt (`help`, `version`, `bdinfo`, `printenv`, `mmc`) | `e0fe3c21d76b408da2117129b6c5a149eb6796e815bc07dc42578c5a3873ec21` |
| imx95-ahab-first.txt (`version`, `ele_info`) | `1b79df8136fa28ada82566c62738dce91ea635b79320979a5d697afc8c4f538b` |
| imx95-ahab-last.txt (`ele_info` at the end) | `34335f978449f9a13c7050c9a60c7e79467fc4f4e535c9287988b4f374d47ef5` |
| imx95-fuses.txt (banks 0 to 95) | `c032a3d61c227e233f0a90d5efcee303a85bfc445cfa19dc892177903d691b93` |
| imx95-fuses-words.txt (refusing banks word by word) | `bc312e3fd363c4c482fc27ddc762bdb558bbda3ff1864968759aa2a2d66a276c` |
| imx95-sm.txt (SM `help`, `info`, `ele info/ext/events`, `err`, `btime`, `reason`) | `38830cdb7cba141bae1de04514a5258da52a3df0edfaed483c83ec109a66a70a` |
| imx95-boot-after-uboot.log (`boot` back to Linux) | `12b5d6f2468b6bb724fa1add80d5d2c3b24a1d2c5e833e3c6d231a1fa9917e18` |
| imx95-dt.txt (device tree nodes for the M7's peripherals, the M7 node) | `d80bbbef63e135a0e20613d09908e771eafddf5b51517b9fe911a6f29379faf1` |

U-Boot has no `ahab_status` command; `ele_info` (display only) is its
lifecycle read, and the first and last reads are identical.

## Cortex-M7 image in RAM, console capture

Row 428 follow-on (ruling 625 (3)). The board has no Ethernet cable; every
step runs over the four-port USB-UART bridge on the board's debug USB (J22).
Ports are named here by role, never by device path. Bridge ports 0 to 3 are
the USB interfaces 0, 2, 4 and 6 of the Consoles row above.

Rules in force: no serial break or SysRq on any port; no fuse, ELE
lifecycle, SRK or secure-boot command; no write to eMMC; no `saveenv`.

### 1. Board state before any load (2026-11-14)

The four ports were opened at 115200 8N1, raw, `-hupcl`, and read for 8 s
with nothing sent: all four were silent. One carriage return was then sent
to each port, one at a time:

| Bridge port | Role | Answer to CR |
|---|---|---|
| port 0 | A55 Linux console (`ttyLP0`) | root shell prompt |
| port 1 | System Manager (M33) debug monitor | `>$` |
| port 2 | M7 console (LPUART3), identified in section 3 | nothing (idle) |
| port 3 | silent | nothing |

A55 (Linux), read-only commands:

```
Linux ... 6.18.20-2.0.0-gb096ce610e95 #1 SMP PREEMPT Tue Jun  9 02:25:20 UTC 2026 aarch64
ID=fsl-imx-xwayland  NAME="NXP i.MX Distro with Wayland and XWayland"
cmdline: console=ttyLP0,115200 earlycon root=(eMMC p2) rootwait rw
remoteproc0 neutron-rproc offline
remoteproc1 imx-rproc     attached   (firmware: unknown)
imx-rproc imx95-cm7: Using LMM Protocol OPS
imx-rproc imx95-cm7: lmm(1) not under Linux Control
remoteproc remoteproc1: remote processor imx-rproc is now attached
/lib/firmware: NXP imx95-15x15-evk_m7_TCM_* demo images (hello_world,
               power_mode_switch, rpmsg_lite_pingpong_rtos_linux_remote)
```

System Manager monitor, read-only commands (`info`, `lm info`, `cpu.r`):

```
SM Version    = Build 896, Commit 44f76dcb
SM Config     = mx95frdm-pro, mSel=0
Silicon       = i.MX95 B0, MIMX9596XVZXNAC
Boot mode     = normal   Boot device = MMC1   Boot stage = primary   Boot set = 1
lm info:  000: SM = on   001: M7 = on   002: AP = on (default)
cpu.r:    001: m7p => run-mode = RUN, slp-mode = RUN, vector = 0x00000000_00000000
          a55c0..5 vector = 0x00000000_8A200178
```

Reading: the SM boots the M7 in its own logical machine (LM1) from the
boot container at reset, with its vector at 0 (ITCM). Linux only attaches
to it (`not under Linux Control`): the LM is not Linux's to stop, load or
start, so the remoteproc path in the brief is closed on this SM
configuration (no remoteproc state change was tried). The M7 is controlled from the SM monitor (`lm`).

### 2. Build

```
make BOARD=frdm-imx95-m7 firmware
frdm-imx95-m7 flash  10696 / 196608 ( 4% of part)   ram   816 / 65536 ( 0% of part)
built build/frdm-imx95-m7/soft/trusted-firmware.hex
```

`arm-none-eabi-objcopy -O binary` gives a 10 696-byte image for ITCM
`0x00008000` (A55 view `0x203C8000`); its vector table opens with SP
`0x2003FF80`, reset `0x0000A061`. The image carries `.resource_table`.

### 3. Load attempts (RAM only): every route is closed on mSel=0

| Route | Command | Result |
|---|---|---|
| Linux remoteproc | (state read only) | `lmm(1) not under Linux Control`: attach only, no stop, no start, no firmware load |
| A55 write to the TCM through `/dev/mem` | read 16 bytes at `0x203C0000` with `mmap` | `Bus error` (SIGBUS) in the process; the kernel is unaffected. TRDC gives the AP domain no access to the M7 TCM |
| SM monitor `md`/`mm` | `md 0x203C0000 8`, `md 0x90000000 4` | `????????` on the TCM and on DDR alike: the monitor reaches no memory outside its own |
| SM monitor M7 vector | `cpu.w 1 vector` | `Invalid parameter(s)!`: the monitor sets no CPU vector |
| U-Boot `loady` + `bootaux` | not run | U-Boot runs on the A55 in the same LM, under the same TRDC as Linux, and NXP's `bootaux` on i.MX 95 needs the M7 LM under AP control |

The M7 LM is booted only by the SM, from the M7 image in the eMMC boot
container. Loading our image therefore needs one of: a boot container with
our M7 image (an eMMC write, forbidden in this lane), the SM configuration
with the M7 under AP control (`mSel` 1, also set in the boot container), or a
serial-download boot of a new container (boot-mode switches and a USB
cable: a person at the bench). Each is an owner decision.

#### M7 console identified (vendor image)

`lm reset 1` on the SM monitor, meant for the M7, rebooted the A55 instead,
and the M7 kept running. Why is not known: the monitor's `lm` argument may
not be the `lm info` row number. The vendor M7
image, still running, printed on port 2 while Linux rebooted:

```
Other side(AP) entered poweroff state through linux command: poweroff
Handle Peer Core Reboot
Handle Peer Core Linkup
```

So port 2 is the M7 console at 115200 8N1. After the reboot the board was
in its section-1 state again: Linux up, `remoteproc1 attached`, SM
`lm info` all three on. Nothing was written to eMMC, fuses or the ELE.
Before the next `lm` command, confirm which LM its argument names
(NXP's SM monitor documentation).

Our image's console output is not captured: no RAM-only loader exists on
this board as configured.

## imx-boot with our M7 image (ruling 644)

Tenant Agent ruling 644 (1): the board is a dev board with full write access
(owner 10-16); load the M7 image the non-volatile way, in the boot container
(imx-boot) that the System Manager boots LM1 from.

Plan, in order:

1. Read the current boot container from the eMMC user area (image set 0 at
   offset 0x8000, the Boot ROM path row above) from the A55 Linux, copy it to
   the bench host, and record its SHA-256, size, device and offset here,
   matching on both sides. The backup binary stays on the bench host, out of
   git.
2. Build an unsigned imx-boot that carries our Cortex-M7 image for LM1, from
   the same SoC firmware pieces as the board's container (ELE firmware,
   OEI, System Manager with the same mSel 0 configuration, TF-A, U-Boot):
   only the M7 image changes.
3. Write it from the A55 Linux to the same device and offset, `sync`, read it
   back and compare digests.
4. `reboot` from the A55 console; capture port 2 (the M7 console) for our
   banner and port 0 for U-Boot.

Recovery path, stated before any write (ruling 644 (1)): restore the backup
from the A55 Linux; if the board does not boot, uuu over USB serial
download, which needs the owner to set SW4.

Rules: no fuse, OTP, ELE lifecycle or SRK command; no signed or
AHAB-authenticated container; the write touches the boot container location
only (no other partition, no `saveenv`, no rootfs change); no serial break.

### 1. Backup of the boot container (2026-11-14)

The board's Ethernet is now up on the development network; every A55 command
below ran over SSH as root, in the foreground, its output captured. The
partition table puts p1 at sector 16384, so the image set lives in the user
area between 32 KiB and 8 MiB:

```
dd if=/dev/mmcblk0 of=emmc-head-8M.bin  bs=512 count=16384           # sectors 0..16383, MBR included
dd if=/dev/mmcblk0 of=imx-boot-orig.bin bs=512 skip=64 count=16320   # offset 0x8000 (32 KiB) to 8 MiB
```

| File | Device, offset | Size | SHA-256 (board and bench host alike) |
|---|---|---|---|
| imx-boot-orig.bin | eMMC user area (`mmcblk0`), 0x8000 | 8355840 | `1d775624af162149a6c974d8c838b8b618f027bcf70dad5dc9a1c397a890081f` |
| emmc-head-8M.bin | eMMC user area, 0x0 | 8388608 | `f2168cefa7eed2dd098bdf20f35d86a624bf4cf7e9e274aa4c48c529c920daa5` |

Copied off the board with `scp` and hashed again on the bench host. Both
files stay on the board (`/root/imxboot-backup/`) and in the lane clone's
`build/imx95-imxboot/`; neither is in git (NXP firmware).

The image set it holds (offsets from 0x8000):

| At | Container | Images |
|---|---|---|
| 0x0000 | ELE firmware, NXP-signed | ELE image at 0xC000, 0xE1D0 bytes |
| 0x8000 | boot container, version 2, unsigned (signature block without a signature), every image SHA-384 | 0: DDR training data marker; 1: OEI (M33, 0x1FFC0000); 2: System Manager (M33); 3: M7, 0x8C00 bytes at 0x303C0000 (ITCM base), entry 0; 4: U-Boot SPL (A55, 0x20480000); 5: dummy at 0x8B000000 |
| 0xEA800 | second container, unsigned | TF-A at 0x8A200000, U-Boot at 0x90200000, OP-TEE at 0x8C000000 |

### 2. Build

The SM starts the M7 at vector 0 (the ITCM base), where the board's map
reserves tfw-boot, which is not built for this board. The bench image is
therefore our application linked at ITCM 0:

```
make BOARD=frdm-imx95-m7 APP_BASE=0x00000000 BOOT_SECTION=0 BUILD_DIR=build/imx95-imxboot/m7-at-0 firmware
arm-none-eabi-objcopy -O binary build/imx95-imxboot/m7-at-0/frdm-imx95-m7/soft/trusted-firmware.elf m7-at-0.bin
boards/frdm-imx95-m7/imx-boot-m7.py imx-boot-orig.bin m7-at-0.bin imx-boot-m7.bin
```

11216 bytes, initial SP `0x2003FF80`, reset `0x00002245`. The script
(`boards/frdm-imx95-m7/imx-boot-m7.py`) does what imx-mkimage's
`-m7 <image> 0 0x303C0000` does without rebuilding the rest: it replaces
entry 3's data (padded to 0x2C00), size and SHA-384 and zeroes the rest of
its slot. Every other byte of the image set is the board's: the ELE
container, OEI, the System Manager with its mSel 0 configuration, SPL, TF-A,
U-Boot and OP-TEE. Changed bytes span 0x8195 to 0xC03AB only (the entry and
the M7 slot); `imx-boot-m7.py --show` checks every SHA-384 again.

imx-boot-m7.bin: 8355840 bytes, SHA-256
`2babcf24c5b1e65a8dce8f00ee3413b5692faa464ed9a9defe14a119e33f285c`.

### 3. Write and read-back

Copied to the board with `scp`; its SHA-256 on the board matched, and the
device still read as the backup just before the write. Then, from the A55
Linux:

```
dd if=imx-boot-m7.bin of=/dev/mmcblk0 bs=512 seek=64 conv=fsync,notrunc   # rc 0
sync
dd if=/dev/mmcblk0 bs=512 skip=64 count=16320 iflag=direct | sha256sum   # 2babcf24...f285c, matches
dd if=/dev/mmcblk0 bs=512 count=64 iflag=direct | sha256sum              # first 32 KiB unchanged (MBR)
```

### 4. Boot and the M7 console

All four ports were read for the whole of each step; nothing but the one
command line was sent, and no break.

**Linux `reboot` (A55 console, port 0).** The A55 went through U-Boot SPL,
U-Boot and Linux to the login prompt, `remoteproc1` attached again. As the
reset-behaviour section says, this resets the A55 LM only: the M7 kept the
vendor image, which printed on port 2 `Handle Peer Core Reboot` and
`Handle Peer Core Linkup`. The ROM did not read the new container.

**System reset (SM monitor `reset`, port 1).** The vendor M7 printed
`SCMI system notification: graceful=0, state=0x8` and was cut off. The SM
console then showed the whole boot from the new container:

```
Reset request: reason=sw, errId=0, srcLm=0
DDR OEI: (Build 217, Commit e9bce6f4, Jun 16 2026 06:04:20)
DDR OEI: Compiled for SOC MIMX95(B0), Board mx95lp5
DDR OEI: done, err = 0
Hello from SM (Build 896, Commit 44f76dcb, Jun 16 2026 06:04:24)
```

The A55 reached U-Boot and the Linux login. SM `lm info`: SM, M7, AP all
`on`; `cpu.r`: `m7p => run-mode = RUN, vector = 0x00000000_00000000`. So
the ROM and ELE accepted the container, and the SM started LM1 at ITCM 0 on
our image.

**Port 2 stayed silent** for the 120 s after the reset: our image's boot
event (`TFW_EVENT_BOOT`, the first frame `main` sends) did not appear.
Reason, from NXP's FRDM-IMX95 M7 board support (mcuxsdk-examples,
`_boards/frdmimx95/common/pin_mux/pin_mux.c`, `BOARD_InitPins`): the M7
console pads, GPIO_IO14 (LPUART3 TX) and GPIO_IO15 (LPUART3 RX), are muxed
by the M7 image itself at startup, through SCMI pin control to the System
Manager (`HAL_PinctrlSetPinMux`), which owns the IOMUXC. The vendor image did
that before the reset; a system reset returns the pads to their default, and
our image has no SCMI channel (`TFW_IMX95_SCMI_MU_BASE` is 0 in
`board_profile.h`), so its LPUART3 drives no pad. Also open: the M7 NVIC line
of LPUART3 (64, TO CONFIRM in `soc_regs.h`), which our transmit path needs.

### 5. Board state after this lane

The eMMC boot container is imx-boot-m7.bin (our M7 image, silent); the
board boots to Linux as before, the M7 runs our image. To return to the
vendor image: `dd if=imx-boot-orig.bin of=/dev/mmcblk0 bs=512 seek=64
conv=fsync,notrunc` from the A55 Linux (the backup is on the board and the
bench host), then SM `reset`. Not done: the next step only replaces the M7
image again.

### 6. Firmware slice: the pad request (no board access)

The M7's SCMI channel is set in `board_profile.h` (MU5_MUA, its SRAM,
doorbell 0, CRC-32 as the SM's M7 mailbox checks it). At startup
`tfw_hal_uart_init` first calls `console_pins.c`, which sends the SM
PINCTRL_SETTINGS_CONFIGURE for GPIO_IO14 then GPIO_IO15 (SM pin ids 18 and
19), mux ALT1, and starts LPUART3 whatever the SM answers. Not sent yet:
the pads' configuration and RX's daisy (both TO CONFIRM), so only TX is
expected to work. Host test: `test_imx95_console_pins`.

### Next slice (device)

Rebuild (`make BOARD=frdm-imx95-m7 APP_BASE=0x00000000 BOOT_SECTION=0
BUILD_DIR=build/imx95-imxboot/m7-at-0 firmware`), rerun `imx-boot-m7.py`,
write at 0x8000, read back, digest match, SM `reset`, then read port 2 at
115200 8N1 for 120 s. What port 2 must show:

- Pass: our boot event, the `TFW_EVENT_BOOT` frame `main` sends first
  (frame bytes, not text), within seconds of the reset. That confirms the
  SCMI channel (MU, shared memory, CRC), pin ids 18/19 with ALT1, and the
  LPUART3 clock.
- Still silent: the SM refused or never answered. Read the SM monitor
  (port 1, `>$`) for the M7 agent's errors; next, check the CRC polynomial
  and the channel's shared memory address against the SM's configuration.
- Frames, but no reply to a host request: TX works and RX does not, so the
  next firmware slice adds RX's daisy and confirms NVIC line 64.

### 7. Device run of the pad-request slice (2026-11-15)

Plan, in order (ruling 644 (1) again):

1. Build the M7 image at `47d3874b8` with the section-2 recipe and
   `imx-boot-m7.py` over the original backup; record both SHA-256s.
2. From the A55 Linux: read the boot container now at 0x8000 (expected: the
   section-2 imx-boot-m7.bin, `2babcf24...f285c`) and record its digest.
   `imx-boot-orig.bin` stays the recovery image.
3. Copy the new imx-boot over, check its digest on the board, write it at
   0x8000 (`dd ... seek=64 conv=fsync,notrunc`), `sync`, read back and
   compare.
4. `reboot` from the A55 console (resets the A55 LM only, section 4), then
   SM `reset` on port 1 so the ROM reads the new container. Capture port 2
   and port 0 throughout; compare port 2 with the list above.

Recovery path, stated before the write: restore `imx-boot-orig.bin` from the
A55 Linux (section 5 command); if the board does not boot, uuu over USB
serial download, which needs the owner to set SW4. If the board does not
reach U-Boot, no further write. No serial break on any port.

#### Build

```
make BOARD=frdm-imx95-m7 APP_BASE=0x00000000 BOOT_SECTION=0 BUILD_DIR=build/imx95-imxboot/m7-at-0 firmware
frdm-imx95-m7 flash  11780 / 196608 ( 4% of part)   ram   840 / 65536 ( 0% of part)
```

The link prints `warning: dot moved backwards before '.faultframe'` (open:
check it is harmless at APP_BASE 0). The original backup was read back off
the board (`1d775624...0081f`, matching section 1) and
`imx-boot-m7.py` rerun over it:

| File | Size | SHA-256 |
|---|---|---|
| m7-at-0.bin (SP `0x2003FF80`, reset `0x00002461`) | 11780 | `d5e5d42937652c7a8798d2c6f428f6144e4dc9073696ff2889f0cb04bf4896ea` |
| imx-boot-m7.bin (pad-request slice) | 8355840 | `034190704c106ae9433218d236c7b935d1984db90c0e94becca796047b8a5980` |

Changed bytes span 0x8195 to 0xC03AB again; entry 3 is now 0x3000 bytes at
0x303C0000, and `--show` matches every SHA-384.

#### Write and read-back

The A55 commands ran over SSH as root, in the foreground. Before the write
the device at 0x8000 read `2babcf24...f285c`, the section-2 image, and the
first 32 KiB `aa20cfeb...67fb9`, equal to the first 32 KiB of
`emmc-head-8M.bin`. The new image's digest on the board matched the build.

```
dd if=imx-boot-m7-pads.bin of=/dev/mmcblk0 bs=512 seek=64 conv=fsync,notrunc   # 8355840 bytes, rc 0
sync
dd if=/dev/mmcblk0 bs=512 skip=64 count=16320 iflag=direct | sha256sum   # 03419070...a5980, matches
dd if=/dev/mmcblk0 bs=512 count=64 iflag=direct | sha256sum              # aa20cfeb...67fb9, unchanged
```

#### Boot

All four ports were captured read-only (115200 8N1, raw, `-hupcl`) from
before the write to the end; commands went to port 0 and port 1 only, and
no break was sent.

**`reboot` (port 0).** U-Boot SPL, U-Boot (ELE firmware 2.0.6), `Starting
kernel`, Linux to the login prompt. Port 2 stayed silent: this resets the
A55 LM only, as in section 4. Linux:
`imx-rproc imx95-cm7: unexpected resource table content, ignore`, then
`remoteproc1 ... attached` (our `.resource_table`, still the section-2 image).

**SM `reset` (port 1).** `Reset request: reason=sw, errId=0, srcLm=0`, DDR
OEI `done, err = 0`, `Hello from SM (Build 896, Commit 44f76dcb ...)`; port 0
went through SPL, U-Boot and Linux to the login prompt, with the same
`imx-rproc` lines. The ROM and ELE accepted the container.

**Port 2: one byte, `0x00`, at the reset, then nothing** for the rest of
the capture (more than 120 s). No boot event frame.

SM monitor afterwards, read-only:

```
err      SCMI err (chn=5): protocolId=0x11, messageId=0x2, status=-4   (the Linux one, as before; nothing on the M7's channel)
syslog   Device err log = 0x00000000
lm info  000: SM = on   001: M7 = on   002: AP = on (default)
cpu.r    001: m7p => run-mode = RUN, slp-mode = RUN, vector = 0x00000000_00000000
clock.r  142: lpuart3 = on, 24000000Hz
md 0x443C0030 12   (IOMUXC mux, one word per SM pin id from 12)
443c0030: 00000000 00000001 00000001 00000001
443c0040: 00000000 00000000 00000001 00000001     <- pin 18 (GPIO_IO14), 19 (GPIO_IO15): ALT1
443c0050: 00000000 00000000 00000000 00000000
md 0x42570000 7    (LPUART3, DATA skipped)
42570000: ???????? ...   the SM monitor cannot read LPUART3
```

#### Against the list

Not the pass case, and not plain silence: the pads are at ALT1 (pins 16, 17
and 20 to 23 around them are 0), the SM logged no error for the M7, and
LPUART3's clock is on at 24 MHz, so the pad request most likely landed (the
SM's mux write is the only one expected on these pads after a reset). The
single `0x00` fits TX going from the GPIO default to LPUART3's line once,
not a frame. What fails is after the pads: our LPUART3 sends no byte. Next
(firmware, no board access needed first): the transmit path from
`tfw_hal_uart_init` to the first frame, its use of NVIC line 64 (TO
CONFIRM), the BAUD divisor for 24 MHz, and the `.faultframe` link warning
at APP_BASE 0. A debug read of LPUART3 or the M7's PC needs a debugger or
an SM build that maps them (owner decision).

#### Board state after this run

The eMMC boot container is the pad-request imx-boot-m7.bin
(`03419070...a5980`); the board boots to Linux, the M7 runs our image. On the
board, `/root/imxboot-backup/` holds `imx-boot-orig.bin` (the recovery
image), `imx-boot-m7.bin` (section 2) and `imx-boot-m7-pads.bin` (this run).
Restore as in section 5. The SM console was left in its monitor (`>$`).

### 8. Transmit-path slice (firmware, no board, 2026-11-15)

The M7 path from `tfw_hal_uart_init` to the first frame, item by item
against section 7's list:

- **BAUD at 24 MHz.** `tfw_imx95_lpuart_baud(24000000, 115200)` gives
  `0x19000008`: OSR x26, SBR 8, 115384 baud, 0.16% fast, BOTHEDGE clear.
  Now pinned by `test_imx95_soc_calc`. Not the fault.
- **CTRL after BAUD.** Already so: GLOBAL reset, BAUD written with TE and RE
  off, then CTRL = TE | RE | RIE. Not the fault.
- **NVIC line 64.** Cited (MCUXpresso `MIMX9596_cm7.h`, `LPUART3_IRQn`); the
  vector table has 80 lines. But transmit hung on that line alone: `tx_try`
  only raised TIE and every byte waited for the interrupt. Now `tx_pump`
  also sends from the main loop's polls (`tx_try`, `rx`, `tx_idle`), and
  `tfw_imx95_uart_irqs` counts the interrupt's runs.
- **`.faultframe` link warning.** Harmless: image.ld placed the reset marker
  (RAM_END - 115) before the fault record (RAM_END - 127), so ld's location
  counter stepped back. The sections are NOLOAD and do not overlap. They are
  now in address order. The addresses did not change. Not linked in this
  lane, because the lane container has no `arm-none-eabi-gcc`.
- **The single `0x00`.** This fits TX held low from the moment the SM muxed
  the pad until TE drove it idle-high. If so, the M7 reached
  `tfw_hal_uart_init`'s CTRL write. The boot event goes out only after all
  of `startup()` and `tfw_hal_irq_enable()` (src/main.c). A stop or fault in
  between is the other candidate.

### Next slice (device, after section 8)

1. Build the relinked image at the branch tip. The profile's container
   switch links it at ITCM 0 and retires section 7's `APP_BASE=0x00000000
   BOOT_SECTION=0` override:
   `make BOARD=frdm-imx95-m7 IMX_BOOT=1 BUILD_DIR=build/imx95-imxboot firmware`.
   Check that the link prints no `dot moved backwards`. Record the addresses of
   `tfw_imx95_uart_irqs`, `tx_head` and `tx_tail` from `arm-none-eabi-nm` on
   the ELF (LTO may suffix the static names).
2. Run `imx-boot-m7.py` on that ELF. It now takes the ELF, not a raw binary.
   It refuses an image that is not linked at the container's M7 load
   address, or whose reset target is outside its loaded bytes. This
   relinked image is what the next device run writes. Write, read back and compare digests, then SM
   `reset`, as in section 7. Capture port 2 for 120 s.
3. Read only, from the A55 as root, through the DTCM alias (M7 `0x2000_0000`
   is A55 `0x2040_0000`, profile.mk): the reset marker at `0x2043FF8C`, the
   fault record's PC, LR and xPSR at `0x2043FF80`, `+4` and `+8`, and the
   three symbols at their nm address + `0x0040_0000`. Do not read LPUART3
   itself from the A55: a refused access may take Linux down.

What port 2 and the reads must show:

| Port 2 | Reads | Meaning, next |
|---|---|---|
| boot frame | irqs > 0 | Pass: TX and NVIC 64 confirmed; RX (daisy) next |
| boot frame | irqs = 0 | The polls carry TX; line 64 or PRIMASK is wrong (firmware) |
| no frame | marker `0x43494E50` (PNIC) | A fault: `addr2line` on the PC names it |
| no frame | no marker, tx_head = tx_tail = 0 | Stops in `startup()` before the boot event: add step marks |
| no frame | tx_head != tx_tail | Queued, TDRE never set: LPUART3 not sending (access, clock gate); debugger is the owner's call |

`/dev/mem` access to the TCM alias on this kernel is unchecked. If it is
refused, record that, and the port 2 result alone separates the first two
rows from the rest.

### 9. Device run of the transmit-pump slice (2026-11-15)

Plan, in order (ruling 644 (1) again), for the image built at `2d57f8599`
(the transmit pump of section 8 and the container image relinked at ITCM 0):

1. Build with the section-8 recipe (`IMX_BOOT=1`); check the link prints no
   `dot moved backwards`; record the nm addresses of `tfw_imx95_uart_irqs`,
   `tx_head` and `tx_tail`.
2. Run `imx-boot-m7.py` on the ELF over `imx-boot-orig.bin` (read back off
   the board and checked against section 1); its load-address and
   reset-target checks must pass. Record both SHA-256s.
3. From the A55 Linux: read the boot container now at 0x8000 (expected:
   section 7's `03419070...a5980`) and record its digest. Copy the new
   imx-boot over, check its digest on the board, write it at 0x8000
   (`dd ... seek=64 conv=fsync,notrunc`), `sync`, read back and compare.
4. `reboot` from the A55 console, then SM `reset` on port 1 so the ROM reads
   the new container (section 4). Capture port 2 and port 0 throughout,
   then the section-8 DTCM reads; compare with section 8's table.

Recovery path, stated before the write: restore `imx-boot-orig.bin` from the
A55 Linux (section 5 command); if the board does not boot, uuu over USB
serial download, which needs the owner to set SW4. If the board does not
reach U-Boot, no further write. No serial break on any port.

#### Build

```
make BOARD=frdm-imx95-m7 IMX_BOOT=1 BUILD_DIR=build/imx95-imxboot firmware
frdm-imx95-m7 flash  11800 / 196608 ( 4% of part)   ram   848 / 65536 ( 0% of part)
boards/frdm-imx95-m7/imx-boot-m7.py imx-boot-orig.bin build/imx95-imxboot/frdm-imx95-m7/soft/trusted-firmware.elf imx-boot-m7-txpump.bin
wrote imx-boot-m7-txpump.bin: 8355840 bytes, changed bytes 0x8195 to 0xc03ab only
```

The link printed no `dot moved backwards`. The ELF's text segment loads at
0 (ITCM), 0x2E00 bytes; vector table SP `0x2003FF80`, reset `0x00002481`.
`imx-boot-m7.py` accepted it (load address and reset target checks), and
`--show` gives entry 3 as 0x3000 bytes at 0x303C0000, entry 0, every
SHA-384 matching. `imx-boot-orig.bin` was read back off the board first
(`1d775624...0081f`, section 1).

| Symbol (`arm-none-eabi-nm -S`) | M7 address | Size |
|---|---|---|
| `tx_tail` | `0x20000087` | 1 |
| `tx_buf` | `0x20000088` | 64 |
| `tx_head` | `0x200000C8` | 1 |
| `tfw_imx95_uart_irqs` | `0x200000CC` | 4 |

| File | Size | SHA-256 |
|---|---|---|
| imx-boot-m7-txpump.bin (transmit-pump slice, `2d57f8599`) | 8355840 | `438cf0446cc18ecbe894a346863c9c7a6fc328fae22f143f36cd2fa7ea7a0daf` |

#### Write and read-back

Before the write the device at 0x8000 read
`034190704c106ae9433218d236c7b935d1984db90c0e94becca796047b8a5980`, section
7's image, and the first 32 KiB `aa20cfeb...67fb9`, as before. The new image
went to `/root/imxboot-backup/` over `ssh ... cat` and its digest there
matched the build. Then, from the A55 Linux as root:

```
dd if=imx-boot-m7-txpump.bin of=/dev/mmcblk0 bs=512 seek=64 conv=fsync,notrunc   # 8355840 bytes, rc 0
sync
dd if=/dev/mmcblk0 bs=512 skip=64 count=16320 iflag=direct | sha256sum   # 438cf044...a0daf, matches
dd if=/dev/mmcblk0 bs=512 count=64 iflag=direct | sha256sum              # aa20cfeb...67fb9, unchanged
```

#### Boot

All four ports were captured read-only (115200 8N1, raw) from before the
write to the end. Commands went to port 0 and port 1 only. No break was
sent.

**`reboot` (port 0).** Port 0 was at the getty login prompt, so the first
`reboot` was taken as a user name. After logging in as `root`, `reboot` ran:
U-Boot SPL, `Image set: 0, offset: 0x8000`, BL31, U-Boot, `Starting kernel`,
Linux to the login prompt, with `imx-rproc imx95-cm7: lmm(1) not under Linux
Control`, `unexpected resource table content, ignore` and `remoteproc1 ...
attached`. Port 2 stayed silent: this resets the A55 LM only (section 4).

**SM `reset` (port 1).** `Reset request: reason=sw, errId=0, srcLm=0`, DDR
OEI `done, err = 0`, `Hello from SM (Build 896, Commit 44f76dcb ...)`. Port
0: SPL (`SYS Boot reason: sw`), U-Boot, Linux to the login prompt, with the
same `imx-rproc` lines. The ROM and ELE accepted the container.

**Port 2: one byte, `0x00`, in the second of the reset, then nothing** for
the rest of the capture (more than 5 minutes). No boot event frame. This is
the same as section 7.

#### Reads after the boot

SM monitor (port 1), read-only:

```
err      SCMI err (chn=5): protocolId=0x11, messageId=0x2, status=-4   (Linux's, as before)
         DOM3 ns prv read from 0x2043FF80, MRC_W2=0, MRC2_DOM3        (the A55 read below)
syslog   Device err log = 0x00000000
lm info  000: SM = on   001: M7 = on   002: AP = on (default)
cpu.r    001: m7p => run-mode = RUN, slp-mode = RUN, vector = 0x00000000_00000000
clock.r  142: lpuart3 = on, 24000000Hz
md 0x443C0030 12   pins 18 and 19 (GPIO_IO14/15) = 00000001 (ALT1), pins 16, 17, 20 to 23 = 0
md 0x2043FF80 4, md 0x3043FF80 4, md 0x204000C8 2   all ????????
```

The section-8 DTCM reads from the A55 (`/dev/mem`, `mmap` read-only at
`0x2043FF80`, the first read of the list) ended the reading process with
SIGBUS (exit 135). Linux was unaffected. The SM logged it as a TRDC
violation: domain 3 (the A55, non-secure) has no read access to the DTCM
alias through MRC2. No further `/dev/mem` read was tried. The SM monitor
cannot read the DTCM either. None of the marker, fault record, `tx_head`,
`tx_tail` or `tfw_imx95_uart_irqs` could be read.

#### Against the lists

Section 6's list: not the pass case. It is also not plain silence: as in
section 7, the pads are at ALT1, the SM logged no error on the M7's channel,
and LPUART3's clock is on at 24 MHz, so the SCMI pad request most likely
landed. The CRC polynomial and the shared-memory address are therefore not
the first suspects.

Section 8's table: no frame, and the reads were refused, so the port 2
result alone leaves the last three rows open (a fault, a stop in
`startup()` before the boot event, or queued bytes that LPUART3 never
sends). The ITCM relink and the transmit pump did not change what port 2
shows.

Next, an owner decision for the read path, or a firmware slice:

- a read path into the M7's DTCM: a debugger on the M7 (the owner's call),
  or an SM configuration that gives domain 3 read access to the TCM through
  MRC2 (a boot-container change, the owner's call); or
- firmware that reports without the DTCM: step marks through `startup()`
  sent as raw bytes by writing LPUART3 DATA directly (no queue, no
  interrupt) before the boot event, so that port 2 itself shows how far the
  M7 gets.

Capture files (bench host only, out of git):

| File | SHA-256 |
|---|---|
| port0.log (A55, both boots) | `b1450d1fccf92c739f5ec81ad49bf1540bdc6e1ab2bb6efe2dc31782939ae98d` |
| port1.log (SM monitor) | `c429818d7d756b2641f675c8c6dcf5f5fde72d9659c29bb75c2f6a4970b9728b` |
| port2.log (M7, the one `0x00`) | `6e340b9cffb37a989ca544e6bb780a2c78901d3fb33738768511a30617afa01d` |
| port3.log (silent) | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |

#### Board state after this run

The eMMC boot container is imx-boot-m7-txpump.bin (`438cf044...a0daf`). The
board boots to Linux and the M7 runs our image. `/root/imxboot-backup/` on
the board now also holds `imx-boot-m7-txpump.bin`. `imx-boot-orig.bin` is
still the recovery image. Restore as in section 5. The SM console was left
in its monitor (`>$`), and port 0 at the login prompt.

### 10. Step marks on port 2 (DS-8876, 2026-11-15)

Firmware: `STEP_MARKS=1` (profile.mk, plan step 14). The reset handler and
`tfw_hal_uart_init` each write one raw byte to LPUART3 DATA, polling TDRE then
TC (bounded), with no queue and no interrupt. If CTRL.TE is clear, the
115200 divisor and TE are set first. Marks: `R` at reset-handler entry, `D`
after .data/.bss, `M` before `main`, `P` after the pad request followed by
`r d m` (the trail, kept in `.noinit`, as the earlier marks may be lost
before the pads are routed), `U` at the end of `tfw_hal_uart_init`. On a board
whose image runs to the console, port 2 shows `RDMPrdmU` (the first `RDM`
only if the pads were already routed), then the boot frame.

Plan and recovery path (stated before the write, as in section 9): build,
`imx-boot-m7.py` over `imx-boot-orig.bin` read back off the board, write at
0x8000 from the A55 Linux, read back, `reboot`, then SM `reset` on port 1.
Recovery: restore `imx-boot-orig.bin` from the A55 Linux (section 5); if the
board does not boot, uuu over USB serial download, which needs the owner to
set SW4. If no mark appears at all, record it and do not write again. No
serial break on any port.

#### Build

```
make BOARD=frdm-imx95-m7 IMX_BOOT=1 STEP_MARKS=1 BUILD_DIR=build/imx95-steps firmware
frdm-imx95-m7 flash  12004 / 196608 ( 4% of part)   ram   852 / 65536 ( 0% of part)
boards/frdm-imx95-m7/imx-boot-m7.py imx-boot-orig.bin build/imx95-steps/frdm-imx95-m7/soft/trusted-firmware.elf imx-boot-m7-steps.bin
wrote imx-boot-m7-steps.bin: 8355840 bytes, changed bytes 0x8195 to 0xc03ab only
```

No `dot moved backwards`. The release build (no flag) is 11800 bytes as in
section 9, with no step symbol. `imx-boot-orig.bin` was read back off the
board first (`1d775624...0081f`, section 1). `--show`: entry 3 is 0x3000
bytes at 0x303C0000, entry 0, every SHA-384 matching. Text loads at 0
(0x2ECC bytes).

| Symbol (`arm-none-eabi-nm -S`) | M7 address | Size |
|---|---|---|
| `tfw_cm_vectors` | `0x00000000` | 0x180 |
| `tfw_cm_soc_step` | `0x00001B84` | 0x68 |
| `tfw_cm_reset` | `0x0000253C` | 0x4A4 |
| `step_trail` (`.noinit`, after `__bss_end`) | `0x200002D0` | 4 |

| File | Size | SHA-256 |
|---|---|---|
| imx-boot-m7-steps.bin (step marks, plan step 14) | 8355840 | `1c6ed98d190e433e8b1b062e85b692bd72e77558ed5706ec266e812d0127cf54` |

#### Write and read-back

Before the write the device at 0x8000 read `438cf044...a0daf` (section 9's
image) and the first 32 KiB `aa20cfeb...67fb9`. The new image went to
`/root/imxboot-backup/` over `ssh ... cat`; its digest there matched the
build. From the A55 Linux as root:

```
dd if=imx-boot-m7-steps.bin of=/dev/mmcblk0 bs=512 seek=64 conv=fsync,notrunc   # 8355840 bytes, rc 0
sync
dd if=/dev/mmcblk0 bs=512 skip=64 count=16320 iflag=direct | sha256sum   # 1c6ed98d...7cf54, matches
dd if=/dev/mmcblk0 bs=512 count=64 iflag=direct | sha256sum              # aa20cfeb...67fb9, unchanged
```

#### Boot

All four ports were captured read-only (115200 8N1, raw), from before the
write for 15 minutes. Commands went to port 1 only (the SM `reset` and the
reads below); `reboot` went over SSH. No break was sent.

**`reboot` (A55, over SSH).** Port 0: SPL, `Image set: 0`, U-Boot, Linux to
the login prompt. Port 2 silent (the A55 LM only, section 4).

**SM `reset` (port 1).** `Reset request: reason=sw, errId=0, srcLm=0`, DDR
OEI `done, err = 0`, `Hello from SM (Build 896, ...)`. Port 0: SPL
(`SYS Boot reason`), U-Boot, Linux to the login prompt, with `imx-rproc
imx95-cm7: lmm(1) not under Linux Control`, `unexpected resource table
content, ignore` and `remoteproc1 ... attached`.

**Port 2: one byte, `0x00`, in the second of the reset, then nothing.** No
step mark: no `R`, `D` or `M`, and neither `P` nor `U`, which go out after
the pad request. No boot event frame. The same as sections 7 and 9.

#### Reads after the boot (SM monitor, port 1, read-only)

```
err      SCMI err (chn=5): protocolId=0x11, messageId=0x2, status=-4   (Linux's, as before)
syslog   Device err log = 0x00000000
lm info  000: SM = on   001: M7 = on   002: AP = on (default)
cpu.r    001: m7p => run-mode = RUN, slp-mode = RUN, vector = 0x00000000_00000000
clock.r  142: lpuart3 = on, 24000000Hz
md 0x443C0030 12   pins 18 and 19 (GPIO_IO14/15) = 00000001 (ALT1); pins 13 to 15 also 1, 12, 16, 17, 20 to 23 = 0
```

No `/dev/mem` read was tried (section 9: TRDC refuses the A55).

#### Against the list

No mark appeared at all, so by the brief's rule the result is recorded as
**the M7 does not reach the reset handler's first mark**, and nothing more
was written to the board. The last stage reached is none of R, D, M, P, U.

What this run cannot separate, for the next decision:

- the M7 does not execute our reset handler (the vector at ITCM 0, or the
  SM's start of LM1, though `cpu.r` shows the M7 in RUN at vector 0); or
- the M7 runs, but its writes to LPUART3 at `0x42570000` (`soc_regs.h`, TO
  CONFIRM for the M7's view) do not reach the line, or fault on the first
  access (the mark at reset entry runs before VTOR is set). The pads at ALT1
  and the clock on are consistent with both: section 7 could not tell whether
  our pad request or the SM set them.

A port-2 probe cannot go further than this. Next is the owner's call: a debugger on the M7, or
an SM configuration that lets the A55 read the M7's TCM (section 9), or
confirming LPUART3's M7-side address against IMX95RM.

Capture files (bench host only, out of git):

| File | SHA-256 |
|---|---|
| port0.log (A55, both boots) | `0e1e2a74d1536932254cededcb9f877f79a98edd13a6de4370718f673f24354c` |
| port1.log (SM monitor) | `47bace8767a18b9b2ab748ad74c8dcc5b63638ec8fd25a0a3a52d2ea25f07151` |
| port2.log (M7, the one `0x00`, 15 minutes; byte for byte section 9's) | `6e340b9cffb37a989ca544e6bb780a2c78901d3fb33738768511a30617afa01d` |
| port3.log (silent) | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |

#### Board state after this run

The eMMC boot container is imx-boot-m7-steps.bin (`1c6ed98d...7cf54`). The
board boots to Linux (checked over SSH after the SM reset) and the M7 runs
our image. `/root/imxboot-backup/` also holds `imx-boot-m7-steps.bin`;
`imx-boot-orig.bin` (`1d775624...0081f`, checked after the run) is still the
recovery image. Restore as in section 5. The SM console was left in its
monitor (`>$`), port 0 at the login prompt.

### 11. Why section 10 shows no mark: read-only checks (DS-8879, 2026-11-15)

Two causes were open: (a) the written image did not carry the marks
(`STEP_MARKS=1` changed the objects but not the build directory), or (b)
the M7 does not run our image, or `0x42570000` is not LPUART3 for the M7.
Firmware fix first: `STEP_MARKS=1` now links into its own directory
(`-steps`; `soft-imxboot-steps` with `IMX_BOOT=1`), as `IMX_BOOT` does.
Everything below is read-only. Nothing was written to the board, and no
break was sent.

#### (a) The image on the board carried the marks

The device at 0x8000 still reads `1c6ed98d...7cf54` (section 10's image).
A clean rebuild of the section-10 commit, in a fresh tree from `git
archive` with section 10's recipe, followed by `imx-boot-m7.py` over
`imx-boot-orig.bin` (copied back off the board, `1d775624...0081f`),
reproduces that container byte for byte (`1c6ed98d...7cf54`, 12004 bytes
of flash). So the rebuilt ELF is the image the board runs. Its objdump:

```
vector 0/1     0x2003FF80 / 0x0000253D            -> tfw_cm_reset (0x253C)
253e  movs r0, #82                                ; 'R'
2548  bl   1b84 <tfw_cm_soc_step>                 ; first call in the reset handler
1b86  ldr  r5, =0x42570000                        ; LPUART3
1b8a  ldr  r3, [r5, #24] ; lsls #12 ; bmi         ; CTRL.TE set?
1b9a  str  r3, [r5, #16] ; str 0x80000, [r5, #24] ; else BAUD, then CTRL = TE
1ba6  bl   bf2 <step_send>
c04   str  r0, [r2, #28]                          ; r2 = 0x42570000: DATA
```

The store to LPUART3 DATA comes before anything else the reset handler does
(only a load from the DTCM and the push come before it). **Cause (a) is
ruled out**, and no new image was written (the brief's condition).

#### (b) LPUART3's address and the M7's state

A55 device tree (`/proc/device-tree`, kernel 6.18):

| Alias | Node | `reg` | status |
|---|---|---|---|
| serial2 (LPUART3) | `/soc/bus@42000000/serial@42570000` | `0x42570000 0x1000` | disabled |
| serial3 (LPUART4) | `serial@42580000` | `0x42580000 0x1000` | disabled |
| serial4 (LPUART5) | `serial@42590000` | `0x42590000 0x1000` | okay |
| serial0 (LPUART1) | `/soc/bus@44000000/serial@44380000` | `0x44380000 0x1000` | okay |

LPUART3 is `0x42570000` in the A55's map, the address in `soc_regs.h`, and
Linux leaves it disabled. The M7's own view is still not confirmed from a
document: the processor reference manual is not in trusted-imx95's
docs/references.

SM monitor (port 1):

```
lm info   000: SM = on   001: M7 = on   002: AP = on (default)
cpu.r     001: m7p => run-mode = RUN, slp-mode = RUN, vector = 0x00000000_00000000
reason    Reset: reason=sw, errId=0, srcLm=0
syslog    Device err log = 0x00000000
md 0x42570000 7   all ????????   (the SM monitor cannot read LPUART3)
```

LPUART3 from the A55 (`/dev/mem`, `mmap` read-only, 32-bit loads of VERID,
PARAM, BAUD, STAT, CTRL, FIFO; never DATA): the first load ended the
reading process with SIGBUS (exit 135). Linux was unaffected. The SM
logged it as an MBC denial:

```
err   DOM3 ns prv read from 0x42570000, MBC_W0=0.85, MBC0_DOM3_MEM0_BLK_CFG_W10[5]
      DOM2 sec prv read from 0x42590000, MBC_W0=0.89, MBC0_DOM2_MEM0_BLK_CFG_W11[1]
```

The DOM2 line (a secure read of LPUART5) is not from this lane; when it
was logged is not known.

#### What this leaves

- The M7 is in RUN at vector 0, and LM1 is on, so the SM started it. The
  image it runs puts `R` on LPUART3 DATA first. Port 2 still shows no mark.
- LPUART3 is behind TRDC MBC0 (block config word 10, bit 5 for domain 3).
  Neither the A55 nor the SM monitor can read it, so whether the M7's
  domain may write it, and whether CTRL.TE and BAUD were set by our
  `tfw_cm_soc_step`, cannot be seen from here. A write that the MBC
  refuses would fault the M7 on its first LPUART3 access, before any mark,
  which matches every run since section 7.
- Next read-only steps: the SM monitor's `trdc` command for MBC0's
  configuration of LPUART3's block per domain (it takes a parameter this
  lane could not find without the SM sources), and the M7's domain ID in
  the SM configuration. If the M7's domain has no LPUART3 write, the fix is
  the SM configuration in the boot container (the owner's call, ruling
  644).

#### Board state after this lane

Unchanged: the eMMC boot container is imx-boot-m7-steps.bin
(`1c6ed98d...7cf54`), and Linux is up. `/root/imxboot-backup/` still holds
`imx-boot-orig.bin` (`1d775624...0081f`), the recovery image (section 5).
The SM console was left in its monitor (`>$`).

### 12. May LM1 (the M7's domain) access LPUART3? Read-only (2026-11-15)

Section 11 left one question: whether TRDC MBC0 lets the M7's domain
write LPUART3 (`0x42570000`). Plan, read-only on the board, no write to the
boot device, no SM configuration change, no break:

1. The System Manager the board runs is the board's own (only the M7 slot
   changed, see "2. Build" above). Identify its release from the SM monitor
   and take the imx-sm source of that release.
2. In that source, find the monitor's `trdc` command and its parameters,
   then run it on port 1 for MBC0 and read the block that holds LPUART3
   for the M7's domain.
3. In the same source, read the board configuration the SM was built from:
   the M7 LM's domain ID and whether LPUART3 is in its resource list.
4. Record both here with the source file and line. If LM1 may not access
   LPUART3, name the configuration change needed (file and line): that
   change is the owner's decision and is not made in this lane.

#### The SM source the board runs

The board's SM reports build 896, commit `44f76dcb`, config `mx95frdm-pro`
(the board table at the top). In nxp-imx/imx-sm that commit is the tag
`lf-6.18.20-2.0.0`, the BSP release of the board's Linux and TF-A. Its
config is `configs/other/mx95frdm-pro.cfg`; nothing in this repository
rebuilds the SM (section "2. Build": only the M7 slot changes).

#### LM1's resources in `configs/other/mx95frdm-pro.cfg` (at `44f76dcb`)

| Line | Text | Meaning |
|---|---|---|
| 340 | `LM1 name="M7", rpc=scmi, boot=2, skip=1, did=4, safe=seenv` | the M7 is domain 4 |
| 342 | `DFMT0: sa=secure` | the M7's bus accesses are secure |
| 344 | `OWNER: perm=rw, api=all` | an OWNER resource gets `rw`, 0x6666: read/write for secure and nonsecure, privileged and user (`sm/doc/config.md`, permission table) |
| 351, 352 | `PD_M7 start=1, stop=2`, `CPU_M7P start=2, stop=1` | mSel 0 starts the M7 |
| 404 | `LPUART3 OWNER, test` | LPUART3 is LM1's |
| 420, 421 | `PIN_GPIO_IO14 OWNER`, `PIN_GPIO_IO15 OWNER` | LPUART3's TX and RX pads are LM1's |

The AP (LM2, line 442) is `did=3`, which is why section 11's A55 read was
denied (`DOM3 ... MBC0_DOM3_MEM0_BLK_CFG_W10[5]`): the config gives LPUART3
to the M7, not the A55. **By the source, LM1 may access LPUART3**, and no
SM configuration change is needed for it.

#### The monitor's `trdc` command (`sm/utilities/monitor/`, at `44f76dcb`)

`monitor_cmds.c` dispatches `trdc` and `trdc.raw` to `MONITOR_CmdTrdc` and
`MONITOR_CmdTrdcRaw` (`monitor_trdc.c`); `sm/doc/monitor.md` lines 53, 54:
`trdc rdc [did]`, a formatted dump of one TRDC, limited to domain `did`.
The TRDC name is matched against `devices/MIMX95/sm/dev_sm_rdc.c`
`s_trdcInfo` (lines 82 to 91): `aon`, `mega`, `wakeup` (0x42460000,
label `W`), `camera`, `hsio`, `noc`, `disp`, `netc`, `gpu`, `vpu`. The
`MBC_W0=0.85` in section 11's error log is TRDC `W` (wakeup), MBC 0,
memory 0, block 85, so the read is `trdc wakeup 4` (the M7) and
`trdc wakeup 3` (the A55, for contrast).

#### The TRDC on the board (SM monitor, port 1, read-only)

Which block is LPUART3: `devices/MIMX95/configtool/wakeupmix.cfg` line 84,
`LPUART3: PD_WAKEUP, CLK_LPUART3, MBC_W0=0.87`, so MBC 0, memory 0,
**block 87** (LPUART7, line 139, is block 105). Which master is the M7:
`devices/MIMX95/configtool/m7mix.cfg` line 40, `CPU_M7P, DFMT0,
MDAC_W0C=0`, master 0 of TRDC `W`.

`trdc wakeup 4` (192 lines, SHA-256 of the capture `5b6954d9...2bf252`,
kept out of git) and `trdc wakeup 3`:

```
MDA_W0_0_DFMT0: 0x90000004, VLD=1 SA=0 DIDS=0 KPA=1 DID=4      # the M7 is DID 4, secure (cfg sa=secure)
MBC0_MEMN_GLBAC1: 0x00006666, LK=0 SP=RW. SU=RW. NP=RW. NU=RW.
MBC0_DOM4_MEM0_BLK_CFG_W10:  0x90000000, BLK[87:80]   = 10000000   # block 87: 9
MBC0_DOM4_MEM0_BLK_CFG_W13:  0x00000090, BLK[111:104] = 00000010   # block 105 (LPUART7): 9
MBC0_DOM4_MEM0_BLK_NSE_W2:   0x00802910                            # bit 23: block 87
MBC0_DOM3_MEM0_BLK_CFG_W10:  0x09999999, BLK[87:80]   = 01111111   # A55: blocks 80..86, not 87
```

Each 4-bit block field is MBACSEL (bits 2..0) and NSE (bit 3). DOM4's
block 87 is 9: access control 1, `0x6666`, read/write for every
privilege and security state, the value the cfg's `OWNER: perm=rw`
gives. It is the same 9 that DOM4 has on LPUART7 and on every other block
LM1 owns, and the same 9 that DOM3 (the A55) has on blocks 80 to 86. DOM3 has
0 on block 87, which is the section 11 SIGBUS. (Section 11's error line
names block 85 for `0x42570000`; the SM's own resource table says 87, and
DOM3's block 85 is 9, so 87 is the block that refused that read.)

Around it, also read-only on port 1:

```
clock.r lpuart3          142: lpuart3 = on, 24000000Hz   # the firmware's TFW_IMX95_LPUART_CLK_HZ
md 0x443C0048 2          00000001 00000001               # GPIO_IO14/15 mux: ALT1, LPUART3 TX/RX
md 0x443C024C 2          0000051e 0000051e               # their pad control
md 0x443C0558 2          00000000 00000000               # LPUART3 RXD/TXD input select
```

(Mux and daisy addresses: imx-sm `devices/MIMX95/drivers/fsl_iomuxc.h`
lines 179 and 186.)

#### Answer

**LM1 may access LPUART3.** The board's SM config gives it to LM1 as an
OWNER (`configs/other/mx95frdm-pro.cfg` line 404), and the live TRDC gives
the M7's domain (DID 4, master 0 of TRDC `W`) read/write on LPUART3's
block 87, as on LM1's other peripherals. Its clock is on at the 24 MHz the
firmware assumes, and its pads are muxed to it. **No SM configuration
change is needed**, so there is nothing for the owner to decide here.

What it leaves for section 10's silent port 2: the MBC is not the cause.
One thing these reads cannot show is the NSE bit's meaning for a secure
master (the M7 is forced secure); the processor reference manual, which
defines it, is not in the repository. NXP sets the same 9 on every M7
peripheral in its own config, so it is the documented working setting,
not a refusal. The next step is on the M7 side: whether it executes our
reset handler at all (vector 0 is the ITCM base, `cpu.r` shows RUN), for
example by a mark that does not need LPUART3 (a write to the M7's own DTCM
read back through the SM monitor's `md`), which needs a new image and so a
write lane.

#### Board state after this lane

Unchanged. Nothing was written to the board: every command above was an
SM monitor read (`lm info`, `trdc`, `clock.r`, `md`), no break was sent,
and the SM console was left in its monitor (`>$`). The eMMC still holds
imx-boot-m7-steps.bin (section 11).

### 13. Does the M7 reach LPUART3? Image, clock and divisor (2026-11-15)

Section 12 ruled out access: the SM config and the live TRDC give LPUART3
to the M7, its clock is on at 24 MHz and its pads are muxed. The SM shows
the M7 in RUN at vector 0, and port 2 is still silent. Plan, read-only
first, no SM configuration change, no fuse, lifecycle or secure-boot
action, no break:

1. **Is the running image ours?** Read the boot container back from the
   eMMC (offset 0x8000) on the A55, copy it to the bench host, extract
   container entry 3 (the M7 image) and compare its SHA-256 with our
   build's `.bin`; record entry 3's load address, entry point, size and
   flags from the container header, against the board's original entry
   (0x303C0000, entry 0).
2. **Does our LPUART3 setup match the board?** Read the step-mark and
   console init (`hal_soc.c` `tfw_cm_soc_step`, `hal_uart.c`,
   `soc_calc.c` `tfw_imx95_lpuart_baud`) and check the BAUD value against
   the 24 MHz root section 12 measured, the register offsets and bits
   against the objdump, and the pins and instance against section 12's
   mux reads.
3. **Did the M7 touch LPUART3?** If the SM monitor has a read that
   reaches LPUART3 (`help`; section 11's `md` returned `????????`), read
   BAUD, CTRL and STAT at `0x42570000`; otherwise say what the reads that
   do work (pad mux and pad control) show about whether the M7 ran our
   pad request.
4. Only if step 2 finds a defect: fix it with a host test, build, and
   load it by ruling 644 (1). The backup, before any write: the board's
   `/root/imxboot-backup/imx-boot-orig.bin` (section 1,
   `1d775624...0081f`); recovery is restoring it from the A55 Linux, or
   uuu over USB serial download, which needs the owner to set SW4.

Result (read-only; nothing was written to the board, so no new backup).

1. The running M7 image is ours. The eMMC read back at 0x8000 has SHA-256
   `1c6ed98d...7cf54`, the same as the section 10 container
   (`/root/imxboot-backup/imx-boot-m7-steps.bin`, board and bench host);
   `imx-boot-orig.bin` (`1d775624...0081f`) is still there. `imx-boot-m7.py
   --show` on it: entry 3 at offset 0x0af800, size 0x3000, load 0x303c0000,
   entry 0, flags 0x01b3, meta 0x1, every SHA-384 matching. The vendor
   container's entry 3 has the same load, entry, flags and meta (size
   0x8c00). The 0x3000 bytes of entry 3 equal a `git archive` rebuild of
   the section 10 commit (`make BOARD=frdm-imx95-m7 IMX_BOOT=1
   STEP_MARKS=1`, 12004 bytes, SHA-256 `bfede767...6774562e`) byte for
   byte, then zero padding. The vector: SP 0x2003FF80, reset 0x253D.
2. No defect in the LPUART3 setup. `tfw_imx95_lpuart_baud` gives BAUD
   0x19000008 at 24 MHz and 115200 (OSR 26, SBR 8, 115384 baud, +0.16 %),
   pinned by `test_imx95_soc_calc`; `TFW_IMX95_LPUART_CLK_HZ` is 24000000,
   the SM's `clock.r lpuart3` rate. BAUD +0x10, STAT +0x14, CTRL +0x18
   (TE bit 19), DATA +0x1C and the step-mark path (`tfw_cm_soc_step`: BAUD
   then TE, bounded TDRE and TC polls) match the reference manual. In the
   imx-sm source (`fsl_clock.h`) LPUART3 is clock root 101 with no LPCG of
   its own, so the root is its whole clock.
3. The SM monitor's `help` lists `md`, `md.b`, `md.w` and `mm` as its only
   memory commands, and section 11 showed `md 0x42570000` returns
   `????????` (the SM's domain has no access to MBC0 block 87); `mm` is
   not used (no SM-side change). LPUART3's BAUD, CTRL and STAT therefore
   cannot be read from the board as it is. What `md` can read is the M7's
   SCMI shared memory in MU5 (`md 0x44611000 32`):

       00000000 00000001 00000003 4725fc0e
       00000000 00000008 00046406 00000000
       00000000 00000004 000000c0 00000001   (rest zero)

   Channel status 1 (free: the SM answered), length 8, header 0x46406 =
   protocol 0x19 (pin control), message 6 (SETTINGS_CONFIGURE), token 1,
   status 0 (SUCCESS). So the M7 runs our image past the R, D and M marks,
   each of which writes LPUART3 BAUD, CTRL and DATA, without a fault (the
   SM's error log has no DOM4 violation), and its second pad request was
   answered. The SCMI wait is bounded, so the P mark runs next. Port 2,
   read for 4 s after this: 0 bytes.

Conclusion: the image on the board is ours, and the M7 reaches and writes
LPUART3 without a bus fault, yet no byte reaches port 2. What is left:
LPUART3 ignores the writes (held in reset or not clocked, despite
`clock.r`), so TDRE never sets and the bounded polls time out, or TX does
not reach the pad. Next slice (a write under ruling 644 (1)): a diagnostic
image in which the M7 copies LPUART3 VERID, PARAM, GLOBAL, BAUD, STAT and
CTRL into spare MU5 SRAM after the SCMI area (0x44611080 on), which the SM
`md` can read; and the CCM root 101 registers read with `md`. Board state:
unchanged, SM console left at its prompt.

### 14. LPUART3's registers, as the M7 sees them (2026-11-15)

Section 13 left the question of what LPUART3 holds after our writes; the
SM cannot read it, so the M7 reports it through MU5 SRAM, which the SM
`md` can read. This is a write under ruling 644 (1).

Plan:

1. A bench-only build option, `REG_DUMP=1` (off in the default image and
   in release builds, like `STEP_MARKS`): at each step mark the M7 copies
   LPUART3 VERID, PARAM, GLOBAL, BAUD, STAT and CTRL, with a magic word
   and the mark, into MU5 SRAM after the SCMI area (from 0x44611080). Host
   test for the record layout.
2. Read-only first: SM `md 0x44611080` (the area is readable and unused)
   and CCM clock root 101 (LPUART3) with `md`.
3. Build an imx-boot with only the M7 entry replaced (`imx-boot-m7.py`),
   write it at the boot container offset from the A55 Linux, read back,
   digest match, SM `reset`; then `md 0x44611080` and port 2 captured.
4. Record the values and their decoding, then restore the normal image
   and say which one the board ends on.

Backup, named before any write: the board's
`/root/imxboot-backup/imx-boot-orig.bin` (section 1, `1d775624...0081f`,
the vendor container), also on the bench host; the current container is
`/root/imxboot-backup/imx-boot-m7-steps.bin` (section 10,
`1c6ed98d...7cf54`). Recovery path, stated before writing: restore a
backup from the A55 Linux; if the board does not boot, uuu over USB serial
download, which needs the owner to set SW4.

Read-only results (SM monitor, before any write):

- `md 0x44611000 0x80`: MU5 SRAM holds three SMT channel headers, at
  +0x000 (the M7's channel, the pad request of section 13), +0x080 and
  +0x100 (each `00000000 00000001 00000003 00000000`: channel free, set up
  by the SM). From +0x180 the SRAM holds uninitialised (random) words, and
  `md 0x446113f0 8` shows it ends at +0x400 (0x44611400 reads `????????`).
  So the dump goes at +0x180 (0x44611180), not +0x080 as planned: +0x080
  and +0x100 are the SM's channels.
- CCM clock root 101 (LPUART3), `md 0x44453280 0x20` (root n at
  0x44450000 + n * 0x80): CONTROL `00000000`, i.e. mux 0 (the 24 MHz
  oscillator), divider 1, OFF bit 24 clear; STATUS0 (+0x20) `00000000`,
  i.e. not off and not changing. The rest: +0x2c `00000020`, +0x30 to
  +0x3c `ffff0000`. This agrees with `clock.r lpuart3` (on, 24 MHz).

Diagnostic container (bench host), from the vendor backup and the
`REG_DUMP=1` image of commit `0c7c2657`:

```
make BOARD=frdm-imx95-m7 IMX_BOOT=1 REG_DUMP=1 firmware     # 12120 bytes, soft-imxboot-steps-regdump
boards/frdm-imx95-m7/imx-boot-m7.py imx-boot-orig.bin \
    build/frdm-imx95-m7/soft-imxboot-steps-regdump/trusted-firmware.elf imx-boot-m7-regdump.bin
#   8355840 bytes, changed bytes 0x8195 to 0xc03ab only
#   entry 3: offset 0x0af800 size 0x003000 load 0x303c0000 entry 0 flags 0x01b3 meta 0x1
#   every image SHA-384 matches; sha256 5d52f073...961902b3
```

Before the write, on the A55 Linux: `/root/imxboot-backup/` still holds
`imx-boot-orig.bin` (`1d775624...0081f`) and `imx-boot-m7-steps.bin`
(`1c6ed98d...7cf54`), and the eMMC at 0x8000 reads `1c6ed98d...7cf54`
(section 10's container). Those are the restore points; uuu over USB
serial download (owner sets SW4) if the board does not boot.

Write and boot (A55 Linux, then the SM monitor):

```
dd if=imx-boot-m7-regdump.bin of=/dev/mmcblk0 bs=512 seek=64 conv=fsync,notrunc   # 8355840 bytes, rc 0
dd if=/dev/mmcblk0 bs=512 skip=64 count=16320 iflag=direct | sha256sum   # 5d52f073...961902b3, matches
dd if=/dev/mmcblk0 bs=512 count=64 iflag=direct | sha256sum              # aa20cfeb...67fb9, unchanged
>$ reset        # port 1: "Reset request: reason=sw", DDR OEI, "Hello from SM (Build 896, Commit 44f76dcb)"
```

Port 2, captured from the `reset` for 40 s: one byte, `00`, as in
sections 7 to 10. Then `md 0x44611000 0x100` on port 1; the dump area:

```
44611180: 4c335244 00000005 ........ ........    magic "L3RD", 5 records
446111a0: 00000052 04040007 00000404 00000000    'R' #0  VERID PARAM GLOBAL
446111b0: 19000008 00c00000 00080000 00000000            BAUD  STAT  CTRL  polls
446111c0: 00000144 04040007 00000404 00000000    'D' #1
446111d0: 19000008 00c00000 00080000 00000000
446111e0: 0000024d 04040007 00000404 00000000    'M' #2
446111f0: 19000008 00c00000 00080000 00000000
44611200: 00000350 04040007 00000404 00000000    'P' #3
44611210: 19000008 00c00000 00080000 00000000
44611220: 00000455 04040007 00000404 00000000    'U' #4
44611230: 19000008 00c00000 002c0000 00000000
```

(header words 2 to 7 and the words past the fifth record are the SRAM's
uninitialised contents; the dump does not write them.)

Decoding:

- All five marks ran, R, D, M, P and U, in order: the M7 runs our image
  from the reset handler through `tfw_hal_uart_init`.
- VERID 0x04040007: LPUART version 4.4, feature 7. PARAM 0x00000404:
  16-word transmit and receive FIFOs. LPUART3 answers the M7's reads.
- GLOBAL 0: out of reset. BAUD 0x19000008 at every mark: our value (OSR
  26, SBR 8; 115200 at the 24 MHz root above), so the writes take.
- CTRL 0x00080000 (TE) at R to P; 0x002c0000 (TE, RE, RIE) after U.
- STAT 0x00c00000 (TDRE and TC) at every mark, read after the byte was
  written and TC polled; polls 0: TDRE was set at once each time. The
  transmitter took each byte and finished shifting it out.

Conclusion: LPUART3 is clocked, programmed as we intend and transmits
(TC sets after each byte), yet port 2 receives none of it. The loss is
past LPUART3's transmitter: the pad route from LPUART3_TX to the pin
port 2 listens on (the mux and pad state of section 12 read ALT1 and
0x51e), or that pin is not the one port 2 is wired to. The registers rule
out the clock, the divisor, the access and a stuck transmitter.

#### Restore, and the vendor image's console route

Restored the vendor container (`imx-boot-orig.bin`) from the A55 Linux:

```
dd if=imx-boot-orig.bin of=/dev/mmcblk0 bs=512 seek=64 conv=fsync,notrunc   # 8355840 bytes, rc 0
dd if=/dev/mmcblk0 bs=512 skip=64 count=16320 iflag=direct | sha256sum   # 1d775624...0081f, matches
dd if=/dev/mmcblk0 bs=512 count=64 iflag=direct | sha256sum              # aa20cfeb...67fb9, unchanged
>$ reset
```

Port 2, from the `reset`, 40 s, 474 bytes: the same leading `00`, then the
vendor M7 demo ("Start SRTM communication", "Power Mode Switch Task", "Core
Clock: 800000000Hz", its menu). So port 2's wiring works at 115200, and
the leading `00` is not ours: the vendor image shows it too.

The SM monitor, with the vendor M7 image running:

```
md 0x443C0048 2      00000000 00000000     # GPIO_IO14/15 mux: ALT0 (GPIO), not ALT1
md 0x443C024C 2      0000051e 0000051e     # their pad control, as with ours
md 0x443C0558 2      00000000 00000000     # LPUART3 RXD/TXD input select
clock.r lpuart3      142: lpuart3 = on, 24000000Hz
```

**The vendor M7 console does not use GPIO_IO14/15.** While its text
reaches port 2, those two pads sit at ALT0; with our image they read ALT1
(section 12). So the pads our firmware routes LPUART3 to are not the ones
port 2 listens on, which is why every byte LPUART3 sent (the dump above)
was lost. The vendor's full mux table (`md 0x443C0000 0x81`, one word per
SM pin id) has these non-zero entries, the vendor console's pads among
them: ids 0 to 3 = 6; 8, 9 = 5; 13 to 15 = 1; 32 to 36 and 39 = 0x11; 40,
41, 44, 45 = 2; 42, 43 = 5; 46, 47 = 0x12; 92 to 95, 100 to 104 and 111 =
5; 96 to 99 = 1; 112 to 115 = 0x10; 122, 124, 125 = 5; 123, 126 = 4; 127 = 1.
