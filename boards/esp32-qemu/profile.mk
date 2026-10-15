# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# Espressif's QEMU, the stand-in for the ESP32-P4 board until it arrives.
#
# One board, two emulated machines, chosen by ESP_TARGET:
#
#   esp32c3  RISC-V, like the P4, and the machine that emulates the HMAC and
#            DS peripherals: secure boot v2, the eFuse/HMAC CDI, the DS-held
#            DeviceID and the wire over OpenCores Ethernet. The default.
#   esp32    the original Xtensa ESP32: secure boot v2 and the wire over
#            OpenCores Ethernet. It has no HMAC and no DS, so it builds the
#            none tier and carries no identity.
#
# Built by `make esp32-build BOARD=esp32-qemu [ESP_TARGET=esp32]` and tested by
# `make esp32-qemu-test`; never by `make firmware`, and never in `make ci`,
# whose image has no ESP-IDF. docs/multi-arch/adr-esp-idf-integration.md.
ARCH         := esp32
IMAGE_TARGET := esp32-build
ESP_TARGETS  := esp32c3 esp32
ESP_TARGET   ?= esp32c3
QEMU_MACHINE  = $(ESP_TARGET)

# The tier follows the machine: only the C3 has the peripherals hmac-ds needs.
# board-sweep compiles the descriptor with the last tier's feature flags; both
# carry none.
BOARD_TIERS  := hmac-ds none
CRYPTO       ?= $(if $(filter esp32,$(ESP_TARGET)),none,hmac-ds)
