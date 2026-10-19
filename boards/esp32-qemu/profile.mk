# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# Espressif's QEMU, the stand-in for the ESP32-P4 board until it arrives.
#
# One board, two emulated machines, chosen by ESP_TARGET:
#
#   esp32c3  RISC-V, like the P4, and the machine that emulates the HMAC
#            peripheral: secure boot v2, the eFuse/HMAC CDI, the P-256
#            DeviceID derived from the same UDS and the wire over OpenCores
#            Ethernet. The default.
#   esp32    the original Xtensa ESP32: secure boot v2 and the wire over
#            OpenCores Ethernet. It has no HMAC, so it builds the none tier
#            and carries no identity.
#
# Built by `make esp32-build BOARD=esp32-qemu [ESP_TARGET=esp32]` and tested by
# `make esp32-qemu-test`; never by `make firmware`, and never in `make ci`,
# whose image has no ESP-IDF. docs/multi-arch/adr-esp-idf-integration.md.
ARCH         := esp32
IMAGE_TARGET := esp32-build
ESP_TARGETS  := esp32c3 esp32
ESP_TARGET   ?= esp32c3
QEMU_MACHINE  = $(ESP_TARGET)

# The tier follows the machine: only the C3 has the HMAC peripheral hmac-p256
# needs. board-sweep compiles the descriptor with the last tier's feature
# flags; both carry none.
BOARD_TIERS  := hmac-p256 none
CRYPTO       ?= $(if $(filter esp32,$(ESP_TARGET)),none,hmac-p256)

# Neither machine's ROM has ECDSA, so layer 0 verifies the ES256 trailer with
# the AVR rows' p256-soft over ESP-IDF's micro-ecc.
ESP_L0_VERIFY := p256-soft

# ESP-IDF's anti-rollback on the C3 machine only, where the test's eFuse is a
# file QEMU writes: OTA_CONFIRM raises its secure-version floor and nothing is
# burned on any chip. The P4 board never sets this; which releases advance a
# real floor is the owner's decision. mk/arch/esp32-targets.mk turns it into
# CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK and the app's secure version.
ESP_ANTI_ROLLBACK_TARGETS := esp32c3
