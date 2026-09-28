/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Compile-time configuration: feature gates and buffer sizing.
 *
 * Every knob has its default here, exactly once, so a board profile states
 * only its deviations. The resolution order is
 *
 *   command line (-D)  >  boards/<board>/board.h  >  this file
 *
 * which works because every default below is wrapped in #ifndef. A board that
 * says nothing gets the defaults; a board that says AG_FEATURE_I2C 0 drops the
 * I2C module entirely, and the Makefile then does not even compile its
 * translation unit -- the gate is file selection, not #if inside the file.
 *
 * This header and the board profiles are two of the four places in the tree
 * where a preprocessor conditional is permitted at all. The other two are
 * src/core/commands.def and src/hal/avr. A CI grep enforces it, because the
 * cost of #if scattered through logic is that no single build is ever the one
 * that was tested.
 */
#ifndef AG_CONFIG_H
#define AG_CONFIG_H

#include <stdint.h>

/* The board profile, named by the Makefile.
 *
 * It is board_profile.h and NOT board.h, deliberately. A quote-include resolves
 * against the INCLUDING file's directory first, so `#include "board.h"` from
 * this file finds include/ag/board.h -- the generic descriptor contract -- and
 * never reaches the board's own header at all. The profile's constants then
 * silently do not apply, every default below is taken instead, and the symptom
 * appears three steps later as a macro redefinition in a file that looks
 * unrelated. */
#ifdef AG_BOARD_HEADER
#include AG_BOARD_HEADER
#endif

/* ------------------------------------------------------------ capabilities */

#ifndef AG_FEATURE_GPIO
#define AG_FEATURE_GPIO        1
#endif
#ifndef AG_FEATURE_SCHED
#define AG_FEATURE_SCHED       1
#endif
#ifndef AG_FEATURE_ANALOG
#define AG_FEATURE_ANALOG      1
#endif
#ifndef AG_FEATURE_PWM
#define AG_FEATURE_PWM         1
#endif
#ifndef AG_FEATURE_TONE
#define AG_FEATURE_TONE        1
#endif
#ifndef AG_FEATURE_PULSE
#define AG_FEATURE_PULSE       1
#endif
#ifndef AG_FEATURE_EEPROM
#define AG_FEATURE_EEPROM      1
#endif
#ifndef AG_FEATURE_I2C
#define AG_FEATURE_I2C         1
#endif
#ifndef AG_FEATURE_SPI
#define AG_FEATURE_SPI         1
#endif
#ifndef AG_FEATURE_ONEWIRE
#define AG_FEATURE_ONEWIRE     1
#endif
#ifndef AG_FEATURE_ATTEST
#define AG_FEATURE_ATTEST      0   /* lands with the crypto provider */
#endif
/* The alias certificate carrying tcg-dice-UCCS-evidence.
 *
 * DEFAULT 0, and the Makefile turns it on only for a tier that has an
 * asymmetric key -- the gate is file selection, as everywhere else here. A
 * certificate is a statement that a named subject key exists, and the software
 * tier's identity is an HMAC: it has no public half, so there is nothing to
 * certify and a build that carried the code would carry a command that can only
 * ever refuse. */
#ifndef AG_FEATURE_ALIAS_CERT
#define AG_FEATURE_ALIAS_CERT  0
#endif
#ifndef AG_FEATURE_DEADMAN
#define AG_FEATURE_DEADMAN     0   /* opt-in: safe for a pump, wrong for a lamp */
#endif
/* Provisioning commands. DEFAULT 0, AND THE DEFAULT IS THE SECURITY PROPERTY.
 *
 * A production image contains no provisioning handler at all -- not a disabled
 * one, not one behind an authentication check. A device that can be
 * re-provisioned over its own command channel has no identity worth attesting
 * to, because whoever reaches the serial port becomes the manufacturer.
 *
 * Turned on only for the image flashed on a provisioning jig, which is
 * replaced by the production image before the part leaves the bench. */
#ifndef AG_FEATURE_PROVISION
#define AG_FEATURE_PROVISION   0
#endif

/* The bitmap CAPS reports. Derived, never hand-maintained -- a hand-written
 * copy of this drifts from the gates above and then the host is told the
 * firmware has a capability it does not. */
#define AG_FEATURE_BITMAP ( \
      ((uint32_t)AG_FEATURE_GPIO    <<  0) \
    | ((uint32_t)AG_FEATURE_SCHED   <<  1) \
    | ((uint32_t)AG_FEATURE_ANALOG  <<  2) \
    | ((uint32_t)AG_FEATURE_PWM     <<  3) \
    | ((uint32_t)AG_FEATURE_TONE    <<  4) \
    | ((uint32_t)AG_FEATURE_PULSE   <<  5) \
    | ((uint32_t)AG_FEATURE_EEPROM  <<  6) \
    | ((uint32_t)AG_FEATURE_I2C     <<  7) \
    | ((uint32_t)AG_FEATURE_SPI     <<  8) \
    | ((uint32_t)AG_FEATURE_ONEWIRE <<  9) \
    | ((uint32_t)AG_FEATURE_ATTEST  << 10) \
    | ((uint32_t)AG_FEATURE_DEADMAN << 11) \
    | ((uint32_t)AG_FEATURE_PROVISION << 12) \
    | ((uint32_t)AG_FEATURE_ALIAS_CERT << 13) )

/* ------------------------------------------------------- board identity */
/* A board profile supplies these. The host build has no board and takes the
 * defaults, which is why they live here rather than as #ifndef guards inside
 * the capability modules that read them -- those are logic, and logic carries
 * no conditionals. */

#ifndef AG_BOARD_NAME
#define AG_BOARD_NAME "host"
#endif
#ifndef AG_BOARD_ID
#define AG_BOARD_ID   0x00u
#endif
#ifndef AG_PIN_COUNT
#define AG_PIN_COUNT  0u
#endif
#ifndef AG_EEPROM_SIZE
#define AG_EEPROM_SIZE 1024u
#endif

/* ---------------------------------------------------- flash geometry, for ag-boot
 *
 * The part's total flash and the size of its boot section. A board profile
 * states both; the defaults below describe an ATmega328P with BOOTSZ at its
 * largest setting, which is what docs/security/fuses-and-lockbits.md specifies.
 *
 * include/ag/boot.h derives the application end, the trailer address and the
 * largest measurable image from these, so a board that changes BOOTSZ cannot
 * leave a stale application-end address behind it -- which would put the
 * trailer at an address the bootloader does not look at, and the device would
 * refuse every image with "no trailer".
 *
 * AG_BOOT_SECTION_SIZE is BOOTSZ IN BYTES, not in words. The fuse is programmed
 * in words (2048 words on the 328P) and the datasheet tables are in words,
 * which is the one place this number is easy to state at half its value. */
#ifndef AG_FLASH_TOTAL
#define AG_FLASH_TOTAL 32768uL
#endif
#ifndef AG_BOOT_SECTION_SIZE
#define AG_BOOT_SECTION_SIZE 4096uL
#endif

/* How many single-ended ADC channels the part has. The HAL bounds against this
 * rather than a literal 8, because a Mega has sixteen and reading channel 12 on
 * a part with eight must be refused rather than folded into channel 4. */
#ifndef AG_ADC_COUNT
#define AG_ADC_COUNT  8u
#endif

/* ----------------------------------------------------------------- sizing */

/* The largest payload this build accepts or emits. Drives both frame buffers
 * and therefore most of the static RAM footprint. */
#ifndef AG_MAX_PAYLOAD
#define AG_MAX_PAYLOAD  64u
#endif

/* Scheduler slots: one per pin that can hold a timed level. */
#ifndef AG_SCHED_SLOTS
#define AG_SCHED_SLOTS  8u
#endif

#ifndef AG_UART_BAUD
#define AG_UART_BAUD    115200UL
#endif

/* How long a partially-received frame may sit before the framer abandons it.
 * Without this a truncated frame holds the machine until the next byte
 * arrives -- and that byte is usually the start of a good frame, which then
 * gets consumed as the dead frame's tail. One lost frame becomes two. */
#ifndef AG_FRAME_IDLE_MS
#define AG_FRAME_IDLE_MS 250u
#endif

/* The ATECC608A's 7-bit I2C address, refused by the generic I2C_XFER command.
 *
 * Refused whether or not an element is fitted: a build that allowed it when
 * absent would allow it the moment one was added, and the host that learned the
 * address works is the one that keeps using it. A board with an element
 * somewhere else overrides this.
 *
 * It is a DEFAULT and therefore lives here. src/caps/bus.c used to carry its
 * own #ifndef, which the whitelist forbids for a reason that is not pedantry:
 * a default defined next to its use is a default the board profile cannot
 * actually override, because config.h has already been included by then. */
#ifndef AG_I2C_RESERVED_ADDR
#define AG_I2C_RESERVED_ADDR 0x60u
#endif

/* Defaults ag_bus_init() (src/caps/bus.c) applies at boot, before the host has
 * sent I2C_CONFIG or SPI_CONFIG -- so a sensor wired up and read without
 * either command still works. 100 kHz is I2C Standard-mode, chosen for the
 * same reason src/crypto/atecc608a/provider.c picks it for the element: it is
 * the speed every I2C part in the wild is guaranteed to accept. SPI mode 0,
 * the slowest divider and MSB-first are the correspondingly conservative
 * choice -- easy to change once a host knows what is on the bus, and quiet by
 * default before it does.
 * MSB and clkdiv are the raw bits ag_hal_spi_init already takes: clkdiv 3 is
 * SPCR's SPR1:SPR0 = 11 (Fosc/128 with SPI2X clear), the slowest this HAL can
 * ask the peripheral for. */
#ifndef AG_I2C_DEFAULT_HZ
#define AG_I2C_DEFAULT_HZ 100000uL
#endif
#ifndef AG_SPI_DEFAULT_MODE
#define AG_SPI_DEFAULT_MODE 0u
#endif
#ifndef AG_SPI_DEFAULT_CLKDIV
#define AG_SPI_DEFAULT_CLKDIV 3u
#endif
#ifndef AG_SPI_DEFAULT_LSB_FIRST
#define AG_SPI_DEFAULT_LSB_FIRST 0u
#endif

/* The interval I2C_CONFIG accepts at the CAPABILITY layer, before F_CPU is
 * even in scope: zero, and anything past Fast-mode, 400 kHz -- the ceiling
 * every TWI peripheral in this family is actually RATED to, not merely the
 * fastest divisor the maths can produce. A 16 MHz board's TWBR can express
 * 1 MHz (Fast-mode-plus, TWBR=0), but the silicon's own electrical
 * characteristics are specified only through Fast-mode; accepting a value
 * past that would be honouring a request the hardware was never qualified to
 * meet, not merely a fast one. Not a claim that every board can hit every
 * value inside this interval, either -- ag_hal_i2c_init's divisor is an
 * 8-bit register and F_CPU varies across this family, so THAT reachability
 * check is a HAL fact this layer cannot see and does not attempt; see the
 * comment in src/hal/avr/hal_bus.c. A value this generic bound lets through
 * and the HAL cannot reach still comes back AG_ERR_RANGE, not a silently
 * wrong clock. */
#ifndef AG_I2C_HZ_MIN
#define AG_I2C_HZ_MIN 1000uL
#endif
#ifndef AG_I2C_HZ_MAX
#define AG_I2C_HZ_MAX 400000uL
#endif

/* A ceiling on how long any single command may hold the main loop. Past this a
 * scheduled pin could miss its deadline, and once a deadman exists a long
 * capture would trip it. A board with no scheduler in its build could raise it;
 * that is why it is a knob and not a constant. */
#ifndef AG_PULSE_MAX_US
#define AG_PULSE_MAX_US 50000UL
#endif

/* A scheduled interval may not exceed 2^31 ms. Above that the signed-difference
 * comparison the scheduler uses to survive the 49.7-day millisecond wrap stops
 * being able to tell "due" from "not yet", so the bound is enforced when a
 * timer is armed and the failure is a clean AG_ERR_RANGE rather than a valve
 * that stays open for seven weeks. */
#define AG_SCHED_MAX_MS 0x7FFFFFFFuL

/* Derived frame geometry. LEN counts SEQ, OP, the optional STATUS and the
 * payload; the frame adds SOF, LEN and two CRC bytes on top. */
#define AG_MAX_LEN      ((uint8_t)(AG_MAX_PAYLOAD + 3u))
#define AG_MAX_FRAME    ((uint16_t)(AG_MAX_LEN + AG_FRAME_OVERHEAD))

/* LEN is one byte and must be able to express AG_MAX_LEN. A profile that sets
 * AG_MAX_PAYLOAD too high is a compile error here rather than a truncation
 * discovered on the wire. */
_Static_assert(AG_MAX_PAYLOAD + 3u <= 255u,
               "AG_MAX_PAYLOAD too large: LEN must fit in one byte");
_Static_assert(AG_SCHED_SLOTS >= 1u && AG_SCHED_SLOTS <= 32u,
               "AG_SCHED_SLOTS out of range");

#endif /* AG_CONFIG_H */
