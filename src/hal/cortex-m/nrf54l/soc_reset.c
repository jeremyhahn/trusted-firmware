/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The nrf54l SoC's reset cause, from RESET.RESETREAS (offset 0x600,
 * nrf54l15_types.h line 28391, from NRF_RESET_S_BASE 0x5010E000,
 * nrf54l15_global.h line 240; nrfx MDK, docs/reference/README.md names the
 * revision). The bits, each a latch that survives every reset but power-on
 * (RESET_RESETREAS_ResetValue 0, line 28395), are nrf54l15_types.h's:
 *
 *   bit  field       line   cause
 *    0   RESETPIN    28398  TFW_RESET_EXTERNAL: the reset pin
 *    1   DOG0        28406  TFW_RESET_WATCHDOG
 *    2   DOG1        28414  TFW_RESET_WATCHDOG
 *    3   CTRLAPSOFT  28422  unknown: the debugger's soft reset
 *    4   CTRLAPHARD  28430  unknown: the debugger's hard reset
 *    5   CTRLAPPIN   28438  unknown: the debugger's pin reset
 *    6   SREQ        28446  TFW_RESET_SOFTWARE when the firmware asked:
 *                           AIRCR.SYSRESETREQ, which tfw_cm_system_reset
 *                           writes after its marker; else unknown (a
 *                           debugger can write it too)
 *    7   LOCKUP      28454  unknown: the CPU locked up
 *    8   OFF         28462  unknown: a GPIO woke the part from System OFF
 *    9   LPCOMP      28470  unknown: LPCOMP woke it
 *   10   DIF         28478  unknown: the debug interface woke it
 *   11   GRTC        28486  unknown: the GRTC woke it
 *   12   NFC         28494  unknown: the NFC field woke it
 *   13   SECTAMPER   28502  unknown: the tamper detector
 *   14-31            -      unknown: the MDK defines none
 *
 * Nothing latched is power-on. The MDK has no brownout bit, so a brownout
 * reads as power-on too (docs/boards/nrf54l15-dk.md). The pin's bit is
 * alone by the time this reads it: tfw_cm_soc_early has already done
 * SystemInit's fold, which clears the rest when it is set.
 *
 * The register is write-1-to-clear: system_nrf54l.c (lines 207-211) clears
 * every bit but RESETPIN by writing ~RESET_RESETREAS_RESETPIN_Msk. A take
 * writes all ones the same way, so every bit goes.
 */

#include <stdint.h>

#include "soc_reset.h"

#define RESETREAS_RESETPIN (1uL << 0)
#define RESETREAS_DOG0     (1uL << 1)
#define RESETREAS_DOG1     (1uL << 2)
#define RESETREAS_SREQ     (1uL << 6)

#define RESETREAS_CLEAR_ALL 0xFFFFFFFFuL

tfw_reset_cause_t tfw_nrf54l_reset_decode(uint32_t resetreas, uint8_t requested)
{
    if (resetreas == 0u) {
        return TFW_RESET_POWER_ON;
    }
    if ((resetreas & ~(RESETREAS_DOG0 | RESETREAS_DOG1)) == 0u) {
        return TFW_RESET_WATCHDOG;
    }
    if (resetreas == RESETREAS_RESETPIN) {
        return TFW_RESET_EXTERNAL;
    }
    if (resetreas == RESETREAS_SREQ && requested != 0u) {
        return TFW_RESET_SOFTWARE;
    }
    return TFW_RESET_UNKNOWN;
}

tfw_reset_cause_t tfw_nrf54l_reset_take(volatile uint32_t *resetreas, uint8_t requested)
{
    uint32_t latched = *resetreas;

    *resetreas = RESETREAS_CLEAR_ALL;
    return tfw_nrf54l_reset_decode(latched, requested);
}
