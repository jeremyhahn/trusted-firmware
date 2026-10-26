# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# The Cortex-M architecture: the GNU Arm Embedded toolchain, newlib-nano, and
# one linker script, mk/arch/cortex-m/image.ld, for every SoC (#161) but those
# whose profile.mk names their own (the i.MX 95 M7's imx95.ld, which includes
# it; rp2350.ld and nrf54l.ld, which place sections image.ld refuses).
#
# Included by mk/firmware.mk after the board's profile.mk says ARCH := cortex-m,
# with CPU (the -mcpu core: cortex-m33 on every board but mps2-an500's and the
# i.MX 95's M7s, which are cortex-m7) and SOC (the directory under
# src/hal/cortex-m/ and boot/hal/cortex-m/ that holds the part's peripherals:
# mps2 on QEMU's mps2-an505 and mps2-an500, which read their addresses from the
# board profile, stm32h5 on ST's STM32H573I-DK, nrf54l on the nRF54L15 DK,
# mcxn94x on the FRDM-MCXN947, which has no tfw-boot SoC directory yet). The
# core's code is
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
# One script for every SoC and both images; the addresses are the board's
# (profile.mk) and come in as symbols, so the script holds no number a profile
# also states. The handoff block, the reset marker and the stack top follow
# mk/arch/avr.mk's one formula from RAM_END, with no 0x800000 offset: the two
# images land on the same addresses because both compute them from the same
# line. The fault record (src/hal/cortex-m/hal_fault_frame.h) is the 12 bytes
# below the marker, and the stack top below both is 8-aligned, as the AAPCS
# requires at a call. A SoC
# whose image needs more than image.ld names a script of its own in its
# profile.mk: imx95.ld includes image.ld for the resource table; rp2350.ld
# (the boot ROM's IMAGE_DEF block, .ramfunc copied to SRAM) and nrf54l.ld (the
# SoC's vector tail) are whole scripts. image.ld fails the link of an image
# that carries one of their sections, so a profile cannot forget its script.
CM_LDSCRIPT ?= mk/arch/cortex-m/image.ld
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
# A silicon board is written through its debug probe, with the tool its
# profile.mk names in CM_FLASH_TOOL, one of the four below, and the probe's
# target name in CM_PROBE_TARGET where the tool needs one. The arch file holds
# every recipe; a profile holds no command line. An emulated board (QEMU_MACHINE
# set, no CM_FLASH_TOOL) has no probe: QEMU loads the image itself
# (mk/arch/cortex-m-targets.mk), and `make flash` says so rather than
# pretending.
#
# THE FACTORY RECORD COMES FIRST (owner, CA m346/m347). `make board-record`
# reads the part's whole flash, FLASH_TOTAL bytes from CM_FLASH_BASE, and what
# the tool can read of its option bytes, OTP or UICR, into a timestamped record
# under CM_RECORD_DIR, named for the board and the probe:
#
#   <board>-<CM_PROBE_SERIAL>-<UTC time>.flash.bin         the flash
#   <board>-<CM_PROBE_SERIAL>-<UTC time>.flash.bin.sha256  its SHA-256
#   <board>-<CM_PROBE_SERIAL>-<UTC time>.<CM_EXTRA_NAME>    the rest, as read
#
# A read-out shorter than FLASH_TOTAL is not a record. `make flash` and
# `make boot-flash` refuse to write unless a whole record of this board with
# this probe is there and matches its SHA-256; each write is read back and
# compared. `make board-restore BOARD_RESTORE_IMAGE=<record>.flash.bin` writes
# a record of this board and probe back over the whole flash and compares it.
#
# NEVER an option-byte, protection, lock, OTP or fuse write, and never a mass
# erase or a recover: no recipe here names one, and CM_PROBE_ARGS (extra
# connection options, the operator's) or CM_PROBE_TARGET naming one is refused
# before the tool runs. Option bytes, OTP and UICR are recorded, never restored.
#
# CM_PROBE_SERIAL is the operator's, on the command line: the probe's serial
# (the board's, for picotool). It selects the one probe a command talks to and
# keys the record. A serial is bench data and never goes in a tracked file.
CM_PROBE_SERIAL ?=
CM_PROBE_ARGS   ?=
# Under the build directory, where mk/arch/avr.mk saves its EEPROM copy; the
# Makefile's `clean` keeps it. Copy a record to the bench host's per-board
# record directory too: it is the only way back to the board as it came.
CM_RECORD_DIR   ?= $(BUILD_DIR)/records

# The tools. CM_TOOL_<tool> is the program, CM_HOWTO_<tool> how to get it; a
# missing program fails with both, never a skip.
#
#   stm32cubeprog  ST's STM32CubeProgrammer CLI over the ST-LINK (UM2237): -u
#                  reads, -d with -v writes and verifies, -ob displ prints the
#                  option bytes without writing them
#   picotool       Raspberry Pi's picotool over USB in BOOTSEL mode, 2.0.0 or
#                  later (the first with the RP2350): save -r reads, load -v
#                  writes and verifies, otp dump reads the OTP
#   jlink          SEGGER's J-Link Commander over a J-Link: savebin reads,
#                  loadbin then verifybin writes and verifies
#   pyocd          pyOCD over a CMSIS-DAP probe, with the part's CMSIS pack:
#                  `cmd -c savemem` reads, `load` writes
CM_TOOL_stm32cubeprog  := STM32_Programmer_CLI
CM_HOWTO_stm32cubeprog := install STM32CubeProgrammer from https://www.st.com/en/development-tools/stm32cubeprog.html and put its bin directory on PATH
CM_TOOL_picotool       := picotool
CM_HOWTO_picotool      := build and install picotool 2.0.0 or later from https://github.com/raspberrypi/picotool
CM_TOOL_jlink          := JLinkExe
CM_HOWTO_jlink          = install the J-Link Software and Documentation Pack from https://www.segger.com/downloads/jlink/ (a release whose device list names $(CM_PROBE_TARGET))
CM_TOOL_pyocd          := pyocd
CM_HOWTO_pyocd          = pip install pyocd and then run pyocd pack install $(CM_PROBE_TARGET)

# Each tool's three primitives. cm_read_<tool> addr,bytes,file reads memory to
# a raw file; cm_write_<tool> file,addr writes a raw file to memory, erasing
# only the sectors it covers; cm_extra_<tool> file records what the tool can
# read beside the flash. CM_EXTRA_NAME_<tool> is that file's suffix.
cm_cubeprog = STM32_Programmer_CLI -c port=SWD sn=$(CM_PROBE_SERIAL) $(CM_PROBE_ARGS)
cm_read_stm32cubeprog  = $(cm_cubeprog) -u $(1) $(2) $(3)
cm_write_stm32cubeprog = $(cm_cubeprog) -d $(1) $(2) -v
cm_extra_stm32cubeprog = $(cm_cubeprog) -ob displ > $(1)
CM_EXTRA_NAME_stm32cubeprog := optionbytes.txt

cm_read_picotool  = picotool save --ser $(CM_PROBE_SERIAL) $(CM_PROBE_ARGS) -r $(1) $$(printf '0x%x' $$(( $(1) + $(2) ))) $(3) -t bin
cm_write_picotool = picotool load --ser $(CM_PROBE_SERIAL) $(CM_PROBE_ARGS) -v $(1) -t bin -o $(2)
cm_extra_picotool = picotool otp dump --ser $(CM_PROBE_SERIAL) $(CM_PROBE_ARGS) > $(1)
CM_EXTRA_NAME_picotool := otp.txt

# J-Link Commander takes its commands from a file, written beside the data;
# its numbers are hexadecimal.
cm_jlink = JLinkExe -USB $(CM_PROBE_SERIAL) -device $(CM_PROBE_TARGET) -if SWD -speed 4000 \
           -autoconnect 1 -ExitOnError 1 -NoGui 1 $(CM_PROBE_ARGS) -CommandFile
cm_read_jlink  = printf 'savebin %s,%s,0x%x\nexit\n' $(3) $(1) $(2) > $(3).jlink && $(cm_jlink) $(3).jlink
cm_write_jlink = printf 'loadbin %s,%s\nverifybin %s,%s\nexit\n' $(1) $(2) $(1) $(2) > $(1).jlink && $(cm_jlink) $(1).jlink
cm_extra_jlink = $(call cm_read_jlink,$(CM_EXTRA_BASE),$(CM_EXTRA_BYTES),$(1))
CM_EXTRA_NAME_jlink := uicr.bin

cm_pyocd = -t $(CM_PROBE_TARGET) -u $(CM_PROBE_SERIAL) $(CM_PROBE_ARGS)
cm_read_pyocd  = pyocd cmd $(cm_pyocd) -c "savemem $(1) $(2) $(3)"
cm_write_pyocd = pyocd load $(cm_pyocd) --format bin --base-address $(2) $(1)
cm_extra_pyocd = echo '$(CM_NOEXTRA_WHY)' > $(1)
CM_EXTRA_NAME_pyocd := extra.txt

CM_EXTRA_NAME = $(CM_EXTRA_NAME_$(CM_FLASH_TOOL))

# Before any probe command: the probe named, nothing refused in its options,
# the tool on PATH.
CM_PROBE_CHECK = \
  case "$(CM_PROBE_SERIAL)" in \
    '') echo "$@: REFUSED. Name the probe: make $@ BOARD=$(BOARD) CM_PROBE_SERIAL=<serial>" >&2; exit 1;; \
    *[!A-Za-z0-9_-]*) echo "$@: REFUSED. CM_PROBE_SERIAL '$(CM_PROBE_SERIAL)' is not letters, digits, - and _" >&2; exit 1;; esac; \
  for w in $(CM_PROBE_ARGS) $(CM_PROBE_TARGET); do case "$$w" in \
    -e|-e=*|*erase*|*-ob*|*ption*|*lock*|*rdp*|*RDP*|*otp*|*OTP*|*seal*|*recover*|*protect*|*fuse*) \
      echo "$@: REFUSED. '$(CM_PROBE_ARGS) $(CM_PROBE_TARGET)' names an erase, option-byte, protection, lock, OTP or fuse operation" >&2; exit 1;; esac; done; \
  command -v $(CM_TOOL_$(CM_FLASH_TOOL)) >/dev/null || { \
    echo "$@: $(CM_TOOL_$(CM_FLASH_TOOL)) is not on PATH: $(BOARD) is written with it. To get it: $(CM_HOWTO_$(CM_FLASH_TOOL))" >&2; exit 1; }

# A record is whole and matches its SHA-256, or it is not one.
CM_RECORD_PREFIX = $(CM_RECORD_DIR)/$(BOARD)-$(CM_PROBE_SERIAL)-
cm_record_ok = [ -f "$(1)" ] && [ "$$(stat -c %s "$(1)")" -eq $(FLASH_TOTAL) ] && sha256sum -c --status "$(1).sha256"

CM_RECORD_GUARD = \
  rec=; for f in $(CM_RECORD_PREFIX)*.flash.bin; do \
    if $(call cm_record_ok,$$f); then rec=$$f; fi; done; \
  if [ -z "$$rec" ]; then \
    echo "$@: REFUSED. No whole factory record of $(BOARD) with probe $(CM_PROBE_SERIAL) in $(CM_RECORD_DIR)/. Take one first:" >&2; \
    echo "  make board-record BOARD=$(BOARD) CM_PROBE_SERIAL=$(CM_PROBE_SERIAL)" >&2; exit 1; fi; \
  echo "$@: factory record $$rec"

# Write a raw file at an address, read the same length back and compare.
cm_write_verify = \
  $(call cm_write_$(CM_FLASH_TOOL),$(1),$(2)) || exit 1; \
  n=$$(stat -c %s $(1)); \
  $(call cm_read_$(CM_FLASH_TOOL),$(2),$$n,$(1).readback) || exit 1; \
  cmp $(1) $(1).readback || { echo "$@: READ-BACK MISMATCH at $(2): written $(1), read $(1).readback" >&2; exit 1; }; \
  rm -f $(1).readback

# `make flash` and `make boot-flash`: the image's .hex as raw bytes from its
# link address, which is the slot's (APP_BASE) or the boot region's (BOOT_BASE).
cm_write_image = \
  $(CM_PROBE_CHECK); \
  $(CM_RECORD_GUARD); \
  $(ARCH_OBJCOPY) -I ihex -O binary $< $(basename $<).bin || exit 1; \
  $(call cm_write_verify,$(basename $<).bin,$(1)); \
  echo "$@: $(BOARD) has $(basename $<).bin at $(1), read back equal"

CM_BOARD_RECORD = \
  $(CM_PROBE_CHECK); \
  mkdir -p $(CM_RECORD_DIR) || exit 1; \
  rec=$(CM_RECORD_PREFIX)$$(date -u +%Y%m%dT%H%M%SZ); \
  $(call cm_extra_$(CM_FLASH_TOOL),$$rec.$(CM_EXTRA_NAME)) || exit 1; \
  $(call cm_read_$(CM_FLASH_TOOL),$(CM_FLASH_BASE),$(FLASH_TOTAL),$$rec.flash.bin) || exit 1; \
  n=$$(stat -c %s $$rec.flash.bin); \
  if [ "$$n" -ne $(FLASH_TOTAL) ]; then \
    mv $$rec.flash.bin $$rec.flash.bin.short; \
    echo "$@: the read-out is $$n bytes, not the part's $(FLASH_TOTAL): kept as $$rec.flash.bin.short, NOT a record" >&2; exit 1; fi; \
  sha256sum $$rec.flash.bin > $$rec.flash.bin.sha256 || exit 1; \
  echo "$@: $(BOARD) recorded: $$rec.flash.bin ($(FLASH_TOTAL) bytes from $(CM_FLASH_BASE)) and $$rec.$(CM_EXTRA_NAME)"

# The record's file name must be this board's and this probe's, so one board's
# record is never written to another.
CM_BOARD_RESTORE = \
  $(CM_PROBE_CHECK); \
  case "$$(basename $<)" in $(BOARD)-$(CM_PROBE_SERIAL)-*.flash.bin) ;; \
    *) echo "$@: REFUSED. $< is not a record of $(BOARD) with probe $(CM_PROBE_SERIAL) ($(BOARD)-$(CM_PROBE_SERIAL)-<time>.flash.bin)" >&2; exit 1;; esac; \
  if ! { $(call cm_record_ok,$<); }; then \
    echo "$@: REFUSED. $< is not $(FLASH_TOTAL) bytes matching $<.sha256" >&2; exit 1; fi; \
  $(call cm_write_verify,$<,$(CM_FLASH_BASE)); \
  echo "$@: $(BOARD) has its record $< back over the whole flash, read back equal"

CM_NOFLASH_WHY   ?= $(if $(QEMU_MACHINE),is an emulated board: QEMU loads the image (make integration-test-cortex-m),has no write path yet: no debug-probe tool is wired)
CM_NORESTORE_WHY ?= $(if $(QEMU_MACHINE),is an emulated board: it has no factory state,has no factory record or restore yet)
ARCH_CM_NOFLASH   = echo "$@: $(BOARD) $(CM_NOFLASH_WHY)" >&2; exit 1
ARCH_CM_NORESTORE = echo "$@: $(BOARD) $(CM_NORESTORE_WHY)" >&2; exit 1
ARCH_FLASH         = $(if $(CM_FLASH_TOOL),$(call cm_write_image,$(APP_BASE)),$(ARCH_CM_NOFLASH))
ARCH_BOOT_FLASH    = $(if $(CM_FLASH_TOOL),$(call cm_write_image,$(BOOT_BASE)),$(ARCH_CM_NOFLASH))
ARCH_BOARD_RECORD  = $(if $(CM_FLASH_TOOL),$(CM_BOARD_RECORD),$(ARCH_CM_NORESTORE))
ARCH_BOARD_RESTORE = $(if $(CM_FLASH_TOOL),$(CM_BOARD_RESTORE),$(ARCH_CM_NORESTORE))
ARCH_LOCK_CHECK    = echo "$@: $(BOARD) $(if $(QEMU_MACHINE),is an emulated board: it has no lock bits,has no lock-bit check yet)" >&2; exit 1
