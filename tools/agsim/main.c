/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * agsim -- the firmware, built for the host.
 *
 * Not a stub and not a model: this links the real src/proto, src/core and
 * src/caps against the mock HAL and speaks the actual protocol on stdin and
 * stdout. That makes the whole wire contract exercisable with no AVR
 * toolchain, no emulator and no hardware, which is what lets the Go SDK be
 * developed and CI-tested against a genuine peer rather than a hand-written
 * fake that would drift from the firmware the moment either changed.
 *
 *   agsim            speak the protocol on stdin/stdout
 *   agsim --pty      the same, on a pty whose path is printed to stderr
 *
 * Two binaries come from this file, one per crypto tier, linked the way the
 * firmware links its provider: `agsim` (software tier) and `agsim-atecc608a`
 * (the real ATECC608A driver over the software element model, with the
 * identity options tier_atecc608a.c documents). See tools/agsim/tier.h.
 *
 * The pty mode exists so a serial client can open it as an ordinary device and
 * exercise its real open/read/write path, rather than a special case for tests.
 */

#define _XOPEN_SOURCE 600
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "transport.h"
#include "sys.h"
#include "hal_mock.h"
#include "ag/hal.h"
#include "ag/crypto.h"
#include "ag/protocol.h"
#include "handoff.h"
#include "platform_id.h"
#include "auth_session.h"
#include "sched.h"
#include "bus.h"
#include "tier.h"

static ag_transport_t t;

/* One pass: hand the bytes to the device, let it run, take back what it said.
 * Polled several times because a single poll handles at most one EVENT --
 * a complete frame, a bad CRC, or (since ag_transport_poll() was fixed to
 * yield there too) one malformed byte sequence -- and emits what the ring
 * will take, the same reason the firmware's main loop polls rather than
 * processing to completion.
 *
 * The iteration count is sized to the input rather than a flat constant: a
 * poll() call consumes at least one byte whenever it produces an event, so
 * draining every event a chunk of n bytes could possibly contain never takes
 * more than n polls, plus a fixed margin for encoding and draining the reply
 * after the last one. A flat 16 was enough only because ag_transport_poll()
 * used to loop past a malformed byte sequence internally rather than
 * returning after each one -- n bytes of nothing but noise could produce far
 * more than sixteen separate BAD_FORMAT events now that it does. */
static void pump(const uint8_t *in, size_t n, int out_fd)
{
    uint8_t out[1024];
    size_t  got;
    size_t  i, iterations = n + 16u;

    if (n) {
        ag_mock_feed_rx(in, n);
    }
    for (i = 0; i < iterations; i++) {
        ag_transport_poll(&t);
        /* Same bridge main.c uses: SYS_INFO cannot reach the framer itself. */
        ag_sys_set_framer_errors(t.framer.err_crc, t.framer.err_format);
        ag_sched_poll();
        ag_sys_poll();
        got = ag_mock_drain_tx(out, sizeof out);
        if (got) {
            ssize_t w = write(out_fd, out, got);
            (void)w;
        }
    }
}

static int run(int in_fd, int out_fd)
{
    uint8_t buf[512];

    ag_mock_reset();
    agsim_tier_seed();
    ag_sys_reset_state();
    /* src/main.c's startup(), in its order, so a test peer cannot behave
     * differently from the device. The bus comes up BEFORE the crypto provider,
     * which sets the I2C rate it needs and must be the call left standing; and
     * both consuming reads precede the provider, whose init is what takes the
     * handed-off CDI. Without them the platform block and the handoff the tier
     * seeded would sit unread and the device would report itself unprovisioned
     * whatever it was given. */
    (void)ag_hal_adc_init();
    ag_bus_init();
    ag_platform_id_init();
    ag_boot_handoff_init();
    (void)ag_crypto_init();
    ag_sched_init();
    ag_auth_session_init();
    ag_transport_init(&t);
    (void)ag_transport_send_event(&t, AG_EVENT_BOOT);
    pump(0, 0u, out_fd);

    for (;;) {
        ssize_t n = read(in_fd, buf, sizeof buf);
        if (n < 0) {
            if (errno == EINTR) { continue; }
            return 1;
        }
        if (n == 0) {
            return 0;                     /* peer closed */
        }
        /* Time advances with traffic. Nothing here needs a real clock, and a
         * simulated one keeps a run reproducible: the same input produces the
         * same output, which is what makes this usable as a test oracle. */
        ag_mock_advance(1u);
        pump(buf, (size_t)n, out_fd);
    }
}

int main(int argc, char **argv)
{
    int i = 1, pty = 0;

    while (i < argc) {
        int used;

        if (strcmp(argv[i], "--pty") == 0) {
            pty = 1;
            i++;
            continue;
        }
        used = agsim_tier_option(argc - i, &argv[i]);
        if (used <= 0) {
            if (used == 0) {
                fprintf(stderr, "agsim: unknown option %s\nusage: %s [--pty]%s\n",
                        argv[i], argv[0], agsim_tier_usage());
            }
            return 2;
        }
        i += used;
    }

    if (pty) {
        int   master = posix_openpt(O_RDWR | O_NOCTTY);
        char *slave;

        if (master < 0 || grantpt(master) || unlockpt(master)) {
            perror("agsim: pty");
            return 1;
        }
        slave = ptsname(master);
        if (!slave) {
            perror("agsim: ptsname");
            return 1;
        }
        /* stderr, so stdout stays clean for anything piping this. */
        fprintf(stderr, "%s\n", slave);
        fflush(stderr);
        return run(master, master);
    }
    return run(STDIN_FILENO, STDOUT_FILENO);
}
