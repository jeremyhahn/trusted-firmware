/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The rp2350 SoC's reset cause, from two registers. The source is the
 * pico-sdk's generated register headers at tag 2.1.1
 * (src/rp2350/hardware_regs/include/hardware/regs/, docs/reference/README.md
 * names them), checked against RP2350 datasheet release 8 (RP-008373-DS):
 * REASON at §12.9.7 table 1361 (offset 0x008, TIMER bit 0, FORCE bit 1),
 * CHIP_RESET at §6.4 table 521 (offset 0x02c, HAD_ flags bits 16-19 and
 * 21-28, bit 20 reserved), every value matched. The datasheet names bit 28
 * HAD_WATCHDOG_RESET_PSM where the header says _RSM; the bit is the same.
 *
 * WATCHDOG.REASON (offset 0x08, watchdog.h line 92, from WATCHDOG_BASE
 * 0x400D8000, addressmap.h line 69) "logs the reason for the last reset.
 * Both bits are zero for the case of a hardware reset" and, on the RP2350,
 * "a debugger warm reset of either core (SYSRESETREQ or hartreset) will also
 * clear" it (watchdog.h lines 84-91):
 *
 *   bit  field  line  cause
 *    0   TIMER  105   TFW_RESET_WATCHDOG: the count hal_soc.c arms ran out
 *    1   FORCE   98   unknown: a forced trigger, which nothing here writes
 *
 * POWMAN.CHIP_RESET (offset 0x2C, powman.h line 491, from POWMAN_BASE
 * 0x40100000, addressmap.h line 75), whose HAD_ flags each say "last reset
 * was" one source and are read-only:
 *
 *   bit  field                       line  cause
 *   16   HAD_POR                     710   TFW_RESET_POWER_ON
 *   17   HAD_BOR                     692   TFW_RESET_BROWNOUT
 *   18   HAD_RUN_LOW                 674   TFW_RESET_EXTERNAL: the RUN pin
 *   19   HAD_DP_RESET_REQ            656   unknown: the debugger
 *   21   HAD_RESCUE                  638   unknown: the debugger's rescue
 *   22   HAD_WATCHDOG_RESET_POWMAN_ASYNC  620  unknown: a watchdog
 *   23   HAD_WATCHDOG_RESET_POWMAN   601   configured to reset the power
 *   24   HAD_WATCHDOG_RESET_SWCORE   582   manager, the switched core or the
 *   28   HAD_WATCHDOG_RESET_RSM      509   state machine, not by hal_soc.c
 *   25   HAD_SWCORE_PD               563   unknown: the core powered down
 *   26   HAD_GLITCH_DETECT           545   unknown: the glitch detector
 *   27   HAD_HZD_SYS_RESET_REQ       527   unknown: a RISC-V core's request
 *
 * hal_soc.c's watchdog resets what PSM.WDSEL names, not through the power
 * manager, so its bite is read in REASON, and CHIP_RESET is taken to keep
 * the chip reset before it: CHIP_RESET records "the source of the last
 * chip-level reset" (§7.3.3), and a watchdog timeout through PSM.WDSEL runs
 * only the PSM's partial sequence (§7.4), not a chip-level reset. So is
 * tfw_cm_system_reset's AIRCR.SYSRESETREQ, which clears REASON: a requested reset reads as the marker, the way mps2-tz reads one,
 * once REASON says the watchdog did not fire. Neither register is cleared
 * (REASON has read-only fields, lines 101 and 108; CHIP_RESET's HAD_ flags
 * are read-only too): what a later reset reads is that reset's.
 *
 * A debugger's warm reset leaves no marker and clears REASON, so it reads as
 * the chip reset before it.
 */

#include <stdint.h>

#include "soc_reset.h"

#define REASON_TIMER (1u << 0)

#define CHIP_RESET_HAD_POR     (1u << 16)
#define CHIP_RESET_HAD_BOR     (1u << 17)
#define CHIP_RESET_HAD_RUN_LOW (1u << 18)
#define CHIP_RESET_HAD_ALL     0x1FEF0000u   /* bits 16-28 less 20 */

tfw_reset_cause_t tfw_rp2350_reset_decode(uint32_t reason, uint32_t chip_reset,
                                          uint8_t requested)
{
    const uint32_t had = chip_reset & CHIP_RESET_HAD_ALL;

    if (reason == REASON_TIMER) {
        return TFW_RESET_WATCHDOG;
    }
    if (reason != 0u) {
        return TFW_RESET_UNKNOWN;
    }
    if (requested != 0u) {
        return TFW_RESET_SOFTWARE;
    }
    if (had == CHIP_RESET_HAD_POR) {
        return TFW_RESET_POWER_ON;
    }
    if (had == CHIP_RESET_HAD_BOR) {
        return TFW_RESET_BROWNOUT;
    }
    if (had == CHIP_RESET_HAD_RUN_LOW) {
        return TFW_RESET_EXTERNAL;
    }
    return TFW_RESET_UNKNOWN;
}

tfw_reset_cause_t tfw_rp2350_reset_take(const volatile uint32_t *reason,
                                        const volatile uint32_t *chip_reset,
                                        uint8_t requested)
{
    const uint32_t why = *reason;

    return tfw_rp2350_reset_decode(why, *chip_reset, requested);
}
