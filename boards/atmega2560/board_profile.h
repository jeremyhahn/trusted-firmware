/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * ATmega2560 -- Arduino Mega 2560.
 *
 * 256 KB flash, 8 KB SRAM, 4 KB EEPROM.
 *
 * A board profile states only its deviations from include/tfw/config.h. This one
 * deviates in both directions at once: many more pins, ports and timers than
 * the 328P, and enough room that none of the sizing has to be defensive.
 */
#ifndef TFW_BOARD_ATMEGA2560_H
#define TFW_BOARD_ATMEGA2560_H

#define TFW_BOARD_NAME "atmega2560"

/* 0x01 is the 328P. Board ids extend and are never renumbered, because an older
 * host must read an unknown id as unrecognised rather than as a board it knows. */
#define TFW_BOARD_ID   0x02u

/* UUIDv5(TFW_SUIT_VENDOR_ID, "atmega2560"): the SUIT class id a manifest for
 * this board names (issue #78). dc97b9d0-38d6-58e0-bb18-98bb2414d131. */
#define TFW_SUIT_CLASS_ID { 0xdcu, 0x97u, 0xb9u, 0xd0u, 0x38u, 0xd6u, 0x58u, 0xe0u, \
                            0xbbu, 0x18u, 0x98u, 0xbbu, 0x24u, 0x14u, 0xd1u, 0x31u }

/* D0..D53 are 0..53 and A0..A15 are 54..69 -- the numbers printed on the board,
 * because that is the number the person holding it can see. Unlike the 328P
 * there is no ADC-only pin here: every one of the seventy has a digital port. */
#define TFW_PIN_COUNT  70u

/* Eleven ports, indexed A=0, B=1, C=2, D=3, E=4, F=5, G=6, H=7, J=8, K=9, L=10.
 * There is no port I: the letter is skipped across the AVR family to keep it
 * from being read as a digit, so J follows H and the index is one less than the
 * letter's alphabetical position from there on. */
#define TFW_PORT_COUNT 11u

/* Thirteen usable output-compare units: Timer1 and Timer2 as on the 328P, plus
 * Timers 3, 4 and 5, minus Timer0's two which the tick has taken. */
#define TFW_PWM_COUNT  13u

#define TFW_ADC_COUNT  16u
#define TFW_EEPROM_SIZE 4096u

/* Timer0 drives the millisecond tick, which costs two pins their PWM: OC0B is
 * PG5, which the Mega numbers as pin 4, and OC0A is PB7, which is pin 13. The
 * descriptor marks both TFW_PWM_RSVD so CAPS tells a host up front rather than
 * letting it write a duty cycle into a timer that is counting something else. */
#define TFW_TICK_TIMER 0u
#define TFW_TONE_TIMER 2u

/* The wire format is shared across boards, so this is not a per-board knob even
 * though this part could afford a larger one. A Mega that accepted 128-byte
 * payloads would be a Mega that only some hosts can talk to. */
#define TFW_MAX_PAYLOAD 64u

/* Twice the 328P's, because with 8 KB of SRAM the slots are affordable and this
 * is the board people reach for when they have a lot of outputs to time. */
#define TFW_SCHED_SLOTS 16u

/* Attestation is on, as on the 328P. A device that cannot say what it is
 * running is not the device this project is for, and here the flash cost is
 * not even a consideration. */
#define TFW_FEATURE_ATTEST 1

/* A quarter of the flash and an eighth of the SRAM. The 328P takes half of each
 * because half of 32 KB is already near what the firmware needs; a quarter of
 * 256 KB is four times that in absolute terms, so the smaller fraction still
 * leaves far more headroom than the 328P has ever had.
 *
 * The budget is a gate, not an estimate: `make size-check` fails the build when
 * an image exceeds it, so flash creep is caught on the commit that causes it.
 * A budget set to the whole part would catch nothing until the day there is no
 * room left to fix it in. */
/* 256 KB of flash, and the Mega's largest BOOTSZ -- 4096 WORDS, so 8192 bytes,
 * twice the 328P's. The bootloader gate in mk/boot.mk follows this rather than
 * a constant, which is the whole point of keeping it here: tfw-boot has twice
 * the room on this part and the gate must say so rather than refusing an image
 * that fits.
 *
 * As on the 328P, BOOTRST puts the boot section at the top of flash and the
 * application region is everything below it, from the reset vector at zero.
 * 256 bytes is this part's SPM_PAGESIZE and the largest page in the family,
 * which is why it is every board's TFW_OTA_PAGE_MAX. */
#define TFW_FLASH_TOTAL       262144uL
#define TFW_BOOT_REGION_SIZE  8192uL
#define TFW_BOOT_REGION_BASE  (TFW_FLASH_TOTAL - TFW_BOOT_REGION_SIZE)

/* The one trial's deadline (decision 9), counted by the application's tick;
 * tfw-boot's 8 s watchdog only catches a hang inside it. It must cover a boot
 * (a taken store boot on the 2560: 5,334 ms at 16 MHz), the connection, one
 * attestation and the confirm. */
#define TFW_OTA_TRIAL_TIMEOUT_S 60u   /* PROPOSED, ADR open question 6 */
#define TFW_APP_REGION_BASE   0uL
#define TFW_APP_REGION_SIZE   TFW_SWAP_SLOT_LEN   /* the A/B primary, below */
#define TFW_OTA_PAGE_MAX      256u

/* The external SPI NOR store (ADR decision 6.2). STAGE is 64 KB plus the
 * manifest sector, and the ADR's table gives the 2560 no backup area. D8
 * is the proposed chip select -- ADR open question 2, not a ruling. */
#define TFW_OTA_STORE_CS_PIN      8u
#define TFW_OTA_STORE_CS_IO       H, 5u   /* D8 = PH5, for tfw-boot, which has no pin table */
#define TFW_ETH_CS_IO             B, 4u   /* D10 = PB4, the WIZnet: held high by tfw-boot */
#define TFW_SD_CS_IO              G, 5u   /* D4 = PG5, the SD socket: likewise */
#define TFW_OTA_STORE_STAGE_ADDR  0x000000ul
#define TFW_OTA_STORE_STAGE_LEN   0x10000ul   /* 65 536: 16 sectors */
#define TFW_OTA_STORE_MANIFEST    0x010000ul
#define TFW_OTA_STORE_MARK_ADDR   0x011000ul

/* A/B by swap-move (ADR decision 7; the layout under "Where every byte goes",
 * slot size open question 7, the record open question 10). The primary is the
 * 64 KB image budget plus the one move page; the secondary follows it. The
 * swap record is ten bytes of EEPROM after the network block. */
#define TFW_SWAP_PRIMARY_BASE     0x00000ul
#define TFW_SWAP_SECONDARY_BASE   0x10100ul
#define TFW_SWAP_SLOT_LEN         0x10000ul
#define TFW_SWAP_RECORD_ADDR      0x0E0u
/* The journal at the record address: two checked five-byte copies
 * (boot/boot_swap.c), 0x0E0-0x0E9. It lies past TFW_BOOT_EE_LEN, the bound the
 * wire states as EEPROMReservedEnd, so it is named here and the application's
 * EEPROM_WRITE refuses it as it refuses the platform block. */
#define TFW_BOOT_EE_EXTRA_BASE    TFW_SWAP_RECORD_ADDR
#define TFW_BOOT_EE_EXTRA_LEN     10u
#define TFW_APP_WRITE_END         (TFW_SWAP_SECONDARY_BASE + TFW_SWAP_SLOT_LEN)
#define TFW_OTA_STAGE_DEST        TFW_SWAP_SECONDARY_BASE

#define TFW_FLASH_BUDGET 65536u
#define TFW_RAM_BUDGET   1024u

/*
 * Register addresses.
 *
 * ONE definition, used by the device and by the host alike, written as plain
 * numbers so the table the host tests verify is byte for byte the table that
 * ships. avr-libc then CHECKS every one of them: the asserts at the bottom of
 * this file compare each against <avr/io.h> whenever this is compiled for AVR,
 * so a transposed digit is a compile error on the firmware build that names the
 * register rather than a wrong pin discovered on a bench.
 *
 * Every classic AVR lays out PINx, DDRx, PORTx consecutively, so one address
 * locates all three.
 *
 * Note where the ports live. A through G are in the low I/O space and so have
 * addresses below 0x60, the same neighbourhood as the 328P's three. H through L
 * are extended I/O at 0x100 and up, which is why tfw_port_desc_t holds a uint16_t
 * and not a uint8_t -- on the 328P that width looked like slack, and here it is
 * the reason this part works at all.
 */
#define TFW_ADDR_PINA 0x20u   /* DDRA 0x21, PORTA 0x22 */
#define TFW_ADDR_PINB 0x23u   /* DDRB 0x24, PORTB 0x25 */
#define TFW_ADDR_PINC 0x26u   /* DDRC 0x27, PORTC 0x28 */
#define TFW_ADDR_PIND 0x29u   /* DDRD 0x2A, PORTD 0x2B */
#define TFW_ADDR_PINE 0x2Cu   /* DDRE 0x2D, PORTE 0x2E */
#define TFW_ADDR_PINF 0x2Fu   /* DDRF 0x30, PORTF 0x31 */
#define TFW_ADDR_PING 0x32u   /* DDRG 0x33, PORTG 0x34 */
#define TFW_ADDR_PINH 0x100u  /* DDRH 0x101, PORTH 0x102 */
#define TFW_ADDR_PINJ 0x103u  /* DDRJ 0x104, PORTJ 0x105 */
#define TFW_ADDR_PINK 0x106u  /* DDRK 0x107, PORTK 0x108 */
#define TFW_ADDR_PINL 0x109u  /* DDRL 0x10A, PORTL 0x10B */

/* Timer control and output-compare registers. Timers 1, 3, 4 and 5 are 16-bit
 * and their compare registers are word-wide, so the table names the LOW byte
 * and tfw_hal_pwm_write stores a uint16_t through it -- writing the halves by
 * hand is what latches a high byte against a stale low one. Timer2 is 8-bit and
 * its OCR2A/OCR2B are single bytes with no L suffix to name. */
#define TFW_ADDR_TCCR1A 0x80u
#define TFW_ADDR_OCR1AL 0x88u
#define TFW_ADDR_OCR1BL 0x8Au
#define TFW_ADDR_TCCR3A 0x90u
#define TFW_ADDR_OCR3AL 0x98u
#define TFW_ADDR_OCR3BL 0x9Au
#define TFW_ADDR_OCR3CL 0x9Cu
#define TFW_ADDR_TCCR4A 0xA0u
#define TFW_ADDR_OCR4AL 0xA8u
#define TFW_ADDR_OCR4BL 0xAAu
#define TFW_ADDR_OCR4CL 0xACu
#define TFW_ADDR_TCCR2A 0xB0u
#define TFW_ADDR_OCR2A  0xB3u
#define TFW_ADDR_OCR2B  0xB4u
#define TFW_ADDR_TCCR5A 0x120u
#define TFW_ADDR_OCR5AL 0x128u
#define TFW_ADDR_OCR5BL 0x12Au
#define TFW_ADDR_OCR5CL 0x12Cu

/* COM bit positions within TCCRnA. Constants rather than <avr/io.h> symbols so
 * the table compiles on the host, where that header does not exist -- and the
 * values are the same ones the datasheet gives, so what the tests check is what
 * the device writes. The pattern is regular: the A unit is bit 7, B is bit 5
 * and C is bit 3, on every timer that has a C. */
#define COM1A1_BIT 7u
#define COM1B1_BIT 5u
#define COM2A1_BIT 7u
#define COM2B1_BIT 5u
#define COM3A1_BIT 7u
#define COM3B1_BIT 5u
#define COM3C1_BIT 3u
#define COM4A1_BIT 7u
#define COM4B1_BIT 5u
#define COM4C1_BIT 3u
#define COM5A1_BIT 7u
#define COM5B1_BIT 5u
#define COM5C1_BIT 3u

/* The check. avr-libc is the authority on where these live; this asserts the
 * numbers above against it, so the host tests and the shipped image cannot
 * disagree about a single register. Costs nothing at run time and is the only
 * preprocessor conditional a board profile needs. */
#ifdef __AVR__
#include <avr/io.h>
_Static_assert(TFW_ADDR_PINA   == _SFR_ADDR(PINA),   "PINA address");
_Static_assert(TFW_ADDR_PINB   == _SFR_ADDR(PINB),   "PINB address");
_Static_assert(TFW_ADDR_PINC   == _SFR_ADDR(PINC),   "PINC address");
_Static_assert(TFW_ADDR_PIND   == _SFR_ADDR(PIND),   "PIND address");
_Static_assert(TFW_ADDR_PINE   == _SFR_ADDR(PINE),   "PINE address");
_Static_assert(TFW_ADDR_PINF   == _SFR_ADDR(PINF),   "PINF address");
_Static_assert(TFW_ADDR_PING   == _SFR_ADDR(PING),   "PING address");
_Static_assert(TFW_ADDR_PINH   == _SFR_ADDR(PINH),   "PINH address");
_Static_assert(TFW_ADDR_PINJ   == _SFR_ADDR(PINJ),   "PINJ address");
_Static_assert(TFW_ADDR_PINK   == _SFR_ADDR(PINK),   "PINK address");
_Static_assert(TFW_ADDR_PINL   == _SFR_ADDR(PINL),   "PINL address");
_Static_assert(TFW_ADDR_TCCR1A == _SFR_ADDR(TCCR1A), "TCCR1A address");
_Static_assert(TFW_ADDR_OCR1AL == _SFR_ADDR(OCR1AL), "OCR1AL address");
_Static_assert(TFW_ADDR_OCR1BL == _SFR_ADDR(OCR1BL), "OCR1BL address");
_Static_assert(TFW_ADDR_TCCR2A == _SFR_ADDR(TCCR2A), "TCCR2A address");
_Static_assert(TFW_ADDR_OCR2A  == _SFR_ADDR(OCR2A),  "OCR2A address");
_Static_assert(TFW_ADDR_OCR2B  == _SFR_ADDR(OCR2B),  "OCR2B address");
_Static_assert(TFW_ADDR_TCCR3A == _SFR_ADDR(TCCR3A), "TCCR3A address");
_Static_assert(TFW_ADDR_OCR3AL == _SFR_ADDR(OCR3AL), "OCR3AL address");
_Static_assert(TFW_ADDR_OCR3BL == _SFR_ADDR(OCR3BL), "OCR3BL address");
_Static_assert(TFW_ADDR_OCR3CL == _SFR_ADDR(OCR3CL), "OCR3CL address");
_Static_assert(TFW_ADDR_TCCR4A == _SFR_ADDR(TCCR4A), "TCCR4A address");
_Static_assert(TFW_ADDR_OCR4AL == _SFR_ADDR(OCR4AL), "OCR4AL address");
_Static_assert(TFW_ADDR_OCR4BL == _SFR_ADDR(OCR4BL), "OCR4BL address");
_Static_assert(TFW_ADDR_OCR4CL == _SFR_ADDR(OCR4CL), "OCR4CL address");
_Static_assert(TFW_ADDR_TCCR5A == _SFR_ADDR(TCCR5A), "TCCR5A address");
_Static_assert(TFW_ADDR_OCR5AL == _SFR_ADDR(OCR5AL), "OCR5AL address");
_Static_assert(TFW_ADDR_OCR5BL == _SFR_ADDR(OCR5BL), "OCR5BL address");
_Static_assert(TFW_ADDR_OCR5CL == _SFR_ADDR(OCR5CL), "OCR5CL address");
_Static_assert(COM1A1_BIT == COM1A1, "COM1A1 bit position");
_Static_assert(COM1B1_BIT == COM1B1, "COM1B1 bit position");
_Static_assert(COM2A1_BIT == COM2A1, "COM2A1 bit position");
_Static_assert(COM2B1_BIT == COM2B1, "COM2B1 bit position");
_Static_assert(COM3A1_BIT == COM3A1, "COM3A1 bit position");
_Static_assert(COM3B1_BIT == COM3B1, "COM3B1 bit position");
_Static_assert(COM3C1_BIT == COM3C1, "COM3C1 bit position");
_Static_assert(COM4A1_BIT == COM4A1, "COM4A1 bit position");
_Static_assert(COM4B1_BIT == COM4B1, "COM4B1 bit position");
_Static_assert(COM4C1_BIT == COM4C1, "COM4C1 bit position");
_Static_assert(COM5A1_BIT == COM5A1, "COM5A1 bit position");
_Static_assert(COM5B1_BIT == COM5B1, "COM5B1 bit position");
_Static_assert(COM5C1_BIT == COM5C1, "COM5C1 bit position");
/* The part itself, not just its registers. A profile pointed at the wrong MCU
 * would otherwise pass every assert above and still be the wrong board. */
_Static_assert(TFW_EEPROM_SIZE == (E2END + 1u), "EEPROM size");
#endif

/* Which USART the firmware talks over.
 *
 * The Mega has four USARTs, so avr-libc indexes every vector name. USART0 is
 * the one on the header's pins 0 and 1 and the one the bootloader uses.
 *
 * The register names are pasted from TFW_UART_N in src/hal/avr/hal_uart.c; the
 * vector names are spelled out because avr-libc is not consistent about them
 * and a pasted name that does not exist compiles to an ordinary function
 * nothing calls -- a device that simply never answers.
 */
#define TFW_UART_N          0
#define TFW_UART_RX_VECT    USART0_RX_vect
#define TFW_UART_UDRE_VECT  USART0_UDRE_vect

/* The one pin tone can use: the output-compare pin of TFW_TONE_TIMER.
 * OC2A is PB4 on this part -- header pin 10, NOT pin 11. PB3 here is MISO
 * at pin 50, so a table copied from the 328P drives the SPI bus with a tone. */
#define TFW_TONE_PIN   10u

/* The SPI pins' bit positions within port B. The HAL must set their direction
 * before the peripheral works, and it cannot ask the pin table for them:
 * the SPI lines are the hardware's, not logical pins a host names.
 * PB2 MOSI, PB1 SCK, PB0 SS -- header pins 51, 52 and 53. NOT the 328P's
 * bits: PB3 here is MISO at pin 50. */
#define TFW_SPI_BIT_MOSI 2u
#define TFW_SPI_BIT_SCK  1u
#define TFW_SPI_BIT_SS   0u

/* The secure element: ATECC608C (MIKROE-6774 Click board) -- issue #7.
 *
 * The same Click and the same part as boards/atmega328p/board_profile.h, whose
 * comment carries the sources: the TrustFLEX ATECC608C at its default 0x36, on
 * a bus held to 100 kHz for the wake pulse.
 *
 * The TWI pins are PD1 SDA and PD0 SCL, header pins 20 and 21. The Mega's A4
 * and A5 are plain analog inputs, so a Click wired as on a Nano is not on the
 * bus at all. Clear of the Ethernet shield's D10/D4 chip selects and of its
 * SPI on D50-D53. */
#define TFW_ELEMENT_I2C_ADDR 0x36u
#define TFW_ELEMENT_I2C_HZ   100000uL
#define TFW_ELEMENT_PIN_SDA  20u   /* D20, PD1 */
#define TFW_ELEMENT_PIN_SCL  21u   /* D21, PD0 */

#endif /* TFW_BOARD_ATMEGA2560_H */
