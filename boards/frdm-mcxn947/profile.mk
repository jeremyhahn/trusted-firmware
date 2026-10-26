# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# NXP FRDM-MCXN947: the MCXN947, a dual Cortex-M33 with the EdgeLock secure
# subsystem, of which this port runs core 0 only and drives no EdgeLock
# function (SPEC row mcxn947, docs/boards/frdm-mcxn947.md). The part's
# peripherals are src/hal/cortex-m/mcxn94x/; the core is the generic port's.
#
# Board id 0x46, allocated in docs/boards/board-id-registry.md. No board has
# run this image.
ARCH           := cortex-m
CPU            := cortex-m33
SOC            := mcxn94x
# The part board-check reads the totals against (scripts/board-check.py PARTS).
MCU            := mcxn947

# No QEMU_MACHINE: QEMU models no MCX part, so integration-test-cortex-m does
# not run this board; `make flash` writes it through its probe (below).

# The memory map, every address the Secure alias, which is what core 0 sees
# out of reset with the SAU off (MCXNx4x Reference Manual, memory map chapter,
# section to confirm; the SDK's MCXN947_cm33_core0.h and its flash linker
# script, docs/reference/README.md):
#
#   0x10000000  internal flash,  tfw-boot's 32 KiB region at its base, where
#               2 MiB in two     the boot ROM enters a plain image; the
#               1 MiB banks      application slot after it, 128 KiB of code;
#                                the platform block in the last 8 KiB sector
#   0x30000000  SRAM A-H,        .data, .bss, the stack, the reset marker and
#               312 KiB with     the handoff block at the top (RAM_END)
#               RAM ECC off
#
# 312 KiB is the SDK's m_data length (0x4E000): the RAM banks once
# hal_soc.c's first act turns their ECC off and the ECC words become RAM.
FLASH_BUDGET   := 131072
RAM_BUDGET     := 16384
FLASH_TOTAL    := 2097152
RAM_TOTAL      := 319488

APP_BASE       := 0x10008000
RAM_BASE       := 0x30000000
RAM_END        := 0x3004DFFF

# The core clock, TFW_CM_CPU_HZ in board_profile.h. The build does not read it
# (the clock is the header's); the provenance manifest's f_cpu_hz does.
F_CPU          := 12000000

# The region tfw-boot measures (mk/release.mk's provenance): the application
# slot, TFW_APP_REGION_BASE and TFW_APP_REGION_SIZE in board_profile.h, which
# boot/hal/cortex-m reads by address. Not flash 0, which is tfw-boot's region.
FWHASH_BASE    := $(APP_BASE)
FWHASH_SIZE    := $(FLASH_BUDGET)

# tfw-boot's geometry. BOOT_BASE is where it links; mk/arch/cortex-m.mk checks
# its load end against BOOT_BASE + BOOT_SECTION.
BOOT_SECTION   := 32768
BOOT_BASE      := 0x10000000
BOOT_BUDGET    := 32768

# The soft root only: the EdgeLock subsystem is not driven, and no element is
# fitted (SPEC row mcxn947).
BOARD_TIERS    := soft

# Written through the board's on-board MCU-Link, a CMSIS-DAP probe, with
# pyOCD and NXP's CMSIS pack for the part (pyocd pack install mcxn947vdf);
# mk/arch/cortex-m.mk holds the recipes. The record is the whole 2 MiB from the
# flash base. The protected flash (CMPA, CFPA) is not read: where it sits in
# this part's map is not cited yet (docs/boards/frdm-mcxn947.md), and it is
# never written.
CM_FLASH_TOOL    := pyocd
CM_PROBE_TARGET  := mcxn947vdf
CM_FLASH_BASE    := 0x10000000
CM_NOEXTRA_WHY   := no protected-flash read: its place in the MCXN947 map is not cited yet
