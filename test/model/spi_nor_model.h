/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
#ifndef TFW_SPI_NOR_MODEL_H
#define TFW_SPI_NOR_MODEL_H

#include <stdint.h>
#include "tfw/status.h"

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
/* No part on the select: the bus answers for it. ECHO returns each byte as
 * sent (the bench Mega, test-matrix.md #137); HIGH reads 0xFF on every byte.
 * Reset restores PRESENT. */
typedef enum {
    TFW_NOR_MODEL_PRESENT = 0,
    TFW_NOR_MODEL_ABSENT_ECHO,
    TFW_NOR_MODEL_ABSENT_HIGH
} tfw_nor_model_bus_t;
void           tfw_nor_model_set_bus(tfw_nor_model_bus_t bus);
tfw_nor_model_bus_t tfw_nor_model_bus(void);
/* The JEP106 manufacturer code the part answers READ JEDEC ID with, in place
 * of TFW_NOR_MODEL_JEDEC0: any part, not only the one the model is. Reset
 * restores JEDEC0. */
void           tfw_nor_model_set_maker(uint8_t m);
/* Non-zero: WREN does not latch, so WEL reads clear and the part ignores the
 * next PROGRAM or ERASE, as one whose WREN was lost on the bus. Reset clears
 * it. */
void           tfw_nor_model_set_wel_stuck(uint8_t on);
/* The lowest and highest byte programmed or erased since the last clear (or
 * reset). Before any, lo is 0xFFFFFFFF and hi 0. */
void           tfw_nor_model_clear_spans(void);
uint32_t       tfw_nor_model_prog_lo(void);
uint32_t       tfw_nor_model_prog_hi(void);
uint32_t       tfw_nor_model_erase_lo(void);
uint32_t       tfw_nor_model_erase_hi(void);
uint32_t       tfw_nor_model_erases(void);
uint32_t       tfw_nor_model_programs(void);
uint32_t       tfw_nor_model_violations(void);
/* Fault switches for the driver's error paths, off by default and cleared by
 * tfw_nor_model_reset(). fail_xfer(n): the n-th transfer to the part from
 * now (1 = the next) answers TFW_ERR_BUSY and moves nothing; 0 is off.
 * set_xfer_ms(ms): every transfer to the part advances the mock clock by ms,
 * so a part held busy runs a driver's deadline out. spi_nor_hal.c asks
 * take_fault() and xfer_ms() on every transfer to the part. */
void           tfw_nor_model_fail_xfer(uint8_t nth);
void           tfw_nor_model_set_xfer_ms(uint16_t ms);
uint8_t        tfw_nor_model_take_fault(void);
uint16_t       tfw_nor_model_xfer_ms(void);

/* One transaction, the reply over the request in buf. Longer than
 * TFW_NOR_MODEL_XFER_MAX is a violation: a page read and its command fit. */
#define TFW_NOR_MODEL_XFER_MAX 512u
void           tfw_nor_model_xfer(uint8_t *buf, uint16_t len);
/* Puts bytes in the array as a programmer would before the test: what a
 * stager left behind, without going through a driver. */
void           tfw_nor_model_load(uint32_t addr, const uint8_t *src, uint32_t len);
/* In spi_nor_hal.c: the next n sector erases on the HAL's bus return st and
 * never reach the part. Each failure spends one, so a suite that sets n and
 * reaches n erases leaves the switch off; off unless a suite sets it. */
void           tfw_nor_model_fail_erases(uint8_t n, tfw_status_t st);

#endif
