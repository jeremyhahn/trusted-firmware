/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/* A soft tfw-boot's handoff for a UDS and a measurement: boot_soft_mock.c. */
#ifndef TFW_BOOT_SOFT_MOCK_H
#define TFW_BOOT_SOFT_MOCK_H
#include <stdint.h>
/* The block alone, for a caller whose own startup parses it (tfwsim). */
void tfw_mock_boot_soft_block(const uint8_t uds[32], const uint8_t measurement[32]);
/* The block, then the application's tfw_boot_handoff_init(). */
void tfw_mock_boot_soft(const uint8_t uds[32], const uint8_t measurement[32]);
#endif
