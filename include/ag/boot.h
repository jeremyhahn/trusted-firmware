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
 * EEPROM [0x000, 0x080). The region src/caps/storage.c refuses writes to, and
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
 *    52    16  device_serial   written by agprov, step 7
 *    68    58  reserved
 *   126     2  crc16           over bytes [0, 126)
 *
 * THE HASH, NOT THE KEY. Thirty-two bytes of EEPROM instead of the key's
 * thirty-two is a wash on space, so the reason is rotation: the key itself
 * lives in boot-section flash beside the code that uses it, where BLB mode 3
 * puts it out of the application's reach, and EEPROM pins which key that may
 * be. Replacing the bootloader replaces the key; the EEPROM hash is what says
 * whether the replacement was authorised. A key held only in EEPROM would be
 * rotatable by anything that could write EEPROM, and a key held only in flash
 * would be rotatable by anything that could write flash -- each defeating the
 * other's protection.
 *
 * THE CRC IS NOT INTEGRITY AGAINST AN ATTACKER and must not be read as one.
 * Anyone who can write this region can recompute it. It is there for the
 * failure fuses-and-lockbits.md calls out by name: an EEPROM write on a sagging
 * supply corrupts rather than fails, and a silently corrupted rollback floor is
 * a downgrade attack that arrives by accident. BODLEVEL is the real mitigation;
 * this is how the bootloader NOTICES when BODLEVEL was not set.
 */
#define AG_BOOT_EE_MAGIC     0x52504741uL   /* 'A','G','P','R' little-endian */
#define AG_BOOT_EE_VERSION   1u
#define AG_BOOT_EE_BASE      0x000u
#define AG_BOOT_EE_LEN       128u

#define AG_BOOT_EE_O_MAGIC          0u
#define AG_BOOT_EE_O_LAYOUT         4u
#define AG_BOOT_EE_O_FLAGS          5u
#define AG_BOOT_EE_O_PUBKEY_HASH    8u
#define AG_BOOT_EE_O_ROLLBACK_FLOOR 40u
#define AG_BOOT_EE_O_BOOT_COUNT     44u
#define AG_BOOT_EE_O_BOOT_STATE     48u
#define AG_BOOT_EE_O_OTA_STATE      49u
#define AG_BOOT_EE_O_FAIL_COUNT     50u
#define AG_BOOT_EE_O_SERIAL         52u
#define AG_BOOT_EE_O_CRC            126u

#define AG_BOOT_SERIAL_LEN   16u

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
} ag_boot_platform_t;

/* -------------------------------------------------- the DICE CDI handoff */
/*
 * How layer 0 reaches layer 1: a block of SRAM at a fixed address, written by
 * the bootloader and read exactly once by the application.
 *
 *   off  size  field
 *     0     4  magic        AG_BOOT_HANDOFF_MAGIC
 *     4     1  version
 *     5     1  tier         the ag_crypto_tier_t the bootloader observed
 *     6     2  flags
 *     8    32  cdi          HMAC(UDS, measurement)
 *    40    32  measurement  so the application reports the FWHash that was
 *                           actually verified, rather than recomputing one
 *    72     4  image_version
 *    76     2  crc16        over bytes [0, 76)
 *    78     2  reserved
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
#define AG_BOOT_HANDOFF_VERSION 1u
#define AG_BOOT_HANDOFF_LEN     80u

#define AG_BOOT_HO_MAGIC        0u
#define AG_BOOT_HO_VERSION      4u
#define AG_BOOT_HO_TIER         5u
#define AG_BOOT_HO_FLAGS        6u
#define AG_BOOT_HO_CDI          8u
#define AG_BOOT_HO_MEASUREMENT  40u
#define AG_BOOT_HO_IMAGE_VER    72u
#define AG_BOOT_HO_CRC          76u

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
