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

## board-restore: put the stock bootloader and the factory fuses back over ISP
BOARD_RESTORE_HEX ?=

ifneq ($(filter board-restore,$(MAKECMDGOALS)),)
ifeq ($(and $(FACTORY_LFUSE),$(FACTORY_HFUSE),$(FACTORY_EFUSE),$(FACTORY_LOCK),$(FACTORY_BOOT_HEX_SHA256)),)
$(error board-restore: boards/$(BOARD)/profile.mk records no factory state (FACTORY_*); a board's is recorded before its first write, see docs/development/bench/)
endif
ifeq ($(and $(BOOT_EESAVE_FUSE),$(BOOT_EESAVE_BIT)),)
$(error board-restore: boards/$(BOARD)/profile.mk names no BOOT_EESAVE_FUSE/BOOT_EESAVE_BIT, so board-restore cannot check that the chip erase keeps the EEPROM)
endif
ifeq ($(BOARD_RESTORE_HEX),)
$(error board-restore: BOARD_RESTORE_HEX is not set. It is the stock bootloader .hex with SHA-256 $(FACTORY_BOOT_HEX_SHA256); the board's page in docs/development/bench/ says where it comes from)
endif
endif

# What the EESAVE refusal tells the operator to run again, BOOT_EEPROM_RESTORE=1
# added.
board-restore: ISP_RERUN = BOARD_RESTORE_HEX=$(BOARD_RESTORE_HEX)
board-restore: $(BOARD_RESTORE_HEX)
	@$(ARCH_BOARD_RESTORE)

.PHONY: board-restore
