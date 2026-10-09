/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host implementation of boot/boot_hal.h, plus the controls a test needs.
 *
 * Linked instead of boot/hal/avr; the code under test cannot tell. Flash is an
 * array a test writes directly, which is what makes "a tampered image refuses"
 * a two-line test: change a byte, call tfw_boot_evaluate, assert the status.
 *
 * Test infrastructure, not a shipping port -- which is why it lives under
 * test/ rather than boot/hal/host/. boot/ holds firmware and nothing else, for
 * the same reason src/ does.
 */
#ifndef TFW_BOOT_HAL_MOCK_H
#define TFW_BOOT_HAL_MOCK_H

#include <stddef.h>
#include <stdint.h>
#include "tfw/boot.h"

void tfw_boot_mock_reset(void);

/* Program memory, as a flat array a test can write. Sized to the board the
 * host build is configured for, so the trailer address the code computes is the
 * address the test writes to -- an assumption worth stating because a mock with
 * its own idea of the geometry would pass every test and prove nothing. */
uint8_t *tfw_boot_mock_flash(void);
size_t   tfw_boot_mock_flash_size(void);

/* Counts page writes and records the last one, so an OTA test can assert that
 * the bytes reached flash in the right order rather than only that the state
 * machine returned TFW_OK. */
unsigned tfw_boot_mock_page_writes(void);
uint32_t tfw_boot_mock_last_page_addr(void);

/* Makes the next `n` page writes fail with TFW_ERR_INTERNAL. This is how the
 * "interrupted update" case is produced: a flash that stops accepting writes
 * partway through a transfer, which no bench can arrange on demand. */
void tfw_boot_mock_fail_writes(unsigned n);

uint8_t *tfw_boot_mock_nvm(void);

/* The byte link, from the test's side. */
void   tfw_boot_mock_feed_rx(const uint8_t *b, size_t n);
size_t tfw_boot_mock_drain_tx(uint8_t *out, size_t cap);

/* How many times tfw_boot_uart_rx() found nothing waiting. The bootloader has no
 * clock, so its idle timeout is counted in these; a test measures the timeout
 * by reading this back. */
unsigned tfw_boot_mock_rx_empty_polls(void);

/* How many bytes tfw_boot_uart_rx() has handed out. */
unsigned tfw_boot_mock_rx_consumed(void);

/* Called from tfw_boot_uart_rx() when nothing is waiting, with the number of
 * empty polls so far and BEFORE the poll is counted, so the hook can feed bytes
 * that the same poll then returns -- a peer that speaks after a silence. */
void tfw_boot_mock_on_rx_idle(void (*hook)(unsigned empty_polls));

/* The handoff block as the bootloader left it, for asserting on what the
 * application would find. */
const uint8_t *tfw_boot_mock_handoff(void);

/* Pre-loads the first `n` bytes of the handoff region, as the application
 * leaves them before a reset. */
void tfw_boot_mock_set_handoff(const uint8_t *b, size_t n);

/* Whether the bootloader jumped or reset, since neither can actually happen on
 * the host and a test must be able to tell which it asked for. */
unsigned tfw_boot_mock_jumped(void);
unsigned tfw_boot_mock_did_reset(void);

#endif /* TFW_BOOT_HAL_MOCK_H */
