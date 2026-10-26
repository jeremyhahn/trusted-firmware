# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# Putting a bench board back as it left the factory (owner, CA m346): run when
# the board is finished with. The factory state is the board's own record --
# docs/development/bench/<board>.md -- and its profile.mk carries the values:
#
#   FACTORY_LFUSE, FACTORY_HFUSE,      the fuse and lock bytes read before the
#   FACTORY_EFUSE, FACTORY_LOCK        project's first write to the board
#   FACTORY_BOOT_HEX_SHA256            the stock bootloader .hex, as published
#
# The hex itself is not in this tree: the stock bootloaders are GPL, which this
# project's commercial licence cannot carry. BOARD_RESTORE_HEX names the file,
# fetched from the source the board's page gives, and the recipe refuses one
# whose SHA-256 is not the profile's. See ARCH_BOARD_RESTORE in mk/arch/$(ARCH).mk.

## board-restore: put the board's factory state back (AVR: ISP; ESP32: the flash image; Cortex-M: a board-record record through the debug probe)
BOARD_RESTORE_HEX ?=
# An ESP32 board is restored from the whole-flash image read before the first
# write: FACTORY_FLASH_BYTES and FACTORY_FLASH_SHA256 in its profile.mk, the
# image named by BOARD_RESTORE_IMAGE, the port by ESP_PORT. eFuses are never
# written. See ARCH_BOARD_RESTORE in mk/arch/esp32.mk.
BOARD_RESTORE_IMAGE ?=

ifneq ($(filter board-restore,$(MAKECMDGOALS)),)
ifeq ($(ARCH),cortex-m)
ifneq ($(CM_FLASH_TOOL),)
ifeq ($(BOARD_RESTORE_IMAGE),)
$(error board-restore: set BOARD_RESTORE_IMAGE, the record `make board-record` took of this board ($(CM_RECORD_DIR)/$(BOARD)-<CM_PROBE_SERIAL>-<time>.flash.bin), and CM_PROBE_SERIAL, its probe)
endif
endif
else ifeq ($(ARCH),esp32)
ifeq ($(and $(FACTORY_FLASH_BYTES),$(FACTORY_FLASH_SHA256)),)
$(error board-restore: boards/$(BOARD)/profile.mk records no factory flash image (FACTORY_FLASH_*); a board's is recorded before its first write, see docs/development/bench/)
endif
ifeq ($(and $(BOARD_RESTORE_IMAGE),$(ESP_PORT)),)
$(error board-restore: set BOARD_RESTORE_IMAGE, the factory flash image with SHA-256 $(FACTORY_FLASH_SHA256), and ESP_PORT, the board's serial port)
endif
else
ifeq ($(and $(FACTORY_LFUSE),$(FACTORY_HFUSE),$(FACTORY_EFUSE),$(FACTORY_LOCK),$(FACTORY_BOOT_HEX_SHA256)),)
$(error board-restore: boards/$(BOARD)/profile.mk records no factory state (FACTORY_*); a board's is recorded before its first write, see docs/development/bench/)
endif
ifeq ($(and $(BOOT_EESAVE_FUSE),$(BOOT_EESAVE_BIT)),)
$(error board-restore: boards/$(BOARD)/profile.mk names no BOOT_EESAVE_FUSE/BOOT_EESAVE_BIT, so board-restore cannot check that the chip erase keeps the EEPROM)
endif
ifeq ($(BOOT_FIRST_FLASH),1)
$(error board-restore: BOOT_FIRST_FLASH=1 belongs to boot-flash; board-restore puts the factory fuses back and programs no EESAVE)
endif
ifeq ($(BOARD_RESTORE_HEX),)
$(error board-restore: BOARD_RESTORE_HEX is not set. It is the stock bootloader .hex with SHA-256 $(FACTORY_BOOT_HEX_SHA256); the board's page in docs/development/bench/ says where it comes from)
endif
endif
endif

# What the EESAVE refusal tells the operator to run again, BOOT_EEPROM_RESTORE=1
# added.
board-restore: ISP_RERUN = BOARD_RESTORE_HEX=$(BOARD_RESTORE_HEX)
board-restore: $(BOARD_RESTORE_HEX) $(BOARD_RESTORE_IMAGE)
	@$(ARCH_BOARD_RESTORE)

## board-record: read the board's factory state to a timestamped record before its first write (Cortex-M: the debug probe)
# AVR and ESP32 boards are recorded by hand, on their pages under
# docs/development/bench/; an architecture with a recipe names it in
# ARCH_BOARD_RECORD (mk/arch/cortex-m.mk).
board-record:
	@$(if $(ARCH_BOARD_RECORD),$(ARCH_BOARD_RECORD),echo "$@: $(ARCH) has no board-record recipe: record $(BOARD) by hand, see docs/development/bench/" >&2; exit 1)

.PHONY: board-restore board-record
