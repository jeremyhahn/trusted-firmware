# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# Tier 2: the REAL firmware image on an emulated ATmega328P.
#
# The unit tests run the logic on x86 against a mock HAL and prove the logic is
# right. These run the .elf that gets flashed -- real register writes, real
# interrupt vectors, real timing -- and prove the thing that ships works. They
# catch what a host build cannot: a wrong baud divisor, an ISR that never fires,
# a peripheral configured in the wrong order.

INT_DIR := $(BUILD_DIR)/integration

## integration-test-simavr: run the firmware image under simavr
# One part of the `integration-test` stage (mk/stages.mk).
integration-test-simavr: $(INT_DIR)/harness $(TARGET).elf
	@./$(INT_DIR)/harness $(TARGET).elf

$(INT_DIR)/harness: test/integration/simavr/harness.c src/proto/crc16.c src/proto/crc16_update.c
	@mkdir -p $(INT_DIR)
	@$(CC) -std=c11 -g -O1 -Wall -Wextra \
	   -Iinclude -Isrc/proto -I$(BOARD_DIR) -DTFW_BOARD_HEADER='"board_profile.h"' \
	   -o $@ $^ -lsimavr -lelf
	@echo "built $@"

.PHONY: integration-test-simavr
