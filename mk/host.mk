# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# The host build. The protocol, dispatch and capability layers compile for
# x86-64 against a mock HAL, so they are testable with no AVR toolchain, no
# emulator and no hardware. This is where the coverage floor is met; the
# register-poking layers under src/hal/avr are deliberately outside it,
# because a number that counted code x86 cannot execute would not mean
# anything.

BUILD_DIR   := build
HOST_DIR    := $(BUILD_DIR)/host
UNITY_DIR   := test/third_party/unity

# The host tests assert against a REAL board descriptor, so the host build is
# configured for that board rather than left boardless. Without this, config.h
# supplies its no-board defaults and a test that includes the board header
# directly redefines them -- and every sizing constant the tests exercise would
# be the default rather than the one the device ships with, which is the more
# insidious half.
#
# Defined BEFORE HOST_CFLAGS: that uses := and expands this immediately, so a
# definition below it would expand to nothing and drop the board's include
# directory -- which is exactly what happened, and surfaced three steps later as
# a macro redefinition rather than as a missing path.
HOST_BOARD ?= atmega328p

# -std=c11 is required, not stylistic: the buffer-sizing invariants are
# enforced with _Static_assert, which C99 does not have.
HOST_CFLAGS := -std=c11 -g -O1 \
               -Wall -Wextra -Werror -Wconversion -Wshadow \
               -Wpointer-arith -Wcast-qual -Wstrict-prototypes \
               -Iinclude -Isrc -Isrc/proto -Isrc/core -Isrc/caps -Itest/mock -Iboards/$(HOST_BOARD) -Itestdata -I$(UNITY_DIR) \
               -DAG_BOARD_HEADER='"board_profile.h"'


# Unity's own sources are third-party; its warnings are not ours to fix, and
# -Werror over a vendored tree turns an upstream bump into a build break.
UNITY_CFLAGS := -std=c11 -g -O1 -I$(UNITY_DIR)

SAN_FLAGS   := -fsanitize=address,undefined -fno-omit-frame-pointer
COV_FLAGS   := --coverage -fprofile-arcs -ftest-coverage

# One binary per test file: Unity's convention, and it means a segfault in one
# suite cannot take the others' results with it.
TEST_SRCS   := $(wildcard test/unit/test_*.c)
TEST_BINS   := $(patsubst test/unit/%.c,$(HOST_DIR)/%,$(TEST_SRCS))

# Production sources reachable from the host build. Grows as layers land.
HOST_LIB_SRCS := src/proto/crc16.c \
                 src/core/reader.c \
                 src/core/writer.c \
                 src/proto/frame.c \
                 src/proto/transport.c \
                 src/core/dispatch.c \
                 src/caps/sys.c \
                 src/caps/gpio.c \
                 src/caps/sched.c \
                 src/caps/analog.c \
                 src/caps/storage.c \
                 test/mock/hal_mock.c \
                 boards/$(HOST_BOARD)/board.c

$(HOST_DIR):
	@mkdir -p $(HOST_DIR)

$(HOST_DIR)/unity.o: $(UNITY_DIR)/unity.c | $(HOST_DIR)
	@$(CC) $(UNITY_CFLAGS) -c -o $@ $<

$(HOST_DIR)/%: test/unit/%.c $(HOST_LIB_SRCS) $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $< $(HOST_LIB_SRCS) $(HOST_DIR)/unity.o $(EXTRA_LDFLAGS)

## test: build and run the host unit tests
test: $(TEST_BINS)
	@fail=0; for t in $(TEST_BINS); do echo "--- $$t"; ./$$t || fail=1; done; exit $$fail

## test-asan: run the host unit tests under AddressSanitizer and UBSan
# The codec is pointer-heavy parser code reached from untrusted bytes. This is
# where a memory bug will be, so it gets its own gate rather than riding along.
test-asan:
	@$(MAKE) --no-print-directory clean-host
	@$(MAKE) --no-print-directory test EXTRA_CFLAGS="$(SAN_FLAGS)" EXTRA_LDFLAGS="$(SAN_FLAGS)"

## coverage: run the host tests with coverage and enforce the floor
coverage:
	@$(MAKE) --no-print-directory clean-host
	@$(MAKE) --no-print-directory test EXTRA_CFLAGS="$(COV_FLAGS)" EXTRA_LDFLAGS="$(COV_FLAGS)"
	@mkdir -p coverage
	@gcovr --root . --filter 'src/(proto|core|caps)/' --filter 'boards/' \
	       --exclude 'src/hal/avr/' --print-summary \
	       --html-details coverage/index.html --fail-under-line $(COVERAGE_FLOOR)

COVERAGE_FLOOR ?= 90

## lint: static analysis over the firmware sources
# cppcheck stands in for `go vet`. It is given the board header and the PROGMEM
# definition explicitly rather than having its complaints suppressed: told the
# configuration it can analyse the code, whereas a suppression would also hide
# the next genuinely unknown macro.
lint:
	@cppcheck --enable=warning,style,performance,portability \
	          --check-level=exhaustive \
	          --error-exitcode=1 --inline-suppr --quiet \
	          --suppress=missingIncludeSystem \
	          --suppress=unusedFunction \
	          -DAG_BOARD_HEADER='"board_profile.h"' -DAG_PROGMEM= \
	          -Iinclude -Isrc -Iboards/atmega328p \
	          src/ boards/
	@echo "cppcheck clean"

## agsim: build the firmware as a host binary speaking the protocol on stdio
agsim: $(HOST_DIR)/agsim
$(HOST_DIR)/agsim: tools/agsim/main.c $(HOST_LIB_SRCS) | $(HOST_DIR)
	@$(CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $< $(HOST_LIB_SRCS) $(EXTRA_LDFLAGS)
	@echo "built $@"

clean-host:
	@rm -rf $(HOST_DIR)

.PHONY: test test-asan coverage clean-host agsim lint
