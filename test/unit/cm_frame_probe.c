/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The stack frame bound's probe (row 482, src/hal/cortex-m/hal_mpu.h): built
 * and linked with the Cortex-M image flags by `make cm-frame-bound-check`.
 * PROBE_FRAME bytes of locals link under TFW_CM_FRAME_MAX and are refused
 * above it, and above the stack guard's size; PROBE_VLA makes the frame grow at
 * run time, which is refused whatever its size.
 */
#include <stdint.h>

__attribute__((noinline)) static void probe_sink(volatile uint8_t *p, uint32_t n)
{
    uint32_t i;

    for (i = 0u; i < n; i++) {
        p[i] = (uint8_t)i;
    }
}

void probe_entry(uint32_t n);

void probe_entry(uint32_t n)
{
#if defined(PROBE_VLA)
    volatile uint8_t buf[n];
#else
    volatile uint8_t buf[PROBE_FRAME];
    (void)n;
#endif
    probe_sink(buf, (uint32_t)sizeof buf);
}
