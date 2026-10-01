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
 *   reserved EEPROM        `agprov`, which writes it at provisioning, and
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
#ifndef AG_BOOT_H
#define AG_BOOT_H

#include <stdint.h>
#include "ag/status.h"
#include "ag/types.h"

/* ------------------------------------------------------- the image trailer */
/*
 * The last AG_BOOT_TRAILER_LEN bytes of the application region.
 *
 * At the END of the region and not the start, because the reset vector is at
 * address zero and must stay there: a header at the front would displace it,
 * and every jump table the compiler emitted would need relocating by the
 * signing tool. A trailer at a fixed address costs the bootloader one
 * subtraction and costs the image nothing.
 *
 *   off  size  field
 *     0     4  magic            AG_BOOT_TRAILER_MAGIC
 *     4     2  trailer_version  AG_BOOT_TRAILER_VERSION
 *     6     2  reserved
 *     8     4  image_version    the anti-rollback security version number
 *    12     4  image_len        bytes measured, counted from address 0
 *    16    32  measurement      SHA-256 over [0, image_len) -- this is FWHash
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
 */
#define AG_BOOT_TRAILER_MAGIC    0x4D494741uL   /* 'A','G','I','M' little-endian */
#define AG_BOOT_TRAILER_VERSION  1u
#define AG_BOOT_TRAILER_LEN      128u

#define AG_BOOT_TR_MAGIC         0u
#define AG_BOOT_TR_VERSION       4u
#define AG_BOOT_TR_RESERVED0     6u
#define AG_BOOT_TR_IMAGE_VERSION 8u
#define AG_BOOT_TR_IMAGE_LEN     12u
#define AG_BOOT_TR_MEASUREMENT   16u
#define AG_BOOT_TR_SIGNED_LEN    48u   /* the span the signature covers */
#define AG_BOOT_TR_SIGNATURE     48u

#define AG_BOOT_MEASURE_LEN      32u
#define AG_BOOT_SIG_LEN          64u
#define AG_BOOT_PUBKEY_LEN       32u

/* The parsed trailer. A value type: parsing happens once, from bytes, and
 * every later decision reads this rather than re-indexing the buffer. */
typedef struct {
    uint32_t image_version;
    uint32_t image_len;
    uint8_t  measurement[AG_BOOT_MEASURE_LEN];
    uint8_t  signature[AG_BOOT_SIG_LEN];
} ag_boot_trailer_t;

/* --------------------------------------------- platform-reserved EEPROM */
/*
 * EEPROM [0x000, 0x0C0). The region src/caps/storage.c refuses writes to, and
 * the reason that refusal is a security boundary rather than tidiness: the
 * pubkey hash below IS the root of trust, and the generic EEPROM write would
 * otherwise let anyone with the serial line replace it.
 *
 *   off  size  field
 *     0     4  magic           AG_BOOT_EE_MAGIC
 *     4     1  layout_version  AG_BOOT_EE_VERSION
 *     5     1  flags
 *     6     2  reserved
 *     8    32  pubkey_hash     SHA-256 of the 32-byte Ed25519 public key
 *    40     4  rollback_floor  software tier only; the element tier uses its
 *                              own monotonic counter and ignores this
 *    44     4  boot_count
 *    48     1  boot_state      AG_BOOT_STATE_*
 *    49     1  ota_state       AG_BOOT_OTA_*
 *    50     2  boot_fail_count
 *    52    16  device_serial   baked into the .eep image by `agprov
 *                              --bake-eeprom` before the part is ever
 *                              flashed -- see docs/device-provisioning/. There
 *                              is deliberately no wire command that writes it:
 *                              the reserved-region refusal in
 *                              src/caps/storage.c is unconditional, and a
 *                              provisioning exemption there would be a second,
 *                              narrower way to defeat the same refusal.
 *    68    64  operator_pubkey the AUTH_CHALLENGE/AUTH_RESPONSE session's
 *                              trust anchor (issue #4) -- a P-256 point,
 *                              X||Y, baked in by `agprov --bake-eeprom`
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
 * headroom it always had. AG_EEPROM_RESERVED_END grows with it, from 0x80 to
 * 0xC0: an address a v1 host could once write is now inside the refused
 * region if it happens to fall in [0x80, 0xC0). That is a real wire-visible
 * narrowing and it is deliberate -- see docs/wire-protocol/api.md and
 * docs/host-sdk/architecture.md for EEPROMReservedEnd's new value.
 *
 * ag_boot_platform_parse refuses a layout_version it does not exactly
 * recognise (AG_ERR_UNSUPPORTED), exactly as the CDI handoff's own version
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
#define AG_BOOT_EE_MAGIC     0x52504741uL   /* 'A','G','P','R' little-endian */
#define AG_BOOT_EE_VERSION   2u
#define AG_BOOT_EE_BASE      0x000u
#define AG_BOOT_EE_LEN       192u

#define AG_BOOT_EE_O_MAGIC           0u
#define AG_BOOT_EE_O_LAYOUT          4u
#define AG_BOOT_EE_O_FLAGS           5u
#define AG_BOOT_EE_O_PUBKEY_HASH     8u
#define AG_BOOT_EE_O_ROLLBACK_FLOOR  40u
#define AG_BOOT_EE_O_BOOT_COUNT      44u
#define AG_BOOT_EE_O_BOOT_STATE      48u
#define AG_BOOT_EE_O_OTA_STATE       49u
#define AG_BOOT_EE_O_FAIL_COUNT      50u
#define AG_BOOT_EE_O_SERIAL          52u
#define AG_BOOT_EE_O_OPERATOR_PUBKEY 68u
#define AG_BOOT_EE_O_CRC             190u

#define AG_BOOT_SERIAL_LEN          16u
#define AG_BOOT_OPERATOR_PUBKEY_LEN 64u

/* boot_state: what the last attempt to run an application achieved.
 *
 * AG_BOOT_STATE_STAGED is the one that earns its keep. An OTA that is
 * interrupted -- the link drops, the supply sags, someone pulls the USB lead --
 * leaves the application region half written, and the bootloader must be able
 * to tell that from a region that simply failed verification. STAGED says "a
 * write was in progress and nobody said it finished", which is the difference
 * between retrying an update and reporting a corrupt device. */
#define AG_BOOT_STATE_EMPTY   0u   /* nothing has ever been committed */
#define AG_BOOT_STATE_STAGED  1u   /* a write started and did not complete */
#define AG_BOOT_STATE_GOOD    2u   /* an image verified and was committed */

/* ota_state, for the host's benefit: reported by OTA_STATUS so an interrupted
 * session can be resumed or abandoned deliberately rather than guessed at. */
#define AG_BOOT_OTA_IDLE      0u
#define AG_BOOT_OTA_RECEIVING 1u
#define AG_BOOT_OTA_VERIFIED  2u
#define AG_BOOT_OTA_FAILED    3u

/* The parsed region. */
typedef struct {
    uint8_t  pubkey_hash[AG_BOOT_MEASURE_LEN];
    uint32_t rollback_floor;
    uint32_t boot_count;
    uint8_t  boot_state;
    uint8_t  ota_state;
    uint16_t fail_count;
    uint8_t  serial[AG_BOOT_SERIAL_LEN];
    uint8_t  operator_pubkey[AG_BOOT_OPERATOR_PUBKEY_LEN];
} ag_boot_platform_t;

/* -------------------------------------------------- the DICE CDI handoff */
/*
 * How layer 0 reaches layer 1: a block of SRAM at a fixed address, written by
 * the bootloader and read exactly once by the application.
 *
 *   off  size  field
 *     0     4  magic         AG_BOOT_HANDOFF_MAGIC
 *     4     1  version
 *     5     1  tier          the ag_crypto_tier_t the bootloader observed
 *     6     2  flags
 *     8    32  cdi           HMAC(UDS, measurement)
 *    40    32  measurement   so the application reports the FWHash that was
 *                            actually verified, rather than recomputing one
 *    72     4  image_version
 *    76     1  reset_cause   raw MCUSR, latched by hal_early.c before ag-boot's
 *                            OWN .init3 clears it -- issue #18. Decoded by the
 *                            same priority order src/hal/avr/hal_sys.c uses,
 *                            so there is one decoder rather than two that can
 *                            drift.
 *    77     1  reserved
 *    78     2  crc16         over bytes [0, 78)
 *
 * VERSION 2 REPURPOSES BYTES RATHER THAN GROWING THE BLOCK. Version 1 held
 * `crc16` at offset 76 and two reserved bytes at 78; version 2 puts
 * `reset_cause` in the first of those reserved bytes and moves `crc16` into
 * the other two, now covering 78 bytes instead of 76. AG_BOOT_HANDOFF_LEN does
 * not change -- there was already room, and moving the CRC to the end of what
 * it covers is the ordinary shape for a growable checksum, not a special case.
 *
 * VERSION MISMATCH IS HANDLED, NOT ASSUMED AWAY. ag_boot_handoff_parse refuses
 * a version it does not exactly recognise (AG_ERR_UNSUPPORTED), exactly as it
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
#define AG_BOOT_HANDOFF_MAGIC  0x49444341uL  /* 'A','C','D','I' little-endian */
#define AG_BOOT_HANDOFF_VERSION 2u
#define AG_BOOT_HANDOFF_LEN     80u

#define AG_BOOT_HO_MAGIC        0u
#define AG_BOOT_HO_VERSION      4u
#define AG_BOOT_HO_TIER         5u
#define AG_BOOT_HO_FLAGS        6u
#define AG_BOOT_HO_CDI          8u
#define AG_BOOT_HO_MEASUREMENT  40u
#define AG_BOOT_HO_IMAGE_VER    72u
#define AG_BOOT_HO_RESET_CAUSE  76u
#define AG_BOOT_HO_RESERVED0    77u
#define AG_BOOT_HO_CRC          78u

/* ------------------------------------------------------------- geometry */
/*
 * Derived, never stated twice. A board profile gives AG_FLASH_TOTAL and
 * AG_BOOT_SECTION_SIZE; everything below follows, so a board that changes
 * BOOTSZ cannot leave a stale application-end address behind it.
 */
#include "ag/config.h"

#define AG_BOOT_APP_END      ((uint32_t)AG_FLASH_TOTAL - (uint32_t)AG_BOOT_SECTION_SIZE)
#define AG_BOOT_TRAILER_ADDR (AG_BOOT_APP_END - (uint32_t)AG_BOOT_TRAILER_LEN)
#define AG_BOOT_START        AG_BOOT_APP_END

/* The largest image the trailer may claim. An image_len that reached into the
 * trailer would let a crafted image cover its own signature with the
 * measurement, so the bound is the trailer's address and not the region's. */
#define AG_BOOT_MAX_IMAGE_LEN AG_BOOT_TRAILER_ADDR

#endif /* AG_BOOT_H */
