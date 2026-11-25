# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# Arm's MPS2+ FPGA image AN500 (a Cortex-M7, no TrustZone), as QEMU models it: `qemu-system-arm -M mps2-an500`.
ARCH           := cortex-m
CPU            := cortex-m7
SOC            := mps2

# The machine integration-test-cortex-m starts (mk/arch/cortex-m-targets.mk).
QEMU_MACHINE   := mps2-an500
# UART0's base, TFW_MPS2_UART0_BASE in board_profile.h: a failed wire row's dump reads its STATE, CTRL and INTSTATUS (qmp_regs.py).
QEMU_UART      := 0x40004000

# The core clock, TFW_CM_CPU_HZ in board_profile.h.
F_CPU          := 25000000

# The memory map (QEMU v10.0.0 hw/arm/mps2.c, Arm AN500):
FLASH_BUDGET   := 131072
RAM_BUDGET     := 16384
FLASH_TOTAL    := 4194304
RAM_TOTAL      := 4194304

# One slot, or two by swap-move (BOOT_AB=1), as on mps2-an505: the slots in board_profile.h, the application linked at the primary.
BOOT_AB          ?= 0
BOOT_AB_OPTIONAL := 1
BOARD_DEFS        = $(if $(filter 1,$(BOOT_AB)),-DTFW_CM_AB=1) $(if $(filter 1,$(I2C)),-DTFW_CM_I2C=1) \
                    $(if $(filter 1,$(SPI)),-DTFW_CM_SPI=1) $(if $(filter 1,$(PULSE)),-DTFW_CM_PULSE=1) \
                    $(if $(filter 1,$(TONE)),-DTFW_CM_TONE=1) \
                    $(if $(filter 1,$(ONEWIRE)),-DTFW_CM_ONEWIRE=1) \
                    $(if $(filter 1,$(PWM)),-DTFW_CM_PWM=1)
APP_BASE         := $(if $(filter 1,$(BOOT_AB)),0x00040000,0x00008000)
RAM_BASE       := 0x20000000
RAM_END        := 0x203FFFFF

# The region tfw-boot measures (mk/release.mk's provenance): the application slot, TFW_APP_REGION_BASE and TFW_APP_REGION_SIZE in board_profile.h, as on mps2-an505.
FWHASH_BASE    := $(APP_BASE)
FWHASH_SIZE    := $(FLASH_BUDGET)

# Tfw-boot's geometry.
BOOT_SECTION   := 32768
BOOT_BASE      := 0x00000000
BOOT_BUDGET    := 32768

# The soft root only: no element on this board, and P-256 in software for the image signature (mk/boot.mk's BOOT_SUPPORTED row), as on mps2-an505.
BOARD_TIERS    := soft

# The image store, as on mps2-an505: memory in SSRAM1 that the reset leaves alone, the span in board_profile.h (src/store/mem.c).
OTA_STORE      := mem

# I2C over the shield 0 SBCon (src/hal/cortex-m/mps2/hal_bus.c), off by default: QEMU puts no device on the bus.
I2C               ?= 0
SPI               ?= 0
PWM               ?= 0
PULSE             ?= 0
TONE              ?= 0
ONEWIRE           ?= 0
BOARD_FEATURES_ON := $(if $(filter 1,$(I2C)),I2C) $(if $(filter 1,$(SPI)),SPI) $(if $(filter 1,$(PULSE)),PULSE) \
                     $(if $(filter 1,$(TONE)),TONE) $(if $(filter 1,$(ONEWIRE)),ONEWIRE) $(if $(filter 1,$(PWM)),PWM)
BOARD_ROWS        := soft:I2C=1 soft:SPI=1 soft:PULSE=1 soft:TONE=1 soft:ONEWIRE=1 soft:PWM=1
