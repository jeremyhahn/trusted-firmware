# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# Layer 0 as the bootloader's `main` component, which replaces ESP-IDF's own
# (the bootloader_override example's mechanism). The handoff encoder and the
# CRC are the portable ones every architecture's bootloader links.
get_filename_component(TFW_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)

idf_component_register(
    SRCS
        "${TFW_ROOT}/boot/hal/esp32/boot_l0.c"
        "${TFW_ROOT}/boot/hal/esp32/boot_hal_esp32.c"
        "${TFW_ROOT}/boot/boot_handoff.c"
        "${TFW_ROOT}/src/proto/crc16.c"
        "${TFW_ROOT}/src/proto/crc16_update.c"
    PRIV_INCLUDE_DIRS
        "${TFW_ROOT}/include" "${TFW_ROOT}/boot" "${TFW_ROOT}/src/proto"
    REQUIRES bootloader bootloader_support efuse esp_app_format esp_rom hal log soc spi_flash)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror -Wshadow)

# The bootloader's default linker scripts, as the example does.
idf_build_get_property(scripts BOOTLOADER_LINKER_SCRIPT)
target_linker_script(${COMPONENT_LIB} INTERFACE "${scripts}")
