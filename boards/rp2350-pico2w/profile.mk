# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# Raspberry Pi Pico 2 W: an RP2350A with its two Cortex-M33 cores, 520 KiB of
# SRAM and 4 MiB of QSPI flash behind the execute-in-place window (SPEC row
# `rp2350`). The port is the generic Cortex-M one (#161) with the part's
# peripherals in src/hal/cortex-m/rp2350/; the board page is
# docs/boards/rp2350-pico2w.md and the onboarding ADR
# docs/multi-arch/adr-rp2350-pico2w-onboarding.md.
#
# Board id 0x44, allocated in docs/boards/board-id-registry.md (the ADR's open
# question 8). No board is written by this slice.
ARCH           := cortex-m
CPU            := cortex-m33
SOC            := rp2350

# The part scripts/board-check.py checks the totals and geometry against.
MCU            := rp2350
# The generic script with the boot ROM's IMAGE_DEF block and .ramfunc
# (mk/arch/cortex-m.mk).
CM_LDSCRIPT    := mk/arch/cortex-m/rp2350.ld

# The memory map ([RP-DS], "Address map"), core 0 only, Secure with the SAU off
# as the boot ROM leaves an Arm image:
#
#   0x10000000  XIP flash, 4 MiB  tfw-boot's 32 KiB region at its base, where
#                                 the boot ROM looks for an IMAGE_DEF; the
#                                 application slot after it, 128 KiB of code;
#                                 the platform block in the last 4 KiB sector
#   0x20000000  SRAM0-7, 512 KiB  .data, .bss, the stack, the reset marker and
#                                 the handoff block at the top (RAM_END)
#
# SRAM8-9 (0x20080000, 8 KiB) is left alone: the boot ROM may use it while it
# runs, and nothing here needs it. The addresses are [RP-DS] release 8 §2.2,
# tables 10-11. That the boot ROM uses SRAM8-9 has no primary source: it is
# not in [RP-DS], and pico-sdk 2.1.1's default map puts the core stacks there
# once an image runs (src/rp2_common/pico_crt0/rp2350/memmap_default.ld lines
# 28-29, SCRATCH_X and SCRATCH_Y). It stays unused because that costs nothing:
# the budgets above need 64 KiB of the 512 KiB in SRAM0-7.
#
# FLASH_TOTAL is 4 MiB: pico-sdk 2.1.1 (bddd20f928ce), board header
# src/boards/include/boards/pico2_w.h line 79, PICO_FLASH_SIZE_BYTES
# (4 * 1024 * 1024), which is what every Pico 2 W image is built for, and the
# Pico 2 W datasheet (release 2) §1, "4 MB of flash memory". The same
# datasheet names a W25Q16JV (2 MB) on that page and says "2 MB" in §3.1; both
# are taken as its errors, against the SDK and its own headline figure. The
# platform block is the last 4 KiB sector of the 4 MiB, 0x103FF000
# (board_profile.h; test/unit/boot/test_boot_rp2350_map.c pins the map). If a
# board's part were 2 MB, that address would alias the sector at 0x101FF000,
# outside every region: reading the part's JEDEC id on the bench settles it
# before any flash write.
FLASH_BUDGET   := 131072
RAM_BUDGET     := 65536
FLASH_TOTAL    := 4194304
RAM_TOTAL      := 524288

APP_BASE       := 0x10008000
RAM_BASE       := 0x20000000
RAM_END        := 0x2007FFFF

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

# The soft root only until the owner rules on OTP (the ADR's open questions
# 1-4): no element on this board and no OTP write in any lane.
BOARD_TIERS    := soft

# Written over USB with picotool, the board in BOOTSEL mode, as the onboarding
# ADR has it: the Pico 2 W has no on-board debug probe. picotool reads and
# writes the flash and dumps the OTP, which is recorded and never written;
# mk/arch/cortex-m.mk holds the recipes. The record is the whole 4 MiB from the
# XIP base.
CM_FLASH_TOOL  := picotool
CM_FLASH_BASE  := 0x10000000
