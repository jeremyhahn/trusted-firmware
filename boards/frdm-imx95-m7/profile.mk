# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# NXP FRDM-IMX95-PRO, the firmware core on the i.MX 95's Cortex-M7 (SPEC row `imx95`, first slice): a board of the generic Cortex-M port, whose reference board is mps2-an505.
ARCH           := cortex-m
CPU            := cortex-m7
SOC            := imx95
# The part board-check's budgets are checked against (scripts/board-check.py, PARTS): the M7 of the board's MIMX9596 (UM12527 §1.2 Table 1).
MCU            := mimx9596-m7
# The generic script with the resource table's checks (mk/arch/cortex-m.mk).
CM_LDSCRIPT    := mk/arch/cortex-m/imx95.ld

# The core clock, TFW_CM_CPU_HZ in board_profile.h (to CONFIRM there).
F_CPU          := 800000000

# The memory map, as the M7 sees it (IMX95RM, the Cortex-M7 memory map; to CONFIRM).
FLASH_BUDGET   := 196608
RAM_BUDGET     := 65536
FLASH_TOTAL    := 262144
RAM_TOTAL      := 262144

APP_BASE       := 0x00008000
RAM_BASE       := 0x20000000
RAM_END        := 0x2003FFFF

# The region tfw-boot measures (mk/release.mk's provenance): the application slot, TFW_APP_REGION_BASE and TFW_APP_REGION_SIZE in board_profile.h.
FWHASH_BASE    := $(APP_BASE)
FWHASH_SIZE    := $(FLASH_BUDGET)

# Tfw-boot's geometry, reserved for its port; mk/arch/cortex-m.mk computes the load limit from it.
BOOT_SECTION   := 32768
BOOT_BASE      := 0x00000000
BOOT_BUDGET    := 32768

# The soft root until the EdgeLock enclave tier lands: the row's key tier is the enclave, and this slice does not touch it.
BOARD_TIERS    := soft

# PWM on the M7's TPM (src/hal/cortex-m/imx95/hal_pwm.c), off by default: the TPM instance the System Manager gives the M7 is open (imx95-design.md question 17), so TFW_IMX95_TPM_BASE is zero and every PWM call answers unsupported.
PWM               ?= 0
BOARD_DEFS        += $(if $(filter 1,$(PWM)),-DTFW_CM_PWM=1)
BOARD_FEATURES_ON += $(if $(filter 1,$(PWM)),PWM)
BOARD_ROWS        += soft:PWM=1

# The SAR ADC (src/hal/cortex-m/imx95/hal_adc.c), off by default: its base is to CONFIRM (TFW_IMX95_ADC_BASE in board_profile.h, zero until a pinned source gives it) and nothing has run it on the board.
ADC               ?= 0
BOARD_DEFS        += $(if $(filter 1,$(ADC)),-DTFW_CM_ADC=1)
BOARD_FEATURES_ON += $(if $(filter 1,$(ADC)),ANALOG)
BOARD_ROWS        += soft:ADC=1

# I2C over one LPI2C as a polled master (src/hal/cortex-m/imx95/hal_bus.c), off by default.
I2C               ?= 0
BOARD_DEFS        += $(if $(filter 1,$(I2C)),-DTFW_CM_I2C=1)
BOARD_FEATURES_ON += $(if $(filter 1,$(I2C)),I2C)
BOARD_ROWS        += soft:I2C=1

# Tone on a GPT output compare, off by default: the timer's base is to CONFIRM (docs/multi-arch/imx95-design.md question 17), so every call answers TFW_ERR_UNSUPPORTED until it is known.
TONE              ?= 0
BOARD_DEFS        += $(if $(filter 1,$(TONE)),-DTFW_CM_TONE=1)
BOARD_FEATURES_ON += $(if $(filter 1,$(TONE)),TONE)
BOARD_ROWS        += soft:TONE=1
