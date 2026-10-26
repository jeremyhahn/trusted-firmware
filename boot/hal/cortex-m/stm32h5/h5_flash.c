/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw-boot's STM32H5 flash driver (#14); h5_flash.h has the contract. The
 * sequences follow ST's HAL flash driver at the Non-secure registers, not yet
 * checked at the commit docs/boards/stm32h573i-dk.md pins: the unlock (FLASH_KEY1 then FLASH_KEY2 into NSKEYR,
 * LOCK then clear), FLASH_Program_QuadWord (PG, four word stores),
 * FLASH_Erase_Sector (SER, SNB, BKSEL for bank 2, STRT in one write) and
 * FLASH_WaitForLastOperation (BSY, WBNE and DBNE clear, then the error flags
 * read and cleared through NSCCR, then EOP).
 *
 * No interrupt is enabled in tfw-boot (boot/boot_hal.h), so the four stores of
 * a quad-word need no critical section here.
 */

#include <stdint.h>
#include <string.h>

#include "tfw/status.h"
#include "stm32h5/soc_regs.h"
#include "h5_flash.h"

#define H5_REG(f, off)   ((f)->regs[(off) / 4u])
#define H5_FLASH_SIZE    (2u * TFW_H5_FLASH_BANK_SIZE)

/* One sector, for the rewrite. */
static uint8_t sector_buf[TFW_H5_FLASH_SECTOR];

static int qword_blank(const uint8_t *q)
{
    uint8_t i;

    for (i = 0u; i < TFW_H5_FLASH_QWORD; i++) {
        if (q[i] != 0xFFu) {
            return 0;
        }
    }
    return 1;
}

/* Lays the part of [off, off + len) that falls in [dst_off, dst_off +
 * dst_len) over dst, which holds the flash from dst_off. */
static void overlay(uint8_t *dst, uint32_t dst_off, uint32_t dst_len,
                    uint32_t off, const uint8_t *data, uint32_t len)
{
    uint32_t lo = off > dst_off ? off : dst_off;
    uint32_t hi = off + len < dst_off + dst_len ? off + len : dst_off + dst_len;

    if (lo < hi) {
        memcpy(dst + (lo - dst_off), data + (lo - off), hi - lo);
    }
}

/* The quad-word at qoff as the update leaves it. */
static void merged_qword(const struct tfw_h5_flash *f, uint32_t qoff, uint8_t *q,
                         uint32_t off, const uint8_t *data, uint32_t len)
{
    memcpy(q, f->mem + qoff, TFW_H5_FLASH_QWORD);
    overlay(q, qoff, TFW_H5_FLASH_QWORD, off, data, len);
}

/* After an operation is started: waits for it, then reads and clears the
 * error flags and EOP. */
static tfw_status_t finish(const struct tfw_h5_flash *f)
{
    uint32_t sr;

    tfw_h5_flash_wait(f);
    sr = H5_REG(f, TFW_H5_FLASH_NSSR_OFF);
    if ((sr & TFW_H5_FLASH_SR_ERRORS) != 0u) {
        H5_REG(f, TFW_H5_FLASH_NSCCR_OFF) = sr & TFW_H5_FLASH_SR_ERRORS;
        return (sr & TFW_H5_FLASH_SR_WRPERR) != 0u ? TFW_ERR_PROTECTED_REGION
                                                   : TFW_ERR_INTERNAL;
    }
    if ((sr & TFW_H5_FLASH_SR_EOP) != 0u) {
        H5_REG(f, TFW_H5_FLASH_NSCCR_OFF) = TFW_H5_FLASH_SR_EOP;
    }
    return TFW_OK;
}

/* The key sequence, when the interface is locked. A wrong sequence locks it
 * until the next reset, so LOCK still set after it is final. Flags an earlier
 * operation left are cleared: a set error flag refuses the next one. */
static tfw_status_t unlock(const struct tfw_h5_flash *f)
{
    uint32_t sr;

    if ((H5_REG(f, TFW_H5_FLASH_NSCR_OFF) & TFW_H5_FLASH_CR_LOCK) != 0u) {
        H5_REG(f, TFW_H5_FLASH_NSKEYR_OFF) = TFW_H5_FLASH_KEY1;
        H5_REG(f, TFW_H5_FLASH_NSKEYR_OFF) = TFW_H5_FLASH_KEY2;
    }
    tfw_h5_flash_wait(f);
    if ((H5_REG(f, TFW_H5_FLASH_NSCR_OFF) & TFW_H5_FLASH_CR_LOCK) != 0u) {
        return TFW_ERR_PROTECTED_REGION;
    }
    sr = H5_REG(f, TFW_H5_FLASH_NSSR_OFF) & (TFW_H5_FLASH_SR_ERRORS | TFW_H5_FLASH_SR_EOP);
    if (sr != 0u) {
        H5_REG(f, TFW_H5_FLASH_NSCCR_OFF) = sr;
    }
    return TFW_OK;
}

static tfw_status_t program_qword(const struct tfw_h5_flash *f, uint32_t qoff,
                                  const uint8_t *q)
{
    volatile uint32_t *dst = (volatile uint32_t *)(void *)(f->mem + qoff);
    uint32_t w[TFW_H5_FLASH_QWORD / 4u];
    tfw_status_t st;
    uint8_t i;

    memcpy(w, q, sizeof w);
    H5_REG(f, TFW_H5_FLASH_NSCR_OFF) |= TFW_H5_FLASH_CR_PG;
    for (i = 0u; i < TFW_H5_FLASH_QWORD / 4u; i++) {
        dst[i] = w[i];
    }
    st = finish(f);
    H5_REG(f, TFW_H5_FLASH_NSCR_OFF) &= ~TFW_H5_FLASH_CR_PG;
    return st;
}

static tfw_status_t erase_sector(const struct tfw_h5_flash *f, uint32_t soff)
{
    uint32_t snb = (soff % TFW_H5_FLASH_BANK_SIZE) / TFW_H5_FLASH_SECTOR;
    uint32_t bk  = soff >= TFW_H5_FLASH_BANK_SIZE ? TFW_H5_FLASH_CR_BKSEL : 0u;
    uint32_t cr  = H5_REG(f, TFW_H5_FLASH_NSCR_OFF) &
                   ~(TFW_H5_FLASH_CR_BKSEL | TFW_H5_FLASH_CR_SNB);
    tfw_status_t st;

    H5_REG(f, TFW_H5_FLASH_NSCR_OFF) = cr | TFW_H5_FLASH_CR_SER | bk |
                                       (snb << TFW_H5_FLASH_CR_SNB_POS) |
                                       TFW_H5_FLASH_CR_STRT;
    st = finish(f);
    H5_REG(f, TFW_H5_FLASH_NSCR_OFF) &= ~(TFW_H5_FLASH_CR_SER | TFW_H5_FLASH_CR_BKSEL |
                                          TFW_H5_FLASH_CR_SNB);
    return st;
}

/* Some quad-word the update changes is already programmed. */
static int needs_erase(const struct tfw_h5_flash *f, uint32_t off,
                       const uint8_t *data, uint32_t len)
{
    uint8_t q[TFW_H5_FLASH_QWORD];
    uint32_t qoff;

    for (qoff = off & ~(TFW_H5_FLASH_QWORD - 1u); qoff < off + len; qoff += TFW_H5_FLASH_QWORD) {
        merged_qword(f, qoff, q, off, data, len);
        if (!qword_blank(f->mem + qoff) &&
            memcmp(q, f->mem + qoff, TFW_H5_FLASH_QWORD) != 0) {
            return 1;
        }
    }
    return 0;
}

/* Every quad-word the update changes is erased: program those. */
static tfw_status_t program_span(const struct tfw_h5_flash *f, uint32_t off,
                                 const uint8_t *data, uint32_t len)
{
    uint8_t q[TFW_H5_FLASH_QWORD];
    uint32_t qoff;
    tfw_status_t st = TFW_OK;

    for (qoff = off & ~(TFW_H5_FLASH_QWORD - 1u);
         st == TFW_OK && qoff < off + len; qoff += TFW_H5_FLASH_QWORD) {
        merged_qword(f, qoff, q, off, data, len);
        if (memcmp(q, f->mem + qoff, TFW_H5_FLASH_QWORD) != 0) {
            st = program_qword(f, qoff, q);
        }
    }
    return st;
}

/* Each sector the update touches: copied, overlaid, erased, programmed back,
 * its erased quad-words left erased. */
static tfw_status_t rewrite_span(const struct tfw_h5_flash *f, uint32_t off,
                                 const uint8_t *data, uint32_t len)
{
    uint32_t soff;
    uint32_t i;
    tfw_status_t st = TFW_OK;

    for (soff = off & ~(TFW_H5_FLASH_SECTOR - 1u);
         st == TFW_OK && soff < off + len; soff += TFW_H5_FLASH_SECTOR) {
        memcpy(sector_buf, f->mem + soff, TFW_H5_FLASH_SECTOR);
        overlay(sector_buf, soff, TFW_H5_FLASH_SECTOR, off, data, len);
        st = erase_sector(f, soff);
        for (i = 0u; st == TFW_OK && i < TFW_H5_FLASH_SECTOR; i += TFW_H5_FLASH_QWORD) {
            if (!qword_blank(sector_buf + i)) {
                st = program_qword(f, soff + i, sector_buf + i);
            }
        }
    }
    return st;
}

tfw_status_t tfw_h5_flash_update(const struct tfw_h5_flash *f, uint32_t off,
                                 const uint8_t *data, uint32_t len)
{
    tfw_status_t st;

    if (off > H5_FLASH_SIZE || len > H5_FLASH_SIZE - off) {
        return TFW_ERR_RANGE;
    }
    if (len == 0u || memcmp(f->mem + off, data, len) == 0) {
        return TFW_OK;
    }
    st = unlock(f);
    if (st == TFW_OK) {
        st = needs_erase(f, off, data, len) ? rewrite_span(f, off, data, len)
                                            : program_span(f, off, data, len);
    }
    H5_REG(f, TFW_H5_FLASH_NSCR_OFF) |= TFW_H5_FLASH_CR_LOCK;
    if (st == TFW_OK && memcmp(f->mem + off, data, len) != 0) {
        st = TFW_ERR_INTERNAL;
    }
    return st;
}
