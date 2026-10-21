/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfwsim -- the firmware, built for the host.
 *
 * Not a stub and not a model: this links the real src/proto, src/core and
 * src/caps against the mock HAL and speaks the actual protocol on stdin and
 * stdout. That makes the whole wire contract exercisable with no AVR
 * toolchain, no emulator and no hardware, which is what lets the Go SDK be
 * developed and CI-tested against a genuine peer rather than a hand-written
 * fake that would drift from the firmware the moment either changed.
 *
 *   tfwsim            speak the protocol on stdin/stdout
 *   tfwsim --pty      the same, on a pty whose path is printed to stderr
 *   tfwsim --eth      also an Ethernet device: it dials the endpoint in its
 *                     EEPROM network block over a host TCP socket and serves
 *                     the Ethernet allow-list there, as an ETH=1 image does
 *                     (tools/tfwsim/eth_socket.c, issue #55)
 *
 * Two binaries come from this file, one per crypto tier, linked the way the
 * firmware links its provider: `tfwsim` (software tier) and `tfwsim-atecc608a`
 * (the real ATECC608A driver over the software element model, with the
 * identity options tier_atecc608a.c documents). See tools/tfwsim/tier.h.
 *
 * The pty mode exists so a serial client can open it as an ordinary device and
 * exercise its real open/read/write path, rather than a special case for tests.
 */

#define _XOPEN_SOURCE 600
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "transport.h"
#include "sys.h"
#include "hal_mock.h"
#include "tfw/hal.h"
#include "tfw/crypto.h"
#include "tfw/protocol.h"
#include "handoff.h"
#include "platform_id.h"
#include "auth_session.h"
#include "sched.h"
#include "bus.h"
#include "app_runner.h"
#include "tier.h"
#include "eth_socket.h"

static tfw_transport_t t;

/* One pass: hand the bytes to the device, let it run, take back what it said.
 * Polled several times because a single poll handles at most one EVENT --
 * a complete frame, a bad CRC, or (since tfw_transport_poll() was fixed to
 * yield there too) one malformed byte sequence -- and emits what the ring
 * will take, the same reason the firmware's main loop polls rather than
 * processing to completion.
 *
 * The iteration count is sized to the input rather than a flat constant: a
 * poll() call consumes at least one byte whenever it produces an event, so
 * draining every event a chunk of n bytes could possibly contain never takes
 * more than n polls, plus a fixed margin for encoding and draining the reply
 * after the last one. A flat 16 was enough only because tfw_transport_poll()
 * used to loop past a malformed byte sequence internally rather than
 * returning after each one -- n bytes of nothing but noise could produce far
 * more than sixteen separate BAD_FORMAT events now that it does. */
static void pump(const uint8_t *in, size_t n, int out_fd)
{
    uint8_t out[1024];
    size_t  got;
    size_t  i, iterations = n + 16u;

    if (n) {
        tfw_mock_feed_rx(in, n);
    }
    for (i = 0; i < iterations; i++) {
        tfw_transport_poll(&t);
        /* Same bridge main.c uses: SYS_INFO cannot reach the framer itself. */
        tfw_sys_set_framer_errors(t.framer.err_crc, t.framer.err_format);
        tfw_sched_poll();
        tfw_sys_poll();
        tfw_app_runner_poll();
        got = tfw_mock_drain_tx(out, sizeof out);
        if (got) {
            ssize_t w = write(out_fd, out, got);
            (void)w;
        }
    }
}

/* Presets for the peripherals the mock HAL answers, so a host test can see a
 * success the mock would otherwise never give: a pulse, a capture, a bus that
 * answers, a 1-Wire device present. Each is an environment variable and each
 * unset leaves the mock as tfw_mock_reset() left it, which is what every
 * existing caller gets.
 *
 *   TFWSIM_PULSE_IN_US    the width every pulse-in measures, in us
 *   TFWSIM_CAPTURE_US     comma-separated edge intervals, in us (at most 32)
 *   TFWSIM_BUS_RESPONSE   hex bytes an I2C read or a 1-Wire read returns
 *   TFWSIM_OW_PRESENCE    1 for a device answering a 1-Wire reset
 */
static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    return -1;
}

static void preset_mock(void)
{
    const char *v;

    v = getenv("TFWSIM_PULSE_IN_US");
    if (v) {
        tfw_mock_set_pulse_in((uint32_t)strtoul(v, NULL, 10));
    }
    v = getenv("TFWSIM_CAPTURE_US");
    if (v) {
        uint16_t us[32];
        uint8_t  n = 0u;
        char    *end;

        while (*v && n < 32u) {
            us[n++] = (uint16_t)strtoul(v, &end, 10);
            if (*end != ',') { break; }
            v = end + 1;
        }
        tfw_mock_set_capture(us, n);
    }
    v = getenv("TFWSIM_BUS_RESPONSE");
    if (v) {
        uint8_t b[32];
        uint8_t n = 0u;

        while (n < sizeof b && hex_nibble(v[0]) >= 0 && hex_nibble(v[1]) >= 0) {
            b[n++] = (uint8_t)((hex_nibble(v[0]) << 4) | hex_nibble(v[1]));
            v += 2;
        }
        tfw_mock_set_bus_response(b, n);
    }
    v = getenv("TFWSIM_OW_PRESENCE");
    if (v) {
        (void)tfw_mock_ow_presence_set((uint8_t)(strcmp(v, "1") == 0));
    }
}

/*
 * Whether a RESET restarts the firmware in place. On a device the reset never
 * returns and the next thing to run is the startup sequence, with the EEPROM
 * and the element as they were; the mock's tfw_hal_reset() only counts, so
 * here nothing restarts unless this says so. 0 for every binary but one:
 * tools/tfwsim/reboot.c, in tfwsim-prov's object list alone, overrides it, so
 * the key `tfwprov --commit` makes is the key the next boot attests with.
 */
__attribute__((weak)) int tfwsim_reboot_on_reset(void) { return 0; }

static void boot(void)
{
    tfw_sys_reset_state();
    /* src/main.c's startup(), in its order, so a test peer cannot behave
     * differently from the device. The bus comes up BEFORE the crypto provider,
     * which sets the I2C rate it needs and must be the call left standing; and
     * both consuming reads precede the provider, whose init is what takes the
     * handed-off CDI. Without them the platform block and the handoff the tier
     * seeded would sit unread and the device would report itself unprovisioned
     * whatever it was given. */
    (void)tfw_hal_adc_init();
    tfw_bus_init();
    tfw_platform_id_init();
    tfw_boot_handoff_init();
    (void)tfw_crypto_init();
    tfw_sched_init();
    tfw_auth_session_init();
    tfw_transport_init(&t);
    tfw_app_runner_start();
    (void)tfw_transport_send_event(&t, TFW_EVENT_BOOT);
}

static int run(int in_fd, int out_fd, int eth)
{
    uint8_t  buf[512];
    unsigned resets;

    tfw_mock_reset();
    tfwsim_tier_seed();
    preset_mock();
    boot();
    pump(0, 0u, out_fd);
    resets = tfw_mock_reset_count();

    for (;;) {
        struct pollfd p[2];
        nfds_t        np = 1u;
        ssize_t       n  = 0;

        /* Without --eth this waits on stdin alone, forever: the same blocking
         * read it always was. With it, the socket is waited on too, and the
         * wait is a 1 ms tick so the link's reconnect deadline arrives with
         * no traffic to carry it. */
        p[0].fd = in_fd; p[0].events = POLLIN; p[0].revents = 0;
        if (eth && tfwsim_eth_fd() >= 0) {
            p[1].fd = tfwsim_eth_fd(); p[1].events = POLLIN; p[1].revents = 0;
            np = 2u;
        }
        if (poll(p, np, eth ? 1 : -1) < 0) {
            if (errno == EINTR) { continue; }
            return 1;
        }
        if (p[0].revents) {
            n = read(in_fd, buf, sizeof buf);
            if (n < 0) {
                if (errno == EINTR) { continue; }
                return 1;
            }
            if (n == 0) {
                return 0;                     /* peer closed */
            }
        } else if (!eth) {
            continue;
        }
        /* Time advances with traffic, and with --eth by one tick per wait.
         * Nothing here needs a real clock, and a simulated one keeps a serial
         * run reproducible: the same input produces the same output, which is
         * what makes this usable as a test oracle. */
        tfw_mock_advance(1u);
        pump(buf, (size_t)n, out_fd);
        /* No tfw_mock_reset() and no tier seed: a reset keeps what a device
         * keeps. The cause is the one the AVR reports for its own reset. */
        if (tfwsim_reboot_on_reset() && tfw_mock_reset_count() != resets) {
            resets = tfw_mock_reset_count();
            tfw_mock_set_reset_cause(TFW_RESET_WATCHDOG);
            boot();
            pump(0, 0u, out_fd);
        }
    }
}

int main(int argc, char **argv)
{
    int i = 1, pty = 0, eth = 0;

    while (i < argc) {
        int used;

        if (strcmp(argv[i], "--pty") == 0) {
            pty = 1;
            i++;
            continue;
        }
        if (strcmp(argv[i], "--eth") == 0) {
            eth = 1;
            tfwsim_eth_enable();
            i++;
            continue;
        }
        used = tfwsim_tier_option(argc - i, &argv[i]);
        if (used <= 0) {
            if (used == 0) {
                fprintf(stderr, "tfwsim: unknown option %s\nusage: %s [--pty] [--eth]%s\n",
                        argv[i], argv[0], tfwsim_tier_usage());
            }
            return 2;
        }
        i += used;
    }

    if (pty) {
        int   master = posix_openpt(O_RDWR | O_NOCTTY);
        char *slave;

        if (master < 0 || grantpt(master) || unlockpt(master)) {
            perror("tfwsim: pty");
            return 1;
        }
        slave = ptsname(master);
        if (!slave) {
            perror("tfwsim: ptsname");
            return 1;
        }
        /* stderr, so stdout stays clean for anything piping this. */
        fprintf(stderr, "%s\n", slave);
        fflush(stderr);
        return run(master, master, eth);
    }
    return run(STDIN_FILENO, STDOUT_FILENO, eth);
}
