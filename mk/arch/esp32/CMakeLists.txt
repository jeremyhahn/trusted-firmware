# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# The ESP-IDF project that links the firmware. Driven by
# `make esp32-build BOARD=<board>` (mk/arch/esp32-targets.mk), which passes
# the firmware's own source list; run by hand, it has nothing to build.
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Layer 0 lives in the bootloader: boot/hal/esp32/bootloader_components/main
# replaces the bootloader's entry point. See boot/hal/esp32/boot_l0.c.
get_filename_component(tfw_l0_dir
    "${CMAKE_CURRENT_LIST_DIR}/../../../boot/hal/esp32/bootloader_components" ABSOLUTE)
idf_build_set_property(BOOTLOADER_EXTRA_COMPONENT_DIRS "${tfw_l0_dir}" APPEND)
project(trusted-firmware)
