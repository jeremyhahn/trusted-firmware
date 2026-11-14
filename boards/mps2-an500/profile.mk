# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# Arm's MPS2+ FPGA image AN500 (a Cortex-M7, no TrustZone), as QEMU models it:
# `qemu-system-arm -M mps2-an500`. The generic Cortex-M port's second CI target
# (row cortex-m, slice 2), so every change to the core boots on an ARMv7-M M7
# as well as the ARMv8-M M33 of mps2-an505; the board page is
# docs/boards/mps2-an500.md.
ARCH           := cortex-m
CPU            := cortex-m7
SOC            := mps2

# The machine integration-test-cortex-m starts (mk/arch/cortex-m-targets.mk).
QEMU_MACHINE   := mps2-an500
# UART0's base, TFW_MPS2_UART0_BASE in board_profile.h: a failed wire row's
# dump reads its STATE, CTRL and INTSTATUS (qmp_regs.py).
QEMU_UART      := 0x40004000

# The core clock, TFW_CM_CPU_HZ in board_profile.h. The build does not read it
# (the clock is the header's); the provenance manifest's f_cpu_hz does.
F_CPU          := 25000000

# The memory map (QEMU v10.0.0 hw/arm/mps2.c, Arm AN500):
#
#   0x00000000  ZBT SSRAM1, 4 MiB  tfw-boot's 32 KiB region at its base, where
#                                  VTOR points at reset (0 on an ARMv7-M core);
#                                  the application slot after it, 128 KiB of
#                                  code; the platform block in the last 4 KiB
#   0x20000000  ZBT SSRAM2 and 3,  .data, .bss, the stack, the reset marker and
#               4 MiB together     the handoff block at the top (RAM_END)
#
# The code region is RAM that QEMU loads from the raw images, so a write "to
# flash" is a store, and a reset restores everything but the platform block and the image store,
# as on mps2-an505 (boards/mps2-an505/profile.mk).
FLASH_BUDGET   := 131072
RAM_BUDGET     := 16384
FLASH_TOTAL    := 4194304
RAM_TOTAL      := 4194304

# One slot, or two by swap-move (BOOT_AB=1), as on mps2-an505: the slots in
# board_profile.h, the application linked at the primary.
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

# The region tfw-boot measures (mk/release.mk's provenance): the application
# slot, TFW_APP_REGION_BASE and TFW_APP_REGION_SIZE in board_profile.h, as on
# mps2-an505. Not flash 0, which is tfw-boot's own region.
FWHASH_BASE    := $(APP_BASE)
FWHASH_SIZE    := $(FLASH_BUDGET)

# tfw-boot's geometry. BOOT_BASE is where it links; mk/arch/cortex-m.mk checks
# its load end against BOOT_BASE + BOOT_SECTION.
BOOT_SECTION   := 32768
BOOT_BASE      := 0x00000000
BOOT_BUDGET    := 32768

# The soft root only: no element on this board, and P-256 in software for the
# image signature (mk/boot.mk's BOOT_SUPPORTED row), as on mps2-an505.
BOARD_TIERS    := soft

# The image store, as on mps2-an505: memory in SSRAM1 that the reset leaves
# alone, the span in board_profile.h (src/store/mem.c).
OTA_STORE      := mem

# I2C over the shield 0 SBCon (src/hal/cortex-m/mps2/hal_bus.c), off by
# default: QEMU puts no device on the bus. I2C=1 turns TFW_FEATURE_I2C on
# (TFW_CM_I2C in board_profile.h) and links the bus. With no element on this
# board the row is the soft tier's: soft:I2C=1 builds it and size-check
# measures it.
#
# SPI over the shield 0 PL022 (the same hal_bus.c), off by default for the same
# reason. SPI=1 turns TFW_FEATURE_SPI on (TFW_CM_SPI in board_profile.h) and
# links the bus; SPI_XFER's select is pin 2, the FPGA I/O block's MISC bit 8
# (board.c). The soft:SPI=1 row builds it and size-check measures it, as
# mps2-an505's does.
#
# Pulse timing (PULSE_IN, PULSE_CAPTURE) on SysTick, src/hal/cortex-m/
# hal_pulse.c, off by default: QEMU drives no pin, so nothing can be measured.
# PULSE=1 turns TFW_FEATURE_PULSE on (TFW_CM_PULSE in board_profile.h) and
# links the timer; the soft:PULSE=1 row builds it, size-check measures it, and
# its QEMU wire row checks that PULSE_IN on an undriven pin times out, as
# mps2-an505's does.
#
# The tone on LED1, pin 1, toggled from the CMSDK timer TIMER0's interrupt
# (src/hal/cortex-m/mps2/hal_tone.c), off by default: QEMU does not measure the
# pin. TONE=1 turns TFW_FEATURE_TONE on (TFW_CM_TONE in board_profile.h); the
# soft:TONE=1 row builds it and size-check measures it.
#
# 1-Wire bit-banged on GPIO block 0 bit 0 (the same hal_bus.c, gpio_line.h),
# off by default for the same reason. ONEWIRE=1 turns TFW_FEATURE_ONEWIRE on
# (TFW_CM_ONEWIRE in board_profile.h) and adds the line as pin 3 (board.c).
# The soft:ONEWIRE=1 row builds it and size-check measures it.
# PWM=1: software PWM on the CMSDK dual timer (src/hal/cortex-m/mps2/
# hal_pwm.c) on the two LEDs, pins 0 and 1, off by default: nothing on the
# board needs it and QEMU cannot show the LEDs' duty. TFW_CM_PWM turns
# TFW_FEATURE_PWM on in board_profile.h; the soft:PWM=1 row builds it and
# size-check measures it, and cortex-m-qemu-pwm-wire runs it.
I2C               ?= 0
SPI               ?= 0
PWM               ?= 0
PULSE             ?= 0
TONE              ?= 0
ONEWIRE           ?= 0
BOARD_FEATURES_ON := $(if $(filter 1,$(I2C)),I2C) $(if $(filter 1,$(SPI)),SPI) $(if $(filter 1,$(PULSE)),PULSE) \
                     $(if $(filter 1,$(TONE)),TONE) $(if $(filter 1,$(ONEWIRE)),ONEWIRE) $(if $(filter 1,$(PWM)),PWM)
BOARD_ROWS        := soft:I2C=1 soft:SPI=1 soft:PULSE=1 soft:TONE=1 soft:ONEWIRE=1 soft:PWM=1
