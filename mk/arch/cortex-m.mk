# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# The Cortex-M architecture: the GNU Arm Embedded toolchain, newlib-nano, and
# one linker script per SoC under mk/arch/cortex-m/ (#161).
#
# Included by mk/firmware.mk after the board's profile.mk says ARCH := cortex-m,
# with CPU (the -mcpu core: cortex-m33 on mps2-an505, cortex-m7 on
# mps2-an500) and SOC (the directory under src/hal/cortex-m/ and
# boot/hal/cortex-m/ that holds the part's peripherals, mps2 on both, which
# read their addresses from the board profile). The core's code is
# src/hal/cortex-m/; mk/firmware.mk's HAL_SOC_DIR and mk/boot.mk's
# BOOT_SOC_SRCS add the SoC's. The contract every name below fills is the one
# mk/arch/avr.mk lists. The ADR: docs/cortex-m/adr-reference-part-and-emulation.md.

# --------------------------------------------------------------- toolchain
#
# Recursive, so the check runs when a recipe needs a compiler and not on every
# make invocation: `make board-tiers` and `make firmware-srcs`, which the sweep
# targets ask of every board, still answer where the toolchain is absent. A
# build that needs it fails here and names the packages; there is no fallback
# and no skip.
CM_PREFIX := arm-none-eabi-
cm_tool = $(if $(shell command -v $(CM_PREFIX)$(1) 2>/dev/null),$(CM_PREFIX)$(1),$(error \
  $(CM_PREFIX)$(1) is not on PATH: $(BOARD) is a Cortex-M board and needs the \
  Debian packages gcc-arm-none-eabi binutils-arm-none-eabi libnewlib-arm-none-eabi \
  (docker/packages.txt; the CI image has them)))

ARCH_CC      = $(call cm_tool,gcc)
ARCH_OBJCOPY = $(call cm_tool,objcopy)
ARCH_SIZE    = $(call cm_tool,size)
ARCH_NM      = $(call cm_tool,nm)
ARCH_LIBC    := newlib-nano

# Static RAM an image occupies: every section the ELF places in the SRAM bank,
# read from the section table by address, as mk/arch/avr.mk does and for its
# reason. The handoff block and the reset marker are in the bank and count.
CM_RAM_LO := $(shell printf '%d' $$(( $(RAM_BASE) )))
CM_RAM_HI := $(shell printf '%d' $$(( $(RAM_END) + 1 )))
ARCH_RAM_USED = $(ARCH_SIZE) -A -d $(1) | awk '$$3 >= $(CM_RAM_LO) && $$3 < $(CM_RAM_HI) { s += $$2 } END { if (NR) print s + 0 }'

# Soft float on every core: no image here uses floating point, and the FPU's
# lazy-stacking state would be one more thing tfw-boot hands across.
ARCH_MACH_CFLAGS := -mcpu=$(CPU) -mthumb -mfloat-abi=soft
ARCH_OPT_CFLAGS  := -Os -flto -ffunction-sections -fdata-sections \
                    -fno-fat-lto-objects

# -nostartfiles: the vector table and the reset handler are the HAL's
# (src/hal/cortex-m/hal_early.c), shared by both images, not the crt's.
# nosys.specs supplies the system calls newlib-nano names and nothing here
# calls; nano.specs is the small newlib.
ARCH_LDFLAGS := -mcpu=$(CPU) -mthumb -mfloat-abi=soft -Os -flto \
                -specs=nano.specs -specs=nosys.specs -nostartfiles \
                -Wl,--gc-sections

ARCH_BOOT_OPT_CFLAGS :=

# micro-ecc for BOOT_SIG=p256-soft (mk/boot.mk): portable C on a 32-bit word.
# Its own detection would name an ARM platform, and uECC.c then includes
# asm_arm.inc at every optimisation level; that file is not vendored, and
# uECC_PLATFORM=uECC_arch_other is micro-ecc's own switch to leave it out.
ARCH_UECC_CFLAGS := -DuECC_PLATFORM=uECC_arch_other -DuECC_WORD_SIZE=4

# ---------------------------------------------------------------- placement
#
# One script per SoC, the same for both images; the addresses are the board's
# (profile.mk) and come in as symbols, so the script holds no number a profile
# also states. The handoff block, the reset marker and the stack top follow
# mk/arch/avr.mk's one formula from RAM_END, with no 0x800000 offset: the two
# images land on the same addresses because both compute them from the same
# line. The fault record (src/hal/cortex-m/hal_fault_frame.h) is the 12 bytes
# below the marker, and the stack top below both is 8-aligned, as the AAPCS
# requires at a call.
CM_LDSCRIPT := mk/arch/cortex-m/$(SOC).ld
ifneq ($(RAM_END),)
CM_HANDOFF_AT  := $(shell printf '0x%x' $$(( $(RAM_END) - 111 )))
CM_RESETMARK_AT := $(shell printf '0x%x' $$(( $(RAM_END) - 115 )))
CM_FAULTFRAME_AT := $(shell printf '0x%x' $$(( $(RAM_END) - 127 )))
CM_STACK_TOP   := $(shell printf '0x%x' $$(( ($(RAM_END) - 127) & ~7 )))
endif
CM_PLACEMENT = -Wl,-T,$(CM_LDSCRIPT) \
               -Wl,--defsym=__tfw_text_base=$(1) -Wl,--defsym=__tfw_text_size=$(2) \
               -Wl,--defsym=__tfw_ram_base=$(RAM_BASE) \
               -Wl,--defsym=__tfw_handoff=$(CM_HANDOFF_AT) \
               -Wl,--defsym=__tfw_resetmark=$(CM_RESETMARK_AT) \
               -Wl,--defsym=__tfw_faultframe=$(CM_FAULTFRAME_AT) \
               -Wl,--defsym=__stack=$(CM_STACK_TOP)

# The application in its slot, tfw-boot in its region. The slot's length for
# the link is the code budget, so an image past it is ld's error, not a
# size-check's afterthought.
ARCH_FW_PLACEMENT_LDFLAGS   = $(call CM_PLACEMENT,$(APP_BASE),$(FLASH_BUDGET))
ARCH_BOOT_PLACEMENT_LDFLAGS = $(call CM_PLACEMENT,$(BOOT_BASE),$(BOOT_SECTION))

# The boot region is not at the top of flash (mps2-an505's is at 0x10000000,
# mps2-an500's at 0), so mk/boot.mk's load-end check is against the region's
# end address rather than FLASH_TOTAL.
ifneq ($(BOOT_SECTION),)
ARCH_BOOT_LOAD_LIMIT := $(shell printf '0x%x' $$(( $(BOOT_BASE) + $(BOOT_SECTION) )))
ARCH_BOOT_STACK_TOP  := $(CM_STACK_TOP)
endif

# No assembly startup: the reset handler is C in hal_early.c, which both
# images link, so mk/boot.mk has no separate object to build.
ARCH_BOOT_START_SRC :=
ARCH_BOOT_START_LDFLAGS := -nostartfiles

# tfw-boot's factory copies on a two-slot board (BOOT_AB=1, the MPS2 A/B rows
# in mk/boot.mk), an empty call on one slot: boot/hal/cortex-m/boot_factory.c
# says why QEMU needs them. mk/boot.mk adds the file to tfw-boot's objects.
ARCH_BOOT_SRCS = $(BOOT_HAL_DIR)/boot_factory$(if $(filter 1,$(BOOT_AB)),,_none).c

# The identity sources and the update request link as on AVR: the handoff
# block is a fixed SRAM address both images name (RAM_END above).

# The portable CRC update the host and the bootloaders link. AVR has its own
# fast one in its HAL (src/hal/avr/hal_crc16.c) and esp32 adds this file to its
# IDF list (ARCH_EXTRA_SRCS); here mk/firmware.mk adds it to the application's
# object list. Optional in the contract: an architecture whose HAL carries the
# CRC leaves it empty.
ARCH_FW_SRCS := src/proto/crc16_update.c

# The SBCon's and the PL022's register access, linked beside the SoC's
# hal_bus.c when a bus is on (mk/firmware.mk BUS_SRCS) and kept out of the HAL
# wildcard otherwise. Recursive: HAL_SOC_DIR is set after this file is included.
ARCH_BUS_SRCS      = $(if $(HAL_SOC_DIR),$(wildcard $(HAL_SOC_DIR)/sbcon.c $(HAL_SOC_DIR)/pl022.c))
ARCH_HAL_OPTIONAL  = $(ARCH_BUS_SRCS)

# ----------------------------------------------------------------- writing
#
# The first board is an emulator: there is no part to write, and `make flash`
# says so rather than pretending. QEMU loads the image itself
# (mk/arch/cortex-m-targets.mk). A board with a debug probe sets CM_FLASH_TOOL.
ARCH_CM_NOFLASH = echo "$@: $(BOARD) is an emulated board: QEMU loads the image (make integration-test-cortex-m)" >&2; exit 1
ARCH_FLASH         = $(ARCH_CM_NOFLASH)
ARCH_BOOT_FLASH    = $(ARCH_CM_NOFLASH)
ARCH_BOARD_RESTORE = echo "$@: $(BOARD) is an emulated board: it has no factory state" >&2; exit 1
ARCH_LOCK_CHECK    = echo "$@: $(BOARD) is an emulated board: it has no lock bits" >&2; exit 1
