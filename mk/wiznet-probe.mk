# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# The Ethernet controller probe: a throwaway bench image that reads which chip an Arduino Ethernet shield carries.

WIZPROBE_SRC_DIR := test/hw/wiznet-probe
# The row is in the path where a row flag is given, as for the bake image: these objects take FW_CFLAGS too.
WIZPROBE_DIR     := $(BUILD_DIR)/wiznet-probe/$(BOARD)$(FW_ROW_SUFFIX)
WIZPROBE_TARGET  := $(WIZPROBE_DIR)/wiznet-probe

WIZPROBE_SRCS := $(WIZPROBE_SRC_DIR)/main.c $(WIZPROBE_SRC_DIR)/wizprobe.c \
                 $(HAL_DIR)/hal_sys.c $(HAL_DIR)/hal_early.c $(HAL_DIR)/hal_tick.c \
                 $(HAL_DIR)/hal_uart.c $(HAL_DIR)/hal_gpio.c $(HAL_DIR)/hal_bus.c \
                 src/core/board_lookup.c $(BOARD_DIR)/board.c
WIZPROBE_OBJS := $(patsubst %.c,$(WIZPROBE_DIR)/%.o,$(WIZPROBE_SRCS))

$(WIZPROBE_DIR)/%.o: %.c $(WIZPROBE_SRC_DIR)/wizprobe.h
	@mkdir -p $(dir $@)
	@$(ARCH_CC) $(FW_CFLAGS) -I$(WIZPROBE_SRC_DIR) -frandom-seed=$(notdir $<) -c -o $@ $<

$(WIZPROBE_TARGET).elf: $(WIZPROBE_OBJS)
	@$(ARCH_CC) $(FW_LDFLAGS) -o $@ $(WIZPROBE_OBJS)

$(WIZPROBE_TARGET).hex: $(WIZPROBE_TARGET).elf
	@$(ARCH_OBJCOPY) -O ihex -R .eeprom $< $@

## wiznet-probe: build the bench probe that reads which Ethernet chip a shield carries
wiznet-probe: $(WIZPROBE_TARGET).hex
	@$(ARCH_SIZE) $(WIZPROBE_TARGET).elf
	@echo "built $<"

## wiznet-probe-flash: write the Ethernet chip probe through the board's bootloader
wiznet-probe-flash: $(WIZPROBE_TARGET).hex
	$(ARCH_FLASH)

## wiznet-probe-dir: print the directory `wiznet-probe` builds in, for the flags given (used by scripts/test_build_dirs.py)
wiznet-probe-dir:
	@echo $(WIZPROBE_DIR)

.PHONY: wiznet-probe wiznet-probe-flash wiznet-probe-dir
