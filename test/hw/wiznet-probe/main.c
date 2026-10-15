/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The probe image's main -- a PROBE, not the driver (#55). Brings up the HAL,
 * deselects the shield's SD card, runs wizprobe_run against D10 and prints the
 * raw bytes and a one-line verdict on the board's UART at TFW_UART_BAUD, every
 * two seconds, so a reader that opens the port late still sees a whole report.
 *
 * Built and flashed by `make wiznet-probe` / `make wiznet-probe-flash`
 * (mk/wiznet-probe.mk). See docs/development/wiznet-probe.md.
 */
#include "tfw/hal.h"
#include "tfw/board.h"
#include "tfw/config.h"
#include "wizprobe.h"

#define PIN_ETH_CS 10u   /* D10 on the Uno, Nano and Mega shields alike */
#define PIN_SD_CS   4u   /* the shield's microSD slot */

/* Not in hal.h: src/main.c declares it the same way, as the one caller. */
tfw_status_t tfw_hal_tick_init(void);

#define SPI_MODE0      0u
#define SPI_DIV16      1u   /* SPR1:0 = 01: 1 MHz at 16 MHz */

static void put_c(char c)
{
    while (!tfw_hal_uart_tx_try((uint8_t)c)) {
    }
}

static void put_s(const char *s)
{
    while (*s != '\0') {
        put_c(*s++);
    }
}

static void put_hex(uint8_t b)
{
    static const char digits[] = "0123456789ABCDEF";

    put_c(digits[b >> 4]);
    put_c(digits[b & 0x0Fu]);
}

static void put_bytes(const char *label, const uint8_t *b, uint8_t n)
{
    uint8_t i;

    put_s(label);
    for (i = 0u; i < n; i++) {
        put_c(' ');
        put_hex(b[i]);
    }
    put_s("\r\n");
}

static void report(const wizprobe_result_t *r, tfw_status_t st)
{
    put_s("wiznet-probe board=");
    put_s(tfw_board_name());
    put_s(" cs=D10 sd_cs=D4(high) spi=mode0/1MHz\r\n");
    put_bytes("w5500 tx=00 39 00 00 rx=", r->w5500_rx, 4u);
    put_bytes("w5200 tx=00 1F 00 01 00 rx=", r->w5200_rx, 5u);
    put_bytes("w5100 MR<-80 tx=F0 00 00 80 rx=", r->w5100_reset_rx, 4u);
    put_bytes("w5100 RTR[0] tx=0F 00 17 00 rx=", r->w5100_rtr_rx, 4u);
    put_bytes("w5100 MR=", &r->w5100_mr, 1u);
    put_bytes("w5100 RTR=", r->w5100_rtr, 2u);
    put_bytes("w5100 RCR=", &r->w5100_rcr, 1u);
    put_bytes("w5100 RTR<-0B B8 readback=", r->w5100_rtr_wb, 2u);
    if (r->enc_tried) {
        put_bytes("enc28j60 ECON1=", &r->enc_econ1, 1u);
        put_bytes("enc28j60 EREVID=", &r->enc_erevid, 1u);
    }
    if (st != TFW_OK) {
        uint8_t code = (uint8_t)st;

        put_bytes("spi status=", &code, 1u);
    }
    put_s("verdict: ");
    put_s(wizprobe_chip_name(r->chip));
    put_s("\r\n\r\n");
}

int main(void)
{
    tfw_pinref_t      eth_cs;
    tfw_pinref_t      sd_cs;
    wizprobe_result_t r;
    tfw_status_t      st;
    tfw_ms_t          last;

    tfw_hal_init();
    tfw_hal_tick_init();
    tfw_hal_uart_init(TFW_UART_BAUD);
    tfw_hal_irq_enable();

    /* Both chip-selects high before the bus comes up: an SD card that sees
     * its CS low answers on MISO and the reads below are its, not the
     * Ethernet controller's. */
    tfw_board_pin(PIN_ETH_CS, &eth_cs);
    tfw_board_pin(PIN_SD_CS, &sd_cs);
    tfw_hal_gpio_write(&sd_cs, 1u);
    tfw_hal_gpio_mode(&sd_cs, TFW_PIN_OUTPUT);
    tfw_hal_gpio_write(&eth_cs, 1u);
    tfw_hal_gpio_mode(&eth_cs, TFW_PIN_OUTPUT);
    tfw_hal_spi_init(SPI_MODE0, SPI_DIV16, 0u);

    /* The W5100 wants its reset released for some hundreds of ms before it
     * answers; the board's reset is also the shield's. */
    last = tfw_hal_millis();
    while ((tfw_ms_t)(tfw_hal_millis() - last) < 500u) {
    }

    for (;;) {
        st   = wizprobe_run(&eth_cs, &r);
        report(&r, st);
        last = tfw_hal_millis();
        while ((tfw_ms_t)(tfw_hal_millis() - last) < 2000u) {
        }
    }
}
