/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * ATmega328P -- Arduino Uno, Nano and Pro Mini. The primary target.
 *
 * 32 KB flash, 2 KB SRAM, 1 KB EEPROM.
 *
 * A board profile states only its deviations from include/tfw/config.h. This one
 * is a full-featured part, so it deviates mostly in sizing.
 */
#ifndef TFW_BOARD_ATMEGA328P_H
#define TFW_BOARD_ATMEGA328P_H

#define TFW_BOARD_NAME "atmega328p"
#define TFW_BOARD_ID   0x01u

/* UUIDv5(TFW_SUIT_VENDOR_ID, "atmega328p"): the SUIT class id a manifest for
 * this board names (issue #78). 7a83e13f-8c4f-51bb-80ab-e1b82735410e. */
#define TFW_SUIT_CLASS_ID { 0x7au, 0x83u, 0xe1u, 0x3fu, 0x8cu, 0x4fu, 0x51u, 0xbbu, \
                            0x80u, 0xabu, 0xe1u, 0xb8u, 0x27u, 0x35u, 0x41u, 0x0eu }

/* D0..D13 are 0..13; A0..A5 are 14..19; A6 and A7 are 20 and 21 and exist only
 * on the TQFP package the Nano uses. These are the numbers printed on the
 * board, because that is the number the person holding it can see. */
#define TFW_PIN_COUNT  22u
#define TFW_PORT_COUNT 3u    /* B=0, C=1, D=2 */
#define TFW_PWM_COUNT  4u
#define TFW_ADC_COUNT  8u
#define TFW_EEPROM_SIZE 1024u

/* Timer0 drives the millisecond tick, which costs pins 5 and 6 their PWM.
 * The descriptor marks them TFW_PWM_RSVD so CAPS tells a host up front rather
 * than letting it write a duty cycle into a timer that is counting something
 * else. */
#define TFW_TICK_TIMER 0u
#define TFW_TONE_TIMER 2u

#define TFW_MAX_PAYLOAD 64u
#define TFW_SCHED_SLOTS 8u

/* Attestation is on. This part has the flash for it, and a device that cannot
 * say what it is running is not the device this project is for.
 *
 * The tier it can honestly claim depends on what is on the board: with no
 * secure element the provider reports TFW_CRYPTO_SOFTWARE and refuses to sign at
 * all until a UDS has been provisioned, which is the correct behaviour for a
 * device that genuinely has no identity yet. */
#define TFW_FEATURE_ATTEST 1

/* The operator-signature auth session (issue #4) does not fit this board's
 * half of the part alongside everything else here. Measured: the atecc608a
 * tier with TFW_FEATURE_AUTH_SESSION on is 16 676 / 16 384 bytes, 292 over.
 * The element-side cost itself is small (a Verify command and a handful of
 * state), but there is no headroom left on this board to spend it in --
 * TFW_FLASH_BUDGET below is already the tightest of the three boards that
 * carry an element.
 *
 * WHAT IS LOST: a host asking for AUTH_CHALLENGE/AUTH_RESPONSE on this board
 * gets TFW_ERR_UNKNOWN_CMD and CAPS reports the feature bit clear, so it learns
 * the truth before it depends on the answer -- the same shape
 * TFW_FEATURE_ALIAS_CERT's absence on the ATmega32U4 takes. EEPROM_READ below
 * TFW_EEPROM_RESERVED_END is consequently not gated on this
 * board either; see docs/security/threat-model.md. */
#define TFW_FEATURE_AUTH_SESSION 0

/* ATTEST_CSR (issue #30) does not fit either. Measured by CI on ac493c8: the
 * atecc608a tier with it on is 16 884 / 16 384 bytes, 500 over -- the request
 * builder is small, but it lands on an image that had no room left to give.
 * The owner's ruling on #6 was that the 328P is excluded from ACME enrolment if
 * the command puts it over budget, the same way the ATmega32U4 is; it does.
 *
 * The alias certificate stays on: this excludes the possession signature only.
 *
 * WHAT IS LOST: a 328P cannot prove possession of its DeviceID key at ACME
 * finalize. A host asking for ATTEST_CSR gets TFW_ERR_UNKNOWN_CMD and CAPS
 * reports bit 15 clear. Only the ATmega2560's element tier carries it. */
#define TFW_FEATURE_ATTEST_CSR 0

/* Half the part, each. The budget is a gate, not an estimate: `make size-check`
 * fails the build when an image exceeds it, so flash creep is caught on the
 * commit that causes it rather than the day someone tries to add a feature and
 * finds there is no room. */
/* The part, and how it is split between the application and tfw-boot. BOOTSZ
 * at 2048 WORDS is 4096 bytes, and the fuse is programmed in words -- which is
 * the one place this number is easy to state at half its value. BOOTRST
 * enters the boot section at the TOP of flash and the reset vector is at
 * address zero, so the application region is everything below it.
 * include/tfw/boot.h derives the application end, the image trailer's address
 * and the largest measurable image from these; profile.mk states BOOT_BASE
 * for the linker, and mk/arch/avr.mk checks the two agree with the top of
 * flash.
 *
 * TFW_OTA_PAGE_MAX is the OTA page buffer, not the part's page: SPM_PAGESIZE
 * is 128 here and the buffer is sized for the 2560's 256, so every board
 * links the same tfw-boot RAM layout (boot/boot_ota.h). */
#define TFW_FLASH_TOTAL       32768uL
#define TFW_BOOT_REGION_SIZE  4096uL
#define TFW_BOOT_REGION_BASE  (TFW_FLASH_TOTAL - TFW_BOOT_REGION_SIZE)
#define TFW_APP_REGION_BASE   0uL
#define TFW_APP_REGION_SIZE   TFW_BOOT_REGION_BASE
#define TFW_OTA_PAGE_MAX      256u

/* The external SPI NOR store the application stages an OTA image into (issue
 * #75, docs/ota/adr-ota-image-format-and-trust.md decision 6.3). A 512 Kbit
 * part on the shield bus, on its own chip select: D8, clear of the WIZnet's D10
 * and the SD socket's D4. The pin is OPEN QUESTION 2 in the ADR -- this is the
 * proposal the stager was built against, not a ruling. The layout is the ADR's
 * table: STAGE holds the whole application region, the manifest sector follows,
 * PREV is the backup of the running image, and the MARK sector is the staged
 * record tfw-boot reads. Every area starts on a 4 KB sector. */
#define TFW_OTA_STORE_CS_PIN      8u
#define TFW_OTA_STORE_CS_IO       B, 0u   /* D8 = PB0, for tfw-boot, which has no pin table */
#define TFW_ETH_CS_IO             B, 2u   /* D10 = PB2, the WIZnet: held high by tfw-boot */
#define TFW_SD_CS_IO              D, 4u   /* D4 = PD4, the SD socket: likewise */
#define TFW_OTA_STORE_STAGE_ADDR  0x000000ul
#define TFW_OTA_STORE_STAGE_LEN   0x7000ul    /* 28 672: 7 sectors */
#define TFW_OTA_STORE_MANIFEST    0x007000ul
#define TFW_OTA_STORE_PREV_ADDR   0x008000ul
#define TFW_OTA_STORE_MARK_ADDR   0x00F000ul

#define TFW_FLASH_BUDGET 16384u
#define TFW_RAM_BUDGET   1024u

/*
 * Port register addresses.
 *
 * On the device these come from <avr/io.h>. On the host they are the same
 * numbers, written out from the datasheet -- so the table the tests verify is
 * byte for byte the table that ships, rather than a stand-in that could agree
 * with a broken one.
 *
 * Every classic AVR lays out PINx, DDRx, PORTx consecutively, so one address
 * locates all three.
 */
/* Register addresses.
 *
 * ONE definition, used by the device and by the host alike. The earlier
 * arrangement had an #ifdef __AVR__ arm taking &PINB from <avr/io.h> and an
 * #else arm with the number written out from the datasheet, which meant the
 * table the tests verified was not provably the table that shipped -- the two
 * arms could disagree and nothing would say so.
 *
 * Now the number is the only source, and avr-libc CHECKS it: the asserts at the
 * bottom of this file compare every address against <avr/io.h> whenever this is
 * compiled for AVR. A transposed digit is a compile error on the firmware
 * build, naming the register.
 */
#define TFW_ADDR_TCCR1A 0x80u
#define TFW_ADDR_OCR1AL 0x88u
#define TFW_ADDR_OCR1BL 0x8Au
#define TFW_ADDR_TCCR2A 0xB0u
#define TFW_ADDR_OCR2A  0xB3u
#define TFW_ADDR_OCR2B  0xB4u

/* COM bit positions within TCCRnA. Constants rather than <avr/io.h> symbols so
 * the table compiles on the host, where that header does not exist -- and the
 * values are the same ones the datasheet gives, so what the tests check is what
 * the device writes. */
#define COM2B1_BIT 5u
#define COM1A1_BIT 7u
#define COM1B1_BIT 5u
#define COM2A1_BIT 7u

#define TFW_ADDR_PINB 0x23u  /* DDRB 0x24, PORTB 0x25 */
#define TFW_ADDR_PINC 0x26u  /* DDRC 0x27, PORTC 0x28 */
#define TFW_ADDR_PIND 0x29u  /* DDRD 0x2A, PORTD 0x2B */

/* The check. avr-libc is the authority on where these live; this asserts the
 * numbers above against it, so the host tests and the shipped image cannot
 * disagree about a single register. Costs nothing at run time and is the only
 * preprocessor conditional a board profile needs. */
#ifdef __AVR__
#include <avr/io.h>
_Static_assert(TFW_ADDR_PINB   == _SFR_ADDR(PINB),   "PINB address");
_Static_assert(TFW_ADDR_PINC   == _SFR_ADDR(PINC),   "PINC address");
_Static_assert(TFW_ADDR_PIND   == _SFR_ADDR(PIND),   "PIND address");
_Static_assert(TFW_ADDR_TCCR1A == _SFR_ADDR(TCCR1A), "TCCR1A address");
_Static_assert(TFW_ADDR_OCR1AL == _SFR_ADDR(OCR1AL), "OCR1AL address");
_Static_assert(TFW_ADDR_OCR1BL == _SFR_ADDR(OCR1BL), "OCR1BL address");
_Static_assert(TFW_ADDR_TCCR2A == _SFR_ADDR(TCCR2A), "TCCR2A address");
_Static_assert(TFW_ADDR_OCR2A  == _SFR_ADDR(OCR2A),  "OCR2A address");
_Static_assert(TFW_ADDR_OCR2B  == _SFR_ADDR(OCR2B),  "OCR2B address");
_Static_assert(COM2B1_BIT == COM2B1, "COM2B1 bit position");
_Static_assert(COM1A1_BIT == COM1A1, "COM1A1 bit position");
_Static_assert(COM1B1_BIT == COM1B1, "COM1B1 bit position");
_Static_assert(COM2A1_BIT == COM2A1, "COM2A1 bit position");
#endif

/* Which USART the firmware talks over.
 *
 * The 328P has one USART and avr-libc spells its vectors without an index.
 *
 * The register names are pasted from TFW_UART_N in src/hal/avr/hal_uart.c; the
 * vector names are spelled out because avr-libc is not consistent about them
 * and a pasted name that does not exist compiles to an ordinary function
 * nothing calls -- a device that simply never answers.
 */
#define TFW_UART_N          0
#define TFW_UART_RX_VECT    USART_RX_vect
#define TFW_UART_UDRE_VECT  USART_UDRE_vect

/* The one pin tone can use: the output-compare pin of TFW_TONE_TIMER.
 * OC2A is PB3, which the Arduino world calls pin 11. */
#define TFW_TONE_PIN   11u

/* The SPI pins' bit positions within port B. The HAL must set their direction
 * before the peripheral works, and it cannot ask the pin table for them:
 * the SPI lines are the hardware's, not logical pins a host names.
 * PB3 MOSI, PB5 SCK, PB2 SS -- pins 11, 13 and 10. */
#define TFW_SPI_BIT_MOSI 3u
#define TFW_SPI_BIT_SCK  5u
#define TFW_SPI_BIT_SS   2u

/* The secure element: ATECC608C (MIKROE-6774 Click board) -- issue #7.
 *
 * The Click carries three ATECC608C variants on one SDA/SCL pair: TNGTLS at
 * 0x35, TFLXTLS at 0x36 and TMNGTLS at 0x38 (MikroE schematic v100,
 * download.mikroe.com/documents/add-on-boards/click/ecc608-trust-click/
 * ecc608-trust-click-schematic.pdf). The element tier talks to the TrustFLEX
 * part, whose default 7-bit address is 0x36 (Microchip DS40002515B section 3.1)
 * -- the one of the three whose slots are meant to be configured by the
 * customer. The other two are ordinary devices on the bus.
 *
 * 100 kHz because the wake pulse is the low period of a byte written to address
 * zero (src/crypto/atecc608a/atecc.c); the part itself is rated to 1 MHz
 * (DS40002515B table 10-3).
 *
 * The pins are the TWI peripheral's, fixed by the silicon: SDA is PC4, SCL is
 * PC5, A4 and A5 on the Uno, Nano and Pro Mini alike. They are stated so the
 * wiring has one written source and the board test can hold it to the pin
 * table; nothing selects them at run time. They share nothing with the
 * Ethernet shield's D10/D4 chip selects or its D11-D13 SPI lines. */
#define TFW_ELEMENT_I2C_ADDR 0x36u
#define TFW_ELEMENT_I2C_HZ   100000uL
#define TFW_ELEMENT_PIN_SDA  18u   /* A4, PC4 */
#define TFW_ELEMENT_PIN_SCL  19u   /* A5, PC5 */

#endif /* TFW_BOARD_ATMEGA328P_H */
