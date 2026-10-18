/*
 * The host shim that puts test/model/spi_nor_model.c on the OTA store's select.
 * It counts the transfers it hands the part and can fail one of them, so a test
 * sees how the driver passes a bus error on.
 */
#ifndef TFW_TEST_SPI_NOR_HAL_H
#define TFW_TEST_SPI_NOR_HAL_H

#include <stdint.h>
#include "tfw/status.h"

/* Forget the count and any armed failure. */
void         tfw_nor_shim_reset(void);
/* Restart the count; the nth transfer from now (1-based) answers st, untouched. */
void         tfw_nor_shim_fail(uint32_t nth, tfw_status_t st);
uint32_t     tfw_nor_shim_xfers(void);

#endif
