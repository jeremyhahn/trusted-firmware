#-------------------------------------------------------------------------------
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# The DICE partition (#608 slice 3, ruling 1024 (a)) as TF-M builds an
# out-of-tree Secure Partition (docs/integration_guide/services/
# tfm_secure_partition_addition.rst, TF-M v2.3.1): this directory is passed as
# TFM_EXTRA_PARTITION_PATHS and tfw_dice_manifest_list.yaml as
# TFM_EXTRA_MANIFEST_LIST_FILES by scripts/tfm-build.sh. The sources are the
# set test_dice_partition and test_tfw_dice_glue link on the host
# (mk/host.mk); no TF-M source is in this tree.
#-------------------------------------------------------------------------------

if (NOT TFM_PARTITION_TFW_DICE)
    return()
endif()

cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0079 NEW)

set(TFW_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
set(TFW_DICE_GEN ${CMAKE_BINARY_DIR}/generated/tfw_dice)

# The name matches the manifest list's linker pattern *tfm_*partition_tfw_dice.*
# and puts the partition in the Application RoT's memory.
add_library(tfm_app_rot_partition_tfw_dice STATIC)

target_sources(tfm_app_rot_partition_tfw_dice
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tfw_dice_partition.c
        ${TFW_ROOT}/src/tz/dice_partition.c
        ${TFW_ROOT}/src/tz/dice_service.c
        ${TFW_ROOT}/src/crypto/soft/id_p256.c
        ${TFW_ROOT}/src/crypto/deviceid_p256.c
        ${TFW_ROOT}/src/caps/alias.c
        ${TFW_ROOT}/src/caps/tmpl.c
        ${TFW_ROOT}/src/caps/uccs.c
        ${TFW_ROOT}/src/core/reader.c
        ${TFW_ROOT}/src/core/writer.c
        ${TFW_ROOT}/src/core/secure_zero.c
        ${TFW_ROOT}/src/crypto/sha256.c
        ${TFW_ROOT}/src/third_party/micro-ecc/uECC.c
        # The partition stack, from the manifest tool.
        ${TFW_DICE_GEN}/auto_generated/intermedia_tfw_dice.c
)

# The partition's static data, from the manifest tool.
target_sources(tfm_partitions
    INTERFACE
        ${TFW_DICE_GEN}/auto_generated/load_info_tfw_dice.c
)

add_dependencies(tfm_app_rot_partition_tfw_dice manifest_tool)

target_include_directories(tfm_app_rot_partition_tfw_dice
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${TFW_ROOT}/include
        ${TFW_ROOT}/src
        ${TFW_ROOT}/src/core
        ${TFW_ROOT}/src/caps
        ${TFW_ROOT}/src/crypto
        ${TFW_ROOT}/src/crypto/soft
        ${TFW_ROOT}/src/third_party/micro-ecc
    PUBLIC
        ${TFW_DICE_GEN}
)

# P-256 only, as the firmware builds micro-ecc (mk/boot.mk UECC_DEFS).
target_compile_definitions(tfm_app_rot_partition_tfw_dice
    PRIVATE
        uECC_SUPPORTS_secp160r1=0
        uECC_SUPPORTS_secp192r1=0
        uECC_SUPPORTS_secp224r1=0
        uECC_SUPPORTS_secp256k1=0
        uECC_SUPPORTS_secp256r1=1
        uECC_SUPPORT_COMPRESSED_POINT=0
)

# tfm_sprt: psa/service.h and the SFN runtime; psa_crypto_config: the
# psa/crypto.h configuration the HUK derivation compiles against. The
# Measured Boot client API comes with tf-m-extras' partition (TFM_MEASURED_BOOT
# is a manifest dependency).
target_link_libraries(tfm_app_rot_partition_tfw_dice
    PRIVATE
        tfm_sprt
        psa_crypto_config
)

target_link_libraries(tfm_partitions
    INTERFACE
        tfm_app_rot_partition_tfw_dice
)

target_compile_definitions(tfm_config
    INTERFACE
        TFM_PARTITION_TFW_DICE
)
