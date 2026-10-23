/* A host-only pin table for test_caps_reserved (#163): the 328P's, cut to the
 * rows that suite drives, with D3 -- a pin that has a compare unit -- marked
 * RESERVED. No shipped board has a reserved PWM-capable pin, so on the real
 * tables pwm's RESERVED term is never the one that refuses: dropping it only
 * shows on a pin that would otherwise pass the board's PWM lookup. Linked in
 * place of boards/$(HOST_BOARD)/board.c; the rows left out are zero and never
 * read. */
#include "tfw/board.h"
#include "tfw/config.h"
#include "tfw/progmem.h"
#include "board_profile.h"

const tfw_port_desc_t tfw_board_ports[TFW_PORT_COUNT] TFW_PROGMEM = {
    { TFW_ADDR_PINB },   /* 0 */
    { TFW_ADDR_PINC },   /* 1 */
    { TFW_ADDR_PIND },   /* 2 */
};

const tfw_pin_desc_t tfw_board_pins[TFW_PIN_COUNT] TFW_PROGMEM = {
    /*  3  D3  PD3 OC2B */ [3] = { TFW_PB(2u, 3u), TFW_ADC_NONE, 0u,
                                   TFW_PIN_DIGITAL | TFW_PIN_PWM | TFW_PIN_RESERVED },
    /*  9  D9  PB1 OC1A */ [9] = { TFW_PB(0u, 1u), TFW_ADC_NONE, 1u,
                                   TFW_PIN_DIGITAL | TFW_PIN_PWM },
};

const tfw_pwm_desc_t tfw_board_pwms[TFW_PWM_COUNT] TFW_PROGMEM = {
/* 0  OC2B, pin 3  */ { TFW_ADDR_TCCR2A, TFW_ADDR_OCR2B,  COM2B1_BIT, 0u },
/* 1  OC1A, pin 9  */ { TFW_ADDR_TCCR1A, TFW_ADDR_OCR1AL, COM1A1_BIT, 1u },
/* 2  OC1B, pin 10 */ { TFW_ADDR_TCCR1A, TFW_ADDR_OCR1BL, COM1B1_BIT, 1u },
/* 3  OC2A, pin 11 */ { TFW_ADDR_TCCR2A, TFW_ADDR_OCR2A,  COM2A1_BIT, 0u },
};
