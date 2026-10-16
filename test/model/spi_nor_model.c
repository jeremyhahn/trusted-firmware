/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The software serial NOR part -- see spi_nor_model.h. The opcodes are the
 * 25-series set src/store/spi_nor.c speaks, plus JEDEC ID, which the driver
 * does not send and tfw-boot's presence probe does. Bus-free: spi_nor_hal.c
 * puts it on the application HAL's SPI, test_boot_stage.c on tfw-boot's.
 */
#include <string.h>
#include "spi_nor_model.h"

#define OP_READ     0x03u
#define OP_PROGRAM  0x02u
#define OP_ERASE_4K 0x20u
#define OP_WREN     0x06u
#define OP_WRDI     0x04u
#define OP_STATUS   0x05u
#define OP_JEDEC    0x9Fu
#define ST_WIP      0x01u
#define ST_WEL      0x02u
#define PAGE        256u
#define SECTOR      4096ul
#define HEAD        4u

static uint8_t  mem[TFW_NOR_MODEL_SIZE];
static uint8_t  wel;
static uint8_t  busy;
static uint8_t  busy_polls = 1u;
static uint8_t  ready;   /* a part leaves the factory erased; tfwsim never resets */
static uint32_t erases, programs, violations;

void tfw_nor_model_reset(void)
{
    ready = 1u;
    memset(mem, 0xFF, sizeof mem);
    wel = busy = 0u;
    busy_polls = 1u;
    erases = programs = violations = 0u;
}

const uint8_t *tfw_nor_model_mem(void)        { return mem; }
void     tfw_nor_model_corrupt(uint32_t addr) { mem[addr % TFW_NOR_MODEL_SIZE] ^= 0x01u; }
void     tfw_nor_model_set_busy_polls(uint8_t n) { busy_polls = n; }
uint32_t tfw_nor_model_erases(void)           { return erases; }
uint32_t tfw_nor_model_programs(void)         { return programs; }
uint32_t tfw_nor_model_violations(void)       { return violations; }

static uint32_t addr_of(const uint8_t *wr)
{
    return (((uint32_t)wr[1] << 16) | ((uint32_t)wr[2] << 8) | wr[3]) % TFW_NOR_MODEL_SIZE;
}

/* The part ignores a write it was not enabled for, or that arrives while it is
 * still busy; a driver that sends one has lost bytes without being told. */
static int writable(void)
{
    if (busy != 0u || wel == 0u) {
        violations++;
        return 0;
    }
    return 1;
}

static void nor(const uint8_t *wr, uint8_t *rd, uint16_t len)
{
    uint32_t a;
    uint16_t i;

    memset(rd, 0, len);
    if (len == 0u) {
        return;
    }
    if (busy != 0u && wr[0] != OP_STATUS) {
        violations++;
        return;
    }
    switch (wr[0]) {
    case OP_JEDEC:
        if (len > 1u) { rd[1] = TFW_NOR_MODEL_JEDEC0; }
        if (len > 2u) { rd[2] = TFW_NOR_MODEL_JEDEC1; }
        if (len > 3u) { rd[3] = TFW_NOR_MODEL_JEDEC2; }
        break;
    case OP_STATUS:
        for (i = 1u; i < len; i++) {
            rd[i] = (uint8_t)((busy != 0u ? ST_WIP : 0u) | (wel != 0u ? ST_WEL : 0u));
        }
        if (busy != 0u) {
            busy--;
        }
        break;
    case OP_WREN: wel = 1u; break;
    case OP_WRDI: wel = 0u; break;
    case OP_READ:
        if (len < HEAD) { violations++; break; }
        a = addr_of(wr);
        for (i = HEAD; i < len; i++) {
            rd[i] = mem[(a + i - HEAD) % TFW_NOR_MODEL_SIZE];
        }
        break;
    case OP_PROGRAM:
        if (len < HEAD || !writable()) { break; }
        a = addr_of(wr);
        if ((a & (PAGE - 1u)) + (uint32_t)(len - HEAD) > PAGE) {
            violations++;   /* the part wraps; the driver should have split */
        }
        for (i = HEAD; i < len; i++) {
            uint32_t at = (a & ~(uint32_t)(PAGE - 1u)) | ((a + i - HEAD) & (PAGE - 1u));
            if ((wr[i] & (uint8_t)~mem[at]) != 0u) {
                violations++;   /* a 0 cannot be programmed back to 1 */
            }
            mem[at] &= wr[i];
        }
        programs++;
        wel = 0u;
        busy = busy_polls;
        break;
    case OP_ERASE_4K:
        if (len < HEAD || !writable()) { break; }
        memset(&mem[addr_of(wr) & ~(SECTOR - 1u)], 0xFF, SECTOR);
        erases++;
        wel = 0u;
        busy = busy_polls;
        break;
    default:
        violations++;
        break;
    }
}

void tfw_nor_model_xfer(uint8_t *buf, uint16_t len)
{
    uint8_t in[TFW_NOR_MODEL_XFER_MAX];

    if (ready == 0u) {
        tfw_nor_model_reset();
    }
    if (len > sizeof in) {
        violations++;
        return;
    }
    /* Callers pass one buffer as both, as full duplex allows. */
    memcpy(in, buf, len);
    nor(in, buf, len);
}

void tfw_nor_model_load(uint32_t addr, const uint8_t *src, uint32_t len)
{
    uint32_t i;

    if (ready == 0u) {
        tfw_nor_model_reset();
    }
    for (i = 0u; i < len; i++) {
        mem[(addr + i) % TFW_NOR_MODEL_SIZE] = src[i];
    }
}
