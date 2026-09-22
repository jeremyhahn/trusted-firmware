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
#include "ag/protocol.h"

static ag_transport_t t;

/* One pass: hand the bytes to the device, let it run, take back what it said.
 * Polled several times because a single poll handles one frame and emits what
 * the ring will take -- the same reason the firmware's main loop polls rather
 * than processing to completion. */
static void pump(const uint8_t *in, size_t n, int out_fd)
{
    uint8_t out[1024];
    size_t  got;
    int     i;

    if (n) {
        ag_mock_feed_rx(in, n);
    }
    for (i = 0; i < 16; i++) {
        ag_transport_poll(&t);
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
    ag_sys_reset_state();
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
    if (argc > 1 && strcmp(argv[1], "--pty") == 0) {
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
