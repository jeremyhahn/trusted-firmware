# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# NXP FRDM-IMX95-PRO, the firmware core on the i.MX 95's Cortex-M7 (SPEC row
# `imx95`, first slice): a board of the generic Cortex-M port (#161), whose
# reference board is mps2-an505. The board is NXP UM12527, "FRDM-IMX95-PRO
# Board User Manual", Rev. 1.0; the SoC is NXP IMX95RM, the "i.MX 95
# Applications Processor Reference Manual", which this project does not hold.
# Every SoC fact below is marked TO CONFIRM against IMX95RM until a lane has
# read it; none is a section number we have seen.
#
# Board id 0x41 is PROVISIONAL, as mps2-an505's 0x40 is: the next id of the
# Cortex-M range, recorded in docs/boards/board-id-registry.md and
# sdk/go/boardid.go until the owner allocates it (docs/multi-arch/imx95-design.md
# question 20).
#
# The M7 is not the boot core. The ROM and NXP's System Manager on the M33
# bring the SoC up, the System Manager configuration (trusted-imx95's) gives the
# M7 its clocks, pins and peripherals, and the M7 image arrives in its TCM by
# one of two loaders: the boot container (AHAB, trusted-imx95's) or Linux's
# remoteproc on the A55. Nothing here is flashed.
ARCH           := cortex-m
CPU            := cortex-m7
SOC            := imx95
# The part board-check's budgets are checked against (scripts/board-check.py,
# PARTS): the M7 of the board's MIMX9596 (UM12527 §1.2 Table 1). The Cortex-M
# build does not read it.
MCU            := mimx9596-m7
# The generic script with the resource table's checks (mk/arch/cortex-m.mk).
CM_LDSCRIPT    := mk/arch/cortex-m/imx95.ld

# The core clock, TFW_CM_CPU_HZ in board_profile.h (TO CONFIRM there). The build
# does not read it (the clock is the header's); the provenance manifest's
# f_cpu_hz does.
F_CPU          := 800000000

# The memory map, as the M7 sees it (IMX95RM, the Cortex-M7 memory map; TO
# CONFIRM). The A55 sees the same TCM at another address, and a loader on the
# A55 side writes the image there; the ELF is linked at the M7's addresses and
# remoteproc translates them (Linux drivers/remoteproc/imx_rproc.c, the i.MX 95
# M7 address table; TO CONFIRM against IMX95RM):
#
#   M7 address   A55 address   size
#   0x00000000   0x203C0000    256 KiB   ITCM: code
#   0x20000000   0x20400000    256 KiB   DTCM: data
#
#   0x00000000  tfw-boot's 32 KiB region at the ITCM's base, where the M7's
#               reset vector points (the System Manager configuration's M7
#               start address; TO CONFIRM). Reserved: tfw-boot is not yet built
#               for this board (mk/boot.mk has no row for it).
#   0x00008000  the application slot, 192 KiB of code.
#   0x0003F000  the platform block, the last 4 KiB of the ITCM.
#   0x20000000  .data, .bss, the stack, the reset marker and the handoff
#               block at the top (RAM_END), in the DTCM.
#
# TCM is RAM: nothing in it survives a power cycle, and a reload by either
# loader rewrites the image's segments, not the platform block. Where the
# platform block persists on this part is the EdgeLock enclave's question
# (a later slice of the row), not this one's.
FLASH_BUDGET   := 196608
RAM_BUDGET     := 65536
FLASH_TOTAL    := 262144
RAM_TOTAL      := 262144

APP_BASE       := 0x00008000
RAM_BASE       := 0x20000000
RAM_END        := 0x2003FFFF

# The region tfw-boot measures (mk/release.mk's provenance): the application
# slot, TFW_APP_REGION_BASE and TFW_APP_REGION_SIZE in board_profile.h. Not
# ITCM 0, which is tfw-boot's region.
FWHASH_BASE    := $(APP_BASE)
FWHASH_SIZE    := $(FLASH_BUDGET)

# The core clock, TFW_CM_CPU_HZ in board_profile.h, TO CONFIRM there against the
# System Manager configuration. The build does not read it (the clock is the
# header's); the provenance manifest's f_cpu_hz does.

# tfw-boot's geometry, reserved for its port; mk/arch/cortex-m.mk computes the
# load limit from it.
BOOT_SECTION   := 32768
BOOT_BASE      := 0x00000000
BOOT_BUDGET    := 32768

# The soft root until the EdgeLock enclave tier lands: the row's key tier is
# the enclave, and this slice does not touch it.
BOARD_TIERS    := soft

# The SAR ADC (src/hal/cortex-m/imx95/hal_adc.c), off by default: its base is
# TO CONFIRM (TFW_IMX95_ADC_BASE in board_profile.h, zero until a pinned source
# gives it) and nothing has run it on the board. ADC=1 turns TFW_FEATURE_ANALOG
# on (TFW_CM_ADC in board_profile.h), appends the analog pins and links the
# driver; the soft:ADC=1 row builds that image, as mps2-an505's soft:I2C=1 does.
ADC               ?= 0
BOARD_DEFS        += $(if $(filter 1,$(ADC)),-DTFW_CM_ADC=1)
BOARD_FEATURES_ON += $(if $(filter 1,$(ADC)),ANALOG)
BOARD_ROWS        += soft:ADC=1
