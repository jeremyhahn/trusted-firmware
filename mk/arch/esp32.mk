# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# The ESP32 architecture: ESP-IDF builds the image.

# Pins
ESP_IDF_VERSION      := v5.5.5
ESP_QEMU_VERSION     := esp_develop_9.2.2_20260417

# The mk/firmware.mk contract.
ARCH_ESP_REFUSE = $(error $(BOARD) is an ESP-IDF board: build it with 'make esp32-build BOARD=$(BOARD)', not with this target)

ARCH_CC      = $(ARCH_ESP_REFUSE)
ARCH_OBJCOPY = $(ARCH_ESP_REFUSE)
ARCH_SIZE    = $(ARCH_ESP_REFUSE)
ARCH_NM      = $(ARCH_ESP_REFUSE)
ARCH_RAM_USED = $(ARCH_ESP_REFUSE)
ARCH_LIBC    := newlib (ESP-IDF $(ESP_IDF_VERSION))

ARCH_MACH_CFLAGS :=
ARCH_OPT_CFLAGS  :=
ARCH_LDFLAGS     :=
ARCH_FW_PLACEMENT_LDFLAGS   :=
ARCH_BOOT_OPT_CFLAGS :=
ARCH_UECC_CFLAGS :=
ARCH_BOOT_PLACEMENT_LDFLAGS :=
ARCH_BOOT_START_SRC :=
ARCH_BOOT_START_LDFLAGS :=
ARCH_FLASH      = $(ARCH_ESP_REFUSE)
ARCH_BOOT_FLASH = $(ARCH_ESP_REFUSE)
ARCH_LOCK_CHECK = $(ARCH_ESP_REFUSE)

# `make board-restore` (mk/board-restore.mk): the whole flash written back from the image read before the project's first write, recorded on the board's page under docs/development/bench/.
ESPTOOL ?= python3 -m esptool
ARCH_BOARD_RESTORE = \
  size=$$(stat -c %s $<); \
  if [ "$$size" -ne $(FACTORY_FLASH_BYTES) ]; then \
    echo "$@: REFUSED. $< is $$size bytes, not the factory flash's $(FACTORY_FLASH_BYTES)" >&2; \
    exit 1; fi; \
  sum=$$(sha256sum $< | cut -d' ' -f1); \
  if [ "$$sum" != "$(FACTORY_FLASH_SHA256)" ]; then \
    echo "$@: REFUSED. $< has SHA-256 $$sum, not the factory image's $(FACTORY_FLASH_SHA256)" >&2; \
    exit 1; fi; \
  $(ESPTOOL) --chip $(ESP_TARGET) --port $(ESP_PORT) write-flash --flash-size keep 0x0 $< || exit 1; \
  echo "$@: $(BOARD) has its factory flash image back, SHA-256 $(FACTORY_FLASH_SHA256)"

# The bootloader's layer 0 hands the CDI off through ESP-IDF's RTC-retained memory (src/hal/esp32/hal_handoff.c), so the real identity sources link.
ARCH_HANDOFF := 1
# ...but takes no update request: OTA is ESP-IDF's, and RESET(1) asks tfw-boot for a session this bootloader does not open.
ARCH_UPDATE_REQUEST := arch

# The OTA session: OTA_BEGIN/DATA/END/STATUS, then OTA_AUTH and OTA_ACTIVATE into the slot that is not running, OTA_CONFIRM on the trial and OTA_INFO, all on esp_ota.
ARCH_OTA_SRCS := src/hal/esp32/hal_ota.c

# Sources the image needs that mk/firmware.mk leaves to the HAL.
ARCH_EXTRA_SRCS := src/proto/crc16_update.c

# The HMAC/P-256 tier: the HMAC peripheral's CDI, and the P-256 DeviceID derived from the same UDS (src/crypto/deviceid_p256.c) and signed with the vendored micro-ecc.
ARCH_CRYPTO_TIERS           := hmac-p256
ARCH_CRYPTO_SRCS_hmac-p256  := src/crypto/sha256.c src/crypto/deviceid_p256.c \
                               src/third_party/micro-ecc/uECC.c \
                               src/hal/esp32/crypto_hmac_p256.c
ARCH_HAL_OPTIONAL           := src/hal/esp32/crypto_hmac_p256.c src/hal/esp32/prov_uds.c \
                               $(ARCH_OTA_SRCS)

# Pulse timing (PULSE_IN, PULSE_CAPTURE), one file of two, named by the board: `rmt` times edges on the RMT receiver (src/hal/esp32/hal_pulse_rmt.c, ADR follow-up 8), `poll` reads the pin against esp_timer (hal_pulse.c).
ESP_PULSE ?= poll
ARCH_PULSE_SRCS_poll := src/hal/esp32/hal_pulse.c
ARCH_PULSE_SRCS_rmt  := src/hal/esp32/hal_pulse_rmt.c
ARCH_HAL_OPTIONAL    += $(ARCH_PULSE_SRCS_poll) $(ARCH_PULSE_SRCS_rmt)
ARCH_EXTRA_SRCS      += $(ARCH_PULSE_SRCS_$(ESP_PULSE))

# Its PROVISION=1 jig image: the wire of src/caps/provision.c over the eFuse UDS backend, which draws the UDS from the part's noise source and burns it into the HMAC_UP key block (docs/multi-arch/esp32-p4-design.md section 2.4).
ARCH_PROV_SRCS_hmac-p256    := src/caps/provision_uds.c src/hal/esp32/prov_uds.c
