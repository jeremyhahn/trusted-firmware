/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * A host register file for the mps2 SoC's watchdog code
 * (src/hal/cortex-m/mps2/hal_soc.c), which mps2_soc_model.c compiles onto
 * it: the CMSDK APB watchdog's LOAD, CTRL, INTCLR and LOCK, each write
 * applied as the watchdog applies it (Cortex-M System Design Kit TRM, Arm
 * DDI 0479), and every write logged in order. test/unit/test_mps2_soc.c
 * drives it; the calls below are hal_soc.c's, by another name, so the
 * suite spells no Cortex-M name (make lint-arch-leak).
 */
#ifndef TFW_MPS2_SOC_MODEL_H
#define TFW_MPS2_SOC_MODEL_H

#include <stdint.h>

/* Register offsets from the watchdog's base. */
#define TFW_MPS2_MODEL_LOAD   0x000u
#define TFW_MPS2_MODEL_CTRL   0x008u
#define TFW_MPS2_MODEL_INTCLR 0x00Cu
#define TFW_MPS2_MODEL_LOCK   0xC00u

#define TFW_MPS2_MODEL_LOG_MAX 32u

struct tfw_mps2_model_write {
    uint32_t off;
    uint32_t val;
    int      ignored; /* written while LOCK was shut */
};

struct tfw_mps2_model {
    uint32_t load;   /* LOAD */
    uint32_t value;  /* the counter: LOAD on a LOAD or INTCLR write */
    uint32_t ctrl;   /* CTRL, bits 0 (INTEN) and 1 (RESEN) */
    int      ris;    /* the interrupt raised; an INTCLR write clears it */
    int      locked; /* LOCK shut: writes to the others are ignored */
    struct tfw_mps2_model_write log[TFW_MPS2_MODEL_LOG_MAX];
    unsigned n;          /* writes logged */
    unsigned ignored;    /* writes ignored for the lock */
    unsigned unmodelled; /* writes outside the four registers */
};

extern struct tfw_mps2_model tfw_mps2_model;

/* Everything to zero: unlocked, stopped, nothing logged. */
void tfw_mps2_model_reset(void);
/* The log and its counters only. */
void tfw_mps2_model_clear_log(void);

/* hal_soc.c's calls; each takes the last write into the file before it
 * returns. */
void tfw_mps2_model_soc_early(void);
void tfw_mps2_model_watchdog_arm(void);
void tfw_mps2_model_watchdog_pet(void);
void tfw_mps2_model_reset_cause(uint32_t mark);

/* The reset marker hal_soc.c writes, and the mark its reset-cause call
 * handed the decoder. */
uint32_t tfw_mps2_model_reset_mark(void);
uint32_t tfw_mps2_model_decoded_mark(void);

#endif /* TFW_MPS2_SOC_MODEL_H */
