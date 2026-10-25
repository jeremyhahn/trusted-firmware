#ifndef TFW_BOOT_VERIFY_MOCK_H
#define TFW_BOOT_VERIFY_MOCK_H

#include <stdint.h>
#include "tfw/status.h"

/* boot_verify.h's seam for suites that link boot/boot_stage.c without a
 * backend. With no signature named every verify passes, as those suites
 * predate #137; once named, only that signature does. */
void tfw_boot_verify_mock_want(const uint8_t *sig);

/* The message that signature was made over, `len` bytes, NULL for any: once
 * named, a verify of any other message fails, as a real signature for
 * another payload does (#231). */
void tfw_boot_verify_mock_want_msg(const uint8_t *msg, uint16_t len);

/* What tfw_boot_pubkey answers, TFW_OK until set: TFW_ERR_UNSUPPORTED is a
 * tier whose element keeps its key. And 1 when the last verify was given no
 * key. */
void    tfw_boot_verify_mock_pubkey(tfw_status_t st);
uint8_t tfw_boot_verify_mock_keyless(void);

/* The last message tfw_boot_verify was asked about. */
const uint8_t *tfw_boot_verify_mock_msg(uint16_t *len);

#endif
