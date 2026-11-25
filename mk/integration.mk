# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# Tier 2: the real firmware image on an emulated ATmega328P.

# The harness compiles the board profile in, so another board's harness is another program: the board names a directory of its own, beside mk/boot.mk's boot_harness, except the default board's, which keeps its path.
INT_DIR := $(BUILD_DIR)/integration$(if $(filter-out $(BOARD_DEFAULT),$(BOARD)),/$(BOARD))

## integration-test-simavr: run the firmware image under simavr
integration-test-simavr: $(INT_DIR)/harness $(TARGET).elf
	@./$(INT_DIR)/harness $(TARGET).elf

$(INT_DIR)/harness: test/integration/simavr/harness.c src/proto/crc16.c src/proto/crc16_update.c
	@mkdir -p $(INT_DIR)
	@$(CC) -std=c11 -g -O1 -Wall -Wextra \
	   -Iinclude -Isrc/proto -I$(BOARD_DIR) -DTFW_BOARD_HEADER='"board_profile.h"' \
	   -o $@ $^ -lsimavr -lelf
	@echo "built $@"

## integration-dir: print the directory `integration-test-simavr` builds its harness in, for the flags given (used by scripts/test_build_dirs.py)
integration-dir:
	@echo $(INT_DIR)

.PHONY: integration-test-simavr integration-dir
