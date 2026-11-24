/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The MPS2 bus registers on the host: see mps2_bus_model.h.
 */
#include <stdint.h>
#include "unity.h"
#include "tfw/config.h"
#include "mps2_bus_model.h"

#define I2C_BASE    ((uint32_t)TFW_MPS2_I2C_BASE)
#define SPI_BASE    ((uint32_t)TFW_MPS2_SPI_BASE)
#define MISC_ADDR   ((uint32_t)TFW_MPS2_FPGAIO_BASE + 0x4Cu)
#define SSP_LAST    0x10u           /* SSPCPSR, the last register pl022.h names */

enum model_dir { MODEL_IDLE, MODEL_READ, MODEL_WRITE };

static enum model_dir   model_dir;
static uint32_t         model_hits;
static uint32_t         model_addr;
static volatile uint32_t model_cell;

static void model_begin(enum model_dir dir)
{
    model_dir = dir;
    model_hits = 0u;
    model_cell = 0u;
}

static int model_is_ssp(uint32_t addr)
{
    return addr >= SPI_BASE && addr - SPI_BASE <= SSP_LAST && ((addr - SPI_BASE) & 3u) == 0u;
}

volatile uint32_t *tfw_mps2_model_reg(uint32_t addr)
{
    if (model_dir == MODEL_IDLE) {
        TEST_FAIL_MESSAGE("register access outside the bus accessors");
    }
    if (++model_hits > 1u) {
        TEST_FAIL_MESSAGE("an accessor made more than one register access");
    }
    model_addr = addr;
    if (model_dir == MODEL_READ) {
        if (addr == I2C_BASE) {
            model_cell = tfw_mps2_model_sb_lines();
        } else if (model_is_ssp(addr)) {
            model_cell = tfw_mps2_model_ssp_read(addr - SPI_BASE);
        } else if (addr == MISC_ADDR) {
            model_cell = tfw_mps2_model_misc_read();
        } else {
            TEST_FAIL_MESSAGE("read of a register the bus does not read");
        }
    } else if (addr != I2C_BASE && addr != I2C_BASE + 4u && !model_is_ssp(addr) &&
               addr != MISC_ADDR) {
        TEST_FAIL_MESSAGE("write to a register the bus does not write");
    }
    return &model_cell;
}

/* The accessor has returned: one access made, and a write goes to the bus. */
static void model_end(void)
{
    enum model_dir dir = model_dir;

    model_dir = MODEL_IDLE;
    if (model_hits != 1u) {
        TEST_FAIL_MESSAGE("an accessor made no register access");
    }
    if (dir != MODEL_WRITE) {
        return;
    }
    if (model_addr == I2C_BASE) {
        tfw_mps2_model_sb_release(model_cell);
    } else if (model_addr == I2C_BASE + 4u) {
        tfw_mps2_model_sb_drive_low(model_cell);
    } else if (model_addr == MISC_ADDR) {
        tfw_mps2_model_misc_write(model_cell);
    } else {
        tfw_mps2_model_ssp_write(model_addr - SPI_BASE, model_cell);
    }
}

uint32_t __wrap_tfw_mps2_sb_lines(void)
{
    uint32_t v;

    model_begin(MODEL_READ);
    v = __real_tfw_mps2_sb_lines();
    model_end();
    return v;
}

void __wrap_tfw_mps2_sb_release(uint32_t mask)
{
    model_begin(MODEL_WRITE);
    __real_tfw_mps2_sb_release(mask);
    model_end();
}

void __wrap_tfw_mps2_sb_drive_low(uint32_t mask)
{
    model_begin(MODEL_WRITE);
    __real_tfw_mps2_sb_drive_low(mask);
    model_end();
}

uint32_t __wrap_tfw_mps2_ssp_read(uint32_t off)
{
    uint32_t v;

    model_begin(MODEL_READ);
    v = __real_tfw_mps2_ssp_read(off);
    model_end();
    return v;
}

void __wrap_tfw_mps2_ssp_write(uint32_t off, uint32_t v)
{
    model_begin(MODEL_WRITE);
    __real_tfw_mps2_ssp_write(off, v);
    model_end();
}

uint32_t __wrap_tfw_mps2_misc_read(void)
{
    uint32_t v;

    model_begin(MODEL_READ);
    v = __real_tfw_mps2_misc_read();
    model_end();
    return v;
}

void __wrap_tfw_mps2_misc_write(uint32_t v)
{
    model_begin(MODEL_WRITE);
    __real_tfw_mps2_misc_write(v);
    model_end();
}
