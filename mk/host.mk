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
HOST_BOARD_DEFAULT := atmega328p
HOST_BOARD ?= $(HOST_BOARD_DEFAULT)

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
# The soft tfwsim is the exception: it answers as a CRYPTO=soft image, which
# compiles the command out, so it takes the alias certificate back out
# (TFWSIM_SOFT_OFF below).
#
# ATTEST_CSR is NOT turned on here. The host build takes the 328P's profile,
# which says 0 for want of flash, so this build's dispatcher has no ATTEST_CSR
# entry and answers it TFW_ERR_UNKNOWN_CMD exactly as the 328P image does --
# test_dispatch pins that. A -D here would also be a macro redefinition of the
# profile's 0. tfw_csr_cmd itself is tested by test_csr, and served by
# tfwsim-atecc608a through its overlay profile.
HOST_EXTRA_DEFS_DEFAULT := -DTFW_FEATURE_ATTEST=1 -DTFW_FEATURE_ALIAS_CERT=1
HOST_EXTRA_DEFS ?= $(HOST_EXTRA_DEFS_DEFAULT)

# HOST_BOARD and HOST_EXTRA_DEFS reach every host object, and make does not
# track a flag (#161): each names the path where it is not its default, so the
# default build keeps build/host -- the path tfwsim's users name. The defines
# are a checksum, as tfw-boot's BOOT_OPT_EXTRA is (mk/boot.mk). EXTRA_CFLAGS is
# not here: coverage and test-asan clean the tree before they set it.
HOST_DIR    := $(BUILD_DIR)/host$(if $(filter-out $(HOST_BOARD_DEFAULT),$(HOST_BOARD)),-$(HOST_BOARD))$(if \
               $(filter-out $(HOST_EXTRA_DEFS_DEFAULT),$(HOST_EXTRA_DEFS))$(filter-out $(HOST_EXTRA_DEFS),$(HOST_EXTRA_DEFS_DEFAULT)),-x$(firstword $(shell printf '%s' '$(subst ','\'',$(strip $(HOST_EXTRA_DEFS)))' | cksum)))

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

# -fno-sanitize-recover=all: by default UBSan prints its report and lets the
# binary carry on and exit 0, so a gate on the exit status passed on undefined
# behaviour. With it, the first report aborts the test binary and fails the gate.
SAN_FLAGS   := -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
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
# rather than by editing a vendored header. The copy is the boot's v1.2 (#130), one
# for both; this is the model's own -D set, and the boot's UECC_DEFS are untouched.
UECC_DIR    := src/third_party/micro-ecc
UECC_CFLAGS := -std=c11 -g -O1 -I$(UECC_DIR) \
               -DuECC_SUPPORTS_secp160r1=0 -DuECC_SUPPORTS_secp192r1=0 \
               -DuECC_SUPPORTS_secp224r1=0 -DuECC_SUPPORTS_secp256k1=0
MODEL_INC     := -Isrc/crypto/atecc608a -Itest/model -I$(UECC_DIR)
ELEMENT_MODEL := test/model/atecc608a.c $(HOST_DIR)/uECC.o

# The soft root's identity (#48, #130): the ES256 P-256 DeviceID, as the 2560
# soft and auto images link it (mk/firmware.mk SOFT_ID=p256). deviceid_p256.c
# includes uECC.h; uECC.o is the model's object, linked once wherever both are.
HOST_CFLAGS   += -I$(UECC_DIR)
SOFT_ID_SRCS  := src/crypto/soft/id_p256.c src/crypto/deviceid_p256.c

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
# test_csr_alg links its own stand-in provider, which would be a second
# tfw_crypto_sign beside the soft provider in HOST_LIB_SRCS.
# test_bus_element joins that list for the same reason: it links the software
# element as the I2C HAL and its own guard stubs for the SPI/1-Wire HAL
# functions, either of which the mock also defines.
# test_crypto_none is excluded for a different reason: HOST_LIB_SRCS already
# links src/crypto/soft/provider.c, and its suite needs src/crypto/none/
# provider.c instead -- linking both would be two definitions of
# tfw_crypto_init and everything else in include/tfw/crypto.h.
TEST_SRCS   := $(filter-out test/unit/test_board_%.c test/unit/test_atecc.c test/unit/test_atecc_cdi_clear.c test/unit/test_crypto_auto.c \
                            test/unit/test_provision.c test/unit/test_provision_mutate.c test/unit/test_auth_mutate.c test/unit/test_auth_robust.c test/unit/test_alias.c \
                            test/unit/test_csr.c test/unit/test_csr_alg.c \
                            test/unit/test_model_p256.c \
                            test/unit/test_bus_element.c \
                            test/unit/test_crypto_none.c test/unit/test_uds_flash.c \
                            test/unit/test_sys_noboot.c test/unit/test_ota_trial.c test/unit/test_ota_trial_mutate.c \
                            test/unit/test_none_variants.c test/unit/test_none_standins.c \
                            test/unit/test_noboot_wire.c test/unit/test_noboot_ota.c \
                            test/unit/test_pin_coord.c \
                            test/unit/test_platform_bake.c \
                            test/unit/test_app_none.c \
                            test/unit/test_app_example.c \
                            test/unit/test_w5x00.c test/unit/test_w5x00_nocs.c test/unit/test_link_eth.c test/unit/test_netcfg_mutate.c test/unit/test_ota_hold_link.c \
                             test/unit/test_w5x00_mutate.c test/unit/test_link_eth_mutate.c test/unit/test_dhcp.c \
                            test/unit/test_dhcp_robust.c test/unit/test_w5x00_robust.c \
                           test/unit/test_link_dhcp.c \
                            test/unit/test_suit.c test/unit/test_suit_emit.c test/unit/test_suit_mutate.c test/unit/test_suit_integrated.c \
                            test/unit/test_suit_integrated_mutate.c test/unit/test_suit_integrated_unbound.c \
                            test/unit/test_hsm_payload.c test/unit/test_hsm_engine.c test/unit/test_hsm_cmd.c \
                           test/unit/test_ota_stage.c test/unit/test_ota_mutate.c test/unit/test_appstate_mutate.c test/unit/test_spi_nor.c test/unit/test_ota_info_atecc.c test/unit/test_ota_single.c test/unit/test_ota_manifest.c \
                            test/unit/test_stage_marked.c test/unit/test_esp_ota.c test/unit/test_esp_wire.c \
                            test/unit/test_storage_2560.c test/unit/test_otactl_floor.c test/unit/test_sha256_small.c \
                            test/unit/test_deviceid_p256.c test/unit/test_provision_uds.c \
                            test/unit/test_esp_crypto_hmac_p256.c test/unit/test_esp_board.c \
                            test/unit/test_esp_hal_eeprom.c \
                            test/unit/test_soft_id_p256.c test/unit/test_dice_evidence.c, \
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
                 src/caps/bus_held_none.c \
                 src/caps/storage.c \
                 src/crypto/sha256.c \
                 src/crypto/soft/provider.c \
                 $(SOFT_ID_SRCS) $(HOST_DIR)/uECC.o \
                 src/caps/attest.c \
                 src/caps/alias.c \
                 src/caps/tmpl.c \
                 src/caps/csr.c \
                 src/caps/uccs.c \
                 src/core/handoff.c src/core/handoff_seed.c src/core/platform_id.c \
                 src/core/secure_zero.c \
                 src/core/auth_session_none.c \
                 src/core/update_request.c boot/boot_request.c \
                 boot/boot_handoff.c boot/boot_platform.c \
                 src/core/app_runner.c \
                 test/mock/hal_mock.c \
                 test/mock/uds_mock.c \
                 test/mock/boot_soft_mock.c \
                 test/mock/app_mock.c \
                 src/core/board_lookup.c \
                 src/core/ota_stage_none.c \
                 boards/$(HOST_BOARD)/board.c

# RESET asks the stager whether its reset installs (#51), so the host library
# links the no-stager stand-in an image without OTA_STAGE does. A link that
# carries ota_stage.c takes HOST_STAGE_LIB_SRCS, the same list without it.
HOST_STAGE_LIB_SRCS := $(filter-out src/core/ota_stage_none.c,$(HOST_LIB_SRCS))

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
                        src/core/handoff.c src/core/handoff_seed.c src/core/secure_zero.c boot/boot_handoff.c src/proto/crc16.c src/proto/crc16_update.c \
                        $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) $(MODEL_INC) \
	   -o $@ $^ $(EXTRA_LDFLAGS)

# The element-root CDI cleared once no longer needed (TCG DICE HW Req 6.4):
# test_atecc's link without src/core/secure_zero.c, so the suite's own
# tfw_secure_zero records what it is handed, as test_deviceid_p256's does.
$(HOST_DIR)/test_atecc_cdi_clear: test/unit/test_atecc_cdi_clear.c $(ELEMENT_MODEL) \
                                  src/crypto/atecc608a/atecc.c src/crypto/atecc608a/provider.c \
                                  src/crypto/sha256.c \
                                  src/core/handoff.c src/core/handoff_seed.c boot/boot_handoff.c src/proto/crc16.c src/proto/crc16_update.c \
                                  $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) $(MODEL_INC) \
	   -o $@ $^ $(EXTRA_LDFLAGS)

# CRYPTO=auto's dispatch (issue #130) against the software element, both
# providers renamed by mk/firmware.mk's CRYPTO_RENAME exactly as the image's are.
# The renamed provider and floor objects are, like unity.o, WITHOUT
# $(EXTRA_CFLAGS): the object outlives one invocation, so one built by
# `test-asan` / `coverage` would be linked into the next, plain build and fail
# there. The link still carries the flags.
$(HOST_DIR)/crypto_auto_%.o: | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(MODEL_INC) $(call CRYPTO_RENAME,$*) -c -o $@ $<
$(HOST_DIR)/crypto_auto_soft.o: src/crypto/soft/provider.c
$(HOST_DIR)/crypto_auto_element.o: src/crypto/atecc608a/provider.c
$(HOST_DIR)/floor_auto_%.o: | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(MODEL_INC) $(call FLOOR_RENAME,$*) -c -o $@ $<
$(HOST_DIR)/floor_auto_soft.o: src/core/ota_floor_eeprom.c
$(HOST_DIR)/floor_auto_element.o: src/core/ota_floor_atecc.c
$(HOST_DIR)/test_crypto_auto: test/unit/test_crypto_auto.c $(ELEMENT_MODEL) \
                              src/crypto/atecc608a/atecc.c src/crypto/auto/provider.c \
                              $(HOST_DIR)/crypto_auto_soft.o $(HOST_DIR)/crypto_auto_element.o $(SOFT_ID_SRCS) \
                              src/core/ota_floor_auto.c $(HOST_DIR)/floor_auto_soft.o $(HOST_DIR)/floor_auto_element.o \
                              src/core/auth_session_auto.c \
                              src/crypto/sha256.c test/mock/uds_mock.c \
                              src/core/handoff.c src/core/handoff_seed.c src/core/secure_zero.c boot/boot_handoff.c src/proto/crc16.c src/proto/crc16_update.c \
                              $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) $(MODEL_INC) \
	   -o $@ $^ $(EXTRA_LDFLAGS)

# OTA_INFO's rollback floor, each tier's reader by its renamed name, against
# the software element -- the same floor objects test_crypto_auto links.
$(HOST_DIR)/test_otactl_floor: test/unit/test_otactl_floor.c $(ELEMENT_MODEL) \
                               src/crypto/atecc608a/atecc.c src/crypto/sha256.c src/core/secure_zero.c \
                               $(HOST_DIR)/floor_auto_soft.o $(HOST_DIR)/floor_auto_element.o \
                               $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) $(MODEL_INC) \
	   -o $@ $^ $(EXTRA_LDFLAGS)

# The SUIT manifest reader and gate (issue #78), the same arrangement: the
# signature is checked by the element tier's own path, against the software
# element holding the vectors' key in the image-signing slot. Built with the
# ATmega2560's profile rather than HOST_BOARD's, because the 2560 is the board
# that links the reader and testdata/suit_vectors.h carries its class id.
SUIT_TEST_CFLAGS := $(subst -Iboards/$(HOST_BOARD) ,-Iboards/atmega2560 ,$(HOST_CFLAGS))
# test_suit_mutate feeds the reader mutated envelopes, on the same link.
$(HOST_DIR)/test_suit $(HOST_DIR)/test_suit_mutate: $(HOST_DIR)/%: test/unit/%.c $(ELEMENT_MODEL) \
                       src/core/suit.c boot/boot_trial.c boot/boot_image.c boot/boot_platform.c \
                       src/crypto/atecc608a/atecc.c \
                       src/crypto/atecc608a/provider.c \
                       src/crypto/sha256.c \
                       src/core/handoff.c src/core/handoff_seed.c src/core/secure_zero.c boot/boot_handoff.c src/proto/crc16.c src/proto/crc16_update.c \
                       $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(SUIT_TEST_CFLAGS) $(EXTRA_CFLAGS) $(MODEL_INC) \
	   -o $@ $^ $(EXTRA_LDFLAGS)

# The integrated-payload reader (docs/hsm-self-defence/ row 1), the same link
# plus its own file: the caller's key goes through the element tier's
# tfw_crypto_verify_operator, against the software element.
#   test_suit_integrated_mutate  mutated envelopes, every refusal path killed
#   test_suit_integrated_unbound the ATtiny85's profile: no policy or script
#                                class (all zero), so nothing is read
SUIT_UNBOUND_CFLAGS := $(filter-out -DTFW_FEATURE_ATTEST=% -DTFW_FEATURE_ALIAS_CERT=%,\
                         $(subst -Iboards/$(HOST_BOARD) ,-Iboards/attiny85 ,$(HOST_CFLAGS)))
$(HOST_DIR)/test_suit_integrated_unbound: CFLAGS_SUIT = $(SUIT_UNBOUND_CFLAGS)
$(HOST_DIR)/test_suit_integrated: CFLAGS_SUIT = $(SUIT_TEST_CFLAGS)
# The mutation test signs its cases with the build's emitter.
$(HOST_DIR)/test_suit_integrated_mutate: CFLAGS_SUIT = $(SUIT_TEST_CFLAGS) -Itools/suit-emit
$(HOST_DIR)/test_suit_integrated_mutate: tools/suit-emit/emit.c
$(HOST_DIR)/test_suit_integrated $(HOST_DIR)/test_suit_integrated_mutate \
$(HOST_DIR)/test_suit_integrated_unbound: $(HOST_DIR)/%: test/unit/%.c $(ELEMENT_MODEL) \
                       src/core/suit.c src/core/suit_integrated.c \
                       boot/boot_trial.c boot/boot_image.c boot/boot_platform.c \
                       src/crypto/atecc608a/atecc.c \
                       src/crypto/atecc608a/provider.c \
                       src/crypto/sha256.c \
                       src/core/handoff.c src/core/handoff_seed.c src/core/secure_zero.c boot/boot_handoff.c src/proto/crc16.c src/proto/crc16_update.c \
                       $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(CFLAGS_SUIT) $(EXTRA_CFLAGS) $(MODEL_INC) \
	   -o $@ $^ $(EXTRA_LDFLAGS)

# The tamper-response engine (docs/hsm-self-defence/ slice 2), in no object
# list yet (architecture.md question 16): the same link as the reader above.
#   test_hsm_payload  the policy set and script reader, on literal payloads
#   test_hsm_engine   the decision engine behind a recording mock port, its
#                     envelopes signed by the build's emitter
$(HOST_DIR)/test_hsm_payload: src/core/hsm_payload.c
$(HOST_DIR)/test_hsm_engine: src/core/hsm_payload.c src/core/hsm_engine.c tools/suit-emit/emit.c
# test_hsm_cmd  the channel's commands (slice 4) over the real engine, as the
#               dispatcher calls them, on the literal envelope
$(HOST_DIR)/test_hsm_cmd: src/core/hsm_payload.c src/core/hsm_engine.c src/core/hsm_cmd.c \
                          src/core/reader.c src/core/writer.c
$(HOST_DIR)/test_hsm_payload $(HOST_DIR)/test_hsm_engine $(HOST_DIR)/test_hsm_cmd: $(HOST_DIR)/%: test/unit/%.c \
                       $(ELEMENT_MODEL) src/core/suit.c src/core/suit_integrated.c \
                       boot/boot_trial.c boot/boot_image.c boot/boot_platform.c \
                       src/crypto/atecc608a/atecc.c \
                       src/crypto/atecc608a/provider.c \
                       src/crypto/sha256.c \
                       src/core/handoff.c src/core/handoff_seed.c src/core/secure_zero.c boot/boot_handoff.c src/proto/crc16.c src/proto/crc16_update.c \
                       $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(SUIT_TEST_CFLAGS) -Itools/suit-emit $(EXTRA_CFLAGS) $(MODEL_INC) \
	   -o $@ $^ $(EXTRA_LDFLAGS)

# The build's SUIT emitter (tools/suit-emit/, mk/suit.mk) against the same
# literals. In no other object list, as tools/platform-bake/ is in none.
$(HOST_DIR)/test_suit_emit: test/unit/test_suit_emit.c tools/suit-emit/emit.c src/crypto/sha256.c \
                            $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(SUIT_TEST_CFLAGS) -Itools/suit-emit $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# Provisioning, same arrangement and the same reason: it drives the element, so
# the software element is its I2C HAL.
$(HOST_DIR)/test_provision $(HOST_DIR)/test_provision_mutate: $(HOST_DIR)/%: test/unit/%.c $(ELEMENT_MODEL) \
                            src/caps/provision.c src/caps/provision_atecc.c \
                            src/core/reader.c src/core/writer.c \
                            src/crypto/atecc608a/atecc.c src/crypto/sha256.c src/core/secure_zero.c \
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
                        src/core/handoff.c src/core/handoff_seed.c src/core/platform_id.c src/core/secure_zero.c \
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
                      src/core/handoff.c src/core/handoff_seed.c src/core/platform_id.c src/core/secure_zero.c \
                      boot/boot_handoff.c boot/boot_platform.c src/proto/crc16.c src/proto/crc16_update.c \
                      $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) $(MODEL_INC) \
	   -o $@ $^ $(EXTRA_LDFLAGS)

# ATTEST_CSR's refusal of a signature that is not ES256. No shipped provider
# gives a public key and signs under anything else, so the provider and the
# attestation identity are the suite's own stand-ins.
$(HOST_DIR)/test_csr_alg: test/unit/test_csr_alg.c \
                          src/caps/csr.c src/caps/alias.c src/caps/tmpl.c src/caps/uccs.c \
                          src/core/reader.c src/core/writer.c src/crypto/sha256.c \
                          $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) \
	   -o $@ $^ $(EXTRA_LDFLAGS)

# I2C_CONFIG's refusal on an element build (src/caps/bus.c) -- the one
# behaviour in that file that depends on which crypto tier is linked, so it
# gets the same arrangement test_atecc/test_provision/test_alias do: the
# software element as the I2C HAL, the real ATECC608A driver and provider as
# the crypto tier. The suite's own guard stubs resolve SPI's and 1-Wire's HAL
# functions, which bus.c's other handlers reference as one translation unit.
$(HOST_DIR)/test_bus_element: test/unit/test_bus_element.c \
                               $(ELEMENT_MODEL) \
                               src/caps/bus.c src/caps/bus_held_none.c \
                               src/core/reader.c src/core/writer.c \
                               src/core/board_lookup.c boards/$(HOST_BOARD)/board.c \
                               src/crypto/atecc608a/atecc.c src/crypto/atecc608a/provider.c \
                               src/crypto/sha256.c \
                               src/core/handoff.c src/core/handoff_seed.c src/core/secure_zero.c boot/boot_handoff.c \
                               src/proto/crc16.c src/proto/crc16_update.c \
                               $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) $(MODEL_INC) \
	   -o $@ $^ $(EXTRA_LDFLAGS)

# The unattested tier. Its own link line for the reason above this rule
# names: HOST_LIB_SRCS carries the software provider, and this suite is
# nothing without the tier that refuses everything instead.
$(HOST_DIR)/test_crypto_none: test/unit/test_crypto_none.c \
                        src/crypto/none/provider.c \
                        src/core/handoff.c src/core/handoff_seed.c src/core/secure_zero.c \
                        boot/boot_handoff.c src/proto/crc16.c src/proto/crc16_update.c \
                        test/mock/hal_mock.c test/mock/uds_mock.c \
                        $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The image's UDS source, src/crypto/soft/uds_flash.c, in place of
# test/mock/uds_mock.c, under the software provider -- HOST_LIB_SRCS carries
# the mock, so this suite has its own link line too.
$(HOST_DIR)/test_uds_flash: test/unit/test_uds_flash.c \
                        src/crypto/soft/uds_flash.c src/crypto/soft/provider.c src/crypto/sha256.c \
                        $(SOFT_ID_SRCS) $(HOST_DIR)/uECC.o \
                        src/core/handoff.c src/core/handoff_seed.c src/core/secure_zero.c \
                        boot/boot_handoff.c src/proto/crc16.c src/proto/crc16_update.c \
                        test/mock/hal_mock.c \
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

# The no-boot stand-ins (#130): the four files a build without tfw-boot links
# where the handoff, the platform block, the stager and PREV would be. No host
# library: each is pinned alone, as the board links it.
$(HOST_DIR)/test_none_variants: test/unit/test_none_variants.c \
                        src/core/handoff_none.c src/core/platform_id_none.c \
                        src/core/ota_stage_none.c src/core/ota_prev_none.c \
                        $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) -Isrc/core -o $@ $^ $(EXTRA_LDFLAGS)

$(HOST_DIR)/test_none_standins: test/unit/test_none_standins.c \
                        src/core/ota_trial_none.c src/core/auth_session_none.c \
                        src/core/update_request_none.c \
                        $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) -Isrc/core -o $@ $^ $(EXTRA_LDFLAGS)

# The same stand-ins on the wire (#130): the whole host library with them where
# handoff.c and platform_id.c would be, so ATTEST answers as a no-boot board's.
$(HOST_DIR)/test_noboot_wire: test/unit/test_noboot_wire.c \
                        $(filter-out src/core/handoff.c src/core/handoff_seed.c src/core/platform_id.c,$(HOST_LIB_SRCS)) \
                        src/core/handoff_none.c src/core/platform_id_none.c \
                        $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(HOST_APP_DEFS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# OTA_INFO on that build (#130): test_ota_stage's link with the same two
# stand-ins in place of handoff.c and platform_id.c.
$(HOST_DIR)/test_noboot_ota: test/unit/test_noboot_ota.c \
                        $(filter-out src/core/handoff.c src/core/handoff_seed.c src/core/platform_id.c,$(HOST_STAGE_LIB_SRCS)) \
                        src/core/handoff_none.c src/core/platform_id_none.c \
                        src/core/ota_stage.c src/core/ota_prev.c src/store/spi_nor.c test/model/spi_nor_model.c test/model/spi_nor_hal.c \
                        src/core/ota_ctl.c src/core/ota_activate.c src/core/ota_manifest_none.c src/core/ota_auth.c boot/boot_trial.c boot/boot_image.c src/core/ota_trial_none.c \
                        src/core/ota_floor_eeprom.c \
                        $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(HOST_APP_DEFS) $(EXTRA_CFLAGS) -Itest/model -Isrc/core -Isrc/crypto -DTFW_FEATURE_OTA_STAGE=1 -DTFW_FEATURE_OTA_ACTIVATE=1 -DTFW_FEATURE_OTA_CTL=1 -o $@ $^ $(EXTRA_LDFLAGS)

# The application's half of the one trial (#77) against the mock HAL's clock
# and EEPROM, with the platform block reader the application links, and the
# runner's after_update entry as a fake (test/mock/runner_fake.h).
# test_ota_trial_mutate mutates that block on the same link.
$(HOST_DIR)/test_ota_trial $(HOST_DIR)/test_ota_trial_mutate: $(HOST_DIR)/%: test/unit/%.c src/core/ota_trial.c src/core/ota_trial_block.c \
                        test/mock/runner_fake.c \
                        boot/boot_platform.c test/mock/hal_mock.c src/core/handoff.c src/core/handoff_seed.c \
                        src/core/secure_zero.c boot/boot_handoff.c \
                        src/proto/crc16.c src/proto/crc16_update.c \
                        $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# test_pin_coord brings its own board tables -- a port 18 and a bit 31, which no
# real board has -- so it links board_lookup.c and the mock HAL without
# boards/$(HOST_BOARD)/board.c, whose tables would be second definitions.
$(HOST_DIR)/test_pin_coord: test/unit/test_pin_coord.c src/core/board_lookup.c \
                        test/mock/hal_mock.c src/core/handoff.c src/core/handoff_seed.c src/core/secure_zero.c \
                        boot/boot_handoff.c src/proto/crc16.c src/proto/crc16_update.c \
                        $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The bench bake image's decision (mk/platform-bake.mk), through the mock HAL.
# Its own link because tools/platform-bake/ is in no other object list -- not
# the firmware's, and not this file's HOST_LIB_SRCS -- and must stay that way.
$(HOST_DIR)/test_platform_bake: test/unit/test_platform_bake.c tools/platform-bake/bake.c \
                        boot/boot_platform.c \
                        test/mock/hal_mock.c src/core/handoff.c src/core/handoff_seed.c src/core/secure_zero.c \
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
$(HOST_DIR)/test_auth_session $(HOST_DIR)/test_auth_mutate $(HOST_DIR)/test_auth_robust: $(HOST_DIR)/%: test/unit/%.c $(ELEMENT_MODEL) \
                                src/core/auth_session.c src/caps/storage.c src/caps/sys.c \
                                src/core/reader.c src/core/writer.c \
                                src/crypto/atecc608a/atecc.c src/crypto/atecc608a/provider.c \
                                src/crypto/sha256.c src/proto/crc16.c src/proto/crc16_update.c \
                                src/core/platform_id.c src/core/handoff.c src/core/handoff_seed.c src/core/secure_zero.c \
                                src/core/update_request.c boot/boot_request.c \
                                boot/boot_platform.c boot/boot_handoff.c \
                                src/core/board_lookup.c boards/$(HOST_BOARD)/board.c \
                                src/core/app_runner_none.c src/core/ota_stage_none.c \
                                $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) $(MODEL_INC) \
	   -o $@ $^ $(EXTRA_LDFLAGS)

# The model's curve, pinned against RFC 6979 and an independent derivation. It
# links the model and the hash and nothing else: no driver, no provider.
$(HOST_DIR)/test_model_p256: test/unit/test_model_p256.c $(ELEMENT_MODEL) \
                             src/crypto/sha256.c src/crypto/atecc608a/atecc.c src/core/secure_zero.c \
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

# The same drivers with the chip select on A7, which has no digital port on the
# host atmega328p: the board lookup refuses it before the bus is touched.
W5X_NOCS_SRCS := test/unit/test_w5x00_nocs.c test/model/w5x00_model.c src/net/w5x00.c \
                 src/core/board_lookup.c boards/$(HOST_BOARD)/board.c

$(HOST_DIR)/test_w5100_nocs: $(W5X_NOCS_SRCS) src/net/w5100.c $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) -Itest/model -DW5X_TEST_CHIP=1 \
	   -DTFW_ETH_CS_PIN=21u -o $@ $^ $(EXTRA_LDFLAGS)

$(HOST_DIR)/test_w5500_nocs: $(W5X_NOCS_SRCS) src/net/w5500.c $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) -Itest/model -DW5X_TEST_CHIP=2 \
	   -DTFW_ETH_CS_PIN=21u -o $@ $^ $(EXTRA_LDFLAGS)

# The DHCP client (src/net/dhcp.c, DHCP=1) on the same drivers and model, its
# server the test, which reads each datagram off the model's wire and hands
# replies up behind the header the chip prepends. The clock is the suite's.
DHCP_TEST_SRCS := test/unit/test_dhcp.c test/model/w5x00_model.c src/net/w5x00.c src/net/dhcp.c \
                  src/core/board_lookup.c boards/$(HOST_BOARD)/board.c

$(HOST_DIR)/test_dhcp_w5100: $(DHCP_TEST_SRCS) src/net/w5100.c $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) -Itest/model -DW5X_TEST_CHIP=1 \
	   -o $@ $^ $(EXTRA_LDFLAGS)

$(HOST_DIR)/test_dhcp_w5500: $(DHCP_TEST_SRCS) src/net/w5500.c $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) -Itest/model -DW5X_TEST_CHIP=2 \
	   -o $@ $^ $(EXTRA_LDFLAGS)

# The same drivers and model under hostile register values: the suite wraps the
# model's tfw_hal_spi_xfer at link time to rewrite what the socket registers
# answer, so the driver and the model are linked exactly as above.
W5X_MUT_SRCS := test/unit/test_w5x00_mutate.c test/model/w5x00_model.c src/net/w5x00.c \
                src/core/board_lookup.c boards/$(HOST_BOARD)/board.c

$(HOST_DIR)/test_w5100_mutate: $(W5X_MUT_SRCS) src/net/w5100.c $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) -Itest/model -DW5X_TEST_CHIP=1 \
	   -Wl,--wrap=tfw_hal_spi_xfer -o $@ $^ $(EXTRA_LDFLAGS)

$(HOST_DIR)/test_w5500_mutate: $(W5X_MUT_SRCS) src/net/w5500.c $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) -Itest/model -DW5X_TEST_CHIP=2 \
	   -Wl,--wrap=tfw_hal_spi_xfer -o $@ $^ $(EXTRA_LDFLAGS)

# The DHCP client against hostile replies, judged by the suite's own reading of
# RFC 2131 and the row: linked exactly as test_dhcp is.
DHCP_ROBUST_SRCS := test/unit/test_dhcp_robust.c test/model/w5x00_model.c src/net/w5x00.c src/net/dhcp.c \
                    src/core/board_lookup.c boards/$(HOST_BOARD)/board.c

$(HOST_DIR)/test_dhcp_robust_w5100: $(DHCP_ROBUST_SRCS) src/net/w5100.c $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) -Itest/model -DW5X_TEST_CHIP=1 \
	   -o $@ $^ $(EXTRA_LDFLAGS)

$(HOST_DIR)/test_dhcp_robust_w5500: $(DHCP_ROBUST_SRCS) src/net/w5500.c $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) -Itest/model -DW5X_TEST_CHIP=2 \
	   -o $@ $^ $(EXTRA_LDFLAGS)

# The drivers' receive path into the framer, on the same model: frames split,
# wrapped, oversized, hung up on and flipped.
W5X_ROBUST_SRCS := test/unit/test_w5x00_robust.c test/model/w5x00_model.c src/net/w5x00.c \
                   src/proto/frame.c src/proto/crc16.c src/proto/crc16_update.c \
                   src/core/board_lookup.c boards/$(HOST_BOARD)/board.c

$(HOST_DIR)/test_w5100_robust: $(W5X_ROBUST_SRCS) src/net/w5100.c $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) -Itest/model -DW5X_TEST_CHIP=1 \
	   -o $@ $^ $(EXTRA_LDFLAGS)

$(HOST_DIR)/test_w5500_robust: $(W5X_ROBUST_SRCS) src/net/w5500.c $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) -Itest/model -DW5X_TEST_CHIP=2 \
	   -o $@ $^ $(EXTRA_LDFLAGS)

# The Ethernet image's link: the whole host library with link_eth.c where
# link_serial.c would be -- what mk/firmware.mk links for ETH=1 -- and the TCP
# socket faked in the suite itself at include/tfw/eth.h. test_netcfg_mutate
# mutates the network block on the same link.
$(HOST_DIR)/test_link_eth $(HOST_DIR)/test_netcfg_mutate: $(HOST_DIR)/%: test/unit/%.c \
                        $(filter-out src/proto/link_serial.c,$(HOST_LIB_SRCS)) \
                        src/proto/link_eth.c src/net/netcfg.c src/net/dhcp_none.c \
                        $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(HOST_APP_DEFS) $(EXTRA_CFLAGS) -DTFW_FEATURE_ETHERNET=1 -o $@ $^ $(EXTRA_LDFLAGS)

# The same link, its faked socket hostile: states that flip, starts that fail,
# replies refused part way, request frames mutated on the way in.
$(HOST_DIR)/test_link_eth_mutate: test/unit/test_link_eth_mutate.c \
                        $(filter-out src/proto/link_serial.c,$(HOST_LIB_SRCS)) \
                        src/proto/link_eth.c src/net/netcfg.c src/net/dhcp_none.c \
                        $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(HOST_APP_DEFS) $(EXTRA_CFLAGS) -DTFW_FEATURE_ETHERNET=1 -o $@ $^ $(EXTRA_LDFLAGS)

# The OTA stager (issue #75) as an OTA_STAGE=1 image links it, with the software
# NOR part as the SPI bus -- its tfw_hal_spi_xfer overrides the mock's weak one.
# test_appstate_mutate mutates the handoff and platform block on the same link.
$(HOST_DIR)/test_ota_stage $(HOST_DIR)/test_ota_mutate $(HOST_DIR)/test_appstate_mutate: $(HOST_DIR)/%: test/unit/%.c $(HOST_STAGE_LIB_SRCS) \
                        src/core/ota_stage.c src/core/ota_prev.c src/store/spi_nor.c test/model/spi_nor_model.c test/model/spi_nor_hal.c \
                        src/core/ota_ctl.c src/core/ota_activate.c src/core/ota_manifest_none.c src/core/ota_auth.c boot/boot_trial.c boot/boot_image.c src/core/ota_trial_none.c \
                        src/core/ota_floor_eeprom.c \
                        $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(HOST_APP_DEFS) $(EXTRA_CFLAGS) -Itest/model -Isrc/core -Isrc/crypto -DTFW_FEATURE_OTA_STAGE=1 -DTFW_FEATURE_OTA_ACTIVATE=1 -DTFW_FEATURE_OTA_CTL=1 -o $@ $^ $(EXTRA_LDFLAGS)

# tfw_stage_marked's refusals, called directly, with test_ota_stage's link.
$(HOST_DIR)/test_stage_marked: test/unit/test_stage_marked.c $(HOST_STAGE_LIB_SRCS) \
                        src/core/ota_stage.c src/core/ota_prev.c src/store/spi_nor.c test/model/spi_nor_model.c test/model/spi_nor_hal.c \
                        src/core/ota_ctl.c src/core/ota_activate.c src/core/ota_manifest_none.c src/core/ota_auth.c boot/boot_trial.c boot/boot_image.c src/core/ota_trial_none.c \
                        src/core/ota_floor_eeprom.c \
                        $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(HOST_APP_DEFS) $(EXTRA_CFLAGS) -Itest/model -Isrc/core -Isrc/crypto -DTFW_FEATURE_OTA_STAGE=1 -DTFW_FEATURE_OTA_ACTIVATE=1 -DTFW_FEATURE_OTA_CTL=1 -o $@ $^ $(EXTRA_LDFLAGS)

# A single-slot stager (the 328P, the 32U4; #77): the stager, PREV and
# OTA_ACTIVATE, without ota_ctl.c -- the shape mk/firmware.mk links there.
$(HOST_DIR)/test_ota_single: test/unit/test_ota_single.c $(HOST_STAGE_LIB_SRCS) \
                        src/core/ota_stage.c src/core/ota_prev.c src/store/spi_nor.c test/model/spi_nor_model.c test/model/spi_nor_hal.c \
                        src/core/ota_activate.c src/core/ota_manifest_none.c src/core/ota_auth.c \
                        $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(HOST_APP_DEFS) $(EXTRA_CFLAGS) -Itest/model -Isrc/core -Isrc/crypto -DTFW_FEATURE_OTA_STAGE=1 -DTFW_FEATURE_OTA_ACTIVATE=1 -o $@ $^ $(EXTRA_LDFLAGS)

# OTA_AUTH's hold through the transport (#137): test_link_eth's link with
# test_ota_single's stager and OTA_ACTIVATE, so a request the Ethernet link
# refuses sits between OTA_AUTH and OTA_ACTIVATE.
$(HOST_DIR)/test_ota_hold_link: test/unit/test_ota_hold_link.c \
                        $(filter-out src/proto/link_serial.c,$(HOST_STAGE_LIB_SRCS)) \
                        src/proto/link_eth.c src/net/netcfg.c src/net/dhcp_none.c \
                        src/core/ota_stage.c src/core/ota_prev.c src/store/spi_nor.c test/model/spi_nor_model.c test/model/spi_nor_hal.c \
                        src/core/ota_activate.c src/core/ota_manifest_none.c src/core/ota_auth.c \
                        $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(HOST_APP_DEFS) $(EXTRA_CFLAGS) -Itest/model -Isrc/core -Isrc/crypto -DTFW_FEATURE_ETHERNET=1 -DTFW_FEATURE_OTA_STAGE=1 -DTFW_FEATURE_OTA_ACTIVATE=1 -o $@ $^ $(EXTRA_LDFLAGS)

# OTA_INFO on the element tier (#77): the same link with the element's floor
# file, the ATECC608A driver and the software element as the I2C bus, whose
# tfw_hal_i2c_xfer overrides the mock's weak one as tfwsim-atecc608a's does.
$(HOST_DIR)/test_ota_info_atecc: test/unit/test_ota_info_atecc.c $(HOST_STAGE_LIB_SRCS) \
                        src/core/ota_stage.c src/core/ota_prev.c src/store/spi_nor.c test/model/spi_nor_model.c test/model/spi_nor_hal.c \
                        src/core/ota_ctl.c src/core/ota_activate.c src/core/ota_manifest_none.c src/core/ota_auth.c boot/boot_trial.c boot/boot_image.c src/core/ota_trial_none.c \
                        src/core/ota_floor_atecc.c src/crypto/atecc608a/atecc.c $(ELEMENT_MODEL) \
                        $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(HOST_APP_DEFS) $(EXTRA_CFLAGS) $(MODEL_INC) -Isrc/core -Isrc/crypto -DTFW_FEATURE_OTA_STAGE=1 -DTFW_FEATURE_OTA_ACTIVATE=1 -DTFW_FEATURE_OTA_CTL=1 -o $@ $^ $(EXTRA_LDFLAGS)

# OTA_ACTIVATE's manifest check (ruling 121), linked as a 2560 element stager
# links it: the stager, ota_manifest.c and the SUIT reader, the element
# provider against the software element holding the vectors' image key, under
# SUIT_TEST_CFLAGS for the 2560's class id. No dispatcher: the test calls
# OTA_AUTH and OTA_ACTIVATE's handlers and supplies tfw_dispatch_last,
# tfw_dispatch_last_ok and tfw_sys_reset_soon.
$(HOST_DIR)/test_ota_manifest: test/unit/test_ota_manifest.c $(ELEMENT_MODEL) \
                        src/core/ota_stage.c src/core/ota_prev_none.c src/store/spi_nor.c test/model/spi_nor_model.c test/model/spi_nor_hal.c \
                        src/core/ota_activate.c src/core/ota_manifest.c src/core/ota_auth.c src/core/auth_session_none.c \
                        src/core/suit.c boot/boot_image.c boot/boot_platform.c \
                        src/crypto/atecc608a/atecc.c src/crypto/atecc608a/provider.c src/crypto/sha256.c \
                        src/core/reader.c src/core/writer.c test/mock/hal_mock.c src/core/board_lookup.c boards/atmega2560/board.c \
                        src/core/handoff.c src/core/handoff_seed.c src/core/secure_zero.c boot/boot_handoff.c src/proto/crc16.c src/proto/crc16_update.c \
                        $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(SUIT_TEST_CFLAGS) $(EXTRA_CFLAGS) $(MODEL_INC) -Itest/model -Isrc/core -Isrc/crypto -DTFW_FEATURE_OTA_STAGE=1 -DTFW_FEATURE_OTA_ACTIVATE=1 -o $@ $^ $(EXTRA_LDFLAGS)

# EEPROM under the ATmega2560's profile, whose bootloader keeps its swap-move
# journal above the platform block (issue #75), so the range EEPROM_WRITE
# refuses there is that profile's data. The suite supplies tfw_auth_gate itself
# to see which reads take it, so neither auth_session file is linked; the host
# board's extra feature defines are left out as test_boot_stage_2560 leaves them.
STORAGE_2560_CFLAGS := $(subst -Iboards/$(HOST_BOARD) ,-Iboards/atmega2560 ,\
                         $(filter-out $(HOST_EXTRA_DEFS),$(HOST_CFLAGS)))
$(HOST_DIR)/test_storage_2560: test/unit/test_storage_2560.c src/caps/storage.c \
                               src/core/reader.c src/core/writer.c test/mock/hal_mock.c \
                               src/core/handoff.c src/core/handoff_seed.c boot/boot_handoff.c src/proto/crc16.c src/proto/crc16_update.c \
                               src/core/secure_zero.c $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(STORAGE_2560_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The SPI NOR store driver alone on the software part (issue #75).
$(HOST_DIR)/test_spi_nor: test/unit/test_spi_nor.c $(HOST_LIB_SRCS) \
                        src/store/spi_nor.c test/model/spi_nor_model.c test/model/spi_nor_hal.c \
                        $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(HOST_APP_DEFS) $(EXTRA_CFLAGS) -Itest/model -Isrc/core -o $@ $^ $(EXTRA_LDFLAGS)

# The soft tfw-boot's size-first SHA-256, in place of sha256.c: both define
# the API of sha256.h, so it links alone.
$(HOST_DIR)/test_sha256_small: test/unit/test_sha256_small.c src/crypto/sha256_small.c src/core/secure_zero.c \
                               $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) -Isrc/crypto -o $@ $^ $(EXTRA_LDFLAGS)

# The P-256 DeviceID from a UDS (issue #48) on micro-ecc, without
# src/core/secure_zero.c: the suite's own tfw_secure_zero records what it is
# handed, which is how it sees the scalar zeroed.
$(HOST_DIR)/test_deviceid_p256: test/unit/test_deviceid_p256.c src/crypto/deviceid_p256.c \
                                src/crypto/sha256.c $(HOST_DIR)/uECC.o $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) -I$(UECC_DIR) -o $@ $^ $(EXTRA_LDFLAGS)

# The soft root's identity as that DeviceID (#48, #130), src/crypto/soft/id_p256.c,
# the same way: the suite's own tfw_secure_zero sees the seed and scalar zeroed,
# and its own tfw_crypto_soft_uds is the UDS, so no provider is linked.
$(HOST_DIR)/test_soft_id_p256: test/unit/test_soft_id_p256.c $(SOFT_ID_SRCS) \
                               src/crypto/sha256.c $(HOST_DIR)/uECC.o $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The soft root's ATTEST frame pinned against the TCG DICE formula (row
# dice-attestation) is the HMAC-SHA256 identity, mk/firmware.mk SOFT_ID=hmac:
# the host library with src/crypto/soft/id_hmac.c in place of id_p256.c.
HOST_HMAC_LIB_SRCS := $(filter-out src/crypto/soft/id_p256.c,$(HOST_LIB_SRCS)) src/crypto/soft/id_hmac.c
$(HOST_DIR)/test_dice_evidence: test/unit/test_dice_evidence.c $(HOST_HMAC_LIB_SRCS) $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(HOST_APP_DEFS) $(EXTRA_CFLAGS) -o $@ $< $(HOST_HMAC_LIB_SRCS) $(HOST_DIR)/unity.o $(EXTRA_LDFLAGS)

# The eFuse-UDS provisioning backend (issue #48) under the shared wire
# handlers, over a mock part in place of src/hal/esp32/prov_uds.c.
$(HOST_DIR)/test_provision_uds: test/unit/test_provision_uds.c src/caps/provision.c \
                                src/caps/provision_uds.c test/mock/prov_uds_mock.c \
                                src/core/reader.c src/core/writer.c src/core/secure_zero.c \
                                src/crypto/deviceid_p256.c src/crypto/sha256.c \
                                $(HOST_DIR)/uECC.o $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) -I$(UECC_DIR) \
	   -DTFW_FEATURE_PROVISION=1 -o $@ $^ $(EXTRA_LDFLAGS)

# The ESP32 OTA session (#48) on the host: src/hal/esp32/hal_ota.c against a
# mock esp_ota (test/mock/esp_ota_mock.c, headers in test/mock/esp32/), with the
# gates mk/firmware.mk gives an ESP32 image: the session, OTA_INFO, OTA_AUTH,
# OTA_ACTIVATE and OTA_CONFIRM. test_esp_ota builds with ESP-IDF's anti-rollback
# on (the esp32c3 QEMU board's sdkconfig), test_esp_wire with it off (the P4's),
# so both arms of hal_ota.c's one CONFIG_ conditional compile and run.
HOST_ESP_OTA_DEFS := -DTFW_FEATURE_OTA_STAGE=1 -DTFW_FEATURE_OTA_INFO=1 \
                     -DTFW_FEATURE_OTA_ACTIVATE=1 -DTFW_FEATURE_OTA_CTL=1
$(HOST_DIR)/test_esp_ota: test/unit/test_esp_ota.c $(HOST_STAGE_LIB_SRCS) \
                          src/hal/esp32/hal_ota.c src/core/ota_auth.c boot/boot_image.c \
                          test/mock/esp_ota_mock.c $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(HOST_APP_DEFS) $(EXTRA_CFLAGS) -Itest/mock/esp32 -Isrc/core -Isrc/hal/esp32 $(HOST_ESP_OTA_DEFS) -DCONFIG_BOOTLOADER_APP_ANTI_ROLLBACK=1 -o $@ $^ $(EXTRA_LDFLAGS)

# The ESP32 wire (#48) under the same session: src/hal/esp32/hal_uart.c over a
# mock lwIP (test/mock/lwip_mock.c) in place of hal_mock.c's weak byte link, so
# a request goes socket, transport, dispatch, hal_ota.c, esp_ota as on the part.
$(HOST_DIR)/test_esp_wire: test/unit/test_esp_wire.c $(HOST_STAGE_LIB_SRCS) \
                           src/hal/esp32/hal_uart.c src/hal/esp32/hal_ota.c \
                           src/core/ota_auth.c boot/boot_image.c \
                           test/mock/lwip_mock.c test/mock/esp_ota_mock.c \
                           $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(HOST_APP_DEFS) $(EXTRA_CFLAGS) -Itest/mock/esp32 -Isrc/core -Isrc/hal/esp32 $(HOST_ESP_OTA_DEFS) -DCONFIG_BOOTLOADER_APP_ANTI_ROLLBACK=0 -o $@ $^ $(EXTRA_LDFLAGS)

# The ESP32 pulse timing on the RMT receiver (#48, ADR follow-up 8):
# src/hal/esp32/hal_pulse_rmt.c over a mock RMT driver and clock
# (test/mock/esp_rmt_mock.c). src/hal/esp32 goes first so tfw/hal.h takes that
# port's pin type, not the host's.
$(HOST_DIR)/test_esp_pulse_rmt: test/unit/test_esp_pulse_rmt.c src/hal/esp32/hal_pulse_rmt.c \
                                test/mock/esp_rmt_mock.c $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) -Isrc/hal/esp32 -Itest/mock/esp32 $(HOST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The ESP32 hmac-p256 provider (#48, row esp32-p4): src/hal/esp32/
# crypto_hmac_p256.c over the suite's HMAC peripheral, eFuse purpose lookup and
# RNG (headers in test/mock/esp32/), with the real handoff layer 0 feeds, and
# the DeviceID and signature pinned to test_deviceid_p256's literals.
$(HOST_DIR)/test_esp_crypto_hmac_p256: test/unit/test_esp_crypto_hmac_p256.c \
                                       src/hal/esp32/crypto_hmac_p256.c src/crypto/deviceid_p256.c \
                                       src/crypto/sha256.c src/core/handoff.c src/core/handoff_seed_none.c \
                                       src/core/secure_zero.c boot/boot_handoff.c \
                                       src/proto/crc16.c src/proto/crc16_update.c \
                                       $(HOST_DIR)/uECC.o $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) -Itest/mock/esp32 -o $@ $^ $(EXTRA_LDFLAGS)

# The P4's C6 hold (#48, row esp32-p4): src/hal/esp32/hal_board.c over the
# suite's gpio driver (test/mock/esp32/driver/gpio.h). boards/esp32p4-poe goes
# first so config.h takes that board's profile, the one naming the C6 enable,
# not the host board's.
$(HOST_DIR)/test_esp_board: test/unit/test_esp_board.c src/hal/esp32/hal_board.c \
                            $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) -Iboards/esp32p4-poe -Isrc/hal/esp32 -Itest/mock/esp32 $(HOST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The ESP32 EEPROM capability and platform block (#48, row esp32-p4):
# src/hal/esp32/hal_eeprom.c over the suite's NOR flash partitions
# (test/mock/esp32/esp_partition.h), against the esp32p4-poe profile's
# TFW_EEPROM_SIZE.
$(HOST_DIR)/test_esp_hal_eeprom: test/unit/test_esp_hal_eeprom.c src/hal/esp32/hal_eeprom.c \
                                 $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) -Iboards/esp32p4-poe -Isrc/hal/esp32 -Itest/mock/esp32 $(HOST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

HOST_EXTRA_BINS := $(HOST_DIR)/test_esp_pulse_rmt $(HOST_DIR)/test_esp_crypto_hmac_p256 $(HOST_DIR)/test_esp_board $(HOST_DIR)/test_esp_hal_eeprom $(HOST_DIR)/test_atecc_cdi_clear $(HOST_DIR)/test_ota_stage $(HOST_DIR)/test_ota_mutate $(HOST_DIR)/test_appstate_mutate $(HOST_DIR)/test_stage_marked $(HOST_DIR)/test_spi_nor $(HOST_DIR)/test_otactl_floor $(HOST_DIR)/test_ota_info_atecc $(HOST_DIR)/test_ota_single $(HOST_DIR)/test_ota_manifest $(HOST_DIR)/test_ota_trial $(HOST_DIR)/test_ota_trial_mutate $(HOST_DIR)/test_link_eth $(HOST_DIR)/test_link_eth_mutate $(HOST_DIR)/test_netcfg_mutate $(HOST_DIR)/test_ota_hold_link $(HOST_DIR)/test_model_p256 $(HOST_DIR)/test_sys_noboot $(HOST_DIR)/test_none_variants $(HOST_DIR)/test_none_standins $(HOST_DIR)/test_noboot_wire $(HOST_DIR)/test_noboot_ota $(HOST_DIR)/test_atecc $(HOST_DIR)/test_crypto_auto $(HOST_DIR)/test_provision $(HOST_DIR)/test_provision_mutate $(HOST_DIR)/test_provision_uds $(HOST_DIR)/test_alias $(HOST_DIR)/test_csr $(HOST_DIR)/test_csr_alg \
                    $(HOST_DIR)/test_bus_element $(HOST_DIR)/test_crypto_none $(HOST_DIR)/test_uds_flash \
                    $(HOST_DIR)/test_auth_session $(HOST_DIR)/test_auth_mutate $(HOST_DIR)/test_auth_robust $(HOST_DIR)/test_pin_coord \
                    $(HOST_DIR)/test_platform_bake \
                    $(HOST_DIR)/test_app_none $(HOST_DIR)/test_app_example \
                    $(HOST_DIR)/test_w5100 $(HOST_DIR)/test_w5500 \
                    $(HOST_DIR)/test_w5100_nocs $(HOST_DIR)/test_w5500_nocs \
                    $(HOST_DIR)/test_dhcp_w5100 $(HOST_DIR)/test_dhcp_w5500 $(HOST_DIR)/test_link_dhcp \
                    $(HOST_DIR)/test_w5100_mutate $(HOST_DIR)/test_w5500_mutate \
                    $(HOST_DIR)/test_suit $(HOST_DIR)/test_suit_emit $(HOST_DIR)/test_suit_mutate \
                    $(HOST_DIR)/test_suit_integrated \
                    $(HOST_DIR)/test_suit_integrated_mutate \
                    $(HOST_DIR)/test_suit_integrated_unbound \
                    $(HOST_DIR)/test_hsm_payload \
                    $(HOST_DIR)/test_hsm_engine $(HOST_DIR)/test_hsm_cmd \
                    $(HOST_DIR)/test_storage_2560 $(HOST_DIR)/test_sha256_small \
                    $(HOST_DIR)/test_deviceid_p256 $(HOST_DIR)/test_soft_id_p256 $(HOST_DIR)/test_dice_evidence \
                    $(HOST_DIR)/boot/test_boot_l0_esp32 $(HOST_DIR)/boot/test_boot_l0_esp32_rom \
                    $(HOST_DIR)/boot/test_boot_p256_rom $(HOST_DIR)/boot/test_boot_flash_span \
                    $(HOST_DIR)/boot/test_boot_measure_esp32 $(HOST_DIR)/boot/test_boot_measure_esp32_classic \
                    $(HOST_DIR)/boot/test_boot_l0_esp32_sha \
                    $(HOST_DIR)/test_esp_ota $(HOST_DIR)/test_esp_wire
HOST_EXTRA_BINS += $(HOST_DIR)/test_dhcp_robust_w5100 $(HOST_DIR)/test_dhcp_robust_w5500 \
                   $(HOST_DIR)/test_w5100_robust $(HOST_DIR)/test_w5500_robust

## host-test: build and run the host unit tests
# One part of the `test` stage (mk/stages.mk), which adds the boot suites, the
# board sweep, coverage and the sanitizers.
#
# Each binary runs as its own target, <binary>.run, so `make -j` runs the
# suites side by side rather than one after another (#163). A run never fails
# its own target: it keeps the binary's output in <binary>.out and its exit
# status in <binary>.rc, and prints the output in one piece under the binary's
# name, so parallel suites do not interleave. host-test and boot-test then read
# the statuses and fail naming every binary that failed -- one failure does not
# stop the rest, as it did not in the serial loop. No .run file is ever
# written, so every run runs; a binary named by both host-test and boot-test
# runs once when one make is asked for both. The binaries themselves stay
# prerequisites, or make would take those only a pattern rule names for
# intermediates and delete them after the run.
HOST_TEST_BINS := $(sort $(TEST_BINS) $(HOST_EXTRA_BINS))

$(HOST_DIR)/%.run: $(HOST_DIR)/%
	@./$< > $<.out 2>&1; echo $$? > $<.rc; printf -- '--- %s\n%s\n' '$<' "$$(< $<.out)"

# $(call test-verdict,<binaries>): fail naming each binary whose run failed.
test-verdict = fail=0; for t in $(1); do read -r rc < $$t.rc; \
                 if [ "$$rc" != 0 ]; then echo "FAILED: $$t (exit $$rc)"; fail=1; fi; \
               done; exit $$fail

host-test: $(HOST_TEST_BINS) $(addsuffix .run,$(HOST_TEST_BINS))
	@$(call test-verdict,$(HOST_TEST_BINS))

## test-asan: run the host and boot unit tests under AddressSanitizer and UBSan
# The codec is pointer-heavy parser code reached from untrusted bytes. This is
# where a memory bug will be, so it gets its own gate rather than riding along.
test-asan:
	@$(MAKE) --no-print-directory clean-host
	@# The boot suites too, the way coverage runs them: the bootloader parses an
	@# untrusted image and verifies its signature, and none of that ran under
	@# the sanitizers while only host-test did. One make for both, after
	@# clean-host has returned, so -j runs every suite side by side.
	@$(MAKE) --no-print-directory host-test boot-test EXTRA_CFLAGS="$(SAN_FLAGS)" EXTRA_LDFLAGS="$(SAN_FLAGS)"

## coverage: run the host tests with coverage and enforce the floor
coverage:
	@$(MAKE) --no-print-directory clean-host
	@# The boot suites too, or boot/'s lines are counted and never executed --
	@# which reads as a coverage collapse caused by adding tests. One make for
	@# both, as test-asan runs them.
	@$(MAKE) --no-print-directory host-test boot-test EXTRA_CFLAGS="$(COV_FLAGS)" EXTRA_LDFLAGS="$(COV_FLAGS)"
	@mkdir -p coverage
	@# boot/ is measured on the same terms as src/: the LOGIC is in, the
	@# register-poking is out. boot/hal/avr is excluded for the reason
	@# src/hal/avr is -- a number counting code x86 cannot execute would not mean
	@# anything. The two entry points are out: boot/boot_main.c and src/main.c
	@# are main()s, superloops with no return, linked only into the AVR images.
	@# The crypto tiers are in: sha256.c and the soft, none and ATECC608A
	@# providers and driver, and boot/measure and boot/verify, against the mock
	@# HAL or the software element. So are the auto provider (test_crypto_auto)
	@# and src/crypto/soft/uds_flash.c (test_uds_flash, which links it in place
	@# of test/mock/uds_mock.c). So are boot/tier's cdi_soft.c, erased
	@# (test_boot_cdi_soft) and with its UDS slot patched as the jig patches
	@# it (test_boot_cdi_soft_uds), and cdi_atecc.c on the software element
	@# (test_boot_cdi_atecc). So is BOOT_SIG=p256-soft, the ES256 check on every
	@# tfw-boot row: verify_p256_soft.c against Wycheproof (test_boot_p256_soft)
	@# and with the key slot that ships, p256_key_slot.c, erased
	@# (test_boot_p256_slot). So is src/crypto/sha256_small.c, the boot's hash
	@# (test_sha256_small, host and boot, and the soft tiers' links). So is
	@# src/crypto/deviceid_p256.c, the P-256 DeviceID from a UDS, against RFC
	@# 6979 and pinned vectors (test_deviceid_p256).
	@# src/net/ is in: w5x00.c, w5100.c and w5500.c against the software W5100 /
	@# W5500 (test_w5100, test_w5500 and their _nocs links), netcfg.c through
	@# test_link_eth. So is src/store/spi_nor.c, on the software NOR part
	@# (test_spi_nor and the stager's links: test_ota_stage, test_stage_marked,
	@# test_ota_single, test_ota_info_atecc, test_noboot_ota).
	@# boot_swap.c (the 2560's swap-move and its journal), boot_trial.c,
	@# boot_store.c and boot_handoff.c are in: the OTA rollback path, all of it
	@# over the boot HAL's EEPROM and flash, which the mock drives. So is
	@# boot_prev.c, the 328P store row's PREV fallback (test_boot_prev). So are
	@# the rows with no staging and no swap, boot_stage_none.c and
	@# boot_swap_none.c (test_boot_none_rows, and test_boot_recovery_session).
	@# The host tools the boards' images carry the output of are in: the SUIT
	@# emitter, tools/suit-emit/emit.c (test_suit_emit), and the bench bake's
	@# decision, tools/platform-bake/bake.c (test_platform_bake). Their main.c
	@# files are out, as no host test links one.
	@# boot_cose.c, OTA_AUTH's COSE_Sign1 check, is in (the boot suites that
	@# link it). So are the address providers, dhcp.c (test_dhcp_w5100,
	@# test_dhcp_w5500, test_link_dhcp) and dhcp_none.c (test_link_eth), the
	@# soft root's identities, id_p256.c (test_soft_id_p256) and id_hmac.c
	@# (test_dice_evidence), and the OPTIGA Trust M driver, optiga_i2c.c,
	@# optiga_cmd.c and optiga_crypto.c, on the software part (test_optiga_*).
	@# src/third_party/ is out: vendored code, measured by its own upstream.
	@gcovr --root . --filter 'src/(proto|core|caps)/' --filter 'boards/' \
	       --filter 'boot/boot_(image|measure|ota|platform|policy|session|request|entry|stage|stage_none|swap|swap_none|trial|store|handoff|cose)\.c' \
	       --filter 'boot/recovery/recovery_ota\.c' \
	       --filter 'src/crypto/(sha256|deviceid_p256|soft/provider|soft/uds_flash|soft/id_p256|soft/id_hmac|none/provider|atecc608a/atecc|atecc608a/provider|auto/provider)\.c' \
	       --filter 'src/net/(netcfg|w5100|w5500|w5x00|dhcp|dhcp_none)\.c' \
	       --filter 'src/crypto/optiga/optiga_(i2c|cmd|crypto)\.c' \
	       --filter 'src/store/spi_nor\.c' \
	       --filter 'boot/measure/measure_(soft|atecc608a)\.c' \
	       --filter 'boot/tier/cdi_(soft|atecc)\.c' \
	       --filter 'boot/verify/(verify_ed25519_donna|verify_atecc608a|donna_shim|verify_p256_soft|p256_key_slot)\.c' \
	       --filter 'boot/boot_prev\.c' --filter 'src/crypto/sha256_small\.c' \
	       --filter 'tools/(suit-emit/emit|platform-bake/bake)\.c' \
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
#
# The configuration includes the architecture. A HAL and boot HAL are analysed
# with their own architecture's HAL includes and reference board, never the
# 328P's: under the 328P's header the Cortex-M application region sits at 0,
# and cppcheck rightly reports a null vector table and a bounds check that can
# never fail -- findings about a build that does not exist. Each LINT_ARCH_RUNS
# row is `hal-dir:board-dir`; its HAL, boot HAL and board are excluded from the
# AVR run and analysed in a run of their own.
LINT_CPPCHECK = cppcheck --enable=warning,style,performance,portability \
	          --check-level=exhaustive \
	          --error-exitcode=1 --inline-suppr --quiet \
	          --suppress=missingIncludeSystem \
	          --suppress=unusedFunction \
	          -DTFW_BOARD_HEADER='"board_profile.h"' -DTFW_PROGMEM= \
	          -Iinclude -Isrc
LINT_ARCH_RUNS := cortex-m:mps2-an505
lint-arch-dirs = src/hal/$(1) boot/hal/$(1) boards/$(2)

lint: lint-arch-leak lint-conditionals lint-timestamps lint-test-sleep
	@$(LINT_CPPCHECK) -Isrc/hal/avr -Iboards/atmega328p -Iboot \
	          -i src/third_party \
	          $(foreach r,$(LINT_ARCH_RUNS),$(addprefix -i ,$(call lint-arch-dirs,$(word 1,$(subst :, ,$(r))),$(word 2,$(subst :, ,$(r)))))) \
	          src/ boards/ boot/
	@$(foreach r,$(LINT_ARCH_RUNS),$(LINT_CPPCHECK) \
	          -Isrc/hal/$(word 1,$(subst :, ,$(r))) -Iboot/hal/$(word 1,$(subst :, ,$(r))) \
	          -Iboards/$(word 2,$(subst :, ,$(r))) -Iboot \
	          $(call lint-arch-dirs,$(word 1,$(subst :, ,$(r))),$(word 2,$(subst :, ,$(r)))) &&) true
	@echo "cppcheck clean"

# The grep gates. Two of them used to live only in the CI workflow, which made
# them gates a developer could not run -- and one of them had been failing
# on main for two commits before anyone saw it, because the only place it
# ran was a job nobody reads when the other five are green.
# A finding is printed as `path:line: rule` and never as the matched line
# (ci-standard.md §5: a lint step reports path, line and rule only).
# LINT_GREP_ROOT is the tree they sweep; lint-grep-selftest points it at a
# throwaway copy.
LINT_GREP_ROOT ?= .

## lint-conditionals: no preprocessor conditional outside the four places
# boot/ is swept on the same terms as src/core, src/proto and src/caps, with
# boot/hal/avr excluded exactly as src/hal/avr is. The bootloader's logic is
# the accept/refuse decision for every image the device will ever run, so it
# is the last place in the tree that should have a build that was never the
# build anyone tested. boot/hal/avr earns the exemption for the same reason
# src/hal/avr does and for exactly one conditional: the far-flash read, which
# is required above 64 KB and is a fact about the silicon rather than a
# feature anyone chose.
# The sweep covers every .c and .h under src/, boot/ and tools/. Exempt:
# the per-architecture HAL directories (src/hal/*/, boot/hal/*/, named as
# globs so a second architecture needs no edit here), commands.def, the
# vendored src/third_party (cppcheck skips it for the same reason), and the
# include guard of a header -- an #ifndef of a NAME_H macro, which is not a
# conditional on anything. config.h, progmem.h and the board profiles live
# under include/ and boards/, which this sweep does not enter.
lint-conditionals:
	@cd $(LINT_GREP_ROOT) && hits=$$(grep -rn '^[[:space:]]*#[[:space:]]*if' src boot tools \
	     --include='*.c' --include='*.h' --exclude-dir=third_party \
	     | grep -v 'commands.def' \
	     | grep -v '^src/hal/[^/]*/' | grep -v '^boot/hal/[^/]*/' \
	     | grep -v ':[[:space:]]*#[[:space:]]*ifndef[[:space:]]\+[A-Za-z0-9_]*_H[[:space:]]*$$' || true); \
	if [ -n "$$hits" ]; then \
	  printf '%s\n' "$$hits" | while IFS=: read -r p n _; do echo "$$p:$$n: preprocessor-conditional"; done; \
	  echo "preprocessor conditional outside the whitelist (see above)"; \
	  echo "a default belongs in include/tfw/config.h, where a board can override it"; \
	  exit 1; \
	fi
	@echo "no out-of-whitelist conditionals"

## lint-timestamps: no __DATE__ or __TIME__ in the firmware
lint-timestamps:
	@cd $(LINT_GREP_ROOT) && hits=$$(grep -rn '__DATE__\|__TIME__' src/ boards/ include/ boot/ || true); \
	if [ -n "$$hits" ]; then \
	  printf '%s\n' "$$hits" | while IFS=: read -r p n _; do echo "$$p:$$n: build-timestamp"; done; \
	  echo "a build timestamp would make the image unreproducible, and its"; \
	  echo "measurement uncomputable from source -- which is signed evidence"; \
	  exit 1; \
	fi
	@echo "no build timestamps"

## lint-test-sleep: no wall-clock sleep in a Go or Python test file
# The owner's test standard (m655, #163): a test waits on a condition, never on
# the clock. A sleep is either too short on a loaded runner, and the test
# flakes, or too long everywhere else, and the suite crawls; either way it
# hides the condition the test should have named. Only test files are swept:
# production code may sleep where the delay is the protocol (internal/peer's
# reset pulse, scripts/deps-check.py's backoff, whose test mocks the sleep).
lint-test-sleep:
	@cd $(LINT_GREP_ROOT) && hits=$$(grep -rnE 'time\.(Sleep|sleep\()' . \
	     --include='*_test.go' --include='test_*.py' --include='*_test.py' \
	     --exclude-dir=.git --exclude-dir=build --exclude-dir=coverage \
	     --exclude-dir=third_party --exclude-dir=vendor || true); \
	if [ -n "$$hits" ]; then \
	  printf '%s\n' "$$hits" | while IFS=: read -r p n _; do echo "$${p#./}:$$n: test-sleep"; done; \
	  echo "a test waits on a condition, never on the clock (m655)"; \
	  exit 1; \
	fi
	@echo "no sleeps in test files"

## lint-grep-selftest: the grep gates fail on a plant and print path and line only
# Plants go into a throwaway tree, never into this one. Each plant must fail its
# gate with `path:line: ` and without the planted text; an include guard, an
# exempt HAL directory and a sleep outside a test file must not.
lint-grep-selftest:
	@t=$$(mktemp -d) && trap 'rm -rf "$$t"' EXIT && \
	mkdir -p $$t/src/core $$t/src/hal/avr $$t/boot $$t/tools $$t/boards $$t/include $$t/sdk $$t/scripts && \
	printf '#ifndef TFW_OK_H\n#define TFW_OK_H\n#endif\n' > $$t/src/core/ok.h && \
	printf '#if 1\n#endif\n' > $$t/src/hal/avr/exempt.c && \
	printf 'time.Sleep(1)\n' > $$t/sdk/exempt.go && \
	$(MAKE) --no-print-directory lint-conditionals lint-timestamps lint-test-sleep LINT_GREP_ROOT=$$t > /dev/null || \
	  { echo "selftest: clean tree was rejected"; exit 1; }; \
	for plant in 'conditionals:src/core/plant.c:#if TFW_PLANTED_TEXT' \
	             'timestamps:src/core/plant.c:const char *d = __DATE__; /* TFW_PLANTED_TEXT */' \
	             'test-sleep:sdk/plant_test.go:time.Sleep(1) // TFW_PLANTED_TEXT' \
	             'test-sleep:scripts/test_plant.py:time.sleep(1)  # TFW_PLANTED_TEXT'; do \
	  gate=$${plant%%:*}; rest=$${plant#*:}; f=$${rest%%:*}; printf '%s\n' "$${rest#*:}" > $$t/$$f; \
	  if out=$$($(MAKE) --no-print-directory lint-$$gate LINT_GREP_ROOT=$$t 2>&1); then \
	    echo "selftest: lint-$$gate passed a plant"; exit 1; \
	  fi; \
	  case $$out in *TFW_PLANTED_TEXT*) echo "selftest: lint-$$gate printed the matched text"; exit 1 ;; esac; \
	  case $$out in *"$$f:1: "*) ;; *) echo "selftest: lint-$$gate did not print path:line"; exit 1 ;; esac; \
	  rm -f $$t/$$f; \
	done; \
	echo "grep lint gates reject every plant and print path and line only"

## lint-arch-leak: AVR and Cortex-M must not leak out of their directories
# The tree is arch-neutral only if nothing outside the AVR's own directories
# names the AVR: <avr/...> headers, F_CPU, MCUSR, or a bare PROGMEM (TFW_PROGMEM
# is the neutral spelling and is not matched -- the pattern wants a non-identifier
# character on each side of the word). Exempt, and nothing else: src/hal/avr/,
# boot/hal/avr/, boards/ and mk/arch/avr.mk (neither of which is swept) and
# include/tfw/progmem.h, the one toolchain shim. A real leak is moved behind the
# HAL or progmem.h, never added to this list.
#
# The Cortex-M core (#161) the same way, outside src/hal/cortex-m/ and
# boot/hal/cortex-m/: its register header cm_regs.h and what it defines (the
# tfw_cm_* helpers, TFW_CM_REG and the SCB, SysTick and NVIC registers), the
# CMSIS headers and names a port might reach for (core_cm*.h, cmsis_*.h, SCB,
# NVIC, SysTick, __DSB, __WFI, __disable_irq), and an Arm system instruction in
# an asm string. The board's own constants, TFW_CM_PLATFORM_BASE among them,
# are board data and not matched.
#
# Only code is matched: each file goes through the preprocessor in
# -fpreprocessed -dD mode first, which removes comments exactly (it knows a
# "//" inside a string from one outside) and expands nothing, so prose such as
# "MCUSR is already clear" is untouched in the source and invisible to the gate.
# A file the compiler cannot strip fails the gate instead of passing it.
ARCH_LEAK_ROOT ?= .
ARCH_LEAK_DIRS := src boot tools include test
ARCH_LEAK_PAT  := <avr/|(^|[^A-Za-z0-9_])(F_CPU|MCUSR|PROGMEM)([^A-Za-z0-9_]|$$)
ARCH_LEAK_CM_PAT := cm_regs\.h|<(core_cm[0-9a-z]*|cmsis_[0-9a-z_]*)\.h>|(^|[^A-Za-z0-9_])(tfw_cm_[a-z_]+|TFW_CM_(REG|VTOR|AIRCR[A-Z_]*|SYST_[A-Z_]+|NVIC_[A-Z]+)|SCB|NVIC|SysTick(_[A-Za-z]+)?|__(DSB|ISB|DMB|WFI|WFE)|__(disable|enable)_irq)([^A-Za-z0-9_]|$$)|"(dsb|isb|dmb|wfi|wfe|cpsi[de]|msr|mrs)[ \\"]
lint-arch-leak:
	@cd $(ARCH_LEAK_ROOT) && fail=0; \
	for f in $$(find $(ARCH_LEAK_DIRS) -type f \( -name '*.c' -o -name '*.h' \) \
	            -not -path 'include/tfw/progmem.h' | sort); do \
	  code=$$($(CC) -w -fpreprocessed -dD -E -P "$$f") || { \
	    echo "arch-leak gate: cannot strip comments from $$f"; exit 1; }; \
	  hits=; \
	  case $$f in src/hal/avr/*|boot/hal/avr/*) ;; \
	    *) hits=$$(printf '%s\n' "$$code" | grep -En '$(ARCH_LEAK_PAT)' || true) ;; esac; \
	  case $$f in src/hal/cortex-m/*|boot/hal/cortex-m/*) ;; \
	    *) hits="$$hits$$(printf '\n'; printf '%s\n' "$$code" | grep -En '$(ARCH_LEAK_CM_PAT)' || true)" ;; esac; \
	  hits=$$(printf '%s\n' "$$hits" | grep -v '^$$' || true); \
	  if [ -n "$$hits" ]; then \
	    printf '%s\n' "$$hits" | while IFS=: read -r n _; do echo "$$f:$$n: arch-leak"; done; fail=1; \
	  fi; \
	done; \
	if [ $$fail -ne 0 ]; then \
	  echo "an architecture leaked out of its directories (see above)"; \
	  echo "move it behind include/tfw/hal.h or include/tfw/progmem.h"; \
	  exit 1; \
	fi
	@echo "no arch leaks"

## lint-arch-leak-selftest: the arch-leak gate fails on a plant, in a temp copy
# Plants go into a throwaway tree, never into this one. Each leak must fail the
# gate; prose in a comment and the exempt directories must not.
lint-arch-leak-selftest:
	@t=$$(mktemp -d) && trap 'rm -rf "$$t"' EXIT && \
	mkdir -p $(addprefix $$t/,$(ARCH_LEAK_DIRS)) $$t/src/crypto $$t/src/hal/avr $$t/src/hal/cortex-m && \
	printf '/* MCUSR is already clear; F_CPU, <avr/io.h>, PROGMEM, tfw_cm_dsb, SysTick, "wfi" */\n// MCUSR\nint ok;\nconst int k TFW_PROGMEM = 0;\nconst char *s = "msrp";\n' > $$t/src/crypto/ok.c && \
	printf '#include <avr/io.h>\nint f_cpu;\n' > $$t/src/hal/avr/exempt.h && \
	printf '#include "cm_regs.h"\nvoid f(void) { tfw_cm_dsb(); __asm__ volatile ("wfi"); }\n' > $$t/src/hal/cortex-m/exempt.c && \
	$(MAKE) --no-print-directory lint-arch-leak ARCH_LEAK_ROOT=$$t > /dev/null || \
	  { echo "selftest: clean tree was rejected"; exit 1; }; \
	for leak in '#include <avr/io.h>' 'int x = F_CPU;' 'int x = MCUSR;' 'const int x PROGMEM = 0;' \
	            '#include "cm_regs.h"' 'void f(void) { tfw_cm_irq_off(); }' 'int x = TFW_CM_SYST_CVR;' \
	            'void f(void) { __WFI(); }' 'void f(void) { __asm__ volatile ("dsb 0xF"); }'; do \
	  printf '%s\n' "$$leak" > $$t/src/crypto/plant.c; \
	  if out=$$($(MAKE) --no-print-directory lint-arch-leak ARCH_LEAK_ROOT=$$t 2>&1); then \
	    echo "selftest: gate passed a planted: $$leak"; exit 1; \
	  fi; \
	  case $$out in *"$$leak"*) echo "selftest: gate printed the matched text: $$leak"; exit 1 ;; esac; \
	  case $$out in *'src/crypto/plant.c:1: arch-leak'*) ;; *) echo "selftest: gate did not print path:line: $$leak"; exit 1 ;; esac; \
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
#   tfwsim-prov         element tier as a provisioning jig image: the same, plus
#                      the PROV_* commands, so tfwprov provisions its element
#                      over the wire.
#
# `make tfwsim CRYPTO=atecc608a` is `make tfwsim-atecc608a`, so the tier is named
# the way it is for `make firmware`.
TFWSIM_COMMON := tools/tfwsim/main.c tools/tfwsim/tier.h tools/tfwsim/sim_boot.h

# Both simulators carry the Ethernet image's link, with a host TCP socket as
# the controller (tools/tfwsim/eth_socket.c), off until `--eth`; without it the
# socket answers as an absent controller and the serial behaviour is the serial
# image's. -DTFW_FEATURE_ETHERNET is NOT passed: all it does is reserve the
# shield's SPI and select pins, and the host's socket sits on no pin -- a
# simulator that refused D10-D13 would only break the GPIO suites that use them.
TFWSIM_LINK_SRCS := src/proto/link_eth.c src/net/netcfg.c src/net/dhcp_none.c tools/tfwsim/eth_socket.c
# Both simulators also carry the OTA stager (issue #75), as an ETH=1 image does,
# with test/model/spi_nor_model.c as the external store on its select. Its
# tfw_hal_spi_xfer overrides the mock's weak one and hands every other select
# back to it.
TFWSIM_STAGE_SRCS := src/core/ota_stage.c src/core/ota_prev.c src/store/spi_nor.c test/model/spi_nor_model.c test/model/spi_nor_hal.c \
                     src/core/ota_activate.c src/core/ota_manifest_none.c src/core/ota_auth.c
TFWSIM_STAGE_DEFS := -DTFW_FEATURE_OTA_STAGE=1 -DTFW_FEATURE_OTA_ACTIVATE=1 -Itest/model
# And decision 8's OTA_INFO, OTA_ACTIVATE and OTA_CONFIRM (#77), as a 2560
# stage image links them. The platform block is the mock's EEPROM. Only the
# soft simulator puts tfw-boot behind it, and only given --boot-key (below);
# the element, prov and auto simulators keep the trial stub, and nothing
# resets into a copy or a rollback -- that leg is simavr's (make integration-test-boot).
TFWSIM_STAGE_SRCS += src/core/ota_ctl.c boot/boot_trial.c boot/boot_image.c src/core/ota_trial_none.c \
                     src/core/ota_floor_eeprom.c
TFWSIM_STAGE_DEFS += -DTFW_FEATURE_OTA_CTL=1
TFWSIM_LIB_SRCS  := $(filter-out src/proto/link_serial.c,$(HOST_STAGE_LIB_SRCS)) $(TFWSIM_LINK_SRCS) \
                    $(TFWSIM_STAGE_SRCS)

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
                                   test/mock/boot_soft_mock.c \
                                   $(SOFT_ID_SRCS) $(HOST_DIR)/uECC.o \
                                   src/core/auth_session_none.c src/core/app_runner.c \
                                   test/mock/app_mock.c src/core/ota_floor_eeprom.c, \
                                   $(TFWSIM_LIB_SRCS)) \
                      src/core/auth_session.c src/core/ota_floor_atecc.c \
                      src/crypto/atecc608a/atecc.c src/crypto/atecc608a/provider.c \
                      $(ELEMENT_MODEL) $(APP_RUNNER_SRCS)

# The simulator links the runner an image would (mk/app.mk): the real one with
# APP_DIR's application, or the empty one. Never the recording mock, which is
# the tests'.
#
# Nor the alias certificate: a CRYPTO=soft image compiles ATTEST_CERT out and
# links none of its files (mk/firmware.mk), so it answers TFW_ERR_UNKNOWN_CMD,
# and the soft simulator must answer what the soft image does. HOST_CFLAGS'
# define comes off its compile and its application's, and the files off its link.
TFWSIM_SOFT_OFF  := -DTFW_FEATURE_ALIAS_CERT=1
TFWSIM_SOFT_SRCS := $(filter-out src/core/app_runner.c test/mock/app_mock.c src/core/ota_trial_none.c \
                                 src/caps/alias.c src/caps/tmpl.c src/caps/uccs.c src/caps/csr.c, \
                                 $(TFWSIM_LIB_SRCS)) \
                    $(APP_RUNNER_SRCS) src/core/ota_trial.c src/core/ota_trial_block.c

# tfw-boot itself in front of the soft simulator (#52), as a 328P stage image's
# tfw-boot is built (mk/boot.mk: store on, OTA over the UART off, the soft tier,
# P-256 in software) with boot/hal/host for its HAL and tools/tfwsim/boot_host.c
# to run it -- at power on and at every tfw_hal_reset, which the link wraps. The
# key slot is boot_host.c's, filled from --boot-key; p256_key_slot.c is not
# linked. sha256.c is the firmware's, the same API and digest as the boot's
# sha256_small.c. boot_main.c is its own object, its main() renamed, so the one
# cc command keeps one main. The element and auto simulators have no bootloader
# (tools/tfwsim/boot_none.c) and keep the trial stub.
TFWSIM_BOOT_SRCS := tools/tfwsim/boot_host.c boot/hal/host/boot_hal_host.c \
                    boot/boot_store.c boot/boot_entry.c boot/boot_policy.c boot/boot_stage.c \
                    boot/boot_cose.c boot/boot_swap_none.c boot/tier/cdi_soft.c \
                    boot/measure/measure_soft.c boot/verify/verify_p256_soft.c
TFWSIM_SOFT_SRCS += $(TFWSIM_BOOT_SRCS)
TFWSIM_BOOT_MAIN := $(HOST_DIR)/tfwsim-boot/boot_main.o
TFWSIM_BOOT_DEFS := -Iboot/hal/host -I$(UECC_DIR)
$(TFWSIM_BOOT_MAIN): boot/boot_main.c | $(HOST_DIR)
	@mkdir -p $(dir $@)
	@$(HOST_CC) $(filter-out $(TFWSIM_SOFT_OFF),$(HOST_CFLAGS)) $(APP_DEFS) $(TFWSIM_STAGE_DEFS) $(TFWSIM_BOOT_DEFS) \
	   -Dmain=tfwsim_boot_main -c -o $@ $<

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
	@$(CC) $(filter-out $(TFWSIM_SOFT_OFF),$(APP_HOST_CFLAGS)) -MMD -MP -c -o $@ $<

$(APP_HOST_ELEMENT_OBJS): $(APP_HOST_ELEMENT_DIR)/%.o: $(APP_ABS)/%.c
	@mkdir -p $(dir $@)
	@$(CC) $(TFWSIM_ELEMENT_INC) $(APP_HOST_CFLAGS) -MMD -MP -c -o $@ $<

-include $(APP_HOST_OBJS:.o=.d) $(APP_HOST_ELEMENT_OBJS:.o=.d)
endif

## tfwsim: build the firmware as a host binary speaking the protocol on stdio
tfwsim: $(HOST_DIR)/tfwsim$(if $(filter atecc608a,$(CRYPTO)),-atecc608a)$(APP_SUFFIX)
$(HOST_DIR)/tfwsim$(APP_SUFFIX): $(TFWSIM_COMMON) tools/tfwsim/tier_soft.c $(TFWSIM_SOFT_SRCS) $(TFWSIM_BOOT_MAIN) \
        $(APP_HOST_OBJS) | $(HOST_DIR)
	@$(HOST_CC) $(filter-out $(TFWSIM_SOFT_OFF),$(HOST_CFLAGS)) $(APP_DEFS) $(TFWSIM_STAGE_DEFS) $(TFWSIM_BOOT_DEFS) $(EXTRA_CFLAGS) -Itools/tfwsim -o $@ \
	   tools/tfwsim/main.c tools/tfwsim/tier_soft.c $(TFWSIM_SOFT_SRCS) $(TFWSIM_BOOT_MAIN) \
	   $(APP_HOST_OBJS) -Wl,--wrap=tfw_hal_reset $(EXTRA_LDFLAGS)
	@echo "built $@"

# The HSM self-defence channel (docs/hsm-self-defence/ slice 4) is in this
# simulator alone: the envelope's signature is verified through the element
# tier, and the soft tier has none. The engine, its SUIT reader and the
# channel's handlers, behind tools/tfwsim/hsm_port.c, whose key is the literal
# tenant key the vectors are signed with. Not in TFWSIM_ELEMENT_SRCS, so the
# jig and auto simulators answer HSM_* with TFW_ERR_UNKNOWN_CMD as before.
TFWSIM_HSM_SRCS := src/core/suit.c src/core/suit_integrated.c src/core/hsm_payload.c \
                   src/core/hsm_engine.c src/core/hsm_cmd.c tools/tfwsim/hsm_port.c
TFWSIM_HSM_DEFS := -DTFW_FEATURE_HSM=1

## tfwsim-atecc608a: build the element-tier simulator (real ATECC608A driver, software element)
tfwsim-atecc608a: $(HOST_DIR)/tfwsim-atecc608a$(APP_SUFFIX)
$(HOST_DIR)/tfwsim-atecc608a$(APP_SUFFIX): $(TFWSIM_COMMON) tools/tfwsim/tier_atecc608a.c $(TFWSIM_ELEMENT_SRCS) $(TFWSIM_HSM_SRCS) $(APP_HOST_ELEMENT_OBJS) | $(HOST_DIR)
	@$(HOST_CC) $(TFWSIM_ELEMENT_INC) $(HOST_CFLAGS) $(APP_DEFS) $(TFWSIM_STAGE_DEFS) $(TFWSIM_HSM_DEFS) $(EXTRA_CFLAGS) \
	   -Itools/tfwsim $(MODEL_INC) -o $@ \
	   tools/tfwsim/main.c tools/tfwsim/tier_atecc608a.c tools/tfwsim/boot_none.c $(TFWSIM_ELEMENT_SRCS) $(TFWSIM_HSM_SRCS) $(APP_HOST_ELEMENT_OBJS) $(EXTRA_LDFLAGS)
	@echo "built $@"

# The element tier as a provisioning JIG image (issue #53): tfwsim-atecc608a's
# object list plus src/caps/provision.c, its element backend
# src/caps/provision_atecc.c and its define, selected exactly as
# mk/firmware.mk's PROVISION=1 selects them (PROV_SRCS, PROV_DEFS), and
# tools/tfwsim/reboot.c, which makes a RESET restart the firmware in place with
# the EEPROM and the element kept. Started without --provision-element its
# element is a blank part, so `tfwprov --commit` runs the real PROV_* sequence
# against the model, and after a RESET ATTEST signs with the key PROV_GENKEY
# made. Never a template for an image: a device that answers PROV_* has no
# identity worth attesting to.
TFWSIM_PROV_SRCS := $(TFWSIM_ELEMENT_SRCS) src/caps/provision.c src/caps/provision_atecc.c tools/tfwsim/reboot.c
TFWSIM_PROV_DEFS := -DTFW_FEATURE_PROVISION=1

## tfwsim-prov: build the element-tier simulator as a provisioning jig image (PROV_* commands, blank element)
tfwsim-prov: $(HOST_DIR)/tfwsim-prov$(APP_SUFFIX)
$(HOST_DIR)/tfwsim-prov$(APP_SUFFIX): $(TFWSIM_COMMON) tools/tfwsim/tier_atecc608a.c $(TFWSIM_PROV_SRCS) $(APP_HOST_ELEMENT_OBJS) | $(HOST_DIR)
	@$(HOST_CC) $(TFWSIM_ELEMENT_INC) $(HOST_CFLAGS) $(APP_DEFS) $(TFWSIM_STAGE_DEFS) $(TFWSIM_PROV_DEFS) $(EXTRA_CFLAGS) \
	   -Itools/tfwsim $(MODEL_INC) -o $@ \
	   tools/tfwsim/main.c tools/tfwsim/tier_atecc608a.c tools/tfwsim/boot_none.c $(TFWSIM_PROV_SRCS) $(APP_HOST_ELEMENT_OBJS) $(EXTRA_LDFLAGS)
	@echo "built $@"

# CRYPTO=auto (issue #130), the Mega's default tier: the element when one
# answers, else the soft root, linked as mk/firmware.mk links the image -- both
# providers, both floors and the element's auth session renamed, behind the
# dispatcher, the auto floor and the auto gate. The bus is the element model,
# as in tfwsim-atecc608a, with that tier's options; --no-element takes the part
# off the bus, so one binary probes either way at startup as the board does.
# The renamed objects have their own directory, built with the element
# overlay profile and the stage defines the link uses, and, like unity.o,
# WITHOUT $(EXTRA_CFLAGS): they outlive one invocation.
TFWSIM_AUTO_DIR  := $(HOST_DIR)/tfwsim-auto.o
TFWSIM_AUTO_OBJS := $(TFWSIM_AUTO_DIR)/crypto_soft.o $(TFWSIM_AUTO_DIR)/crypto_element.o \
                    $(TFWSIM_AUTO_DIR)/floor_soft.o $(TFWSIM_AUTO_DIR)/floor_element.o \
                    $(TFWSIM_AUTO_DIR)/auth_element.o
TFWSIM_AUTO_SRCS := $(filter-out src/crypto/atecc608a/provider.c src/core/auth_session.c \
                                 src/core/ota_floor_atecc.c,$(TFWSIM_ELEMENT_SRCS)) \
                    src/crypto/auto/provider.c src/core/auth_session_auto.c \
                    src/core/ota_floor_auto.c test/mock/uds_mock.c $(SOFT_ID_SRCS)
TFWSIM_AUTO_CC   = @mkdir -p $(TFWSIM_AUTO_DIR) && $(HOST_CC) $(TFWSIM_ELEMENT_INC) $(HOST_CFLAGS) \
                   $(TFWSIM_STAGE_DEFS) -Itools/tfwsim $(MODEL_INC)
$(TFWSIM_AUTO_DIR)/crypto_%.o:
	$(TFWSIM_AUTO_CC) $(call CRYPTO_RENAME,$*) -c -o $@ $<
$(TFWSIM_AUTO_DIR)/crypto_soft.o: src/crypto/soft/provider.c
$(TFWSIM_AUTO_DIR)/crypto_element.o: src/crypto/atecc608a/provider.c
$(TFWSIM_AUTO_DIR)/floor_%.o:
	$(TFWSIM_AUTO_CC) $(call FLOOR_RENAME,$*) -c -o $@ $<
$(TFWSIM_AUTO_DIR)/floor_soft.o: src/core/ota_floor_eeprom.c
$(TFWSIM_AUTO_DIR)/floor_element.o: src/core/ota_floor_atecc.c
$(TFWSIM_AUTO_DIR)/auth_element.o: src/core/auth_session.c
	$(TFWSIM_AUTO_CC) $(AUTH_RENAME) -c -o $@ $<

## tfwsim-auto: build the CRYPTO=auto simulator (element if it answers, else soft; --no-element)
tfwsim-auto: $(HOST_DIR)/tfwsim-auto$(APP_SUFFIX)
$(HOST_DIR)/tfwsim-auto$(APP_SUFFIX): $(TFWSIM_COMMON) tools/tfwsim/tier_atecc608a.c $(TFWSIM_AUTO_SRCS) $(TFWSIM_AUTO_OBJS) $(APP_HOST_ELEMENT_OBJS) | $(HOST_DIR)
	@$(HOST_CC) $(TFWSIM_ELEMENT_INC) $(HOST_CFLAGS) $(APP_DEFS) $(TFWSIM_STAGE_DEFS) $(EXTRA_CFLAGS) \
	   -Itools/tfwsim $(MODEL_INC) -o $@ \
	   tools/tfwsim/main.c tools/tfwsim/tier_atecc608a.c tools/tfwsim/boot_none.c $(TFWSIM_AUTO_SRCS) $(TFWSIM_AUTO_OBJS) $(APP_HOST_ELEMENT_OBJS) $(EXTRA_LDFLAGS)
	@echo "built $@"

clean-host:
	@rm -rf $(HOST_DIR)

## host-dir: print the directory the host build writes to, for the flags given (used by scripts/test_build_dirs.py)
host-dir:
	@echo $(HOST_DIR)

# The .d files mk/ccdeps.sh wrote. After every rule in the file so nothing here
# becomes the default goal; mk/boot.mk's land in $(HOST_DIR)/boot and are read
# by the same line. -include, because the first build has none.
-include $(wildcard $(HOST_DIR)/*.d $(HOST_DIR)/boot/*.d $(HOST_DIR)/test-app-example/*.d)

.PHONY: host-test test-asan coverage clean-host host-dir tfwsim tfwsim-atecc608a tfwsim-prov tfwsim-auto lint \
        lint-arch-leak lint-arch-leak-selftest lint-conditionals lint-timestamps lint-test-sleep lint-grep-selftest

# ---------------------------------------------------------------- board sweep
#
# Every board's descriptor, each compiled against ITS OWN profile.
#
# The ordinary `make host-test` configures the host build for one board, so it proves
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

# The CAPS handlers on an ETH=1 pin table (#75): the host library as it is, with
# the shield's pins D10-D13 marked in use, so tone's refusal of D11 runs. It
# stays in TEST_SRCS; this explicit rule takes the place of the pattern rule.
$(HOST_DIR)/test_caps_eth: test/unit/test_caps_eth.c $(HOST_LIB_SRCS) $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(HOST_APP_DEFS) $(EXTRA_CFLAGS) -DTFW_FEATURE_ETHERNET=1 -o $@ $^ $(EXTRA_LDFLAGS)

# The ATECC608A driver's I2C transactions mutated (#163): atecc.c alone, with the
# bus faked in the suite, so no mock HAL and no software element. It stays in
# TEST_SRCS; this explicit rule takes the place of the pattern rule.
$(HOST_DIR)/test_atecc_mutate: test/unit/test_atecc_mutate.c src/crypto/atecc608a/atecc.c \
                               src/core/secure_zero.c \
                               $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) -Isrc/crypto/atecc608a -o $@ $^ $(EXTRA_LDFLAGS)

# The OPTIGA Trust M transport against recorded frames (row optiga-trust-m):
# optiga_i2c.c alone, with the bus faked in the suite. It stays in TEST_SRCS;
# this explicit rule takes the place of the pattern rule.
$(HOST_DIR)/test_optiga_i2c: test/unit/test_optiga_i2c.c src/crypto/optiga/optiga_i2c.c \
                             $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) -Isrc/crypto/optiga -o $@ $^ $(EXTRA_LDFLAGS)

# The OPTIGA Trust M command layer (row optiga-trust-m): optiga_cmd.c alone,
# the transport faked in the suite at the APDU, so each command's bytes are
# the manual's table. CRYPTO=optiga links it (mk/firmware.mk). It stays in
# TEST_SRCS; this explicit rule takes the place of the pattern rule.
$(HOST_DIR)/test_optiga_cmd: test/unit/test_optiga_cmd.c src/crypto/optiga/optiga_cmd.c \
                             $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) -Isrc/crypto/optiga -o $@ $^ $(EXTRA_LDFLAGS)

# The command layer on the real transport, the bus faked in the suite: the
# 0xE0E0 certificate read as frames. It stays in TEST_SRCS; this explicit
# rule takes the place of the pattern rule.
$(HOST_DIR)/test_optiga_cmd_i2c: test/unit/test_optiga_cmd_i2c.c src/crypto/optiga/optiga_cmd.c \
                                 src/crypto/optiga/optiga_i2c.c \
                                 $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) -Isrc/crypto/optiga -o $@ $^ $(EXTRA_LDFLAGS)

# The crypto tier on the real command layer, the transport faked in the suite.
# CRYPTO=optiga links it, a tier only the 2560's measurement rows build
# (mk/firmware.mk); there is no tfwsim for it, the mock HAL's bus answering no
# OPTIGA. It stays in TEST_SRCS; this explicit rule takes the place of the
# pattern rule.
$(HOST_DIR)/test_optiga_crypto: test/unit/test_optiga_crypto.c src/crypto/optiga/optiga_crypto.c \
                                src/crypto/optiga/optiga_cmd.c src/crypto/sha256.c \
                                $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) -Isrc/crypto/optiga -o $@ $^ $(EXTRA_LDFLAGS)

# I2C_XFER on a build whose object list carries the OPTIGA transport: bus.c
# with optiga_i2c.c in place of bus_held_none.c, the bus faked in the suite.
# It stays in TEST_SRCS; this explicit rule takes the place of the pattern rule.
$(HOST_DIR)/test_bus_optiga: test/unit/test_bus_optiga.c src/caps/bus.c \
                             src/crypto/optiga/optiga_i2c.c \
                             src/core/reader.c src/core/writer.c \
                             src/core/board_lookup.c boards/$(HOST_BOARD)/board.c \
                             $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) -Isrc/crypto/optiga -o $@ $^ $(EXTRA_LDFLAGS)

# A trial whose platform block leaves TRIAL before its deadline (#163), on
# test_ota_trial's link. It stays in TEST_SRCS; this explicit rule takes the
# place of the pattern rule.
$(HOST_DIR)/test_ota_trial_leave: test/unit/test_ota_trial_leave.c src/core/ota_trial.c src/core/ota_trial_block.c \
                        test/mock/runner_fake.c \
                        boot/boot_platform.c test/mock/hal_mock.c src/core/handoff.c src/core/handoff_seed.c \
                        src/core/secure_zero.c boot/boot_handoff.c \
                        src/proto/crc16.c src/proto/crc16_update.c \
                        $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The link a DHCP=1 image runs: link_eth.c over the DHCP client, not its stand-in,
# with both sockets faked at include/tfw/eth.h and the lease on the mock clock.
$(HOST_DIR)/test_link_dhcp: test/unit/test_link_dhcp.c \
                        $(filter-out src/proto/link_serial.c,$(HOST_LIB_SRCS)) \
                        src/proto/link_eth.c src/net/netcfg.c src/net/dhcp.c \
                        $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(HOST_CFLAGS) $(HOST_APP_DEFS) $(EXTRA_CFLAGS) -DTFW_FEATURE_ETHERNET=1 -o $@ $^ $(EXTRA_LDFLAGS)
