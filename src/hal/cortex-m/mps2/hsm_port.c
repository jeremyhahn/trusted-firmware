/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The tamper-response engine's port on mps2 (docs/hsm-self-defence/
 * m7-design.md slice 3): the loop on QEMU's mps2-an505, where the board's own
 * I/O stands in for the M7's lines and outputs.
 *
 *   sense   push button 0 is S1 (tamper pin), button 1 is S5 (hot-plug).
 *           QEMU does not model the buttons and reads zero, so on QEMU a
 *           signal arrives off the channel (HSM_REPORT) and the button path
 *           is the host test's (test/unit/test_hsm_loop.c).
 *   act     LED 0 lights for A2 (power off) and A5 (shutdown), so a lockdown
 *           always lights it; LED 1 lights for A4, the wipe. A1, A3 and A6
 *           have no part here to act on and are done at once. The LEDs read
 *           back over GPIO_READ (hal_gpio.c), which is how the QEMU case sees
 *           what the engine did; the wipe is this record, not an erase.
 *   key     the throwaway tenant key the test envelopes are signed with
 *           (testdata/suit_vectors.h SUIT_TENANT_PUB, private scalar
 *           0x40..0x5F). An emulated board verifies test envelopes and
 *           nothing else; a part's port takes its key from the image.
 */
#include <stdint.h>

#include "core/hsm.h"
#include "core/hsm_port.h"
#include "soc_regs.h"

#define LED_DOWN 0x1u   /* A2, A5 */
#define LED_WIPE 0x2u   /* A4 */

static const uint8_t TENANT_PUB[64] = {
    0x68, 0xec, 0x7c, 0xf0, 0x8c, 0xd4, 0x10, 0x6e, 0x43, 0xb1, 0x4d, 0xe8,
    0x95, 0x42, 0x65, 0x22, 0xbd, 0x0a, 0x45, 0x15, 0x0c, 0x02, 0x7e, 0x45,
    0xc7, 0x95, 0x34, 0x34, 0xd7, 0x47, 0xe7, 0xba, 0xe3, 0xaf, 0x39, 0xa8,
    0x8e, 0xbb, 0xee, 0x86, 0x79, 0xbb, 0x61, 0xe7, 0x84, 0x5c, 0x3a, 0x89,
    0xcb, 0x9b, 0x5a, 0x32, 0x37, 0xc3, 0xfd, 0xb0, 0xb0, 0x58, 0x7d, 0xba,
    0xf4, 0x15, 0x11, 0x8d,
};

tfw_status_t tfw_hsm_port_act(uint8_t action)
{
    if (action == TFW_HSM_ACT_POWER_OFF || action == TFW_HSM_ACT_SHUTDOWN) {
        TFW_MPS2_FPGAIO_LED |= LED_DOWN;
    } else if (action == TFW_HSM_ACT_KEYS_WIPE) {
        TFW_MPS2_FPGAIO_LED |= LED_WIPE;
    }
    return TFW_OK;
}

const uint8_t *tfw_hsm_port_key(void)
{
    return TENANT_PUB;
}

uint8_t tfw_hsm_port_sense(void)
{
    uint32_t b = TFW_MPS2_FPGAIO_BUTTON;
    uint8_t  s = 0u;

    if ((b & 0x1u) != 0u) {
        s |= (uint8_t)(1u << (TFW_HSM_SIG_TAMPER_PIN - 1u));
    }
    if ((b & 0x2u) != 0u) {
        s |= (uint8_t)(1u << (TFW_HSM_SIG_HOTPLUG - 1u));
    }
    return s;
}

/* S3: the AN505 model has no root of trust that attests the boot, so nothing
 * is measured and nothing raised. */
tfw_status_t tfw_hsm_port_measure(const tfw_hsm_set_t *set)
{
    (void)set;
    return TFW_ERR_UNSUPPORTED;
}
