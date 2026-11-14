# FRDM-IMX95-PRO bench: Cortex-M7 image in RAM, console capture

Row 428 follow-on (ruling 625 (3)). The board has no Ethernet cable; every
step runs over the four-port USB-UART bridge on the board's debug USB.
Ports are named here by role, never by device path.

Rules in force: no serial break or SysRq on any port; no fuse, ELE
lifecycle, SRK or secure-boot command; no write to eMMC; no `saveenv`.

## 1. Board state before any load (2026-11-14)

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
cmdline: console=ttyLP0,115200 earlycon root=/dev/mmcblk0p2 rootwait rw
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
to it (`not under Linux Control`), so `echo start/stop` on remoteproc1 is
refused: the remoteproc path in the brief is closed on this SM
configuration. The M7 is controlled from the SM monitor (`lm`).

## 2. Build

```
make BOARD=frdm-imx95-m7 firmware
frdm-imx95-m7 flash  10696 / 196608 ( 4% of part)   ram   816 / 65536 ( 0% of part)
built build/frdm-imx95-m7/soft/trusted-firmware.hex
```

`arm-none-eabi-objcopy -O binary` gives a 10 696-byte image for ITCM
`0x00008000` (A55 view `0x203C8000`); its vector table opens with SP
`0x2003FF80`, reset `0x0000A061`. The image carries `.resource_table`.

## 3. Load attempts (RAM only): every route is closed on mSel=0

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

### M7 console identified (vendor image)

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
