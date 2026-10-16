/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
#ifndef TFW_SPI_NOR_MODEL_H
#define TFW_SPI_NOR_MODEL_H

#include <stdint.h>

/*
 * A software 25-series serial NOR part on the HAL's SPI bus -- the external OTA
 * store of issue #75, for the unit suites and for tfwsim.
 *
 * The part itself is bus-free: tfw_nor_model_xfer is one transaction with its
 * select held. Linking test/model/spi_nor_hal.c as well puts it on the
 * application HAL's bus: that file defines tfw_hal_spi_xfer, and hal_mock.c's
 * definition is weak for exactly this. A transfer on the board's
 * TFW_OTA_STORE_CS_PIN goes to the part; any other select goes to the mock's
 * canned bus response, as it would without the model. The boot suites hand
 * tfw_nor_model_xfer to tfw_boot_mock_on_store instead.
 *
 * It behaves as the part does where the driver could get it wrong: a program
 * or erase without WREN is ignored, a program only clears bits, a page program
 * wraps inside its 256-byte page, and the busy bit stays set for a number of
 * status reads after each program and erase. Each of those a correct driver
 * never relies on is counted as a violation, so a suite can assert there were
 * none.
 */

#define TFW_NOR_MODEL_SIZE   0x20000ul   /* 1 Mbit: room for every AVR layout */
#define TFW_NOR_MODEL_JEDEC0 0xEFu       /* a W25X05CL's id, 512 Kbit */
#define TFW_NOR_MODEL_JEDEC1 0x30u
#define TFW_NOR_MODEL_JEDEC2 0x10u

/* Every byte erased, the counters zero, busy for one status read per write. */
void           tfw_nor_model_reset(void);
const uint8_t *tfw_nor_model_mem(void);
/* Flips one bit of the array behind the driver's back, as a fault would. */
void           tfw_nor_model_corrupt(uint32_t addr);
void           tfw_nor_model_set_busy_polls(uint8_t n);
uint32_t       tfw_nor_model_erases(void);
uint32_t       tfw_nor_model_programs(void);
uint32_t       tfw_nor_model_violations(void);

/* One transaction, the reply over the request in buf. Longer than
 * TFW_NOR_MODEL_XFER_MAX is a violation: a page read and its command fit. */
#define TFW_NOR_MODEL_XFER_MAX 512u
void           tfw_nor_model_xfer(uint8_t *buf, uint16_t len);
/* Puts bytes in the array as a programmer would before the test: what a
 * stager left behind, without going through a driver. */
void           tfw_nor_model_load(uint32_t addr, const uint8_t *src, uint32_t len);

#endif
