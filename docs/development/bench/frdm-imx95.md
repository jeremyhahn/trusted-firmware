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
