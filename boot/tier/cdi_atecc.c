/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * DICE-HW layer 0: the CDI computed inside the element, the floor held by its
 * monotonic counter.
 *
 * The transport is src/crypto/atecc608a/atecc.c -- the SAME driver the
 * application links, not a second copy. A bootloader with its own element
 * driver would be a second implementation of a packet format whose CRC covers
 * the count byte onward, and the two would drift on the day one of them was
 * fixed. It costs the boot section the driver's flash and buys one place where
 * the element protocol is written down.
 *
 * What this tier gets that the software tier does not:
 *
 *   - The UDS never crosses the bus. HMAC_START names slot 1 and the key stays
 *     in the part; the bootloader sees the CDI and never the secret it came
 *     from. DICE HW Requirements §6.4 -- "UDS access disabled before the first
 *     mutable code" -- is met in effect, because it never left.
 *
 *   - The rollback floor is a counter that cannot be made to descend. Not by a
 *     sagging supply, not by a bootloader bug, not by anyone with the serial
 *     line. The EEPROM floor the software tier uses is only as monotonic as the
 *     write that maintains it.
 */

#include "../boot_cdi.h"
#include "../boot_image.h"
#include "ag/boot.h"
#include "ag/hal.h"
#include "atecc.h"

/*
 * 100 kHz, and it is a precondition rather than a preference.
 *
 * The element wakes on a low SDA of at least 60 us, which ag_atecc_wake
 * produces as the low period of a byte written to address zero -- nine bit
 * times, so 90 us at this rate. Leave TWBR at its reset value and the ATmega's
 * TWI runs at F_CPU/16, which is 1 MHz at 16 MHz: the same write is 9 us, the
 * part never wakes, and the symptom is an element that reads as absent on a
 * board that has one. Every boot then fails AG_ERR_CRYPTO with nothing to point
 * at. src/crypto/atecc608a/atecc.c says the same thing over ag_atecc_wake.
 */
#define AG_ATECC_BUS_HZ 100000uL

/* Datasheet maximum execution times, in milliseconds. Not guesses: the element
 * NACKs while busy, so these bound the polling rather than time it, and a
 * command that finishes early is read early. */
#define T_SHA     36u
#define T_COUNTER  6u

static uint8_t present;

ag_status_t ag_boot_cdi_init(void)
{
    /* The bus, before anything tries to use it. The application does this in
     * src/crypto/atecc608a/provider.c; the bootloader shares the driver but not
     * that provider, so this is where the same call belongs. It is first
     * because ag_atecc_wake below is the first transfer on the bus. */
    (void)ag_hal_i2c_init(AG_ATECC_BUS_HZ);

    present = (ag_atecc_wake() == AG_OK) ? 1u : 0u;
    if (present) {
        (void)ag_atecc_sleep();
    }
    /* AG_OK either way. A board built for the element tier and fitted with no
     * element is a build-or-assembly mistake, and it is not the bootloader's
     * place to refuse to boot over it -- the application reports the tier
     * truthfully in CAPS and the platform decides. Bricking the device here
     * would turn a wrong reel on a pick-and-place into a scrapped board. */
    return AG_OK;
}

uint8_t ag_boot_cdi_tier(void)
{
    return (uint8_t)(present ? AG_CRYPTO_SECURE_ELEMENT : AG_CRYPTO_NONE);
}

ag_status_t ag_boot_cdi_derive(const uint8_t measurement[AG_BOOT_MEASURE_LEN],
                               uint8_t cdi[AG_BOOT_MEASURE_LEN])
{
    ag_status_t st;

    if (!present) {
        return AG_ERR_NOT_PROVISIONED;
    }
    if (ag_atecc_wake() != AG_OK) {
        return AG_ERR_CRYPTO;
    }

    /* Start names the key by slot; End carries the whole 32-byte measurement,
     * which is under one block, so there is no Update in between. */
    st = ag_atecc_exec(AG_ATECC_OP_SHA, AG_ATECC_SHA_HMAC_START,
                       AG_ATECC_SLOT_UDS, 0, 0u, 0, 0u, T_SHA);
    if (st == AG_OK) {
        st = ag_atecc_exec(AG_ATECC_OP_SHA, AG_ATECC_SHA_HMAC_END,
                           AG_BOOT_MEASURE_LEN, measurement,
                           AG_BOOT_MEASURE_LEN, cdi, AG_BOOT_MEASURE_LEN, T_SHA);
    }

    /* Sleep zeroes the part's volatile state, TempKey included. Without it a
     * reset between here and the jump would leave a digest derived from the UDS
     * sitting in the element for whatever ran next. */
    (void)ag_atecc_sleep();
    return st;
}

ag_status_t ag_boot_floor_read(uint32_t *out)
{
    uint8_t     v[4];
    ag_status_t st;

    if (!present) {
        return AG_ERR_UNSUPPORTED;
    }
    if (ag_atecc_wake() != AG_OK) {
        return AG_ERR_CRYPTO;
    }
    st = ag_atecc_exec(AG_ATECC_OP_COUNTER, AG_ATECC_COUNTER_READ,
                       AG_ATECC_COUNTER_BOOT, 0, 0u, v, 4u, T_COUNTER);
    (void)ag_atecc_sleep();
    if (st != AG_OK) {
        return st;
    }
    *out = (uint32_t)v[0] | ((uint32_t)v[1] << 8)
         | ((uint32_t)v[2] << 16) | ((uint32_t)v[3] << 24);
    return AG_OK;
}

/*
 * Raising the floor means incrementing a counter that only moves by one.
 *
 * The bound is AG_BOOT_FLOOR_MAX_STEP and it is checked by the caller, in
 * ag_boot_floor_advance, before this is reached -- so the loop below cannot be
 * made to run away by a signed image with an absurd version. Both halves matter:
 * the caller's check is the policy, and this loop re-reads the counter each
 * time rather than trusting its own arithmetic, because the counter is the
 * authority and the arithmetic is not.
 */
ag_status_t ag_boot_floor_write(uint32_t value)
{
    uint8_t     v[4];
    uint8_t     bump;

    if (!present) {
        return AG_ERR_UNSUPPORTED;
    }
    for (bump = 0u; bump <= (uint8_t)AG_BOOT_FLOOR_MAX_STEP; bump++) {
        uint32_t now;

        ag_status_t st = ag_boot_floor_read(&now);
        if (st != AG_OK) {
            return st;
        }
        if (now >= value) {
            return AG_OK;
        }
        if (ag_atecc_wake() != AG_OK) {
            return AG_ERR_CRYPTO;
        }
        st = ag_atecc_exec(AG_ATECC_OP_COUNTER, AG_ATECC_COUNTER_INCR,
                           AG_ATECC_COUNTER_BOOT, 0, 0u, v, 4u, T_COUNTER);
        (void)ag_atecc_sleep();
        if (st != AG_OK) {
            return st;
        }
    }
    /* The counter did not reach the target within the permitted number of
     * increments. Reported rather than retried: a counter that will not advance
     * is a part at the end of its two-million-increment life, and spinning on
     * it would turn a worn element into a device that never boots again. */
    return AG_ERR_RANGE;
}
