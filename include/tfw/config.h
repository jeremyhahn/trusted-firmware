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
/* ATTEST_CSR, the possession signature over a CertificationRequestInfo --
 * issue #30.
 *
 * DEFAULT 0, and the Makefile turns it on only beside TFW_FEATURE_ALIAS_CERT:
 * the request reads its key and evidence headers out of the alias certificate's
 * template, so it cannot exist without it. A board can still say 0 to this
 * alone, which is how the ATmega32U4's and ATmega328P's profiles record the
 * owner's exclusion. */
#ifndef TFW_FEATURE_ATTEST_CSR
#define TFW_FEATURE_ATTEST_CSR  0
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
/* The wire protocol over a WIZnet W5100 or W5500 beside serial -- issue #55,
 * docs/ethernet-transport/. DEFAULT 0, turned on by `make ETH=1`, which also
 * links the Ethernet link and the board's controller file in place of the
 * serial-only link. Read in one place, include/tfw/board.h, to claim the
 * shield's pins. Not in the CAPS bitmap: a bit there is a wire change, and
 * whether a host needs one is an open question in the ADR. */
#ifndef TFW_FEATURE_ETHERNET
#define TFW_FEATURE_ETHERNET    0
#endif

/* The application-side OTA stager -- issue #75, docs/ota/. DEFAULT 0, turned on
 * by `make OTA_STAGE=1` (which ETH=1 implies), which links src/core/ota_stage.c
 * and the board's external store driver. Read in one place, commands.def, to
 * claim OTA_BEGIN..OTA_STATUS for the application. */
#ifndef TFW_FEATURE_OTA_STAGE
#define TFW_FEATURE_OTA_STAGE   0
#endif
/* OTA_INFO, OTA_CONFIRM and OTA_ACTIVATE (issue #77), set by mk/firmware.mk
 * from the object list: a BOOT_AB board's image that also stages, and the
 * ESP32 session (#48). Never set by hand; the handlers are src/core/ota_ctl.c,
 * or src/hal/esp32/hal_ota.c on esp_ota, linked only with this. */
#ifndef TFW_FEATURE_OTA_CTL
#define TFW_FEATURE_OTA_CTL     0
#endif
/* OTA_INFO alone: follows OTA_CTL. Set by mk/firmware.mk, never by hand. */
#ifndef TFW_FEATURE_OTA_INFO
#define TFW_FEATURE_OTA_INFO    TFW_FEATURE_OTA_CTL
#endif
/* OTA_ACTIVATE alone (issue #77), set by mk/firmware.mk for every OTA_STAGE=1
 * image, single-slot or A/B: OTA_END does not write the MARK, so a stager
 * without it could never install what it staged. Never set by hand; the
 * handler is src/core/ota_activate.c, or src/hal/esp32/hal_ota.c on esp_ota,
 * linked only with this. */
#ifndef TFW_FEATURE_OTA_ACTIVATE
#define TFW_FEATURE_OTA_ACTIVATE 0
#endif
/* An application is linked -- docs/application-sdk/. DEFAULT 0, turned on by
 * naming an APP_DIR (mk/app.mk), which also links the real runner in place of
 * src/core/app_runner_none.c. Read in one place, commands.def, to give
 * APP_INFO its entry: an image with no application has no handler and answers
 * TFW_ERR_UNKNOWN_CMD, so it is unchanged on the wire. Not in the CAPS bitmap
 * for the reason ETHERNET is not: a bit there is a wire change, and a host
 * learns whether an application is present by asking APP_INFO. */
#ifndef TFW_FEATURE_APP
#define TFW_FEATURE_APP         0
#endif

/* The HSM self-defence channel (docs/hsm-self-defence/): HSM_HEARTBEAT,
 * HSM_REPORT, HSM_VERDICT, HSM_SELECT and HSM_LOAD, wired to the response
 * engine by src/core/hsm_cmd.c. The envelope's signature is verified through
 * the element tier, so a board sets this only where an element is linked.
 * Off by default and not in the CAPS bitmap, for APP's reason: a host learns
 * whether the channel is present by asking, and TFW_ERR_UNKNOWN_CMD answers. */
#ifndef TFW_FEATURE_HSM
#define TFW_FEATURE_HSM         0
#endif

/* The anti-rollback floor the engine starts from (ADR D4). The image's own
 * minimum; the floor only rises from here, and only in RAM. */
#ifndef TFW_HSM_FLOOR_MIN
#define TFW_HSM_FLOOR_MIN       0u
#endif

/* The bit each feature occupies in the CAPS Features field. These are the wire:
 * they extend and never renumber. Named so an application can say which
 * capabilities it needs (include/tfw/app.h) without a copy of this table. */
#define TFW_CAPBIT_GPIO          0u
#define TFW_CAPBIT_SCHED         1u
#define TFW_CAPBIT_ANALOG        2u
#define TFW_CAPBIT_PWM           3u
#define TFW_CAPBIT_TONE          4u
#define TFW_CAPBIT_PULSE         5u
#define TFW_CAPBIT_EEPROM        6u
#define TFW_CAPBIT_I2C           7u
#define TFW_CAPBIT_SPI           8u
#define TFW_CAPBIT_ONEWIRE       9u
#define TFW_CAPBIT_ATTEST       10u
#define TFW_CAPBIT_DEADMAN      11u
#define TFW_CAPBIT_PROVISION    12u
#define TFW_CAPBIT_ALIAS_CERT   13u
#define TFW_CAPBIT_AUTH_SESSION 14u
#define TFW_CAPBIT_ATTEST_CSR   15u

/* The bitmap CAPS reports. Derived, never hand-maintained -- a hand-written
 * copy of this drifts from the gates above and then the host is told the
 * firmware has a capability it does not. */
#define TFW_FEATURE_BITMAP ( \
      ((uint32_t)TFW_FEATURE_GPIO         << TFW_CAPBIT_GPIO) \
    | ((uint32_t)TFW_FEATURE_SCHED        << TFW_CAPBIT_SCHED) \
    | ((uint32_t)TFW_FEATURE_ANALOG       << TFW_CAPBIT_ANALOG) \
    | ((uint32_t)TFW_FEATURE_PWM          << TFW_CAPBIT_PWM) \
    | ((uint32_t)TFW_FEATURE_TONE         << TFW_CAPBIT_TONE) \
    | ((uint32_t)TFW_FEATURE_PULSE        << TFW_CAPBIT_PULSE) \
    | ((uint32_t)TFW_FEATURE_EEPROM       << TFW_CAPBIT_EEPROM) \
    | ((uint32_t)TFW_FEATURE_I2C          << TFW_CAPBIT_I2C) \
    | ((uint32_t)TFW_FEATURE_SPI          << TFW_CAPBIT_SPI) \
    | ((uint32_t)TFW_FEATURE_ONEWIRE      << TFW_CAPBIT_ONEWIRE) \
    | ((uint32_t)TFW_FEATURE_ATTEST       << TFW_CAPBIT_ATTEST) \
    | ((uint32_t)TFW_FEATURE_DEADMAN      << TFW_CAPBIT_DEADMAN) \
    | ((uint32_t)TFW_FEATURE_PROVISION    << TFW_CAPBIT_PROVISION) \
    | ((uint32_t)TFW_FEATURE_ALIAS_CERT   << TFW_CAPBIT_ALIAS_CERT) \
    | ((uint32_t)TFW_FEATURE_AUTH_SESSION << TFW_CAPBIT_AUTH_SESSION) \
    | ((uint32_t)TFW_FEATURE_ATTEST_CSR   << TFW_CAPBIT_ATTEST_CSR) )

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

/* The image_flags a signer stamps into the image trailer (include/tfw/boot.h):
 * a build setting, 0 for a release image. Never a board's to choose. */
#ifndef TFW_IMAGE_FLAGS
#define TFW_IMAGE_FLAGS 0x00u
#endif

/* The SUIT manifest's identifiers (issue #78, docs/ota/adr-ota-image-format-
 * and-trust.md decision 3). The vendor id is the project's, the same on every
 * board: UUIDv5(NAMESPACE_URL, "https://github.com/jeremyhahn/trusted-firmware"),
 * f5ce0048-68e5-5fcf-b79b-5b1cffbf22ac. The class id is the board's --
 * UUIDv5(vendor id, TFW_BOARD_NAME) -- and so is board data: a profile states
 * it. All zero means no class is bound, and the manifest gate refuses every
 * manifest, the way tfw-boot refuses a trailer on board id 0. */
#define TFW_SUIT_VENDOR_ID { 0xf5u, 0xceu, 0x00u, 0x48u, 0x68u, 0xe5u, 0x5fu, 0xcfu, \
                             0xb7u, 0x9bu, 0x5bu, 0x1cu, 0xffu, 0xbfu, 0x22u, 0xacu }
#ifndef TFW_SUIT_CLASS_ID
#define TFW_SUIT_CLASS_ID  { 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u }
#endif
/* The policy-set and script class ids of the signed response policy
 * (docs/hsm-self-defence/adr-response-policy-format.md D2):
 * UUIDv5(vendor id, "<board>/policy") and (..., "<board>/script"), stated by
 * the profile beside TFW_SUIT_CLASS_ID. All zero binds no class, and
 * tfw_suit_read_integrated refuses every envelope of that kind. */
#ifndef TFW_SUIT_POLICY_CLASS_ID
#define TFW_SUIT_POLICY_CLASS_ID  { 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u }
#endif
#ifndef TFW_SUIT_SCRIPT_CLASS_ID
#define TFW_SUIT_SCRIPT_CLASS_ID  { 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u }
#endif
#ifndef TFW_PIN_COUNT
#define TFW_PIN_COUNT  0u
#endif
#ifndef TFW_EEPROM_SIZE
#define TFW_EEPROM_SIZE 1024u
#endif
/* EEPROM a bootloader keeps ABOVE the reserved end (include/tfw/boot.h's
 * TFW_BOOT_EE_LEN, the wire's EEPROMReservedEnd), which EEPROM_WRITE refuses
 * as it refuses the reserved span itself and EEPROM_READ gates the same way. A
 * board whose bootloader keeps state there names the range. None does since
 * the end moved to 0x140 (#125): the ATmega2560's swap journal and confirm
 * request now lie below it. Every board says nothing and gets an empty range at
 * the reserved end, which the block's own check already covers, so the test
 * folds away; an empty range at 0 would be a comparison against zero that
 * -Wtype-limits rejects. Read only where include/tfw/boot.h is in scope. */
#ifndef TFW_BOOT_EE_EXTRA_BASE
#define TFW_BOOT_EE_EXTRA_BASE TFW_BOOT_EE_LEN
#endif
#ifndef TFW_BOOT_EE_EXTRA_LEN
#define TFW_BOOT_EE_EXTRA_LEN  0u
#endif
/* OTA_CONFIRM's request record (include/tfw/boot.h). Only a board with the
 * trial links OTA_CONFIRM and places it, inside its EXTRA span (the 2560); the
 * host builds that link it for tests and tfwsim take the 2560's place. */
#ifndef TFW_BOOT_CONFIRM_ADDR
#define TFW_BOOT_CONFIRM_ADDR  0x0EAu
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
/* The highest address tfw-boot may program, and where it copies a staged
 * image: the application region itself, unless the board keeps a second slot
 * (the 2560's swap-move). */
#ifndef TFW_APP_WRITE_END
#define TFW_APP_WRITE_END (TFW_APP_REGION_BASE + TFW_APP_REGION_SIZE)
#endif
#ifndef TFW_OTA_STAGE_DEST
#define TFW_OTA_STAGE_DEST TFW_APP_REGION_BASE
#endif
#ifndef TFW_OTA_PAGE_MAX
#define TFW_OTA_PAGE_MAX 256u
#endif

/* The unit boot/boot_swap.c moves a slot in, and so a RAM buffer in tfw-boot:
 * a whole number of the part's pages, and at most 256 of them to a slot, since
 * the swap record's step is one byte. One OTA page by default (the 2560's 64 KiB
 * slot is 256 units); a board with a larger slot names a larger unit. */
#ifndef TFW_SWAP_UNIT
#define TFW_SWAP_UNIT TFW_OTA_PAGE_MAX
#endif

/* How many OTA_BEGINs the stager admits between resets (#232, the peer-binding
 * ADR's decision 1 B). Each one erases the MARK sector and, where the PREV
 * backup is linked, an application-sized span of the store, and the network
 * peer can send it unauthenticated: past this, OTA_BEGIN answers
 * TFW_ERR_BAD_STATE before anything is erased. A resumed transfer spends none,
 * so a control plane needs one per image. Counted in RAM only, 1 to 255; a
 * reset or a successful OTA_ACTIVATE refills it. */
#ifndef TFW_OTA_BEGIN_BUDGET
#define TFW_OTA_BEGIN_BUDGET 4u
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

/* The secure element's 7-bit I2C address and the bus clock its driver runs,
 * src/crypto/atecc608a/ -- issue #7.
 *
 * Board-profile data, because the address is a fact about the part on the
 * board, not about the driver: the ATECC608C variants ship at different
 * addresses (DS40002515B section 3.1 puts the TrustFLEX at 0x36 and says it
 * differs from the standard part's). The default is 0x60, the address this
 * driver always used, so a board that states nothing builds what it built
 * before. The blank ATECC608C's own shipped address is only in Microchip's
 * NDA datasheet and is not guessed here.
 *
 * I2C_XFER refuses this address as well as TFW_I2C_RESERVED_ADDR above: the
 * reserved address stays refused because hosts already rely on it (the Go
 * SDK's I2CReservedAddr), and this one is refused wherever the element
 * actually sits.
 *
 * The clock must stay at or below 100 kHz: the wake pulse is the low period of
 * a byte, and src/crypto/atecc608a/atecc.h asserts it. */
#ifndef TFW_ELEMENT_I2C_ADDR
#define TFW_ELEMENT_I2C_ADDR 0x60u
#endif
#ifndef TFW_ELEMENT_I2C_HZ
#define TFW_ELEMENT_I2C_HZ 100000uL
#endif

/* The Ethernet shield's two chip selects: the W5x00's and its SD card's. The
 * Arduino shields put them on D10 and D4 on every board; a board wired
 * otherwise overrides them. */
#ifndef TFW_ETH_CS_PIN
#define TFW_ETH_CS_PIN    10u
#endif
#ifndef TFW_ETH_SD_CS_PIN
#define TFW_ETH_SD_CS_PIN 4u
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

/* How long one tfw_app_run() may take before the runner counts an overrun
 * (docs/application-sdk/engineering.md). At 115200 baud the 64-byte receive
 * ring overflows about 5.5 ms after the loop stops draining it; with a 1 ms
 * tick a measured 2 ms is under 3 ms real, which leaves the transport half.
 * Measured, never enforced -- a board with a slower link can raise it. */
#ifndef TFW_APP_RUN_BUDGET_MS
#define TFW_APP_RUN_BUDGET_MS 2u
#endif

/* Image slots this board's update path has: 1 is tfw-boot replacing the image
 * in place, 2 is A/B with rollback. An application that declares more than the
 * board has does not start. Only counted when the image can request an update
 * at all -- a board with no bootloader serves none. */
#ifndef TFW_OTA_SLOTS
#define TFW_OTA_SLOTS 1u
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
