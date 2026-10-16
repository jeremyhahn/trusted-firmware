/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * THE BOOTLOADER CONTRACT.
 *
 * Three on-medium layouts live here rather than in boot/, because each has a
 * second party that must agree with it byte for byte:
 *
 *   the image trailer      the build system that signs an image, and the host
 *                          tool that ships one over the air
 *   reserved EEPROM        `tfwprov`, which writes it at provisioning, and
 *                          src/caps/storage.c, which refuses writes to it
 *   the CDI handoff        the application's crypto provider, which reads the
 *                          CDI the bootloader derived and then zeroizes it
 *
 * A layout with two parties and one definition is a layout that can only be
 * wrong in one place. The alternative -- a struct in boot/ and a mirror of it
 * somewhere else -- is the arrangement testdata/vectors.h exists to prevent.
 *
 * EVERY OFFSET IS A LITERAL AND EVERY FIELD IS READ BY SHIFT AND MASK. No
 * struct is ever memcpy'd onto these bytes: the padding a compiler is entitled
 * to insert is not a wire fact, and a layout that depends on one is a layout
 * that changes when the compiler does. The codec in src/proto does it this way
 * for the same reason.
 *
 * These numbers extend and never renumber, exactly as the status codes and
 * command ids do -- with one difference that matters more here. A host that
 * misreads a status byte gets a wrong error message; a bootloader that misreads
 * a trailer either refuses to boot a good image or accepts a bad one.
 */
#ifndef TFW_BOOT_H
#define TFW_BOOT_H

#include <stdint.h>
#include "tfw/status.h"
#include "tfw/types.h"

/* ------------------------------------------------------- the image trailer */
/*
 * The last TFW_BOOT_TRAILER_LEN bytes of the application region.
 *
 * At the END of the region and not the start, because the reset vector is at
 * address zero and must stay there: a header at the front would displace it,
 * and every jump table the compiler emitted would need relocating by the
 * signing tool. A trailer at a fixed address costs the bootloader one
 * subtraction and costs the image nothing.
 *
 *   off  size  field
 *     0     4  magic            TFW_BOOT_TRAILER_MAGIC
 *     4     2  trailer_version  TFW_BOOT_TRAILER_VERSION
 *     6     1  board_id         the board the image was built for: the
 *                               registry's TFW_BOARD_ID, never 0
 *     7     1  image_flags      bit 0 TFW_BOOT_IMAGE_FLAG_DEV, a development
 *                               image; bits 1-7 reserved, zero
 *     8     4  image_version    the anti-rollback security version number
 *    12     4  image_len        bytes measured, counted from the region's base
 *                               (TFW_BOOT_APP_BASE; address 0 on AVR)
 *    16    32  measurement      SHA-256 over those bytes -- this is FWHash
 *   ----------  the signed span ends here: bytes [0, 48)
 *    48    64  signature        Ed25519 over the signed span
 *   112    16  reserved
 *
 * THE SIGNATURE COVERS THE VERSION, and that is not incidental. A signature
 * over the bare measurement would leave image_version unauthenticated, so an
 * attacker could take a genuine old image, relabel it with a high version and
 * walk it straight past the rollback floor -- defeating anti-rollback with a
 * four-byte edit and no key at all. Covering the whole prefix costs nothing and
 * closes that.
 *
 * image_version is a SECURITY VERSION NUMBER, not a release number. It counts
 * up by one when an image must never be accepted again, and a cosmetic release
 * does not move it. On the element tier the floor is the part's monotonic
 * counter, which has roughly two million increments for the life of the device;
 * a version bumped per release would spend that budget on nothing.
 *
 * THE SIGNATURE COVERS THE BOARD ID for the same reason. One image key per
 * board id (docs/ota/adr-ota-image-format-and-trust.md, decisions 2 and 10)
 * means a 328P image and a 2560 image are both genuine; an unsigned board_id
 * would let either be relabelled for the other and flashed into a part whose
 * fuses, pins and boot section it was never built for. board_id 0 is the value
 * every trailer carried before this field existed, so it is refused: an image
 * that does not name its board is rebuilt, not guessed at.
 */
#define TFW_BOOT_TRAILER_MAGIC    0x4D494654uL   /* 'T','F','I','M' little-endian */
#define TFW_BOOT_TRAILER_VERSION  1u
#define TFW_BOOT_TRAILER_LEN      128u

#define TFW_BOOT_TR_MAGIC         0u
#define TFW_BOOT_TR_VERSION       4u
#define TFW_BOOT_TR_BOARD_ID      6u
#define TFW_BOOT_TR_IMAGE_FLAGS   7u
#define TFW_BOOT_TR_IMAGE_VERSION 8u
#define TFW_BOOT_TR_IMAGE_LEN     12u
#define TFW_BOOT_TR_MEASUREMENT   16u
#define TFW_BOOT_TR_SIGNED_LEN    48u   /* the span the signature covers */
#define TFW_BOOT_TR_SIGNATURE     48u

#define TFW_BOOT_MEASURE_LEN      32u

/*
 * The MARK record at TFW_OTA_STORE_MARK_ADDR in a board's external OTA store
 * (issue #75): [magic:4 LE 'TFSG'][total_len:4 LE][measurement:32]. The
 * application's stager (src/core/ota_stage.c) writes it once a staged image
 * passed its checks, programming the magic last, so a record cut short by a
 * reset reads as no record; tfw-boot (boot/boot_stage.c) reads it. One
 * definition here because two programs must agree on it.
 */
#define TFW_STAGE_MARK_MAGIC  0x47534654uL
#define TFW_STAGE_MARK_O_LEN  4u
#define TFW_STAGE_MARK_O_HASH 8u
#define TFW_STAGE_MARK_LEN    40u

#define TFW_BOOT_SIG_LEN          64u
#define TFW_BOOT_PUBKEY_LEN       32u

/* image_flags. Only the bits named here are defined; an image with any other
 * bit set is refused, so a later bit cannot mean something to the signer and
 * nothing to a bootloader that predates it. */
#define TFW_BOOT_IMAGE_FLAG_DEV    0x01u   /* a development image */
#define TFW_BOOT_IMAGE_FLAGS_KNOWN 0x01u

/* The parsed trailer. A value type: parsing happens once, from bytes, and
 * every later decision reads this rather than re-indexing the buffer. */
typedef struct {
    uint32_t image_version;
    uint32_t image_len;
    uint8_t  board_id;
    uint8_t  image_flags;
    uint8_t  measurement[TFW_BOOT_MEASURE_LEN];
    uint8_t  signature[TFW_BOOT_SIG_LEN];
} tfw_boot_trailer_t;

/* ------------------------------------------------ platform-reserved NVM */
/*
 * NVM [0x000, 0x0C0). tfw-boot reads and writes it through tfw_boot_nvm_*
 * (boot/boot_hal.h); the application can only read it, through
 * tfw_hal_nvm_read (include/tfw/hal.h). On AVR the NVM is the part's EEPROM,
 * and this is the region src/caps/storage.c refuses writes to -- and the
 * reason that refusal is a security boundary rather than tidiness: the
 * pubkey hash below IS the root of trust, and the generic EEPROM write would
 * otherwise let anyone with the serial line replace it.
 *
 *   off  size  field
 *     0     4  magic           TFW_BOOT_EE_MAGIC
 *     4     1  layout_version  TFW_BOOT_EE_VERSION
 *     5     1  flags
 *     6     2  reserved
 *     8    32  pubkey_hash     SHA-256 of the 32-byte Ed25519 public key
 *    40     4  rollback_floor  software tier only; the element tier uses its
 *                              own monotonic counter and ignores this
 *    44     4  boot_count
 *    48     1  boot_state      TFW_BOOT_STATE_*
 *    49     1  ota_state       TFW_BOOT_OTA_*
 *    50     2  boot_fail_count
 *    52    16  device_serial   baked into the .eep image by `tfwprov
 *                              --bake-eeprom` before the part is ever
 *                              flashed -- see docs/device-provisioning/. There
 *                              is deliberately no wire command that writes it:
 *                              the reserved-region refusal in
 *                              src/caps/storage.c is unconditional, and a
 *                              provisioning exemption there would be a second,
 *                              narrower way to defeat the same refusal.
 *    68    64  operator_pubkey the AUTH_CHALLENGE/AUTH_RESPONSE session's
 *                              trust anchor (issue #4) -- a P-256 point,
 *                              X||Y, baked in by `tfwprov --bake-eeprom`
 *                              exactly like device_serial above, and for the
 *                              same reason: there is no wire command that
 *                              writes it either. Element tier only; a
 *                              soft-tier or CRYPTO=none image never reads
 *                              this field (AUTH_CHALLENGE/AUTH_RESPONSE are
 *                              not compiled into either -- see
 *                              docs/security/threat-model.md).
 *   132    58  reserved
 *   190     2  crc16           over bytes [0, 190)
 *
 * VERSION 2 GROWS THE BLOCK RATHER THAN REPURPOSING BYTES, unlike the CDI
 * handoff below: there was no spare room here, so operator_pubkey is inserted
 * after device_serial and everything after it -- the original 58 reserved
 * bytes and the CRC -- simply moves down by 64, keeping the same amount of
 * headroom it always had. TFW_EEPROM_RESERVED_END grows with it, from 0x80 to
 * 0xC0: an address a v1 host could once write is now inside the refused
 * region if it happens to fall in [0x80, 0xC0). That is a real wire-visible
 * narrowing and it is deliberate -- see docs/wire-protocol/api.md and
 * docs/host-sdk/architecture.md for EEPROMReservedEnd's new value.
 *
 * tfw_boot_platform_parse refuses a layout_version it does not exactly
 * recognise (TFW_ERR_UNSUPPORTED), exactly as the CDI handoff's own version
 * check does. A v1 region under a v2-aware build reads as unprovisioned for
 * every field this parses, device_serial included -- there is no attempt to
 * read the v1 prefix and default the rest, on purpose: a compatibility shim
 * over a security-relevant layout is exactly the wrong instinct here.
 *
 * RE-BAKING IS SAFE ONLY FOR A DEVICE THAT HAS NEVER BEEN FIELDED.
 * sdk/go's BuildPlatformBlock has no read-modify-write path -- it always
 * writes rollback_floor, boot_count and boot_fail_count as zero, which is
 * correct for a factory-fresh part and a rollback-floor RESET for one that
 * is not. A device already in service must not simply be re-baked to pick
 * up a new layout version; it needs a migration path that preserves those
 * fields, which does not exist yet. Known gap, not a deliberate omission.
 */
#define TFW_BOOT_EE_MAGIC     0x52504654uL   /* 'T','F','P','R' little-endian */
#define TFW_BOOT_EE_VERSION   2u
#define TFW_BOOT_EE_BASE      0x000u
#define TFW_BOOT_EE_LEN       192u

#define TFW_BOOT_EE_O_MAGIC           0u
#define TFW_BOOT_EE_O_LAYOUT          4u
#define TFW_BOOT_EE_O_FLAGS           5u
#define TFW_BOOT_EE_O_PUBKEY_HASH     8u
#define TFW_BOOT_EE_O_ROLLBACK_FLOOR  40u
#define TFW_BOOT_EE_O_BOOT_COUNT      44u
#define TFW_BOOT_EE_O_BOOT_STATE      48u
#define TFW_BOOT_EE_O_OTA_STATE       49u
#define TFW_BOOT_EE_O_FAIL_COUNT      50u
#define TFW_BOOT_EE_O_SERIAL          52u
#define TFW_BOOT_EE_O_OPERATOR_PUBKEY 68u
#define TFW_BOOT_EE_O_CRC             190u

#define TFW_BOOT_SERIAL_LEN          16u
#define TFW_BOOT_OPERATOR_PUBKEY_LEN 64u

/* boot_state: what the last attempt to run an application achieved.
 *
 * TFW_BOOT_STATE_STAGED is the one that earns its keep. An OTA that is
 * interrupted -- the link drops, the supply sags, someone pulls the USB lead --
 * leaves the application region half written, and the bootloader must be able
 * to tell that from a region that simply failed verification. STAGED says "a
 * write was in progress and nobody said it finished", which is the difference
 * between retrying an update and reporting a corrupt device. */
#define TFW_BOOT_STATE_EMPTY   0u   /* nothing has ever been committed */
#define TFW_BOOT_STATE_STAGED  1u   /* a write started and did not complete */
#define TFW_BOOT_STATE_GOOD    2u   /* an image verified and was committed */

/* The one trial (docs/ota/adr-ota-image-format-and-trust.md, decision 9). The
 * values are the ADR's proposal (6.3, open question 10): they extend the
 * enumeration and none renumbers. TRIAL is written before the jump, so ANY
 * reset that finds it -- the deadline, the watchdog, a crash, a power cut --
 * ends the trial as failed. CONFIRMED is the control plane's answer; the floor
 * advances to the running image's version at the next reset, and only then. */
#define TFW_BOOT_STATE_TRIAL     3u /* running once, not confirmed */
#define TFW_BOOT_STATE_CONFIRMED 4u /* confirmed; the floor not yet advanced */

/* ota_state, for the host's benefit: reported by OTA_STATUS so an interrupted
 * session can be resumed or abandoned deliberately rather than guessed at. */
#define TFW_BOOT_OTA_IDLE      0u
#define TFW_BOOT_OTA_RECEIVING 1u
#define TFW_BOOT_OTA_VERIFIED  2u
#define TFW_BOOT_OTA_FAILED    3u   /* refused, or the trial ended unconfirmed */
#define TFW_BOOT_OTA_PENDING   4u   /* activated: to be tried at the next reset */

/* The parsed region. */
typedef struct {
    uint8_t  pubkey_hash[TFW_BOOT_MEASURE_LEN];
    uint32_t rollback_floor;
    uint32_t boot_count;
    uint8_t  boot_state;
    uint8_t  ota_state;
    uint16_t fail_count;
    uint8_t  serial[TFW_BOOT_SERIAL_LEN];
    uint8_t  operator_pubkey[TFW_BOOT_OPERATOR_PUBKEY_LEN];
} tfw_boot_platform_t;

/* -------------------------------------------------- the DICE CDI handoff */
/*
 * How layer 0 reaches layer 1: a block of SRAM at a fixed address, written by
 * the bootloader and read exactly once by the application.
 *
 *   off  size  field
 *     0     4  magic         TFW_BOOT_HANDOFF_MAGIC
 *     4     1  version
 *     5     1  tier          the tfw_crypto_tier_t the bootloader observed
 *     6     2  flags
 *     8    32  cdi           HMAC(UDS, measurement)
 *    40    32  measurement   so the application reports the FWHash that was
 *                            actually verified, rather than recomputing one
 *    72     4  image_version
 *    76     1  reset_cause   tfw_reset_cause_t, as the boot HAL decoded it from
 *                            the latch it took before tfw-boot's OWN early init
 *                            cleared the register -- issue #18. Abstract: no
 *                            register image crosses this block, so neither
 *                            side decodes anything and the value means the
 *                            same on every architecture.
 *    77     1  reserved
 *    78     2  crc16         over bytes [0, 78)
 *
 * VERSION 2 REPURPOSES BYTES RATHER THAN GROWING THE BLOCK. Version 1 held
 * `crc16` at offset 76 and two reserved bytes at 78; version 2 puts
 * `reset_cause` in the first of those reserved bytes and moves `crc16` into
 * the other two, now covering 78 bytes instead of 76. TFW_BOOT_HANDOFF_LEN does
 * not change -- there was already room, and moving the CRC to the end of what
 * it covers is the ordinary shape for a growable checksum, not a special case.
 *
 * VERSION 3 CHANGES WHAT BYTE 76 MEANS, NOT WHERE IT IS. Version 2 carried the
 * raw MCUSR there; version 3 carries the abstract tfw_reset_cause_t. The layout
 * is identical, so only the version byte tells the two apart -- an application
 * that read a version 2 block as an enum would report a watchdog reset (MCUSR
 * 0x08) as a value no host knows. Hence the bump rather than a quiet
 * reinterpretation: a mismatched pair degrades to "no handoff" below.
 *
 * VERSION MISMATCH IS HANDLED, NOT ASSUMED AWAY. tfw_boot_handoff_parse refuses
 * a version it does not exactly recognise (TFW_ERR_UNSUPPORTED), exactly as it
 * always has -- a build that reads this block never guesses at a layout it was
 * not compiled for. An application newer than its bootloader (a v1 block) or
 * older than its bootloader (a v2 block it does not know) both take that path:
 * DeviceID and FWHash stay zero-rather-than-fabricated and the reset cause
 * falls back to whatever the application's own .init3 observed -- UNKNOWN
 * under a stock bootloader, since MCUSR is already clear by the time either
 * one's .init3 runs. That is the SAME fallback an entirely missing handoff
 * produces, so a version an application does not understand degrades exactly
 * like no bootloader having run at all, never like a block misread as
 * something it is not.
 *
 * SRAM AND NOT FLASH, AND NOT A CALL. The obvious alternative -- the
 * application asks the bootloader for its CDI through a trampoline into the
 * boot section -- is exactly the thing the whole tier forbids. One BLS callable
 * voids the BLB mode 3 read barrier from the inside, and the software tier's
 * UDS sits behind precisely that barrier. So the handoff is one-way, it is data
 * rather than code, and by the time the application runs there is nothing left
 * to call.
 *
 * The cost is that the CDI transits SRAM with no MPU to protect it. That is the
 * "partial" against DICE HW Requirements §6.4 in
 * docs/dice-attestation/architecture.md, it is not new here, and the mitigation
 * is the same one that document names: the application zeroizes the block the moment it has consumed
 * it, and the bootloader zeroizes its own copy before jumping.
 */
#define TFW_BOOT_HANDOFF_MAGIC  0x49444341uL  /* 'A','C','D','I' little-endian */
#define TFW_BOOT_HANDOFF_VERSION 3u
#define TFW_BOOT_HANDOFF_LEN     80u

#define TFW_BOOT_HO_MAGIC        0u
#define TFW_BOOT_HO_VERSION      4u
#define TFW_BOOT_HO_TIER         5u
#define TFW_BOOT_HO_FLAGS        6u
#define TFW_BOOT_HO_CDI          8u
#define TFW_BOOT_HO_MEASUREMENT  40u
#define TFW_BOOT_HO_IMAGE_VER    72u
#define TFW_BOOT_HO_RESET_CAUSE  76u
#define TFW_BOOT_HO_RESERVED0    77u
#define TFW_BOOT_HO_CRC          78u

/* -------------------------------------------- the update-session request */
/*
 * How the application asks tfw-boot to open an OTA session on the next reset --
 * RESET with mode 1. The OPPOSITE direction to the handoff above, application
 * to bootloader, and it lives in the same place for the same reasons.
 *
 *   off  size  field
 *     0     4  magic       TFW_BOOT_REQUEST_MAGIC
 *     4     4  inverse     ~magic
 *
 * WHY THE HANDOFF REGION. It is the one piece of SRAM both images already
 * agree on the address of, pinned by the same formula at link time, outside
 * .bss and .noinit so no C runtime ever zeroes it and no linker placement can
 * move it in one image and not the other. A `.noinit` byte of its own would
 * need that agreement built a second time. And it is free when the request is
 * written: the application zeroizes the handoff the moment it has consumed it
 * (src/core/handoff.c), long before any RESET can arrive, so the request
 * cannot overwrite a CDI that is still needed. A watchdog reset preserves SRAM,
 * which is the whole mechanism; nothing is written to EEPROM or flash, so the
 * request costs no wear and survives nothing it should not.
 *
 * ROBUSTNESS. SRAM is garbage after power-up, and a single magic byte would be
 * honoured by roughly one cold boot in 256. Eight bytes -- a 32-bit magic and
 * its complement -- are a 1 in 2^32 coincidence, and the complement is what
 * rejects the all-zero and all-ones fills that are the common power-on
 * patterns. The request is ALSO only honoured when tfw-boot's own
 * reset cause says the reset was a watchdog reset and nothing else, because only the
 * application's RESET causes one: a brown-out, a power cycle or the reset
 * button must never open a session even if the bytes happen to match.
 *
 * ONE SHOT. tfw-boot clears the bytes before it does anything else, whether or
 * not it honours them, so a session that is abandoned or times out leaves the
 * next reset to boot normally.
 *
 * It is a request, not a grant. Whatever is sent in the session is still
 * signature- and floor-checked by the same tfw_boot_evaluate as a cold boot.
 */
#define TFW_BOOT_REQUEST_MAGIC  0x21445055uL  /* 'U','P','D','!' little-endian */
#define TFW_BOOT_REQUEST_INVERSE 0xDEBBAFAAuL /* ~TFW_BOOT_REQUEST_MAGIC, as a literal so it is 32 bits on every host */
#define TFW_BOOT_REQ_OFF_MAGIC      0u
#define TFW_BOOT_REQ_OFF_INVERSE    4u
#define TFW_BOOT_REQUEST_LEN    8u

/* ------------------------------------------------------------- geometry */
/*
 * Derived, never stated twice. A board profile gives the application and boot
 * regions as a base and a size each (include/tfw/config.h); everything below
 * follows, so a board that changes BOOTSZ cannot leave a stale
 * application-end address behind it.
 *
 * The application region starts at TFW_BOOT_APP_BASE and an image is laid out
 * from there: the measurement and every OTA offset count from the base, and
 * the trailer sits at the region's end. On every AVR board the base is zero,
 * where the reset vector is.
 */
#include "tfw/config.h"

#define TFW_BOOT_APP_BASE     ((uint32_t)TFW_APP_REGION_BASE)
#define TFW_BOOT_APP_SIZE     ((uint32_t)TFW_APP_REGION_SIZE)
#define TFW_BOOT_APP_END      (TFW_BOOT_APP_BASE + TFW_BOOT_APP_SIZE)
#define TFW_BOOT_TRAILER_ADDR (TFW_BOOT_APP_END - (uint32_t)TFW_BOOT_TRAILER_LEN)
#define TFW_BOOT_START        ((uint32_t)TFW_BOOT_REGION_BASE)

/* The largest image the trailer may claim. An image_len that reached into the
 * trailer would let a crafted image cover its own signature with the
 * measurement, so the bound is the trailer's offset and not the region's
 * size. */
#define TFW_BOOT_MAX_IMAGE_LEN (TFW_BOOT_TRAILER_ADDR - TFW_BOOT_APP_BASE)

/* A profile whose regions do not fit the part, or overlap, would have tfw-boot
 * accept an OTA page on top of itself: the protected-region check in the flash
 * HAL is only as right as TFW_BOOT_APP_END. Refused here, at compile time, on
 * every board and on the host. An empty boot region (the ATtiny85) overlaps
 * nothing. */
_Static_assert(TFW_APP_REGION_SIZE > TFW_BOOT_TRAILER_LEN,
               "the application region must hold at least an image trailer");
_Static_assert(TFW_APP_REGION_BASE + TFW_APP_REGION_SIZE <= TFW_FLASH_TOTAL,
               "the application region runs past the end of flash");
_Static_assert(TFW_BOOT_REGION_BASE + TFW_BOOT_REGION_SIZE <= TFW_FLASH_TOTAL,
               "the boot region runs past the end of flash");
_Static_assert(TFW_BOOT_REGION_SIZE == 0u
               || TFW_APP_REGION_BASE + TFW_APP_REGION_SIZE <= TFW_BOOT_REGION_BASE
               || TFW_BOOT_REGION_BASE + TFW_BOOT_REGION_SIZE <= TFW_APP_REGION_BASE,
               "the application and boot regions overlap");
_Static_assert(TFW_OTA_PAGE_MAX != 0u && (TFW_OTA_PAGE_MAX & (TFW_OTA_PAGE_MAX - 1u)) == 0u,
               "TFW_OTA_PAGE_MAX must be a power of two");

/*
 * The most bytes a requested update session may receive in total (see
 * include/tfw/config.h on why it is a ceiling nothing restarts). A full image
 * is TFW_BOOT_APP_SIZE bytes. Each OTA_DATA frame carries
 *
 *   SOF + LEN + SEQ + OP + CRC16  =  6 bytes of framing
 *   offset                        =  4 bytes
 *
 * so 10 bytes of overhead around its data. A host is assumed to send at least
 * TFW_BOOT_SESSION_CHUNK_MIN data bytes per frame (the maximum is
 * TFW_MAX_PAYLOAD - 4 = 60 by default; 32 is already half of it). The allowance
 * is one overhead per CHUNK_MIN of image, plus 16 for OTA_BEGIN and OTA_END,
 * plus TFW_BOOT_SESSION_SLACK for PINGs, OTA_STATUS polls and retransmitted
 * frames -- 8 KB covers over a hundred full-size retransmits. For the
 * ATmega2560 that is 56 KB + 18 KB + 8 KB = about 82 KB against a 56 KB image
 * (and 7 s of wire at 115 200 baud); the 28 KB parts get about 45 KB.
 */
#define TFW_BOOT_SESSION_CHUNK_MIN      32uL
#define TFW_BOOT_SESSION_FRAME_OVERHEAD 10uL
#define TFW_BOOT_SESSION_SLACK          8192uL
#define TFW_BOOT_SESSION_MAX_BYTES \
    (TFW_BOOT_APP_SIZE \
     + ((TFW_BOOT_APP_SIZE / TFW_BOOT_SESSION_CHUNK_MIN) + 1uL) \
       * TFW_BOOT_SESSION_FRAME_OVERHEAD \
     + 16uL + TFW_BOOT_SESSION_SLACK)

#endif /* TFW_BOOT_H */
