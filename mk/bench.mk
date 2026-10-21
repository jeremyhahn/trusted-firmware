# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# Tier 3: what the firmware COSTS, in cycles, on an exact model of the part.
#
# mk/integration.mk asks whether the shipped image works. This asks what it
# spends, and it is a GATE rather than a report for the same reason size-check
# is: performance creep is caught on the commit that causes it, or it is
# discovered the day a software UART starts dropping bits and nobody can say
# which of two hundred commits did it.
#
# Every number comes from simavr's cycle counter running a real .elf. There is
# no host-side timing here and there must not be: an x86 measurement of
# tfw_framer_push says nothing about an eight-bit part with a two-cycle SRAM
# access, and a budget measured against the wrong machine is worse than no
# budget because it looks like one.
#
# The budget itself lives in test/bench/targets.h -- one table, each row with
# the project plan's reasoning beside it.

BENCH_DIR    := $(BUILD_DIR)/bench/$(BOARD)/$(CRYPTO)
BENCH_TARGET := $(BENCH_DIR)/agbench

# The firmware's own object list with ONE file exchanged. Not a subset, not a
# hand-picked list of the modules under test: swapping main.c is the smallest
# edit that produces an image the harness can drive, and everything else -- the
# dispatcher, the capabilities, the crypto provider, the HAL -- is compiled and
# linked exactly as it is for the part, so LTO sees the same whole program and
# makes the same inlining decisions about the code being measured.
BENCH_SRCS := $(filter-out src/main.c,$(FW_SRCS)) test/bench/bench_main.c
BENCH_OBJS := $(patsubst %.c,$(BENCH_DIR)/%.o,$(BENCH_SRCS))

# Identical flags to mk/firmware.mk's, by reference and not by copy. A second
# copy of FW_CFLAGS here would drift from the first, and the whole claim of this
# gate is that it measures the compilation the firmware gets.
$(eval $(call SOFT_ID_RULES,$(BENCH_DIR)))
ifeq ($(CRYPTO),auto)
$(eval $(call AUTO_RENAMES,$(BENCH_DIR)))
endif
$(BENCH_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@$(ARCH_CC) $(FW_CFLAGS) -frandom-seed=$(notdir $<) -c -o $@ $<

$(BENCH_TARGET).elf: $(BENCH_OBJS)
	@$(ARCH_CC) $(FW_LDFLAGS) -o $@ $(BENCH_OBJS)

# The harness. Held to the firmware's warning set, not the looser one
# mk/integration.mk uses: it does pointer arithmetic over an emulated address
# space and computes the numbers a budget is enforced against, so a narrowing
# conversion in it is a wrong measurement rather than a style complaint.
#
# The board is in the path and in the flags. The harness compiles the board
# profile in -- it needs TFW_SCHED_SLOTS to know how many slots to arm and
# TFW_UART_BAUD to model the wire -- so a 2560 harness and a 328P harness are
# different programs and must not share a binary.
# targets.h IS A PREREQUISITE and not merely an include. The budget lives in it,
# and without the dependency a change to a ceiling leaves the previous harness
# binary in place -- so the gate reports the OLD bound against the new code and
# passes. That is the one failure mode a budget must not have.
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

# WHICH BOARDS THE BUDGET IS ENFORCED ON.
#
# The 328P because it is the reference part and the one the wire protocol was
# sized for. The 2560 because the plan states the scheduler budget "at 16
# slots", and sixteen is the 2560's TFW_SCHED_SLOTS -- the 328P has eight, so a
# 328P-only gate would never once have measured the number the plan asks about.
#
# The 32U4 is not here and the ATtiny85 is not here, and that is a scope, not a
# skip: neither introduces a code path the two above do not already cover, the
# 32U4 being a 328P-class core and the ATtiny85 having no USART to time an
# exchange over. Adding a board to this list is the whole change required.
BENCH_BOARDS ?= atmega328p atmega2560

## bench-check: the performance gate -- fails the build on a regression
# ETH=0: the budget times a serial exchange, and the 2560's default is now the
# Ethernet image (#55 item 48), so the serial image is named.
bench-check:
	@fail=0; \
	for b in $(BENCH_BOARDS); do \
	  $(MAKE) --no-print-directory bench-run BOARD=$$b ETH=0 || fail=1; \
	done; \
	if [ $$fail -ne 0 ]; then echo "bench-check: FAILED"; else echo "bench-check: green"; fi; \
	exit $$fail

.PHONY: bench-run bench-check
