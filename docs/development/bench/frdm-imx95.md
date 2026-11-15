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

1. Build at the branch tip with section 7's command and check that the link
   prints no `dot moved backwards`. Record the addresses of
   `tfw_imx95_uart_irqs`, `tx_head` and `tx_tail` from `arm-none-eabi-nm` on
   the ELF (LTO may suffix the static names).
2. Rerun `imx-boot-m7.py`, write, read back and compare digests, then SM
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
