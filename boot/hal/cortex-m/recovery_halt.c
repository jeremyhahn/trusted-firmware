/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Recovery by stopping: linked when BOOT_OTA=0, as boot/hal/avr/recovery_halt.c
 * is and for its reasons. No image may run and this row has no way to accept
 * one, so the core stops, with interrupts masked, until someone loads an image.
 *
 * It stops saying why (#542). A board that refuses in silence -- no platform
 * block, a tampered image, another key's -- looks on its console exactly like
 * a dead one. The reason goes out as the EVENT frame recovery_ota.c sends on
 * entry (src/proto/frame.c), and again for every whole frame that arrives:
 * a console opened after the reset, or a host whose first PING is the first
 * byte on the line, still learns it. Nothing is ever answered as a command,
 * and nothing boots. The 328P's 4 KB section has no room for the framer, so
 * the AVR halt keeps its silence; a Cortex-M part has the room.
 */

#include "cm_regs.h"
#include "frame.h"
#include "../../boot_hal.h"
#include "../../boot_recovery.h"

/* No loop to open a session in: the request is spent by the caller and this
 * returns at once, so the device boots its image. */
void tfw_boot_session_open(tfw_boot_platform_t *plat,
                          const tfw_boot_session_limits_t *lim)
{
    (void)plat;
    (void)lim;
}

/* Static, not on the stack: the framer holds a whole frame's body. */
static tfw_framer_t framer;

static void announce(tfw_status_t reason)
{
    uint8_t  out[3u + TFW_FRAME_OVERHEAD];   /* SEQ, OP, STATUS; no payload */
    uint16_t n = tfw_frame_encode_event(out, (uint16_t)sizeof out, reason, 0, 0u);

    for (uint16_t i = 0u; i < n; i++) {
        tfw_boot_uart_tx(out[i]);
    }
}

_Noreturn void tfw_boot_recovery(tfw_boot_platform_t *plat, tfw_status_t reason)
{
    (void)plat;

    /* No interrupt is ever enabled in tfw-boot; masking them here says so at
     * the point it matters. The UART is polled, so the core spins rather than
     * sleeps: a WFI would wait for an interrupt that never comes, and a
     * request would go unanswered. */
    tfw_cm_irq_off();
    tfw_framer_init(&framer);
    announce(reason);
    for (;;) {
        uint8_t byte;

        if (tfw_boot_uart_rx(&byte) &&
            tfw_framer_push(&framer, byte) == TFW_FRAME_READY) {
            announce(reason);
        }
    }
}
