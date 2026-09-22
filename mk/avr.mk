# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# The device build.

BOARD      ?= atmega328p
BOARD_DIR  := boards/$(BOARD)
-include $(BOARD_DIR)/profile.mk

AVR_DIR    := $(BUILD_DIR)/$(BOARD)
TARGET     := $(AVR_DIR)/arduino-generic

CC_AVR     := avr-gcc
OBJCOPY    := avr-objcopy
SIZE       := avr-size

# -flto and --gc-sections together are what make link-time HAL substitution pay
# off: an unused peripheral's code is not merely unreferenced, it is gone, and
# ag_hal_gpio_write inlines to a two-instruction sbi at its call site.
#
# The reproducibility flags are NOT optional. FWHash is signed evidence, so a
# verifier needs a golden measurement computable from source -- which requires
# that the same source produce the same bytes on any machine. -ffile-prefix-map
# strips the build path, -frandom-seed makes the compiler's internal symbol
# naming deterministic, and SOURCE_DATE_EPOCH pins anything time-derived.
AVR_CFLAGS := -std=c11 -mmcu=$(MCU) -DF_CPU=$(F_CPU)UL \
              -Os -flto -ffunction-sections -fdata-sections \
              -fno-fat-lto-objects \
              -Wall -Wextra -Werror -Wconversion -Wshadow \
              -Wpointer-arith -Wcast-qual -Wstrict-prototypes \
              -ffile-prefix-map=$(CURDIR)=. \
              -Iinclude -Isrc -I$(BOARD_DIR) \
              -DAG_BOARD_HEADER='"board.h"'

AVR_LDFLAGS := -mmcu=$(MCU) -Os -flto -Wl,--gc-sections -Wl,--relax

# Sorted, not wildcard-ordered. Link order affects the output image, and a
# wildcard's order depends on the filesystem -- which would make the
# reproducibility check pass on one machine and fail on another for no reason
# anyone could see.
AVR_SRCS := $(sort \
  src/main.c \
  src/proto/crc16.c src/proto/frame.c src/proto/transport.c \
  src/core/reader.c src/core/writer.c src/core/dispatch.c \
  src/caps/sys.c src/caps/gpio.c src/caps/sched.c \
  src/caps/analog.c src/caps/storage.c \
  $(BOARD_DIR)/board.c \
  $(wildcard src/hal/avr/*.c))

AVR_OBJS := $(patsubst %.c,$(AVR_DIR)/%.o,$(AVR_SRCS))

$(AVR_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@$(CC_AVR) $(AVR_CFLAGS) -frandom-seed=$(notdir $<) -c -o $@ $<

$(TARGET).elf: $(AVR_OBJS)
	@$(CC_AVR) $(AVR_LDFLAGS) -o $@ $(AVR_OBJS)

$(TARGET).hex: $(TARGET).elf
	@$(OBJCOPY) -O ihex -R .eeprom $< $@

## firmware: build the firmware for BOARD (default atmega328p)
firmware: $(TARGET).hex size-check
	@echo "built $(TARGET).hex"

## size-check: fail the build when an image exceeds its board's budget
# A GATE, not a report, and a prerequisite of `firmware` rather than something
# to run when someone remembers. Flash creep is caught on the commit that
# causes it instead of the day a feature will not fit.
size-check: $(TARGET).elf
	@flash=$$($(SIZE) --format=berkeley $< | awk 'NR==2{print $$1+$$2}'); \
	 ram=$$($(SIZE)  --format=berkeley $< | awk 'NR==2{print $$2+$$3}'); \
	 printf '%-12s flash %6s / %-6s (%2d%% of part)   ram %5s / %-5s (%2d%% of part)\n' \
	   "$(BOARD)" "$$flash" "$(FLASH_BUDGET)" "$$((100*flash/$(FLASH_TOTAL)))" \
	   "$$ram" "$(RAM_BUDGET)" "$$((100*ram/$(RAM_TOTAL)))"; \
	 fail=0; \
	 if [ "$$flash" -gt "$(FLASH_BUDGET)" ]; then echo "FLASH BUDGET EXCEEDED for $(BOARD)"; fail=1; fi; \
	 if [ "$$ram"   -gt "$(RAM_BUDGET)"   ]; then echo "RAM BUDGET EXCEEDED for $(BOARD)";   fail=1; fi; \
	 exit $$fail

## reproducible-check: build twice in different directories and diff the image
# Without this the platform's attestation verdict can never be better than
# "trust whatever hash the device reported", because nobody can compute the
# expected hash from source.
reproducible-check:
	@rm -rf /tmp/agrepro-a /tmp/agrepro-b
	@cp -a . /tmp/agrepro-a && cp -a . /tmp/agrepro-b
	@$(MAKE) -C /tmp/agrepro-a --no-print-directory BOARD=$(BOARD) $(TARGET).hex >/dev/null
	@$(MAKE) -C /tmp/agrepro-b --no-print-directory BOARD=$(BOARD) $(TARGET).hex >/dev/null
	@if cmp -s /tmp/agrepro-a/$(TARGET).hex /tmp/agrepro-b/$(TARGET).hex; then \
	   echo "reproducible: two builds in different directories are byte-identical"; \
	 else \
	   echo "NOT REPRODUCIBLE: the same source produced different images"; \
	   cmp /tmp/agrepro-a/$(TARGET).hex /tmp/agrepro-b/$(TARGET).hex || true; exit 1; \
	 fi

## flash: write the firmware to a connected board
flash: $(TARGET).hex
	avrdude -v -p$(AVRDUDE_PART) -c$(AVRDUDE_PROG) -P$(AVRDUDE_PORT) \
	        -b$(AVRDUDE_BAUD) -D -Uflash:w:$<:i

.PHONY: firmware size-check reproducible-check flash
