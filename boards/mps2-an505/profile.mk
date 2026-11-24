# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# Arm's MPS2+ FPGA image AN505 (a Cortex-M33 in the IoT Kit subsystem), as QEMU models it: `qemu-system-arm -M mps2-an505`.
ARCH           := cortex-m
CPU            := cortex-m33
SOC            := mps2

# The machine integration-test-cortex-m starts (mk/arch/cortex-m-targets.mk).
QEMU_MACHINE   := mps2-an505
# UART0's base, TFW_MPS2_UART0_BASE in board_profile.h: a failed wire row's dump reads its STATE, CTRL and INTSTATUS (qmp_regs.py).
QEMU_UART      := 0x40200000

# The core clock, TFW_CM_CPU_HZ in board_profile.h.
F_CPU          := 20000000

# The memory map, every address the Secure alias, which is what the core sees with the SAU off (QEMU, hw/arm/mps2-tz.c and hw/arm/armsse.c):
FLASH_BUDGET   := 131072
RAM_BUDGET     := 16384
FLASH_TOTAL    := 4194304
RAM_TOTAL      := 32768


# One application slot, or two by swap-move (BOOT_AB=1), as on the 2560: mk/boot.mk's A/B rows.
BOOT_AB          ?= 0
BOOT_AB_OPTIONAL := 1
BOARD_DEFS        = $(if $(filter 1,$(BOOT_AB)),-DTFW_CM_AB=1) $(if $(filter 1,$(I2C)),-DTFW_CM_I2C=1) \
                    $(if $(filter 1,$(SPI)),-DTFW_CM_SPI=1) $(if $(filter 1,$(TONE)),-DTFW_CM_TONE=1) \
                    $(if $(filter 1,$(PULSE)),-DTFW_CM_PULSE=1) \
                    $(if $(filter 1,$(ONEWIRE)),-DTFW_CM_ONEWIRE=1) \
                    $(if $(filter 1,$(PWM)),-DTFW_CM_PWM=1)
APP_BASE         := $(if $(filter 1,$(BOOT_AB)),0x38040000,0x38000000)
RAM_BASE       := 0x30000000
RAM_END        := 0x30007FFF

# The region tfw-boot measures (mk/release.mk's provenance): the application slot, TFW_APP_REGION_BASE and TFW_APP_REGION_SIZE in board_profile.h, which boot/hal/cortex-m reads by address.
FWHASH_BASE    := $(APP_BASE)
FWHASH_SIZE    := $(FLASH_BUDGET)

# Tfw-boot's geometry.
BOOT_SECTION   := 32768
BOOT_BASE      := 0x10000000
BOOT_BUDGET    := 32768

# The soft root only: no element on this board, and P-256 in software for the image signature (mk/boot.mk's BOOT_SUPPORTED row).
BOARD_TIERS    := soft

# I2C over the shield 0 SBCon (src/hal/cortex-m/mps2/hal_bus.c), off by default: QEMU puts no device on the bus.
I2C               ?= 0
SPI               ?= 0
PWM               ?= 0
TONE              ?= 0
PULSE             ?= 0
ONEWIRE           ?= 0
BOARD_FEATURES_ON := $(if $(filter 1,$(I2C)),I2C) $(if $(filter 1,$(SPI)),SPI) $(if $(filter 1,$(TONE)),TONE) $(if $(filter 1,$(PULSE)),PULSE) \
                     $(if $(filter 1,$(ONEWIRE)),ONEWIRE) $(if $(filter 1,$(PWM)),PWM)
BOARD_ROWS        := atecc608a:I2C=1 soft:I2C=1 soft:SPI=1 soft:TONE=1 soft:PULSE=1 soft:ONEWIRE=1 soft:PWM=1 optiga:I2C=1
# The optiga row is a measurement, as on the 2560 (row optiga-trust-m): the OPTIGA Trust M provider over the same SBCon, built and size-checked so its Cortex-M cost is a number.

# The image store: memory in the code bank that the reset leaves alone, the span in board_profile.h (src/store/mem.c).
OTA_STORE      := mem

# The tamper-response engine and its loop, behind src/hal/cortex-m/mps2/hsm_port.c: the M7's response loop exercised on QEMU before the part (mk/firmware.mk HSM_SRCS).
HSM            := 1

# TZ=1: the TrustZone-M split's two images instead of tfw-boot and the application (mk/arch/cortex-m-targets.mk tz-firmware), on the map in TZ_MAP; tz_soc_mps2.c opens the MPC and PPCs QEMU models.
TZ_MAP         := src/hal/cortex-m/tz/tz_map_mps2_an505.h
TZ_SOC_SRCS    := src/hal/cortex-m/tz/tz_soc_mps2.c
