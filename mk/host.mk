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

# -std=c11 is required, not stylistic: the buffer-sizing invariants are
# enforced with _Static_assert, which C99 does not have.
HOST_CFLAGS := -std=c11 -g -O1 \
               -Wall -Wextra -Werror -Wconversion -Wshadow \
               -Wpointer-arith -Wcast-qual -Wstrict-prototypes \
               -Iinclude -Isrc/proto -Isrc/core -Itestdata -I$(UNITY_DIR)

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
                 src/core/writer.c

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

clean-host:
	@rm -rf $(HOST_DIR)

.PHONY: test test-asan coverage clean-host
