# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# Layer 0 as the bootloader's `main` component, which replaces ESP-IDF's own
# (the bootloader_override example's mechanism). The handoff encoder and the
# CRC are the portable ones every architecture's bootloader links.
get_filename_component(TFW_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)

# The ES256 gate (boot/hal/esp32/l0_gate.c) with the AVR rows' trailer parser
# and policy primitives. The curve arithmetic and the SHA-256 are the
# bootloader's own, never copies of ours: the SHA is its bootloader SHA
# (boot/hal/esp32/measure_esp32.c), and the verify is the board's
# TFW_L0_VERIFY (boards/*/profile.mk). p256-rom is the ROM's ECDSA through
# ESP-IDF's patched ets_ecdsa_verify (boot/hal/esp32/verify_p256_rom.c), on a
# target whose ROM has it: the P4, whose iram_loader_seg micro-ecc did not fit.
# p256-soft is the AVR rows' backend over ESP-IDF's micro-ecc component, for the
# C3 and the ESP32, whose ROMs have no ECDSA, with verify_digest_soft.c beside it
# for the manifest. The SUIT manifest gate is src/core/suit.c, the application's
# reader, joined here by this list; l0_crypto.c hands it the bootloader SHA and
# the verify's digest entry is its signature check. Copies of ours fell to iram_seg,
# the small region that takes every .text the script does not name, and its
# calls into iram_loader_seg no longer reached (R_RISCV_RVC_JUMP truncated). The
# bootloader is a subproject that is handed nothing of ours but SDKCONFIG, so
# esp32-build writes what it needs beside it: tfw_l0.cmake names the board
# directory and the verify, and p256_key_slot.c is boot/verify/p256_key_slot.c's
# slot with the image key in it -- generated, not patched, because the build
# RSA-signs the bootloader.
idf_build_get_property(tfw_sdkconfig SDKCONFIG)
get_filename_component(TFW_L0_GEN "${tfw_sdkconfig}" DIRECTORY)
set(TFW_L0_GEN "${TFW_L0_GEN}/tfw_l0")
if(NOT EXISTS "${TFW_L0_GEN}/tfw_l0.cmake" OR NOT EXISTS "${TFW_L0_GEN}/p256_key_slot.c")
    message(FATAL_ERROR "layer 0: ${TFW_L0_GEN} has no tfw_l0.cmake or p256_key_slot.c: build with `make esp32-build`")
endif()
include("${TFW_L0_GEN}/tfw_l0.cmake")

if(TFW_L0_VERIFY STREQUAL "p256-rom")
    # Without the patch the symbol is the bare ROM routine, which does not
    # range-check r and s (components/esp_rom/patches/esp_rom_ecdsa.c). The
    # requirements pass has no sdkconfig, so the check is the build pass's.
    if(NOT CMAKE_BUILD_EARLY_EXPANSION AND NOT CONFIG_ESP_ROM_ECDSA_VERIFY_PATCH)
        message(FATAL_ERROR "layer 0: p256-rom needs ESP-IDF's ets_ecdsa_verify patch, which ${IDF_TARGET} does not build")
    endif()
    set(tfw_verify "${TFW_ROOT}/boot/hal/esp32/verify_p256_rom.c")
    set(tfw_verify_requires "")
elseif(TFW_L0_VERIFY STREQUAL "p256-soft")
    set(tfw_verify "${TFW_ROOT}/boot/verify/verify_p256_soft.c"
                   "${TFW_ROOT}/boot/hal/esp32/verify_digest_soft.c")
    set(tfw_verify_requires micro-ecc)
else()
    message(FATAL_ERROR "layer 0: TFW_L0_VERIFY is '${TFW_L0_VERIFY}', not p256-rom or p256-soft")
endif()

idf_component_register(
    SRCS
        "${TFW_ROOT}/boot/hal/esp32/boot_l0.c"
        "${TFW_ROOT}/boot/hal/esp32/l0_gate.c"
        "${TFW_ROOT}/boot/hal/esp32/boot_hal_esp32.c"
        "${TFW_ROOT}/boot/hal/esp32/flash_span.c"
        "${TFW_ROOT}/boot/hal/esp32/measure_esp32.c"
        "${TFW_ROOT}/boot/boot_cose.c"
        "${TFW_ROOT}/boot/boot_handoff.c"
        "${TFW_ROOT}/boot/boot_image.c"
        "${TFW_ROOT}/boot/boot_platform.c"
        "${TFW_ROOT}/boot/boot_policy.c"
        ${tfw_verify}
        "${TFW_L0_GEN}/p256_key_slot.c"
        "${TFW_ROOT}/boot/hal/esp32/l0_crypto.c"
        "${TFW_ROOT}/src/core/suit.c"
        "${TFW_ROOT}/src/proto/crc16.c"
        "${TFW_ROOT}/src/proto/crc16_update.c"
    PRIV_INCLUDE_DIRS
        "${TFW_ROOT}/include" "${TFW_ROOT}/boot" "${TFW_ROOT}/boot/verify"
        "${TFW_ROOT}/src/core" "${TFW_ROOT}/src/proto" "${TFW_BOARD_DIR}"
    REQUIRES bootloader bootloader_support efuse esp_app_format esp_rom hal log soc
             spi_flash ${tfw_verify_requires})

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror -Wshadow)
# The board's id for the binding, and the geometry that is the trailer
# parser's bound: both from the board profile, which the app's OTA_ACTIVATE
# check reads too (src/hal/esp32/hal_ota.c), so the two layers share one bound.
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    TFW_BOARD_HEADER="board_profile.h")

# The bootloader's default linker scripts, as the example does.
idf_build_get_property(scripts BOOTLOADER_LINKER_SCRIPT)
target_linker_script(${COMPONENT_LIB} INTERFACE "${scripts}")
