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
 * says nothing gets the defaults; a board that says TFW_FEATURE_I2C 0 drops the
 * I2C module entirely, and the Makefile then does not even compile its
 * translation unit -- the gate is file selection, not #if inside the file.
 *
 * This header and the board profiles are two of the four places in the tree
 * where a preprocessor conditional is permitted at all. The other two are
 * src/core/commands.def and src/hal/<arch>/. A CI grep enforces it, because the
 * cost of #if scattered through logic is that no single build is ever the one
 * that was tested.
 */
#ifndef TFW_CONFIG_H
#define TFW_CONFIG_H

#include <stdint.h>

/* The board profile, named by the Makefile.
 *
 * It is board_profile.h and NOT board.h, deliberately. A quote-include resolves
 * against the INCLUDING file's directory first, so `#include "board.h"` from
 * this file finds include/tfw/board.h -- the generic descriptor contract -- and
 * never reaches the board's own header at all. The profile's constants then
 * silently do not apply, every default below is taken instead, and the symptom
 * appears three steps later as a macro redefinition in a file that looks
 * unrelated. */
#ifdef TFW_BOARD_HEADER
#include TFW_BOARD_HEADER
#endif

/* ------------------------------------------------------------ capabilities */

#ifndef TFW_FEATURE_GPIO
#define TFW_FEATURE_GPIO        1
#endif
#ifndef TFW_FEATURE_SCHED
#define TFW_FEATURE_SCHED       1
#endif
#ifndef TFW_FEATURE_ANALOG
#define TFW_FEATURE_ANALOG      1
#endif
#ifndef TFW_FEATURE_PWM
#define TFW_FEATURE_PWM         1
#endif
#ifndef TFW_FEATURE_TONE
#define TFW_FEATURE_TONE        1
#endif
#ifndef TFW_FEATURE_PULSE
#define TFW_FEATURE_PULSE       1
#endif
#ifndef TFW_FEATURE_EEPROM
#define TFW_FEATURE_EEPROM      1
#endif
#ifndef TFW_FEATURE_I2C
#define TFW_FEATURE_I2C         1
#endif
#ifndef TFW_FEATURE_SPI
#define TFW_FEATURE_SPI         1
#endif
#ifndef TFW_FEATURE_ONEWIRE
#define TFW_FEATURE_ONEWIRE     1
#endif
#ifndef TFW_FEATURE_ATTEST
#define TFW_FEATURE_ATTEST      0   /* lands with the crypto provider */
#endif
/* The alias certificate carrying tcg-dice-UCCS-evidence.
 *
 * DEFAULT 0, and the Makefile turns it on only for a tier that has an
 * asymmetric key -- the gate is file selection, as everywhere else here. A
 * certificate is a statement that a named subject key exists, and the software
 * tier's identity is an HMAC: it has no public half, so there is nothing to
 * certify and a build that carried the code would carry a command that can only
 * ever refuse. */
#ifndef TFW_FEATURE_ALIAS_CERT
#define TFW_FEATURE_ALIAS_CERT  0
#endif
#ifndef TFW_FEATURE_DEADMAN
#define TFW_FEATURE_DEADMAN     0   /* opt-in: safe for a motor, wrong for a lamp */
#endif
/* The operator-signature AUTH_CHALLENGE/AUTH_RESPONSE session -- issue #4.
 *
 * DEFAULT 0, turned on by the Makefile only for CRYPTO=atecc608a, the same
 * gate-by-file-selection shape as TFW_FEATURE_ALIAS_CERT just above and for a
 * related reason: the only software-tier primitive that could check an
 * external P-256/Ed25519 signature, ed25519-donna, does not fit this device
 * family (see docs/security/threat-model.md). A soft-tier or CRYPTO=none
 * image compiles in neither AUTH_CHALLENGE/AUTH_RESPONSE nor the gate they
 * would otherwise require on reserved-region EEPROM_READ -- an accepted gap on
 * those tiers, not a hidden one. */
#ifndef TFW_FEATURE_AUTH_SESSION
#define TFW_FEATURE_AUTH_SESSION 0
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
#ifndef TFW_FEATURE_PROVISION
#define TFW_FEATURE_PROVISION   0
#endif

/* The bitmap CAPS reports. Derived, never hand-maintained -- a hand-written
 * copy of this drifts from the gates above and then the host is told the
 * firmware has a capability it does not. */
#define TFW_FEATURE_BITMAP ( \
      ((uint32_t)TFW_FEATURE_GPIO    <<  0) \
    | ((uint32_t)TFW_FEATURE_SCHED   <<  1) \
    | ((uint32_t)TFW_FEATURE_ANALOG  <<  2) \
    | ((uint32_t)TFW_FEATURE_PWM     <<  3) \
    | ((uint32_t)TFW_FEATURE_TONE    <<  4) \
    | ((uint32_t)TFW_FEATURE_PULSE   <<  5) \
    | ((uint32_t)TFW_FEATURE_EEPROM  <<  6) \
    | ((uint32_t)TFW_FEATURE_I2C     <<  7) \
    | ((uint32_t)TFW_FEATURE_SPI     <<  8) \
    | ((uint32_t)TFW_FEATURE_ONEWIRE <<  9) \
    | ((uint32_t)TFW_FEATURE_ATTEST  << 10) \
    | ((uint32_t)TFW_FEATURE_DEADMAN << 11) \
    | ((uint32_t)TFW_FEATURE_PROVISION << 12) \
    | ((uint32_t)TFW_FEATURE_ALIAS_CERT << 13) \
    | ((uint32_t)TFW_FEATURE_AUTH_SESSION << 14) )

/* ------------------------------------------------------- board identity */
/* A board profile supplies these. The host build has no board and takes the
 * defaults, which is why they live here rather than as #ifndef guards inside
 * the capability modules that read them -- those are logic, and logic carries
 * no conditionals. */

#ifndef TFW_BOARD_NAME
#define TFW_BOARD_NAME "host"
#endif
#ifndef TFW_BOARD_ID
#define TFW_BOARD_ID   0x00u
#endif
#ifndef TFW_PIN_COUNT
#define TFW_PIN_COUNT  0u
#endif
#ifndef TFW_EEPROM_SIZE
#define TFW_EEPROM_SIZE 1024u
#endif

/* ---------------------------------------------------- flash geometry, for tfw-boot
 *
 * The part's total flash, and where in it the application and tfw-boot each
 * live: a base and a size for both regions. A board profile states them; the
 * defaults below describe an ATmega328P with BOOTSZ at its largest setting,
 * which is what docs/security/fuses-and-lockbits.md specifies.
 *
 * Base and size, not "the boot section is whatever is left at the top". That
 * is where BOOTRST puts it on AVR, and so it is what every AVR profile says --
 * but it is a fact about the part, and stating it in the profile keeps the
 * shared code from depending on it. include/tfw/boot.h derives the application
 * end, the trailer address and the largest measurable image from these, and
 * asserts the two regions fit the part without overlapping. A stale
 * application end would put the trailer at an address the bootloader does not
 * look at, and the device would refuse every image with "no trailer".
 *
 * TFW_BOOT_REGION_SIZE is BOOTSZ IN BYTES, not in words. The fuse is programmed
 * in words (2048 words on the 328P) and the datasheet tables are in words,
 * which is the one place this number is easy to state at half its value.
 *
 * TFW_OTA_PAGE_MAX is the largest flash page tfw-boot's OTA buffer holds
 * (boot/boot_ota.h). It is a buffer size and so a RAM cost: it must be at least
 * the part's page and a power of two, and an OTA session refuses a HAL whose
 * page is larger. */
#ifndef TFW_FLASH_TOTAL
#define TFW_FLASH_TOTAL 32768uL
#endif
#ifndef TFW_BOOT_REGION_SIZE
#define TFW_BOOT_REGION_SIZE 4096uL
#endif
#ifndef TFW_BOOT_REGION_BASE
#define TFW_BOOT_REGION_BASE (TFW_FLASH_TOTAL - TFW_BOOT_REGION_SIZE)
#endif
#ifndef TFW_APP_REGION_BASE
#define TFW_APP_REGION_BASE 0uL
#endif
#ifndef TFW_APP_REGION_SIZE
#define TFW_APP_REGION_SIZE TFW_BOOT_REGION_BASE
#endif
#ifndef TFW_OTA_PAGE_MAX
#define TFW_OTA_PAGE_MAX 256u
#endif

/*
 * How long tfw-boot keeps an application-requested update session open without
 * seeing the transfer make progress, before it gives up and boots the image.
 *
 * Counted in ticks of the boot HAL, TFW_BOOT_TICK_HZ a second (boot/boot_hal.h).
 * tfw-boot has no timer and must not grow a delay, so on AVR its tick is one
 * pass of boot/recovery/recovery_ota.c's receive loop with nothing received,
 * and boot/hal/avr/boot_tick.h states what that pass costs. The simavr suite
 * (test/integration/simavr/boot_harness.c) measures the real duration against
 * it, so a change to the loop that makes the number wrong fails CI rather than
 * silently stretching or shrinking the window.
 */
#ifndef TFW_BOOT_SESSION_IDLE_SECONDS
#define TFW_BOOT_SESSION_IDLE_SECONDS 10uL
#endif
#define TFW_BOOT_SESSION_IDLE_POLLS(tick_hz) \
    ((uint32_t)(tick_hz) * TFW_BOOT_SESSION_IDLE_SECONDS)

/* The same bound for a BUSY line: bytes, not empty polls. 8N1 is ten bits a
 * byte, so baud/10 bytes a second is the most the line can deliver and the
 * session duration times that is the most a peer can send without ever leaving
 * a poll empty. */
#define TFW_BOOT_SESSION_IDLE_BYTES(baud) \
    (((uint32_t)(baud) / 10uL) * TFW_BOOT_SESSION_IDLE_SECONDS)

/*
 * The CEILING on a requested session, which accepted frames do not restart: the
 * idle bounds above restart on every accepted OTA frame, and an accepted frame
 * is well-formed, not authenticated. Counted in every loop iteration, received
 * byte or not, so a slow drip of valid frames meets it. Default six idle
 * windows (60 s).
 *
 * Margin for a legitimate update, ATmega2560 at 115 200 baud, a 56 KB image sent
 * stop-and-wait in 60-byte chunks: about 940 frames. Wire time is about 6 s for
 * the requests and 1 s for the responses; the 220 page erase-and-writes add
 * about 2 s that this count does not even see. So roughly 9 s end to end with a
 * host that answers instantly, and 60 s allows an average of 64 ms of host
 * latency per frame -- more than any USB-serial adapter adds. The smaller
 * parts have half the image and the same ceiling. If a board's host path is
 * slower than that, raise TFW_BOOT_SESSION_MAX_SECONDS rather than the idle time.
 */
#ifndef TFW_BOOT_SESSION_MAX_SECONDS
#define TFW_BOOT_SESSION_MAX_SECONDS (6uL * TFW_BOOT_SESSION_IDLE_SECONDS)
#endif
#define TFW_BOOT_SESSION_MAX_POLLS(tick_hz) \
    ((uint32_t)(tick_hz) * TFW_BOOT_SESSION_MAX_SECONDS)

/* How many single-ended ADC channels the part has. The HAL bounds against this
 * rather than a literal 8, because a Mega has sixteen and reading channel 12 on
 * a part with eight must be refused rather than folded into channel 4. */
#ifndef TFW_ADC_COUNT
#define TFW_ADC_COUNT  8u
#endif

/* ----------------------------------------------------------------- sizing */

/* The largest payload this build accepts or emits. Drives both frame buffers
 * and therefore most of the static RAM footprint. */
#ifndef TFW_MAX_PAYLOAD
#define TFW_MAX_PAYLOAD  64u
#endif

/* Scheduler slots: one per pin that can hold a timed level. */
#ifndef TFW_SCHED_SLOTS
#define TFW_SCHED_SLOTS  8u
#endif

#ifndef TFW_UART_BAUD
#define TFW_UART_BAUD    115200UL
#endif

/* How long a partially-received frame may sit before the framer abandons it.
 * Without this a truncated frame holds the machine until the next byte
 * arrives -- and that byte is usually the start of a good frame, which then
 * gets consumed as the dead frame's tail. One lost frame becomes two. */
#ifndef TFW_FRAME_IDLE_MS
#define TFW_FRAME_IDLE_MS 250u
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
#ifndef TFW_I2C_RESERVED_ADDR
#define TFW_I2C_RESERVED_ADDR 0x60u
#endif

/* Defaults tfw_bus_init() (src/caps/bus.c) applies at boot, before the host has
 * sent I2C_CONFIG or SPI_CONFIG -- so a sensor wired up and read without
 * either command still works. 100 kHz is I2C Standard-mode, chosen for the
 * same reason src/crypto/atecc608a/provider.c picks it for the element: it is
 * the speed every I2C part in the wild is guaranteed to accept. SPI mode 0,
 * the slowest divider and MSB-first are the correspondingly conservative
 * choice -- easy to change once a host knows what is on the bus, and quiet by
 * default before it does.
 * MSB and clkdiv are the raw bits tfw_hal_spi_init already takes: clkdiv 3 is
 * SPCR's SPR1:SPR0 = 11 (Fosc/128 with SPI2X clear), the slowest this HAL can
 * ask the peripheral for. */
#ifndef TFW_I2C_DEFAULT_HZ
#define TFW_I2C_DEFAULT_HZ 100000uL
#endif
#ifndef TFW_SPI_DEFAULT_MODE
#define TFW_SPI_DEFAULT_MODE 0u
#endif
#ifndef TFW_SPI_DEFAULT_CLKDIV
#define TFW_SPI_DEFAULT_CLKDIV 3u
#endif
#ifndef TFW_SPI_DEFAULT_LSB_FIRST
#define TFW_SPI_DEFAULT_LSB_FIRST 0u
#endif

/* The interval I2C_CONFIG accepts at the CAPABILITY layer, before F_CPU is
 * even in scope: zero, and anything past Fast-mode, 400 kHz -- the ceiling
 * every TWI peripheral in this family is actually RATED to, not merely the
 * fastest divisor the maths can produce. A 16 MHz board's TWBR can express
 * 1 MHz (Fast-mode-plus, TWBR=0), but the silicon's own electrical
 * characteristics are specified only through Fast-mode; accepting a value
 * past that would be honouring a request the hardware was never qualified to
 * meet, not merely a fast one. Not a claim that every board can hit every
 * value inside this interval, either -- tfw_hal_i2c_init's divisor is an
 * 8-bit register and F_CPU varies across this family, so THAT reachability
 * check is a HAL fact this layer cannot see and does not attempt; see the
 * comment in src/hal/avr/hal_bus.c. A value this generic bound lets through
 * and the HAL cannot reach still comes back TFW_ERR_RANGE, not a silently
 * wrong clock. */
#ifndef TFW_I2C_HZ_MIN
#define TFW_I2C_HZ_MIN 1000uL
#endif
#ifndef TFW_I2C_HZ_MAX
#define TFW_I2C_HZ_MAX 400000uL
#endif

/* A ceiling on how long any single command may hold the main loop. Past this a
 * scheduled pin could miss its deadline, and once a deadman exists a long
 * capture would trip it. A board with no scheduler in its build could raise it;
 * that is why it is a knob and not a constant. */
#ifndef TFW_PULSE_MAX_US
#define TFW_PULSE_MAX_US 50000UL
#endif

/* A scheduled interval may not exceed 2^31 ms. Above that the signed-difference
 * comparison the scheduler uses to survive the 49.7-day millisecond wrap stops
 * being able to tell "due" from "not yet", so the bound is enforced when a
 * timer is armed and the failure is a clean TFW_ERR_RANGE rather than a valve
 * that stays open for seven weeks. */
#define TFW_SCHED_MAX_MS 0x7FFFFFFFuL

/* Derived frame geometry. LEN counts SEQ, OP, the optional STATUS and the
 * payload; the frame adds SOF, LEN and two CRC bytes on top. */
#define TFW_MAX_LEN      ((uint8_t)(TFW_MAX_PAYLOAD + 3u))
#define TFW_MAX_FRAME    ((uint16_t)(TFW_MAX_LEN + TFW_FRAME_OVERHEAD))

/* LEN is one byte and must be able to express TFW_MAX_LEN. A profile that sets
 * TFW_MAX_PAYLOAD too high is a compile error here rather than a truncation
 * discovered on the wire. */
_Static_assert(TFW_MAX_PAYLOAD + 3u <= 255u,
               "TFW_MAX_PAYLOAD too large: LEN must fit in one byte");
_Static_assert(TFW_SCHED_SLOTS >= 1u && TFW_SCHED_SLOTS <= 32u,
               "TFW_SCHED_SLOTS out of range");

#endif /* TFW_CONFIG_H */
