#ifndef TFW_BOOT_VERIFY_MOCK_H
#define TFW_BOOT_VERIFY_MOCK_H

#include <stdint.h>

/* boot_verify.h's seam for suites that link boot/boot_stage.c without a
 * backend. With no signature named every verify passes, as those suites
 * predate #137; once named, only that signature does. */
void tfw_boot_verify_mock_want(const uint8_t *sig);

/* The last message tfw_boot_verify was asked about. */
const uint8_t *tfw_boot_verify_mock_msg(uint16_t *len);

#endif
