/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The mcxn94x SoC's reset cause, from CMC0's SRS, System Reset Status
 * (offset 0x80, MCXN947_cm33_core0.h line 10970, from the Secure alias
 * CMC0_BASE 0x50048000, line 12329; mcux-sdk, docs/reference/README.md names
 * the revision and hash). It is read-only (__I) and records the last reset;
 * SSRS beside it (0x88) is the sticky record, not read here. The bits are
 * the header's CMC_SRS_*_MASK:
 *
 *   bit  field   line   cause
 *    0   WAKEUP  11135  unknown: a wake-up from a low-power mode
 *    1   POR     11143  TFW_RESET_POWER_ON
 *    2   VD      11151  TFW_RESET_BROWNOUT: the voltage detector
 *    4   WARM    11159  not a cause: the reset's class, beside its source
 *    5   FATAL   11167  not a cause: the reset's class, beside its source
 *    8   PIN     11175  TFW_RESET_EXTERNAL: the reset pin
 *    9   DAP     11183  unknown: the debugger
 *   10   RSTACK  11191  unknown: a reset acknowledge timeout
 *   11   LPACK   11199  unknown: a low-power acknowledge timeout
 *   12   SCG     11207  unknown: the clock generator
 *   13   WWDT0   11215  TFW_RESET_WATCHDOG: the WWDT hal_soc.c arms
 *   14   SW      11223  TFW_RESET_SOFTWARE when the firmware asked:
 *                       AIRCR.SYSRESETREQ, which tfw_cm_system_reset writes
 *                       after its marker; else unknown (a debugger can ask
 *                       too)
 *   15   LOCKUP  11231  unknown: the CPU locked up
 *   16   CPU1    11239  unknown: the second core
 *   24   VBAT    11247  unknown: the VBAT domain
 *   25   WWDT1   11255  unknown: the second core's watchdog, not armed here
 *   26   CDOG0   11263  unknown: a code watchdog
 *   27   CDOG1   11271  unknown: a code watchdog
 *   28   JTAG    11279  unknown: the debugger
 *   30   SECVIO  11287  unknown: a security violation
 *   31   TAMPER  11295  unknown: the tamper detector
 *
 * The header names WARM and FATAL without saying which sources set them; the
 * reference manual says, and it is not read (docs/reference/README.md), so
 * either beside a source is taken as that source's class. That SW is the
 * Arm core's SYSRESETREQ is the reference manual's to confirm too.
 */

#include <stdint.h>

#include "soc_reset.h"

#define SRS_POR   (1u << 1)
#define SRS_VD    (1u << 2)
#define SRS_WARM  (1u << 4)
#define SRS_FATAL (1u << 5)
#define SRS_PIN   (1u << 8)
#define SRS_WWDT0 (1u << 13)
#define SRS_SW    (1u << 14)

tfw_reset_cause_t tfw_mcxn_reset_decode(uint32_t srs, uint8_t requested)
{
    const uint32_t source = srs & ~(SRS_WARM | SRS_FATAL);

    if (source == SRS_POR) {
        return TFW_RESET_POWER_ON;
    }
    if (source == SRS_VD) {
        return TFW_RESET_BROWNOUT;
    }
    if (source == SRS_PIN) {
        return TFW_RESET_EXTERNAL;
    }
    if (source == SRS_WWDT0) {
        return TFW_RESET_WATCHDOG;
    }
    if (source == SRS_SW && requested != 0u) {
        return TFW_RESET_SOFTWARE;
    }
    return TFW_RESET_UNKNOWN;
}

tfw_reset_cause_t tfw_mcxn_reset_take(const volatile uint32_t *srs, uint8_t requested)
{
    return tfw_mcxn_reset_decode(*srs, requested);
}
