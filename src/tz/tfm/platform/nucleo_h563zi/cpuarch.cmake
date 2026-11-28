#-------------------------------------------------------------------------------
# Copyright (c) 2020, Arm Limited. All rights reserved.
# Copyright (c) 2021 STMicroelectronics. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
#
#-------------------------------------------------------------------------------

# cpuarch.cmake is used to set things that related to the platform that are both
# immutable and global, which is to say they should apply to any kind of project
# that uses this platform. In practise this is normally compiler definitions and
# variables related to hardware.

# Set architecture and CPU
set(TFM_SYSTEM_PROCESSOR cortex-m33)
set(TFM_SYSTEM_ARCHITECTURE armv8-m.main)
# NUCLEO-H563ZI (trusted-firmware #608 slice 4): TF-M v2.3.1's
# stm/common/stm32h5xx ships one CMSIS device header, stm32h573xx.h, so the
# device define stays STM32H573xx. The H563 is the H573 without SAES, PKA and
# AES (the same 2 MB flash and 640 KB SRAM); with CRYPTO_HW_ACCELERATOR off
# (config.cmake) nothing drives those blocks, and the accelerator type below
# is unused.
set(CRYPTO_HW_ACCELERATOR_TYPE stm)
add_compile_definitions(
    STM32H573xx
    USE_HAL_DRIVER
)
