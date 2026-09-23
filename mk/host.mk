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

# The host build enables attestation so the evidence path is exercised. A real
# device secret is NOT baked in: the provider refuses to sign while the UDS is
# still its default, so an unprovisioned build cannot pretend to have an
# identity, and the tests assert exactly that refusal.
HOST_EXTRA_DEFS ?= -DAG_FEATURE_ATTEST=1

# -std=c11 is required, not stylistic: the buffer-sizing invariants are
# enforced with _Static_assert, which C99 does not have.
HOST_CFLAGS := -std=c11 -g -O1 \
               -Wall -Wextra -Werror -Wconversion -Wshadow \
               -Wpointer-arith -Wcast-qual -Wstrict-prototypes \
               -Iinclude -Isrc -Isrc/proto -Isrc/core -Isrc/caps -Isrc/crypto -Isrc/crypto/soft -Itest/mock -Iboards/$(HOST_BOARD) -Itestdata -I$(UNITY_DIR) \
               -DAG_BOARD_HEADER='"board_profile.h"' $(HOST_EXTRA_DEFS)


# Unity's own sources are third-party; its warnings are not ours to fix, and
# -Werror over a vendored tree turns an upstream bump into a build break.
UNITY_CFLAGS := -std=c11 -g -O1 -I$(UNITY_DIR)

SAN_FLAGS   := -fsanitize=address,undefined -fno-omit-frame-pointer
COV_FLAGS   := --coverage -fprofile-arcs -ftest-coverage

# One binary per test file: Unity's convention, and it means a segfault in one
# suite cannot take the others' results with it.
# The board descriptor suites are excluded here and run by `make board-sweep`
# instead. Each asserts against ITS OWN board's profile, and this build is
# configured for exactly one board -- compiling a Mega's test against the 328P's
# header fails on a register the 328P does not have, which is correct behaviour
# reported as a build break.
# test_atecc is excluded too, and for a different reason: it links the SOFTWARE
# ELEMENT rather than the mock HAL, because both define ag_hal_i2c_xfer and the
# whole point is that the driver talks to something that behaves like the part.
# It gets its own rule below.
TEST_SRCS   := $(filter-out test/unit/test_board_%.c test/unit/test_atecc.c, \
                            $(wildcard test/unit/test_*.c))
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
                 src/caps/pwm.c \
                 src/caps/tone.c \
                 src/caps/pulse.c \
                 src/caps/bus.c \
                 src/caps/storage.c \
                 src/crypto/sha256.c \
                 src/crypto/soft/provider.c \
                 src/caps/attest.c \
                 test/mock/hal_mock.c \
                 test/mock/uds_mock.c \
                 src/core/board.c \
                 boards/$(HOST_BOARD)/board.c

$(HOST_DIR):
	@mkdir -p $(HOST_DIR)

$(HOST_DIR)/unity.o: $(UNITY_DIR)/unity.c | $(HOST_DIR)
	@$(CC) $(UNITY_CFLAGS) -c -o $@ $<

$(HOST_DIR)/%: test/unit/%.c $(HOST_LIB_SRCS) $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $< $(HOST_LIB_SRCS) $(HOST_DIR)/unity.o $(EXTRA_LDFLAGS)

# The ATECC608A driver against the software element. A separate link because
# the model IS the I2C HAL here -- linking it beside the mock would be two
# definitions of ag_hal_i2c_xfer, and choosing between them with a flag would
# mean the tested build is not the shipped one.
$(HOST_DIR)/test_atecc: test/unit/test_atecc.c test/model/atecc608a.c \
                        src/crypto/atecc608a/atecc.c \
                        src/crypto/atecc608a/provider.c \
                        src/crypto/sha256.c $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) -Isrc/crypto/atecc608a -Itest/model \
	   -o $@ $^ $(EXTRA_LDFLAGS)

## test: build and run the host unit tests
test: $(TEST_BINS) $(HOST_DIR)/test_atecc
	@fail=0; for t in $(TEST_BINS) $(HOST_DIR)/test_atecc; do \
	  echo "--- $$t"; ./$$t || fail=1; done; exit $$fail

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

# ---------------------------------------------------------------- board sweep
#
# Every board's descriptor, each compiled against ITS OWN profile.
#
# The ordinary `make test` configures the host build for one board, so it proves
# nothing about the others -- and a board whose table is never compiled is a
# rung that checks nothing, which is the failure that looks most like success.
# This builds one descriptor test per board, each with HOST_BOARD set to that
# board, and runs them all.
#
# Only the board suite is swept. The capability suites assert against the 328P's
# pin numbers on purpose; running them against a Mega would test the test.
BOARDS := $(notdir $(wildcard boards/*))

SWEEP_DIR := $(BUILD_DIR)/sweep

## board-sweep: compile and run every board's descriptor test against its own profile
board-sweep:
	@fail=0; ran=0; \
	for b in $(BOARDS); do \
	  src=test/unit/test_board_$${b#atmega}.c; \
	  [ -f "$$src" ] || src=test/unit/test_board_$$b.c; \
	  if [ ! -f "$$src" ]; then \
	    echo "MISSING: $$b has no descriptor test"; fail=1; continue; fi; \
	  mkdir -p $(SWEEP_DIR); \
	  $(CC) -std=c11 -g -O1 -Wall -Wextra -Werror -Wconversion -Wshadow \
	     -Iinclude -Isrc -Isrc/core -Itest/mock -Iboards/$$b -I$(UNITY_DIR) \
	     -DAG_BOARD_HEADER='"board_profile.h"' \
	     -o $(SWEEP_DIR)/$$b "$$src" src/core/board.c boards/$$b/board.c \
	     $(UNITY_DIR)/unity.c || { echo "BUILD FAILED: $$b"; fail=1; continue; }; \
	  echo "--- $$b"; ./$(SWEEP_DIR)/$$b || fail=1; ran=$$((ran+1)); \
	done; \
	echo "board-sweep: $$ran board(s)"; \
	[ "$$ran" -gt 0 ] || { echo "board-sweep swept nothing"; exit 1; }; \
	exit $$fail

.PHONY: board-sweep
