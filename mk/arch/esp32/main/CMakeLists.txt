# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# The firmware as one ESP-IDF component.
#
# Its sources are NOT listed here. mk/firmware.mk already decides, from the
# board profile and the crypto tier, which files make up an image -- the same
# FW_SRCS every architecture links -- and mk/arch/esp32-targets.mk hands that
# list in as TFW_SRCS. A second list here would be a second answer to the same
# question, free to drift from the first.
#
#   TFW_ROOT       the repository
#   TFW_SRCS       the image's sources, absolute
#   TFW_BOARD_DIR  boards/<board>, for board_profile.h
#   TFW_DEFS       the feature -D flags FW_CFLAGS carries (mk/firmware.mk)

set(tfw_requires
    app_update bootloader_support driver efuse esp_adc esp_driver_rmt esp_eth esp_event esp_hw_support
    esp_netif esp_partition esp_security esp_system esp_timer freertos log
    lwip mbedtls)

# ESP-IDF first reads every component's CMakeLists in script mode, only to
# collect its requirements; the -D variables do not exist there.
if(CMAKE_BUILD_EARLY_EXPANSION)
    idf_component_register(PRIV_REQUIRES ${tfw_requires})
    return()
endif()

if(NOT TFW_SRCS)
    message(FATAL_ERROR "TFW_SRCS is empty: build with 'make esp32-build BOARD=<board>'")
endif()

# The device speaks the wire over Ethernet (src/hal/esp32/hal_eth.c), on QEMU's
# OpenCores MAC or the part's EMAC. A board that selects neither is refused
# here, from its sdkconfig, and not by an #error in C: the host lint reads
# hal_eth.c without any board's sdkconfig and would stop on it.
if(NOT CONFIG_ETH_USE_OPENETH AND NOT CONFIG_ETH_USE_ESP32_EMAC)
    message(FATAL_ERROR "the board's sdkconfig.defaults selects no Ethernet MAC")
endif()

# A static address (mk/arch/esp32/main/Kconfig.projbuild) is three dotted quads.
# Checked here, at configure time, so a typo is a build error and not a board
# that boots with no address.
if(CONFIG_TFW_ETH_STATIC_IP)
    foreach(knob TFW_ETH_IP_ADDR TFW_ETH_NETMASK TFW_ETH_GATEWAY)
        set(quad "${CONFIG_${knob}}")
        if(NOT quad MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
            message(FATAL_ERROR "CONFIG_${knob}=\"${quad}\" is not a dotted-quad IPv4 address")
        endif()
        foreach(octet ${CMAKE_MATCH_1} ${CMAKE_MATCH_2} ${CMAKE_MATCH_3} ${CMAKE_MATCH_4})
            if(octet GREATER 255)
                message(FATAL_ERROR "CONFIG_${knob}=\"${quad}\" has an octet above 255")
            endif()
        endforeach()
    endforeach()
endif()

idf_component_register(
    SRCS ${TFW_SRCS}
    PRIV_INCLUDE_DIRS
        "${TFW_ROOT}/include" "${TFW_ROOT}/src" "${TFW_ROOT}/src/proto"
        "${TFW_ROOT}/src/crypto" "${TFW_ROOT}/boot" "${TFW_ROOT}/src/hal/esp32"
        "${TFW_ROOT}/src/third_party/micro-ecc" "${TFW_BOARD_DIR}"
    PRIV_REQUIRES ${tfw_requires})

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    TFW_BOARD_HEADER="board_profile.h" ${TFW_DEFS})

# The project's warning set, as -Werror. -Wconversion only on the portable
# sources: ESP-IDF's own inline headers do not compile clean under it, and the
# HAL is the one layer that includes them. Vendored code (micro-ecc, for the
# hmac-p256 tier) is built as the host build builds it: its own -D set, none
# of our warnings, and no vendored byte edited.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Werror -Wshadow -Wpointer-arith -Wstrict-prototypes
    -ffile-prefix-map=${TFW_ROOT}=.)
foreach(src ${TFW_SRCS})
    if(src MATCHES "/src/third_party/micro-ecc/")
        set_source_files_properties(${src} PROPERTIES
            COMPILE_OPTIONS "-w"
            COMPILE_DEFINITIONS "uECC_SUPPORTS_secp160r1=0;uECC_SUPPORTS_secp192r1=0;uECC_SUPPORTS_secp224r1=0;uECC_SUPPORTS_secp256k1=0;uECC_SUPPORT_COMPRESSED_POINT=0")
    elseif(NOT src MATCHES "/src/hal/esp32/")
        set_source_files_properties(${src} PROPERTIES COMPILE_OPTIONS "-Wconversion;-Wcast-qual")
    endif()
endforeach()
