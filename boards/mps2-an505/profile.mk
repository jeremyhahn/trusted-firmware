# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# Arm's MPS2+ FPGA image AN505 (a Cortex-M33 in the IoT Kit subsystem), as QEMU
# models it: `qemu-system-arm -M mps2-an505`. The generic Cortex-M port's
# reference board and CI's only Cortex-M target (#161); the ADR is
# docs/cortex-m/adr-reference-part-and-emulation.md.
#
# Board id 0x40 is PROVISIONAL: the first id of a Cortex-M range, taken for this
# port's tests until docs/boards/board-id-registry.md records it. Nothing
# outside CI is built for it.
ARCH           := cortex-m
CPU            := cortex-m33
SOC            := mps2

# The machine integration-test-cortex-m starts (mk/arch/cortex-m-targets.mk).
QEMU_MACHINE   := mps2-an505
# UART0's base, TFW_MPS2_UART0_BASE in board_profile.h: a failed wire row's
# dump reads its STATE, CTRL and INTSTATUS (qmp_regs.py).
QEMU_UART      := 0x40200000

# The core clock, TFW_CM_CPU_HZ in board_profile.h. The build does not read it
# (the clock is the header's); the provenance manifest's f_cpu_hz does.
F_CPU          := 20000000

# The memory map, every address the Secure alias, which is what the core sees
# with the SAU off (QEMU, hw/arm/mps2-tz.c and hw/arm/armsse.c):
#
#   0x10000000  SSRAM-0, 4 MiB   tfw-boot's 32 KiB region at its base, where
#                                the IoT Kit's INITSVTOR0 points the reset
#   0x38000000  SSRAM-1 and -2,  the application slot at the base, 128 KiB of
#               4 MiB together   code; the image store; the platform block in
#                                the last 4 KiB. Two slots (BOOT_AB=1): the
#                                table in board_profile.h
#   0x30000000  internal SRAM,   .data, .bss, the stack, the reset marker and
#               32 KiB           the handoff block at the top (RAM_END)
#
# The code regions are RAM that QEMU loads from the ELF files, so a write
# "to flash" is a store. QEMU copies the loaded images in again on every system
# reset, so only a store outside them (the platform block, the image store)
# survives a reset, and none survives a new QEMU.
FLASH_BUDGET   := 131072
RAM_BUDGET     := 16384
FLASH_TOTAL    := 4194304
RAM_TOTAL      := 32768


# One application slot, or two by swap-move (BOOT_AB=1), as on the 2560:
# mk/boot.mk's A/B rows. The slots are board data in board_profile.h, chosen by
# TFW_CM_AB; the application links at the slot it runs from, the primary.
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

# The region tfw-boot measures (mk/release.mk's provenance): the application
# slot, TFW_APP_REGION_BASE and TFW_APP_REGION_SIZE in board_profile.h, which
# boot/hal/cortex-m reads by address. Not flash 0, which this map leaves empty.
FWHASH_BASE    := $(APP_BASE)
FWHASH_SIZE    := $(FLASH_BUDGET)

# tfw-boot's geometry. BOOT_BASE is where it links; mk/arch/cortex-m.mk checks
# its load end against BOOT_BASE + BOOT_SECTION.
BOOT_SECTION   := 32768
BOOT_BASE      := 0x10000000
BOOT_BUDGET    := 32768

# The soft root only: no element on this board, and P-256 in software for the
# image signature (mk/boot.mk's BOOT_SUPPORTED row).
BOARD_TIERS    := soft

# I2C over the shield 0 SBCon (src/hal/cortex-m/mps2/hal_bus.c), off by
# default: QEMU puts no device on the bus. I2C=1 turns TFW_FEATURE_I2C on
# (TFW_CM_I2C in board_profile.h) and links the bus; the row builds the element
# tier with it, measured by size-check as every row is. Not a supported tier:
# no element has been on this board's bus. QEMU has no element either, so the
# QEMU I2C wire row boots the soft tier with I2C=1: soft:I2C=1 builds that
# image, so size-check measures the image the wire row runs.
#
# SPI over the shield 0 PL022 (the same hal_bus.c), off by default for the same
# reason. SPI=1 turns TFW_FEATURE_SPI on (TFW_CM_SPI in board_profile.h) and
# links the bus; SPI_XFER's select is pin 2, the FPGA I/O block's MISC bit 8
# (board.c). The soft:SPI=1 row builds it and size-check measures it.
#
# The tone on LED1, pin 1, toggled from the CMSDK timer TIMER0's interrupt
# (src/hal/cortex-m/mps2/hal_tone.c), off by default: QEMU does not measure the
# pin. TONE=1 turns TFW_FEATURE_TONE on (TFW_CM_TONE in board_profile.h); the
# soft:TONE=1 row builds it and size-check measures it.
# Pulse timing (PULSE_IN, PULSE_CAPTURE) on SysTick, src/hal/cortex-m/
# hal_pulse.c, off by default: QEMU drives no pin, so nothing can be measured.
# PULSE=1 turns TFW_FEATURE_PULSE on (TFW_CM_PULSE in board_profile.h) and
# links the timer; the soft:PULSE=1 row builds it, size-check measures it, and
# its QEMU wire row checks that PULSE_IN on an undriven pin times out.
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
TONE              ?= 0
PULSE             ?= 0
ONEWIRE           ?= 0
BOARD_FEATURES_ON := $(if $(filter 1,$(I2C)),I2C) $(if $(filter 1,$(SPI)),SPI) $(if $(filter 1,$(TONE)),TONE) $(if $(filter 1,$(PULSE)),PULSE) \
                     $(if $(filter 1,$(ONEWIRE)),ONEWIRE) $(if $(filter 1,$(PWM)),PWM)
BOARD_ROWS        := atecc608a:I2C=1 soft:I2C=1 soft:SPI=1 soft:TONE=1 soft:PULSE=1 soft:ONEWIRE=1 soft:PWM=1 optiga:I2C=1
# The optiga row is a MEASUREMENT, as on the 2560 (row optiga-trust-m): the
# OPTIGA Trust M provider over the same SBCon, built and size-checked so its
# Cortex-M cost is a number. Whether the tier is the board's is the OPTIGA ADR's
# open question 5; until it is answered BOARD_TIERS does not name it.

# The image store: memory in the code bank that the reset leaves alone, the
# span in board_profile.h (src/store/mem.c). The stager is linked where an image
# asks for it (OTA_STAGE=1); tfw-boot reads the store in its BOOT_STORE row.
OTA_STORE      := mem

# The tamper-response engine and its loop (#50, docs/hsm-self-defence/
# m7-design.md slice 3), behind src/hal/cortex-m/mps2/hsm_port.c: the
# M7's response loop exercised on QEMU before the part (mk/firmware.mk HSM_SRCS).
HSM            := 1
