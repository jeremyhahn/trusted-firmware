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
| port 2 | silent (M7 console candidate) | nothing |
| port 3 | silent (M7 console candidate) | nothing |

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
