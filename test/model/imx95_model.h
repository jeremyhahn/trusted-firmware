/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * A register model of the i.MX 95 peripherals the M7 port drives (SPEC row
 * `imx95`), for the host tests of src/hal/cortex-m/imx95/: LPUART3, the RGPIO
 * banks GPIO1 to GPIO5, WDOG5 and the NVIC words the UART's interrupt uses.
 *
 * The SoC code reaches a register only through TFW_CM_REG. The host build
 * puts test/model/cortex-m/ ahead of src/hal/cortex-m/ on the include path,
 * so its cm_regs.h is the one soc_regs.h includes, and there TFW_CM_REG(a) is
 * *tfw_imx95_model_reg(a). Nothing in the SoC code changes and no vtable is
 * involved: the substitution is the host link's.
 *
 * C gives an lvalue no hook on a read or a write, so the model tells them
 * apart afterwards. Each access gets a scratch word holding the register as
 * a read would see it; at the next access, or at tfw_imx95_model_get(), the
 * model looks at the word again. Changed, it was a write; unchanged, a read.
 * A read-modify-write is a write. LPUART STAT and DATA are presented with a
 * bit the code never reads set (STAT bit 0, DATA bit 31), so no write the
 * code makes can be mistaken for a read of them. The one limit: a full
 * expression may touch one register, which every access in the SoC
 * directory does.
 *
 * Only the registers and bits the code touches are modelled. The layouts are
 * NXP's LPUART, RGPIO and WDOG32 blocks as soc_regs.h states them, TO
 * CONFIRM against IMX95RM, which docs/reference/ does not hold
 * (docs/multi-arch/imx95-design.md section 9.4). The ELE MU's transmit
 * registers are write-only and its receive registers read-only to the
 * agent, so every access to a TR is a write and every access to an RR a
 * read, whatever the value. An access to any other
 * address is counted in `unmodelled`, and every test asserts it is zero.
 */
#ifndef TFW_TEST_IMX95_MODEL_H
#define TFW_TEST_IMX95_MODEL_H

#include <stdint.h>

#define TFW_IMX95_MODEL_RX_MAX 256u
#define TFW_IMX95_MODEL_TX_MAX 512u
#define TFW_IMX95_MODEL_BANKS  5u
#define TFW_IMX95_MODEL_SHM_WORDS 64u   /* the largest SCMI area a test maps */
#define TFW_IMX95_MODEL_ELE_WORDS 16u   /* the longest ELE message a test sends */

struct tfw_imx95_model;

/* The System Manager's side of an SCMI channel, called when the agent rings
 * the doorbell: it reads the request from `scmi.shm`, writes its answer there
 * and, to complete, sets channel free. With none set, the channel stays busy.
 * The enclave's side of the ELE MU has the same type, called once the
 * request's last word is in: it reads `ele.req` and fills `ele.rsp` and
 * `ele.rsp_len`. With none set, no response comes. */
typedef void (*tfw_imx95_model_platform_fn)(struct tfw_imx95_model *m);

struct tfw_imx95_model {
    struct {
        uint32_t global;                 /* GLOBAL, +0x08 */
        uint32_t baud;                   /* BAUD,   +0x10 */
        uint32_t ctrl;                   /* CTRL,   +0x18 */
        uint8_t  overrun;                /* STAT.OR, latched until written one */
        uint8_t  busy;                   /* set by a test: STAT.TDRE and TC clear */
        uint8_t  rx[TFW_IMX95_MODEL_RX_MAX];   /* the line's bytes, in order */
        uint32_t rx_len, rx_pos;
        uint8_t  tx[TFW_IMX95_MODEL_TX_MAX];   /* the bytes written to DATA */
        uint32_t tx_len;
        uint32_t tx_dropped;             /* DATA written with CTRL.TE clear */
        uint32_t resets;                 /* GLOBAL.RST writes */
        uint32_t baud_while_enabled;     /* BAUD written with CTRL.TE or RE set */
        uint32_t ctrl_unmasked;          /* CTRL written with PRIMASK clear */
    } lpuart;
    struct {
        uint32_t pddr;                   /* PDDR, +0x54: 1 = output */
        uint32_t pdor;                   /* the output latch PSOR/PCOR drive */
        uint32_t pins;                   /* set by a test: an input pad's level */
        uint32_t pddr_unmasked;          /* PDDR written with PRIMASK clear */
    } gpio[TFW_IMX95_MODEL_BANKS];
    struct {
        uint32_t cs;                     /* CS, +0x0, without ULK, RCS, FLG */
        uint32_t cs_written;             /* the last value written to CS */
        uint32_t toval;                  /* TOVAL, +0x8 */
        uint8_t  flg;                    /* CS.FLG, write one to clear */
        uint8_t  unlocked;               /* CS.ULK: an unlock opened the window */
        uint8_t  rcs;                    /* CS.RCS: the last configuration took */
        uint8_t  fixed;                  /* CS written with UPDATE clear */
        uint32_t unlocks, refreshes, bad_keys;
        uint32_t refused;                /* CS or TOVAL written outside the window */
        uint32_t masked_configs;         /* configurations with PRIMASK set */
    } wdog;
    struct {
        uintptr_t mu_base;               /* set by a test: the MU's base */
        uintptr_t shm_base;              /* set by a test: the area's base */
        uint32_t  shm_words;             /* set by a test: its length in words */
        uint32_t  shm[TFW_IMX95_MODEL_SHM_WORDS];
        uint32_t  gcr;                   /* MU GCR, +0x114 */
        uint32_t  rings;                 /* GCR bits raised */
        uint32_t  rung;                  /* the bits the last ring raised */
        uint32_t  status_reads;          /* reads of word 1, channel_status */
        tfw_imx95_model_platform_fn platform;
    } scmi;
    struct {
        uintptr_t mu_base;               /* set by a test: the ELE MU's base */
        uint32_t  par;                   /* set by a test: PAR, +0x004 */
        uint32_t  req[TFW_IMX95_MODEL_ELE_WORDS];   /* words through TR, +0x200 */
        uint32_t  req_len;
        uint32_t  rsp[TFW_IMX95_MODEL_ELE_WORDS];   /* words for RR, +0x280 */
        uint32_t  rsp_len, rsp_pos;
        uint32_t  tx_busy;               /* set by a test: TSR reads zero */
        uint32_t  out_of_turn;           /* TR or RR touched out of the word order */
        uint32_t  requests;              /* complete requests handed to `enclave` */
        tfw_imx95_model_platform_fn enclave;   /* answers ele.req into ele.rsp */
    } ele;
    struct {
        uint32_t iser[3];                /* enabled, 0xE000E100 */
        uint32_t icpr[3];                /* the pending bits cleared, 0xE000E280 */
    } nvic;
    uint32_t  primask;                   /* 1 while interrupts are masked */
    uint32_t  unmodelled;
    uintptr_t unmodelled_addr;
};

/* Out of reset: the LPUART at its reset values, the watchdog as the System
 * Manager hands it over (enabled, UPDATE set, CMD32EN), nothing pending. */
void tfw_imx95_model_reset(void);

/* The model, with the last access settled. Tests read and set it here. */
struct tfw_imx95_model *tfw_imx95_model_get(void);

/* Bytes arriving on LPUART3's receive line. */
void tfw_imx95_model_line_rx(const uint8_t *bytes, uint32_t n);

/* The substitutes test/model/cortex-m/cm_regs.h links in. */
volatile uint32_t *tfw_imx95_model_reg(uintptr_t addr);
uint32_t tfw_imx95_model_irq_save(void);
void tfw_imx95_model_irq_restore(uint32_t primask);
void tfw_imx95_model_irq_mask(uint32_t masked);

/* The SoC code's entry points and the reset marker, in the model's names, so a
 * test names nothing of the core (test/model/cortex-m/imx95_entry.c). */
void tfw_imx95_model_uart_interrupt(void);   /* LPUART3's handler, interrupt 64 */
void tfw_imx95_model_soc_early(void);        /* the reset handler's SoC step */
void tfw_imx95_model_watchdog_arm(void);
void tfw_imx95_model_watchdog_pet(void);
uint32_t tfw_imx95_model_reset_mark(void);
void tfw_imx95_model_reset_mark_clear(void);

#endif /* TFW_TEST_IMX95_MODEL_H */
