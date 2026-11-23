/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The secure world's NSC entry points (#590, include/tfw/tz_s_api.h). Each is
 * cmse_nonsecure_entry: the compiler gives it the __acle_se_ symbol and the
 * BXNS return that clears the registers, and the secure link places its SG
 * veneer in the NSC region (tz_s.ld) and lists it in the import library.
 * The DICE service's four entry points are src/tz/dice_service.c's, linked
 * into this image only; tz_s_dice_start gives it its inputs before the
 * non-secure world runs.
 */
#include <arm_cmse.h>
#include "tfw/protocol.h"
#include "tfw/tz_s_api.h"
#include "tz_s.h"
#include "../../../tz/dice_service.h"

uint32_t __attribute__((cmse_nonsecure_entry)) tfw_s_version(void)
{
    return TFW_S_VERSION;
}

/* Fixed inputs: these images run on QEMU only (the H563 TZ=1 build is not
 * flashed), so the UDS and CDI are test vectors, not a device's secrets.
 * The UDS read from the device and the CDI from secure boot replace them in
 * the slice that boots this image from tfw-boot. */
static const uint8_t tz_dice_test_uds[32] = {
    0x55, 0x44, 0x53, 0x2d, 0x74, 0x7a, 0x2d, 0x35, 0x39, 0x30, 0x2d, 0x71, 0x65, 0x6d, 0x75, 0x00,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
};
static const uint8_t tz_dice_test_cdi[32] = {
    0x43, 0x44, 0x49, 0x2d, 0x74, 0x7a, 0x2d, 0x35, 0x39, 0x30, 0x2d, 0x71, 0x65, 0x6d, 0x75, 0x00,
    0x10, 0x0f, 0x0e, 0x0d, 0x0c, 0x0b, 0x0a, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
};
static const uint8_t tz_dice_test_ueid[TFW_ATTEST_ID_LEN] = {
    0x01, 0x74, 0x7a, 0x35, 0x39, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
};
static const uint8_t tz_dice_test_fw_hash[32] = {
    0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff,
    0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff,
};

int tz_s_dice_start(void)
{
    tfw_dice_inputs_t in;

    in.uds     = tz_dice_test_uds;
    in.cdi     = tz_dice_test_cdi;
    in.ueid    = tz_dice_test_ueid;
    in.fw_hash = tz_dice_test_fw_hash;
    in.tier    = TFW_CRYPTO_SOFTWARE;
    return tfw_dice_service_init(&in) == TFW_OK;
}
