# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# Nordic's nRF54L15 DK (PCA10156): an nRF54L15, whose application core is a
# Cortex-M33 at 128 MHz, on the generic Cortex-M port (#161) as its first
# silicon board, SPEC row nrf54l15 (#17). The ADR is
# docs/multi-arch/adr-nrf54l15-dk-onboarding.md; the board page, with every
# value below and its vendor source, is docs/boards/nrf54l15-dk.md.
#
# The application image (the clock, the console UARTE and the DK's LEDs and
# buttons) and tfw-boot's soft row, mk/boot.mk's BOOT_SUPPORTED: soft hash and
# P-256 verify in software, no OTA, no store, its platform-block writes through
# the RRAM controller (boot/hal/cortex-m/nrf54l/). The KMU and TrustZone are
# later slices.
#
# Board id 0x45, recorded in docs/boards/board-id-registry.md and
# sdk/go/boardid.go.
ARCH           := cortex-m
CPU            := cortex-m33
SOC            := nrf54l
MCU            := nrf54l15
# The generic script with the SoC's vector tail (mk/arch/cortex-m.mk).
CM_LDSCRIPT    := mk/arch/cortex-m/nrf54l.ld

# The memory map, every address the Secure alias the core sees out of reset
# (nrfx MDK nrf54l15_xxaa_application_memory.h):
#
#   0x00000000  RRAM, 1524 KiB    tfw-boot's 32 KiB region at the base, where
#                                 the core fetches its reset vector; the
#                                 application slot after it, 128 KiB of code;
#                                 the platform block in the last 4 KiB
#   0x20000000  RAM, 256 KiB      .data, .bss, the stack, the reset marker and
#                                 the handoff block at the top (RAM_END)
#
# The application slot is 1 KiB-aligned: VTOR takes the table's size rounded
# up to a power of two, and this SoC's table is 860 bytes
# (mk/arch/cortex-m/nrf54l.ld asserts it).
FLASH_BUDGET   := 131072
RAM_BUDGET     := 16384
FLASH_TOTAL    := 1560576
RAM_TOTAL      := 262144

APP_BASE       := 0x00008000
RAM_BASE       := 0x20000000
RAM_END        := 0x2003FFFF

# The core clock, TFW_CM_CPU_HZ in board_profile.h. The build does not read it
# (the clock is the header's); the provenance manifest's f_cpu_hz does.
F_CPU          := 128000000

# The region tfw-boot measures (mk/release.mk's provenance): the application
# slot, TFW_APP_REGION_BASE and TFW_APP_REGION_SIZE in board_profile.h, which
# boot/hal/cortex-m reads by address. Not flash 0, which is tfw-boot's region.
FWHASH_BASE    := $(APP_BASE)
FWHASH_SIZE    := $(FLASH_BUDGET)

BOOT_SECTION   := 32768
BOOT_BASE      := 0x00000000
BOOT_BUDGET    := 32768

# The soft root only, as on mps2-an505: the KMU-held root is the later slice.
BOARD_TIERS    := soft

# Written through the DK's on-board J-Link OB with SEGGER's J-Link Commander,
# whose savebin, loadbin and verifybin give a raw read-out and a verified write
# of the RRAM; mk/arch/cortex-m.mk holds the recipes. The record is the whole
# RRAM from 0 and the UICR, which is recorded and never written (the onboarding
# ADR: no UICR, APPROTECT or KMU write, no erase, no recover). The UICR's
# address and size are the nrfx MDK's NRF_UICR_S_BASE and its 4 KiB page, to
# confirm on the board page before the first record.
CM_FLASH_TOOL    := jlink
CM_PROBE_TARGET  := NRF54L15_M33
CM_FLASH_BASE    := 0x00000000
CM_EXTRA_BASE    := 0x00FFD000
CM_EXTRA_BYTES   := 4096
