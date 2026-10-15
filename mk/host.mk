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
#
# The alias certificate is enabled here too, though the host build links the
# SOFTWARE provider and that provider has no asymmetric key. That is on purpose:
# the refusal is the behaviour worth pinning, and a build with the command
# compiled out would answer TFW_ERR_UNKNOWN_CMD -- which says the firmware has
# never heard of certificates, not that this tier cannot issue one. The
# element-backed path gets its own binary below, against the software element.
#
# ATTEST_CSR is NOT turned on here. The host build takes the 328P's profile,
# which says 0 for want of flash, so this build's dispatcher has no ATTEST_CSR
# entry and answers it TFW_ERR_UNKNOWN_CMD exactly as the 328P image does --
# test_dispatch pins that. A -D here would also be a macro redefinition of the
# profile's 0. tfw_csr_cmd itself is tested by test_csr, and served by
# tfwsim-atecc608a through its overlay profile.
HOST_EXTRA_DEFS ?= -DTFW_FEATURE_ATTEST=1 -DTFW_FEATURE_ALIAS_CERT=1

# APP_INFO's entry, for every link that carries the real runner -- which is
# where its handler is, as mk/app.mk pairs APP_DEFS with app_runner.c. Not in
# HOST_CFLAGS: test_app_none and a tfwsim with no APP_DIR link the empty
# runner, and must answer APP_INFO as an image with no application does.
HOST_APP_DEFS := -DTFW_FEATURE_APP=1

# -std=c11 is required, not stylistic: the buffer-sizing invariants are
# enforced with _Static_assert, which C99 does not have.
HOST_CFLAGS := -std=c11 -g -O1 \
               -Wall -Wextra -Werror -Wconversion -Wshadow \
               -Wpointer-arith -Wcast-qual -Wstrict-prototypes \
               -Iinclude -Isrc -Isrc/proto -Isrc/core -Isrc/caps -Isrc/crypto -Isrc/crypto/soft -Iboot -Itest/mock -Iboards/$(HOST_BOARD) -Itestdata -I$(UNITY_DIR) \
               -DTFW_BOARD_HEADER='"board_profile.h"' $(HOST_EXTRA_DEFS)


# Unity's own sources are third-party; its warnings are not ours to fix, and
# -Werror over a vendored tree turns an upstream bump into a build break.
UNITY_CFLAGS := -std=c11 -g -O1 -I$(UNITY_DIR)

# Header dependencies. Every host binary is built by ONE cc command over many
# sources, so there is no object file for gcc's -MMD to hang a .d file on; the
# wrapper writes $@.d beside the output instead (see mk/ccdeps.sh), and the
# -include at the foot of this file reads them back. Without it an edit to a
# header rebuilt nothing: a stale tfwsim reported protocol 1.0 after protocol.h
# had moved to 2.
HOST_CC := sh mk/ccdeps.sh $(CC)

SAN_FLAGS   := -fsanitize=address,undefined -fno-omit-frame-pointer
COV_FLAGS   := --coverage -fprofile-arcs -ftest-coverage

# The software ATECC608A (test/model/atecc608a.c) does real P-256 through the
# vendored micro-ecc. Host and test builds only: it is compiled here, once, and
# linked by the suites and simulator that use the model, never by an AVR image.
# Compiled without our warning set, as Unity is and for the same reason -- an
# upstream's warnings are not ours to fix; its integrity is pinned by hash in
# test/third_party/MANIFEST.md. Also, like unity.o, WITHOUT $(EXTRA_CFLAGS): the
# object outlives one invocation, so an object built with the sanitizer or
# coverage flags of `test-asan` / `coverage` would be linked into the next,
# plain build and fail there. The link still carries the flags. Only secp256r1 is selected, on the command line
# rather than by editing a vendored header.
UECC_DIR    := test/third_party/micro-ecc
UECC_CFLAGS := -std=c11 -g -O1 -I$(UECC_DIR) \
               -DuECC_SUPPORTS_secp160r1=0 -DuECC_SUPPORTS_secp192r1=0 \
               -DuECC_SUPPORTS_secp224r1=0 -DuECC_SUPPORTS_secp256k1=0
MODEL_INC     := -Isrc/crypto/atecc608a -Itest/model -I$(UECC_DIR)
ELEMENT_MODEL := test/model/atecc608a.c $(HOST_DIR)/uECC.o

$(HOST_DIR)/uECC.o: $(UECC_DIR)/uECC.c | $(HOST_DIR)
	@$(CC) $(UECC_CFLAGS) -c -o $@ $<

# One binary per test file: Unity's convention, and it means a segfault in one
# suite cannot take the others' results with it.
# The board descriptor suites are excluded here and run by `make board-sweep`
# instead. Each asserts against ITS OWN board's profile, and this build is
# configured for exactly one board -- compiling a Mega's test against the 328P's
# header fails on a register the 328P does not have, which is correct behaviour
# reported as a build break.
# test_atecc is excluded too, and for a different reason: it links the SOFTWARE
# ELEMENT rather than the mock HAL, because both define tfw_hal_i2c_xfer and the
# whole point is that the driver talks to something that behaves like the part.
# It gets its own rule below.
# test_alias joins that list for the same reason test_atecc is on it: the alias
# certificate is signed by an element, so its suite links the software element
# as the I2C HAL and the ATECC provider as the crypto tier. Linking it beside
# the mock would be two definitions of tfw_hal_i2c_xfer.
# test_csr joins it for the same reason: the possession signature is the element's.
# test_bus_element joins that list for the same reason: it links the software
# element as the I2C HAL and its own guard stubs for the SPI/1-Wire HAL
# functions, either of which the mock also defines.
# test_crypto_none is excluded for a different reason: HOST_LIB_SRCS already
# links src/crypto/soft/provider.c, and its suite needs src/crypto/none/
# provider.c instead -- linking both would be two definitions of
# tfw_crypto_init and everything else in include/tfw/crypto.h.
TEST_SRCS   := $(filter-out test/unit/test_board_%.c test/unit/test_atecc.c \
                            test/unit/test_provision.c test/unit/test_alias.c \
                            test/unit/test_csr.c \
                            test/unit/test_model_p256.c \
                            test/unit/test_bus_element.c \
                            test/unit/test_crypto_none.c \
                            test/unit/test_sys_noboot.c \
                            test/unit/test_pin_coord.c \
                            test/unit/test_platform_bake.c \
                            test/unit/test_app_none.c \
                            test/unit/test_app_example.c \
                            test/unit/test_w5x00.c test/unit/test_link_eth.c, \
                            $(wildcard test/unit/test_*.c))
TEST_BINS   := $(patsubst test/unit/%.c,$(HOST_DIR)/%,$(TEST_SRCS))

# Production sources reachable from the host build. Grows as layers land.
HOST_LIB_SRCS := src/proto/crc16.c \
                 src/proto/crc16_update.c \
                 src/core/reader.c \
                 src/core/writer.c \
                 src/proto/frame.c \
                 src/proto/transport.c \
                 src/proto/link_serial.c \
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
                 src/caps/alias.c \
                 src/caps/tmpl.c \
                 src/caps/csr.c \
                 src/caps/uccs.c \
                 src/core/handoff.c src/core/platform_id.c \
                 src/core/secure_zero.c \
                 src/core/auth_session_none.c \
                 src/core/update_request.c boot/boot_request.c \
                 boot/boot_handoff.c boot/boot_platform.c \
                 src/core/app_runner.c \
                 test/mock/hal_mock.c \
                 test/mock/uds_mock.c \
                 test/mock/app_mock.c \
                 src/core/board_lookup.c \
                 boards/$(HOST_BOARD)/board.c

$(HOST_DIR):
	@mkdir -p $(HOST_DIR)

$(HOST_DIR)/unity.o: $(UNITY_DIR)/unity.c | $(HOST_DIR)
	@$(CC) $(UNITY_CFLAGS) -c -o $@ $<

$(HOST_DIR)/%: test/unit/%.c $(HOST_LIB_SRCS) $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(HOST_APP_DEFS) $(EXTRA_CFLAGS) -o $@ $< $(HOST_LIB_SRCS) $(HOST_DIR)/unity.o $(EXTRA_LDFLAGS)

# The ATECC608A driver against the software element. A separate link because
# the model IS the I2C HAL here -- linking it beside the mock would be two
# definitions of tfw_hal_i2c_xfer, and choosing between them with a flag would
# mean the tested build is not the shipped one.
$(HOST_DIR)/test_atecc: test/unit/test_atecc.c $(ELEMENT_MODEL) \
                        src/crypto/atecc608a/atecc.c \
                        src/crypto/atecc608a/provider.c \
                        src/crypto/sha256.c \
                        src/core/handoff.c src/core/secure_zero.c boot/boot_handoff.c src/proto/crc16.c src/proto/crc16_update.c \
                        $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) $(MODEL_INC) \
	   -o $@ $^ $(EXTRA_LDFLAGS)

# Provisioning, same arrangement and the same reason: it drives the element, so
# the software element is its I2C HAL.
$(HOST_DIR)/test_provision: test/unit/test_provision.c $(ELEMENT_MODEL) \
                            src/caps/provision.c src/core/reader.c src/core/writer.c \
                            src/crypto/atecc608a/atecc.c src/crypto/sha256.c \
                            $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) $(MODEL_INC) \
	   -DTFW_FEATURE_PROVISION=1 -o $@ $^ $(EXTRA_LDFLAGS)

# The alias certificate, driven by the element that signs it. Its DER and CBOR
# vectors need no provider at all, but the command path does, and pinning the
# bytes in the same binary that exercises the command is what stops the two
# drifting apart.
$(HOST_DIR)/test_alias: test/unit/test_alias.c $(ELEMENT_MODEL) \
                        src/caps/alias.c src/caps/tmpl.c src/caps/uccs.c \
                        src/core/reader.c src/core/writer.c \
                        src/crypto/atecc608a/atecc.c src/crypto/atecc608a/provider.c \
                        src/crypto/sha256.c src/caps/attest.c \
                        src/core/handoff.c src/core/platform_id.c src/core/secure_zero.c \
                        boot/boot_handoff.c boot/boot_platform.c src/proto/crc16.c src/proto/crc16_update.c \
                        $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) $(MODEL_INC) \
	   -o $@ $^ $(EXTRA_LDFLAGS)

# ATTEST_CSR, the same arrangement as test_alias: its CertificationRequestInfo
# vector needs no provider, the command it pins does.
$(HOST_DIR)/test_csr: test/unit/test_csr.c $(ELEMENT_MODEL) \
                      src/caps/csr.c src/caps/alias.c src/caps/tmpl.c src/caps/uccs.c \
                      src/core/reader.c src/core/writer.c \
                      src/crypto/atecc608a/atecc.c src/crypto/atecc608a/provider.c \
                      src/crypto/sha256.c src/caps/attest.c \
                      src/core/handoff.c src/core/platform_id.c src/core/secure_zero.c \
                      boot/boot_handoff.c boot/boot_platform.c src/proto/crc16.c src/proto/crc16_update.c \
                      $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) $(MODEL_INC) \
	   -o $@ $^ $(EXTRA_LDFLAGS)

# I2C_CONFIG's refusal on an element build (src/caps/bus.c) -- the one
# behaviour in that file that depends on which crypto tier is linked, so it
# gets the same arrangement test_atecc/test_provision/test_alias do: the
# software element as the I2C HAL, the real ATECC608A driver and provider as
# the crypto tier. The suite's own guard stubs resolve SPI's and 1-Wire's HAL
# functions, which bus.c's other handlers reference as one translation unit.
$(HOST_DIR)/test_bus_element: test/unit/test_bus_element.c \
                               $(ELEMENT_MODEL) \
                               src/caps/bus.c \
                               src/core/reader.c src/core/writer.c \
                               src/core/board_lookup.c boards/$(HOST_BOARD)/board.c \
                               src/crypto/atecc608a/atecc.c src/crypto/atecc608a/provider.c \
                               src/crypto/sha256.c \
                               src/core/handoff.c src/core/secure_zero.c boot/boot_handoff.c \
                               src/proto/crc16.c src/proto/crc16_update.c \
                               $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) $(MODEL_INC) \
	   -o $@ $^ $(EXTRA_LDFLAGS)

# The unattested tier. Its own link line for the reason above this rule
# names: HOST_LIB_SRCS carries the software provider, and this suite is
# nothing without the tier that refuses everything instead.
$(HOST_DIR)/test_crypto_none: test/unit/test_crypto_none.c \
                        src/crypto/none/provider.c \
                        src/core/handoff.c src/core/secure_zero.c \
                        boot/boot_handoff.c src/proto/crc16.c src/proto/crc16_update.c \
                        test/mock/hal_mock.c test/mock/uds_mock.c \
                        $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# test_sys_noboot is the same case from the other side: the whole host library
# with the no-bootloader stub where update_request.c would be, which is exactly
# what mk/avr.mk links on a board with no boot section.
$(HOST_DIR)/test_sys_noboot: test/unit/test_sys_noboot.c \
                        $(filter-out src/core/update_request.c,$(HOST_LIB_SRCS)) \
                        src/core/update_request_none.c \
                        $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(HOST_APP_DEFS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# test_pin_coord brings its own board tables -- a port 18 and a bit 31, which no
# real board has -- so it links board_lookup.c and the mock HAL without
# boards/$(HOST_BOARD)/board.c, whose tables would be second definitions.
$(HOST_DIR)/test_pin_coord: test/unit/test_pin_coord.c src/core/board_lookup.c \
                        test/mock/hal_mock.c src/core/handoff.c src/core/secure_zero.c \
                        boot/boot_handoff.c src/proto/crc16.c src/proto/crc16_update.c \
                        $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The bench bake image's decision (mk/platform-bake.mk), through the mock HAL.
# Its own link because tools/platform-bake/ is in no other object list -- not
# the firmware's, and not this file's HOST_LIB_SRCS -- and must stay that way.
$(HOST_DIR)/test_platform_bake: test/unit/test_platform_bake.c tools/platform-bake/bake.c \
                        boot/boot_platform.c \
                        test/mock/hal_mock.c src/core/handoff.c src/core/secure_zero.c \
                        boot/boot_handoff.c src/proto/crc16.c src/proto/crc16_update.c \
                        $(HOST_DIR)/unity.o tools/platform-bake/bake.h | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) -Itools/platform-bake $(EXTRA_CFLAGS) -o $@ \
	   $(filter %.c %.o,$^) $(EXTRA_LDFLAGS)

# The operator-signature auth session, driven against the software element --
# issue #4. Same arrangement as test_alias: the model IS the I2C HAL, so no
# hal_mock.c, and the test file supplies its own tiny stand-ins for the
# handful of other HAL calls storage.c's and sys.c's non-auth handlers still
# reference even though this suite never calls them (RESET's own poll, CAPS,
# PING, SYS_INFO) -- the same reason test_alias.c defines tfw_hal_millis.
$(HOST_DIR)/test_auth_session: test/unit/test_auth_session.c $(ELEMENT_MODEL) \
                                src/core/auth_session.c src/caps/storage.c src/caps/sys.c \
                                src/core/reader.c src/core/writer.c \
                                src/crypto/atecc608a/atecc.c src/crypto/atecc608a/provider.c \
                                src/crypto/sha256.c src/proto/crc16.c src/proto/crc16_update.c \
                                src/core/platform_id.c src/core/handoff.c src/core/secure_zero.c \
                                src/core/update_request.c boot/boot_request.c \
                                boot/boot_platform.c boot/boot_handoff.c \
                                src/core/board_lookup.c boards/$(HOST_BOARD)/board.c \
                                src/core/app_runner_none.c \
                                $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) $(MODEL_INC) \
	   -o $@ $^ $(EXTRA_LDFLAGS)

# The model's curve, pinned against RFC 6979 and an independent derivation. It
# links the model and the hash and nothing else: no driver, no provider.
$(HOST_DIR)/test_model_p256: test/unit/test_model_p256.c $(ELEMENT_MODEL) \
                             src/crypto/sha256.c src/crypto/atecc608a/atecc.c \
                             src/proto/crc16.c src/proto/crc16_update.c \
                             $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) $(MODEL_INC) \
	   -o $@ $^ $(EXTRA_LDFLAGS)

# The application interface (docs/application-sdk/). An application is compiled
# with its own flags, as a separate object: against include/, the board and the
# mock HAL's types, never src/ -- the same boundary mk/firmware.mk draws -- and
# with the project's warning set, since it is linked into the image. Like
# unity.o, WITHOUT $(EXTRA_CFLAGS): the object outlives one invocation.
APP_HOST_BASE_CFLAGS := -std=c11 -g -O1 $(APP_WARN_CFLAGS) \
                        -Iinclude -Itest/mock -Iboards/$(HOST_BOARD) \
                        -DTFW_BOARD_HEADER='"board_profile.h"' $(HOST_EXTRA_DEFS)

# test_app_none: the build with no application, which is every build without
# APP_DIR. The host library minus the runner and the recording mock, plus the
# empty runner.
$(HOST_DIR)/test_app_none: test/unit/test_app_none.c \
                        $(filter-out src/core/app_runner.c test/mock/app_mock.c,$(HOST_LIB_SRCS)) \
                        src/core/app_runner_none.c \
                        $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# test_app_example: the example application, compiled as an application is --
# APP_HOST_BASE_CFLAGS, no -Isrc -- and driven through the runner by the suite.
$(HOST_DIR)/test-app-example/example.o: test/app/example/example.c
	@mkdir -p $(dir $@)
	@$(CC) $(APP_HOST_BASE_CFLAGS) -MMD -MP -c -o $@ $<

$(HOST_DIR)/test_app_example: test/unit/test_app_example.c \
                        $(filter-out test/mock/app_mock.c,$(HOST_LIB_SRCS)) \
                        $(HOST_DIR)/test-app-example/example.o \
                        $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(HOST_APP_DEFS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The Ethernet controller drivers -- issue #55 -- against the software W5100 /
# W5500, which IS the SPI HAL here, so no hal_mock.c: both define
# tfw_hal_spi_xfer. One test source, linked once per chip file, with the chip
# it expects named on the command line; each binary also puts the other part on
# the bus and asserts it is refused untouched.
# No two sources in one host binary may share a basename: a binary built by one
# cc command names its coverage files <binary>-<basename>.gcno/.gcda, so two
# w5x00.c's would write one pair and `make coverage` would read a stamp
# mismatch. Hence the model is w5x00_model.c, after its header.
W5X_TEST_SRCS := test/unit/test_w5x00.c test/model/w5x00_model.c src/net/w5x00.c \
                 src/core/board_lookup.c boards/$(HOST_BOARD)/board.c

$(HOST_DIR)/test_w5100: $(W5X_TEST_SRCS) src/net/w5100.c $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) -Itest/model -DW5X_TEST_CHIP=1 \
	   -o $@ $^ $(EXTRA_LDFLAGS)

$(HOST_DIR)/test_w5500: $(W5X_TEST_SRCS) src/net/w5500.c $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) -Itest/model -DW5X_TEST_CHIP=2 \
	   -o $@ $^ $(EXTRA_LDFLAGS)

# The Ethernet image's link: the whole host library with link_eth.c where
# link_serial.c would be -- what mk/firmware.mk links for ETH=1 -- and the TCP
# socket faked in the suite itself at include/tfw/eth.h.
$(HOST_DIR)/test_link_eth: test/unit/test_link_eth.c \
                        $(filter-out src/proto/link_serial.c,$(HOST_LIB_SRCS)) \
                        src/proto/link_eth.c src/net/netcfg.c \
                        $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(HOST_APP_DEFS) $(EXTRA_CFLAGS) -DTFW_FEATURE_ETHERNET=1 -o $@ $^ $(EXTRA_LDFLAGS)

HOST_EXTRA_BINS := $(HOST_DIR)/test_link_eth $(HOST_DIR)/test_model_p256 $(HOST_DIR)/test_sys_noboot $(HOST_DIR)/test_atecc $(HOST_DIR)/test_provision $(HOST_DIR)/test_alias $(HOST_DIR)/test_csr \
                    $(HOST_DIR)/test_bus_element $(HOST_DIR)/test_crypto_none \
                    $(HOST_DIR)/test_auth_session $(HOST_DIR)/test_pin_coord \
                    $(HOST_DIR)/test_platform_bake \
                    $(HOST_DIR)/test_app_none $(HOST_DIR)/test_app_example \
                    $(HOST_DIR)/test_w5100 $(HOST_DIR)/test_w5500

## test: build and run the host unit tests
test: $(TEST_BINS) $(HOST_EXTRA_BINS)
	@fail=0; for t in $(TEST_BINS) $(HOST_EXTRA_BINS); do \
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
	@# The boot suites too, or boot/'s lines are counted and never executed --
	@# which reads as a coverage collapse caused by adding tests.
	@$(MAKE) --no-print-directory boot-test EXTRA_CFLAGS="$(COV_FLAGS)" EXTRA_LDFLAGS="$(COV_FLAGS)"
	@mkdir -p coverage
	@# boot/ is measured on the same terms as src/: the LOGIC is in, the
	@# register-poking is out. boot/hal/avr is excluded for the reason
	@# src/hal/avr is -- a number counting code x86 cannot execute would not mean
	@# anything -- and so are boot_main.c, which is a superloop with no return,
	@# and boot/tier and boot/verify, which reach flash and an element through
	@# PROGMEM and I2C rather than through anything a host build can drive.
	@gcovr --root . --filter 'src/(proto|core|caps)/' --filter 'boards/' \
	       --filter 'boot/boot_(image|measure|ota|platform|policy|session|request|entry)\.c' \
	       --filter 'boot/recovery/recovery_ota\.c' \
	       --exclude 'src/hal/avr/' --print-summary \
	       --html-details coverage/index.html --fail-under-line $(COVERAGE_FLOOR)

COVERAGE_FLOOR ?= 90

## lint: static analysis over the firmware sources
# Vendored trees are skipped, for the same reason Unity is built without
# -Werror: an upstream's warnings are not ours to fix, and a lint gate that a
# dependency bump can turn red is one that gets suppressed wholesale the first
# time it happens. Their integrity is guaranteed by hash in
# test/third_party/MANIFEST.md instead, which is the check that matters for code
# we did not write.
#
# cppcheck stands in for `go vet`. It is given the board header and the PROGMEM
# definition explicitly rather than having its complaints suppressed: told the
# configuration it can analyse the code, whereas a suppression would also hide
# the next genuinely unknown macro.
lint: lint-arch-leak
	@cppcheck --enable=warning,style,performance,portability \
	          --check-level=exhaustive \
	          --error-exitcode=1 --inline-suppr --quiet \
	          --suppress=missingIncludeSystem \
	          --suppress=unusedFunction \
	          -DTFW_BOARD_HEADER='"board_profile.h"' -DTFW_PROGMEM= \
	          -Iinclude -Isrc -Isrc/hal/avr -Iboards/atmega328p -Iboot \
	          -i src/third_party \
	          src/ boards/ boot/
	@echo "cppcheck clean"
	@# The two grep gates. They used to live only in the CI workflow, which made
	@# them gates a developer could not run -- and one of them had been failing
	@# on main for two commits before anyone saw it, because the only place it
	@# ran was a job nobody reads when the other five are green.
	@# boot/ is swept on the same terms as src/core, src/proto and src/caps, with
	@# boot/hal/avr excluded exactly as src/hal/avr is. The bootloader's logic is
	@# the accept/refuse decision for every image the device will ever run, so it
	@# is the last place in the tree that should have a build that was never the
	@# build anyone tested. boot/hal/avr earns the exemption for the same reason
	@# src/hal/avr does and for exactly one conditional: the far-flash read, which
	@# is required above 64 KB and is a fact about the silicon rather than a
	@# feature anyone chose.
	@# The sweep covers every .c and .h under src/, boot/ and tools/. Exempt:
	@# the per-architecture HAL directories (src/hal/*/, boot/hal/*/, named as
	@# globs so a second architecture needs no edit here), commands.def, the
	@# vendored src/third_party (cppcheck skips it for the same reason), and the
	@# include guard of a header -- an #ifndef of a NAME_H macro, which is not a
	@# conditional on anything. config.h, progmem.h and the board profiles live
	@# under include/ and boards/, which this sweep does not enter.
	@if grep -rn '^[[:space:]]*#[[:space:]]*if' src boot tools \
	     --include='*.c' --include='*.h' --exclude-dir=third_party \
	     | grep -v 'commands.def' \
	     | grep -v '^src/hal/[^/]*/' | grep -v '^boot/hal/[^/]*/' \
	     | grep -v ':[[:space:]]*#[[:space:]]*ifndef[[:space:]]\+[A-Za-z0-9_]*_H[[:space:]]*$$'; then \
	  echo "preprocessor conditional outside the whitelist (see above)"; \
	  echo "a default belongs in include/tfw/config.h, where a board can override it"; \
	  exit 1; \
	fi
	@echo "no out-of-whitelist conditionals"
	@if grep -rn '__DATE__\|__TIME__' src/ boards/ include/ boot/; then \
	  echo "a build timestamp would make the image unreproducible, and its"; \
	  echo "measurement uncomputable from source -- which is signed evidence"; \
	  exit 1; \
	fi
	@echo "no build timestamps"

## lint-arch-leak: AVR must not leak out of its directories
# The tree is arch-neutral only if nothing outside the AVR's own directories
# names the AVR: <avr/...> headers, F_CPU, MCUSR, or a bare PROGMEM (TFW_PROGMEM
# is the neutral spelling and is not matched -- the pattern wants a non-identifier
# character on each side of the word). Exempt, and nothing else: src/hal/avr/,
# boot/hal/avr/, boards/ and mk/arch/avr.mk (neither of which is swept) and
# include/tfw/progmem.h, the one toolchain shim. A real leak is moved behind the
# HAL or progmem.h, never added to this list.
#
# Only code is matched: each file goes through the preprocessor in
# -fpreprocessed -dD mode first, which removes comments exactly (it knows a
# "//" inside a string from one outside) and expands nothing, so prose such as
# "MCUSR is already clear" is untouched in the source and invisible to the gate.
# A file the compiler cannot strip fails the gate instead of passing it.
ARCH_LEAK_ROOT ?= .
ARCH_LEAK_DIRS := src boot tools include test
ARCH_LEAK_PAT  := <avr/|(^|[^A-Za-z0-9_])(F_CPU|MCUSR|PROGMEM)([^A-Za-z0-9_]|$$)
lint-arch-leak:
	@cd $(ARCH_LEAK_ROOT) && fail=0; \
	for f in $$(find $(ARCH_LEAK_DIRS) -type f \( -name '*.c' -o -name '*.h' \) \
	            -not -path 'src/hal/avr/*' -not -path 'boot/hal/avr/*' \
	            -not -path 'include/tfw/progmem.h' | sort); do \
	  code=$$($(CC) -w -fpreprocessed -dD -E -P "$$f") || { \
	    echo "arch-leak gate: cannot strip comments from $$f"; exit 1; }; \
	  hits=$$(printf '%s\n' "$$code" | grep -En '$(ARCH_LEAK_PAT)' || true); \
	  if [ -n "$$hits" ]; then echo "$$f:"; printf '%s\n' "$$hits"; fail=1; fi; \
	done; \
	if [ $$fail -ne 0 ]; then \
	  echo "AVR leaked out of its directories (see above)"; \
	  echo "move it behind include/tfw/hal.h or include/tfw/progmem.h"; \
	  exit 1; \
	fi
	@echo "no arch leaks"

## lint-arch-leak-selftest: the arch-leak gate fails on a plant, in a temp copy
# Plants go into a throwaway tree, never into this one. Each leak must fail the
# gate; prose in a comment and the exempt directories must not.
lint-arch-leak-selftest:
	@t=$$(mktemp -d) && trap 'rm -rf "$$t"' EXIT && \
	mkdir -p $(addprefix $$t/,$(ARCH_LEAK_DIRS)) $$t/src/crypto $$t/src/hal/avr && \
	printf '/* MCUSR is already clear; F_CPU, <avr/io.h>, PROGMEM */\n// MCUSR\nint ok;\nconst int k TFW_PROGMEM = 0;\n' > $$t/src/crypto/ok.c && \
	printf '#include <avr/io.h>\nint f_cpu;\n' > $$t/src/hal/avr/exempt.h && \
	$(MAKE) --no-print-directory lint-arch-leak ARCH_LEAK_ROOT=$$t > /dev/null || \
	  { echo "selftest: clean tree was rejected"; exit 1; }; \
	for leak in '#include <avr/io.h>' 'int x = F_CPU;' 'int x = MCUSR;' 'const int x PROGMEM = 0;'; do \
	  printf '%s\n' "$$leak" > $$t/src/crypto/plant.c; \
	  if $(MAKE) --no-print-directory lint-arch-leak ARCH_LEAK_ROOT=$$t > /dev/null 2>&1; then \
	    echo "selftest: gate passed a planted: $$leak"; exit 1; \
	  fi; \
	done; \
	echo "arch-leak gate rejects every plant"

# ----------------------------------------------------------------- tfwsim
#
# The firmware as a host binary, one per crypto tier -- the same choice the
# firmware's own build makes, and made the same way: by which files are linked.
#
#   tfwsim              software tier. No asymmetric key, so ATTEST_CERT answers
#                      "unsupported" and ATTEST "not provisioned".
#   tfwsim-atecc608a    element tier. The REAL ATECC608A driver and provider, with
#                      test/model/atecc608a.c as the I2C bus. Started with
#                      explicit options it holds a deterministic TEST identity
#                      and mints DICE evidence over the wire. See
#                      tools/tfwsim/tier_atecc608a.c for the options and for why
#                      the evidence is real P-256 under a TEST key nobody may trust.
#
# `make tfwsim CRYPTO=atecc608a` is `make tfwsim-atecc608a`, so the tier is named
# the way it is for `make firmware`.
TFWSIM_COMMON := tools/tfwsim/main.c tools/tfwsim/tier.h

# Both simulators carry the Ethernet image's link, with a host TCP socket as
# the controller (tools/tfwsim/eth_socket.c), off until `--eth`; without it the
# socket answers as an absent controller and the serial behaviour is the serial
# image's. -DTFW_FEATURE_ETHERNET is NOT passed: all it does is reserve the
# shield's SPI and select pins, and the host's socket sits on no pin -- a
# simulator that refused D10-D13 would only break the GPIO suites that use them.
TFWSIM_LINK_SRCS := src/proto/link_eth.c src/net/netcfg.c tools/tfwsim/eth_socket.c
TFWSIM_LIB_SRCS  := $(filter-out src/proto/link_serial.c,$(HOST_LIB_SRCS)) $(TFWSIM_LINK_SRCS)

# The element tier swaps the software provider (and its UDS mock) for the ATECC608A
# driver, its provider and the software element. hal_mock.c is still linked for
# everything that is not the I2C bus: its I2C and delay symbols are weak for
# exactly this.
#
# The operator-signature auth session is ON in this binary, unlike the
# atmega328p profile it otherwise takes (which says 0 for want of flash, a
# constraint that does not exist on a host). The element tier is the only tier
# that can verify an operator signature, and the simulator exists to let a host
# exercise exactly what that tier does. ATTEST_CSR is on in it for the same
# reason: the profile's 0 is the 328P's flash, and the element tier is the tier
# that serves it. The override is an overlay
# profile, tools/tfwsim/element/board_profile.h, found first on the include path;
# no firmware source or board profile is touched, and no AVR image sees it.
TFWSIM_ELEMENT_INC := -Itools/tfwsim/element
TFWSIM_ELEMENT_SRCS := $(filter-out src/crypto/soft/provider.c test/mock/uds_mock.c \
                                   src/core/auth_session_none.c src/core/app_runner.c \
                                   test/mock/app_mock.c, \
                                   $(TFWSIM_LIB_SRCS)) \
                      src/core/auth_session.c \
                      src/crypto/atecc608a/atecc.c src/crypto/atecc608a/provider.c \
                      $(ELEMENT_MODEL) $(APP_RUNNER_SRCS)

# The simulator links the runner an image would (mk/app.mk): the real one with
# APP_DIR's application, or the empty one. Never the recording mock, which is
# the tests'.
TFWSIM_SOFT_SRCS := $(filter-out src/core/app_runner.c test/mock/app_mock.c,$(TFWSIM_LIB_SRCS)) \
                    $(APP_RUNNER_SRCS)

# An application's objects, one directory per tier: the element tier's overlay
# profile is a different config.h, and an application's TFW_APP_STATIC_CHECK is
# checked against the profile of the image it is linked into.
APP_HOST_DIR          := $(HOST_DIR)/app-$(APP_NAME)
APP_HOST_OBJS         := $(patsubst %.c,$(APP_HOST_DIR)/%.o,$(APP_SRCS))
APP_HOST_ELEMENT_DIR  := $(HOST_DIR)/app-$(APP_NAME)-atecc608a
APP_HOST_ELEMENT_OBJS := $(patsubst %.c,$(APP_HOST_ELEMENT_DIR)/%.o,$(APP_SRCS))
APP_HOST_CFLAGS       := $(APP_HOST_BASE_CFLAGS) -ffile-prefix-map=$(CURDIR)=. $(APP_PREFIX_MAP) $(APP_CFLAGS)

ifneq ($(APP_SRCS),)
$(APP_HOST_OBJS): $(APP_HOST_DIR)/%.o: $(APP_ABS)/%.c
	@mkdir -p $(dir $@)
	@$(CC) $(APP_HOST_CFLAGS) -MMD -MP -c -o $@ $<

$(APP_HOST_ELEMENT_OBJS): $(APP_HOST_ELEMENT_DIR)/%.o: $(APP_ABS)/%.c
	@mkdir -p $(dir $@)
	@$(CC) $(TFWSIM_ELEMENT_INC) $(APP_HOST_CFLAGS) -MMD -MP -c -o $@ $<

-include $(APP_HOST_OBJS:.o=.d) $(APP_HOST_ELEMENT_OBJS:.o=.d)
endif

## tfwsim: build the firmware as a host binary speaking the protocol on stdio
tfwsim: $(HOST_DIR)/tfwsim$(if $(filter atecc608a,$(CRYPTO)),-atecc608a)$(APP_SUFFIX)
$(HOST_DIR)/tfwsim$(APP_SUFFIX): $(TFWSIM_COMMON) tools/tfwsim/tier_soft.c $(TFWSIM_SOFT_SRCS) $(APP_HOST_OBJS) | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(APP_DEFS) $(EXTRA_CFLAGS) -Itools/tfwsim -o $@ \
	   tools/tfwsim/main.c tools/tfwsim/tier_soft.c $(TFWSIM_SOFT_SRCS) $(APP_HOST_OBJS) $(EXTRA_LDFLAGS)
	@echo "built $@"

## tfwsim-atecc608a: build the element-tier simulator (real ATECC608A driver, software element)
tfwsim-atecc608a: $(HOST_DIR)/tfwsim-atecc608a$(APP_SUFFIX)
$(HOST_DIR)/tfwsim-atecc608a$(APP_SUFFIX): $(TFWSIM_COMMON) tools/tfwsim/tier_atecc608a.c $(TFWSIM_ELEMENT_SRCS) $(APP_HOST_ELEMENT_OBJS) | $(HOST_DIR)
	@$(HOST_CC) $(TFWSIM_ELEMENT_INC) $(HOST_CFLAGS) $(APP_DEFS) $(EXTRA_CFLAGS) \
	   -Itools/tfwsim $(MODEL_INC) -o $@ \
	   tools/tfwsim/main.c tools/tfwsim/tier_atecc608a.c $(TFWSIM_ELEMENT_SRCS) $(APP_HOST_ELEMENT_OBJS) $(EXTRA_LDFLAGS)
	@echo "built $@"

clean-host:
	@rm -rf $(HOST_DIR)

# The .d files mk/ccdeps.sh wrote. After every rule in the file so nothing here
# becomes the default goal; mk/boot.mk's land in $(HOST_DIR)/boot and are read
# by the same line. -include, because the first build has none.
-include $(wildcard $(HOST_DIR)/*.d $(HOST_DIR)/boot/*.d $(HOST_DIR)/test-app-example/*.d)

.PHONY: test test-asan coverage clean-host tfwsim tfwsim-atecc608a lint \
        lint-arch-leak lint-arch-leak-selftest

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
#
# Each is compiled with the feature flags of its board's LAST tier in
# BOARD_TIERS -- the most capable image it ships, the element tier where there is
# one -- as `make feature-defs` reports them from the same variables the image's
# FW_CFLAGS carry. Without them every tier-selected gate reads config.h's 0, and
# a board test asserting where ATTEST_CSR (#30) is present could not pass, nor
# one asserting where it is absent mean anything.
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
	  tiers=$$($(MAKE) -s --no-print-directory board-tiers BOARD=$$b); tier=$${tiers##* }; \
	  defs=$$($(MAKE) -s --no-print-directory feature-defs BOARD=$$b CRYPTO=$$tier) || \
	    { echo "NO FEATURE DEFS: $$b $$tier"; fail=1; continue; }; \
	  $(CC) -std=c11 -g -O1 -Wall -Wextra -Werror -Wconversion -Wshadow \
	     -Iinclude -Isrc -Isrc/core -Itest/mock -Iboards/$$b -I$(UNITY_DIR) \
	     -DTFW_BOARD_HEADER='"board_profile.h"' $$defs \
	     -o $(SWEEP_DIR)/$$b "$$src" src/core/board_lookup.c boards/$$b/board.c \
	     $(UNITY_DIR)/unity.c || { echo "BUILD FAILED: $$b"; fail=1; continue; }; \
	  echo "--- $$b ($$tier:$${defs:- no tier features})"; ./$(SWEEP_DIR)/$$b || fail=1; ran=$$((ran+1)); \
	done; \
	echo "board-sweep: $$ran board(s)"; \
	[ "$$ran" -gt 0 ] || { echo "board-sweep swept nothing"; exit 1; }; \
	exit $$fail

.PHONY: board-sweep

# ------------------------------------------------------------ header deps check
#
## header-deps-check: prove an edited header makes the built binaries out of date
# For each pair "binary header": build the binary, assert `make -q` calls it up
# to date, bump the header's mtime, assert `make -q` now calls it out of date.
# The first assertion is not decoration -- a binary that is stale before the
# touch makes the second one pass for the wrong reason.
#
# Nothing tracked is modified. The header's mtime is the only thing changed, and
# it is put back from a reference copy on every exit path, so a failed check
# does not leave the tree looking edited. The binaries are the ordinary ones and
# are rebuilt at the end so the check does not leave them stale either.
#
#   tfwsim, protocol.h   the case that was reported: a stale tfwsim answered
#                        protocol 1.0 after protocol.h had moved to 2
#   a unit test, config.h    the pattern rule every suite shares
#   a boot test, config.h    mk/boot.mk's own rules
HDEP_UNIT := $(HOST_DIR)/test_frame
HDEP_BOOT := $(HOST_DIR)/boot/test_boot_image
HDEP_PAIRS := $(HOST_DIR)/tfwsim:include/tfw/protocol.h \
              $(HDEP_UNIT):include/tfw/config.h \
              $(HDEP_BOOT):include/tfw/config.h

header-deps-check:
	@set -e; ref=$$(mktemp); cur=; \
	 restore() { [ -z "$$cur" ] || touch -r "$$ref" "$$cur"; rm -f "$$ref"; }; \
	 trap restore EXIT; \
	 for pair in $(HDEP_PAIRS); do \
	   bin=$${pair%%:*}; hdr=$${pair#*:}; \
	   $(MAKE) --no-print-directory $$bin > /dev/null; \
	   if ! $(MAKE) -q --no-print-directory $$bin; then \
	     echo "header-deps-check: $$bin is out of date straight after being built"; exit 1; fi; \
	   cur=$$hdr; touch -r "$$hdr" "$$ref"; touch "$$hdr"; \
	   if $(MAKE) -q --no-print-directory $$bin; then \
	     echo "header-deps-check: touching $$hdr did not make $$bin out of date"; exit 1; fi; \
	   touch -r "$$ref" "$$hdr"; cur=; \
	   $(MAKE) --no-print-directory $$bin > /dev/null; \
	   echo "  ok  $$bin depends on $$hdr"; \
	 done; \
	 echo "header-deps-check: edited headers rebuild the host binaries"

.PHONY: header-deps-check
