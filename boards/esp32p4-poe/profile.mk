# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# The ESP32-P4 on a PoE Ethernet board, with an ESP32-C6 co-processor this
# firmware holds in reset. Built by `make esp32-build BOARD=esp32p4-poe`;
# never by `make firmware` and never in `make ci`, whose image has no ESP-IDF.
# Not yet run on hardware: the board has not arrived, and the port is
# exercised on Espressif's QEMU (boards/esp32-qemu) until it does.
ARCH         := esp32
IMAGE_TARGET := esp32-build
ESP_TARGETS  := esp32p4
ESP_TARGET   := esp32p4
BOARD_TIERS  := hmac-ds
CRYPTO       ?= hmac-ds

# The factory state, read 2026-10-17 before the project's first write
# (docs/development/bench/esp32-p4.md); `make board-restore` writes it back.
FACTORY_FLASH_BYTES  := 33554432
FACTORY_FLASH_SHA256 := c2d4ac0f25246b053d4d25f62580578a5f2b2a37002f7cda7a9d3a548702fde0
