# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# Tier 3: what the firmware COSTS, in cycles, on an exact model of the part.

# The image's row, not only its tier: the benchmark compiles FW_SRCS with FW_CFLAGS, so every flag that names the firmware's directory changes these objects too.
BENCH_DIR    := $(BUILD_DIR)/bench/$(BOARD)/$(FW_ROW)
BENCH_TARGET := $(BENCH_DIR)/agbench

# The firmware's own object list with one file exchanged.
BENCH_SRCS := $(filter-out src/main.c,$(FW_SRCS)) test/bench/bench_main.c
BENCH_OBJS := $(patsubst %.c,$(BENCH_DIR)/%.o,$(BENCH_SRCS))

# Identical flags to mk/firmware.mk's, by reference and not by copy.
$(eval $(call SOFT_ID_RULES,$(BENCH_DIR)))
ifeq ($(CRYPTO),auto)
$(eval $(call AUTO_RENAMES,$(BENCH_DIR)))
endif
$(BENCH_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@$(ARCH_CC) $(FW_CFLAGS) -frandom-seed=$(notdir $<) -c -o $@ $<

$(BENCH_TARGET).elf: $(BENCH_OBJS)
	@$(ARCH_CC) $(FW_LDFLAGS) -o $@ $(BENCH_OBJS)

# The harness.
BENCH_HARNESS_SRCS := test/integration/simavr/bench_harness.c \
                      src/proto/crc16.c src/proto/crc16_update.c

$(BENCH_DIR)/bench_harness: $(BENCH_HARNESS_SRCS) test/bench/targets.h
	@mkdir -p $(BENCH_DIR)
	@$(CC) -std=c11 -g -O1 -Wall -Wextra -Werror -Wconversion -Wshadow \
	   -Iinclude -Isrc/proto -Itest/bench -I$(BOARD_DIR) \
	   -DTFW_BOARD_HEADER='"board_profile.h"' \
	   -DTFW_SIMAVR_MCU='"$(MCU)"' -DTFW_BENCH_F_CPU=$(F_CPU)uL \
	   -DTFW_BENCH_BOARD='"$(BOARD)"' \
	   -o $@ $(BENCH_HARNESS_SRCS) -lsimavr -lelf
	@echo "built $@"

## bench-run: measure one board and enforce its budget
bench-run: $(BENCH_DIR)/bench_harness $(BENCH_TARGET).elf $(TARGET).elf
	@./$(BENCH_DIR)/bench_harness $(TARGET).elf $(BENCH_TARGET).elf

# Which boards the budget is enforced on.
BENCH_BOARDS ?= atmega328p atmega2560

## bench-check: the performance gate -- fails the build on a regression
bench-check:
	@fail=0; \
	for b in $(BENCH_BOARDS); do \
	  $(MAKE) --no-print-directory bench-run BOARD=$$b ETH=0 || fail=1; \
	done; \
	if [ $$fail -ne 0 ]; then echo "bench-check: FAILED"; else echo "bench-check: green"; fi; \
	exit $$fail

## bench-dir: print the directory `bench-run` builds the benchmark in, for the flags given (used by scripts/test_build_dirs.py)
bench-dir:
	@echo $(BENCH_DIR)

.PHONY: bench-run bench-check bench-dir
