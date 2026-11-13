/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The STM32H5 buses: I2C, a polled master on I2C4, the instance ST's BSP
 * drives on the STM32H573I-DK (PB8 SCL, PB9 SDA; soc_regs.h cites each
 * register, bit and timing value). The block sends START, the address, the
 * bytes and their acknowledges itself; this file loads CR2 with the address,
 * direction and count, moves each byte through TXDR or RXDR as TXIS or RXNE
 * asks, and ends every transfer with a software STOP (AUTOEND stays 0), so
 * a write followed by a read is a repeated START once the write's TC is set.
 *
 * The status codes are the AVR TWI driver's and the MPS2 SBCon driver's for
 * the same cases (src/hal/avr/hal_bus.c, src/hal/cortex-m/mps2/hal_bus.c): an
 * address or data byte nobody acknowledges (NACKF) is TFW_ERR_NACK; a bus that
 * does not come free (BUSY held before START, a flag that never comes, a bus
 * error or lost arbitration) is TFW_ERR_TIMEOUT. STOP is sent on every return
 * that reached the bus; when STOPF does not follow, the block is reset by
 * clearing PE, as ST's HAL regenerates a STOP when the bus stays busy after a
 * NACK (stm32h5xx_hal_i2c.c, I2C_IsErrorOccurred).
 *
 * SPI and 1-Wire are not driven on this board: the profile says
 * TFW_FEATURE_SPI 0 and TFW_FEATURE_ONEWIRE 0. Their entry points are here
 * because src/caps/bus.c is one translation unit with all three buses'
 * handlers, and they refuse with TFW_ERR_UNSUPPORTED.
 */
#include <stdint.h>

#include "tfw/hal.h"
#include "soc_regs.h"
#include "h5_calc.h"

/* A flag not seen in this many microseconds is a stuck bus, not clock
 * stretching: 25 ms is more than 250 bit times at 100 kHz. */
#define H5_I2C_POLL_US 25000u

#define H5_I2C_ICR_ALL (TFW_H5_I2C_ICR_NACKCF | TFW_H5_I2C_ICR_STOPCF | \
                        TFW_H5_I2C_ICR_BERRCF | TFW_H5_I2C_ICR_ARLOCF)

static void i2c_pins(void)
{
    uint32_t bank = tfw_h5_gpio_base(TFW_H5_GPIOA_BASE, TFW_H5_GPIO_STRIDE, TFW_H5_I2C_BANK);

    TFW_H5_RCC_AHB2ENR |= 1u << TFW_H5_I2C_BANK;
    (void)TFW_H5_RCC_AHB2ENR;                   /* the enable lands before the bank is written */
    TFW_H5_GPIO_OTYPER(bank) |= (1u << TFW_H5_I2C_SCL_PIN) | (1u << TFW_H5_I2C_SDA_PIN);
    TFW_H5_GPIO_PUPDR(bank) = tfw_h5_field2(tfw_h5_field2(TFW_H5_GPIO_PUPDR(bank),
                                                          TFW_H5_I2C_SCL_PIN, TFW_H5_PULL_UP),
                                            TFW_H5_I2C_SDA_PIN, TFW_H5_PULL_UP);
    TFW_H5_GPIO_AFRH(bank) = tfw_h5_field4(tfw_h5_field4(TFW_H5_GPIO_AFRH(bank),
                                                         TFW_H5_I2C_SCL_PIN, TFW_H5_I2C_AF),
                                           TFW_H5_I2C_SDA_PIN, TFW_H5_I2C_AF);
    TFW_H5_GPIO_MODER(bank) = tfw_h5_field2(tfw_h5_field2(TFW_H5_GPIO_MODER(bank),
                                                          TFW_H5_I2C_SCL_PIN, TFW_H5_MODE_AF),
                                            TFW_H5_I2C_SDA_PIN, TFW_H5_MODE_AF);
}

tfw_status_t tfw_hal_i2c_init(uint32_t hz)
{
    uint32_t timing;

    /* The two rates ST's table gives, the nearer one at or below the rate
     * asked, so the clock never runs faster than asked (the TWI driver's
     * rule). Below standard mode there is no table row: refused, not
     * clamped. */
    if (hz >= 400000uL) {
        timing = TFW_H5_I2C_TIMING_400K;
    } else if (hz >= 100000uL) {
        timing = TFW_H5_I2C_TIMING_100K;
    } else {
        return TFW_ERR_RANGE;
    }
    i2c_pins();
    TFW_H5_RCC_APB3ENR |= TFW_H5_RCC_APB3ENR_I2C4EN;
    (void)TFW_H5_RCC_APB3ENR;
    TFW_H5_I2C_CR1 = 0u;                        /* TIMINGR is written with PE clear */
    TFW_H5_I2C_TIMINGR = timing;
    TFW_H5_I2C_CR1 = TFW_H5_I2C_CR1_PE;         /* analog filter on, no digital filter */
    return TFW_OK;
}

/* Waits for one of want in ISR. A NACK ends the wait as TFW_ERR_NACK, a bus
 * error or lost arbitration as TFW_ERR_TIMEOUT. */
static tfw_status_t i2c_wait(uint32_t want)
{
    uint32_t n;

    for (n = 0u; n < H5_I2C_POLL_US; n++) {
        uint32_t isr = TFW_H5_I2C_ISR;
        if ((isr & TFW_H5_I2C_ISR_NACKF) != 0u) {
            return TFW_ERR_NACK;
        }
        if ((isr & (TFW_H5_I2C_ISR_BERR | TFW_H5_I2C_ISR_ARLO)) != 0u) {
            return TFW_ERR_TIMEOUT;
        }
        if ((isr & want) != 0u) {
            return TFW_OK;
        }
        tfw_hal_delay_us(1u);
    }
    return TFW_ERR_TIMEOUT;
}

/* The STOP every transfer that reached the bus ends with. A STOP that does
 * not complete is a bus held low: the block is reset (PE cleared, then set)
 * and the transfer is TFW_ERR_TIMEOUT. */
static tfw_status_t i2c_stop(tfw_status_t st)
{
    uint32_t n;

    TFW_H5_I2C_CR2 |= TFW_H5_I2C_CR2_STOP;
    for (n = 0u; n < H5_I2C_POLL_US; n++) {
        if ((TFW_H5_I2C_ISR & TFW_H5_I2C_ISR_STOPF) != 0u) {
            break;
        }
        tfw_hal_delay_us(1u);
    }
    if (n == H5_I2C_POLL_US) {
        TFW_H5_I2C_CR1 = 0u;
        TFW_H5_I2C_CR1 = TFW_H5_I2C_CR1_PE;
        st = TFW_ERR_TIMEOUT;
    }
    if (st != TFW_OK) {
        /* TXIS asks for the next byte while one is still on the wire: a NACK
         * of that one leaves the next in TXDR. TXE=1 flushes it (RM0481) so
         * the next transfer cannot send it ahead of its own payload. */
        TFW_H5_I2C_ISR = TFW_H5_I2C_ISR_TXE;
    }
    TFW_H5_I2C_ICR = H5_I2C_ICR_ALL;
    return st;
}

static uint32_t i2c_cr2(uint8_t addr7, uint8_t len, uint32_t rd)
{
    return ((uint32_t)addr7 << (TFW_H5_I2C_CR2_SADD_POS + 1u)) | rd |
           ((uint32_t)len << TFW_H5_I2C_CR2_NBYTES_POS) | TFW_H5_I2C_CR2_START;
}

tfw_status_t tfw_hal_i2c_xfer(uint8_t addr7, const uint8_t *wr, uint8_t wlen,
                            uint8_t *rd, uint8_t rlen)
{
    tfw_status_t st = TFW_OK;
    uint32_t     n;
    uint8_t      i;

    if (wlen == 0u && rlen == 0u) {
        return TFW_OK;
    }
    for (n = 0u; (TFW_H5_I2C_ISR & TFW_H5_I2C_ISR_BUSY) != 0u; n++) {
        if (n == H5_I2C_POLL_US) {
            return TFW_ERR_TIMEOUT;             /* never on the bus: nothing to stop */
        }
        tfw_hal_delay_us(1u);
    }
    TFW_H5_I2C_ICR = H5_I2C_ICR_ALL;
    if (wlen) {
        TFW_H5_I2C_CR2 = i2c_cr2(addr7, wlen, 0u);
        for (i = 0u; st == TFW_OK && i < wlen; i++) {
            st = i2c_wait(TFW_H5_I2C_ISR_TXIS);
            if (st == TFW_OK) {
                TFW_H5_I2C_TXDR = wr[i];
            }
        }
        if (st == TFW_OK) {
            st = i2c_wait(TFW_H5_I2C_ISR_TC);
        }
    }
    if (st == TFW_OK && rlen) {
        TFW_H5_I2C_CR2 = i2c_cr2(addr7, rlen, TFW_H5_I2C_CR2_RD_WRN);
        for (i = 0u; st == TFW_OK && i < rlen; i++) {
            st = i2c_wait(TFW_H5_I2C_ISR_RXNE);
            if (st == TFW_OK) {
                rd[i] = (uint8_t)TFW_H5_I2C_RXDR;
            }
        }
        if (st == TFW_OK) {
            st = i2c_wait(TFW_H5_I2C_ISR_TC);
        }
    }
    return i2c_stop(st);
}

tfw_status_t tfw_hal_spi_init(uint8_t mode, uint8_t clkdiv, uint8_t lsb_first)
{
    (void)mode;
    (void)clkdiv;
    (void)lsb_first;
    return TFW_ERR_UNSUPPORTED;
}

tfw_status_t tfw_hal_spi_xfer(const tfw_pinref_t *cs, const uint8_t *wr,
                            uint8_t *rd, uint8_t len)
{
    (void)cs;
    (void)wr;
    (void)rd;
    (void)len;
    return TFW_ERR_UNSUPPORTED;
}

tfw_status_t tfw_hal_ow_reset(const tfw_pinref_t *p, uint8_t *out_presence)
{
    (void)p;
    (void)out_presence;
    return TFW_ERR_UNSUPPORTED;
}

tfw_status_t tfw_hal_ow_write(const tfw_pinref_t *p, const uint8_t *buf, uint8_t len)
{
    (void)p;
    (void)buf;
    (void)len;
    return TFW_ERR_UNSUPPORTED;
}

tfw_status_t tfw_hal_ow_read(const tfw_pinref_t *p, uint8_t *buf, uint8_t len)
{
    (void)p;
    (void)buf;
    (void)len;
    return TFW_ERR_UNSUPPORTED;
}
