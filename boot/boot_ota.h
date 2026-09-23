/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * THE OTA STATE MACHINE.
 *
 * Pure logic over the same framed protocol the application speaks, reached
 * through a page-writer callback so the whole of it runs on the host. What is
 * interesting here is not the happy path -- it is what happens when the link
 * drops in the middle, and that is precisely the case a bench cannot produce on
 * demand and a host test produces in one line.
 *
 * ------------------------------------------------------------------------
 * THE DESIGN DECISION THE PART FORCES, stated plainly because it is the one
 * thing about this file a reader should not have to infer.
 *
 * There is NO A/B SLOT and there cannot be one. An ATmega328P has 32 KB of
 * flash: 4 KB of boot section and 28 KB of application. Two application slots
 * would need 56 KB. Nothing in the board contract provides external storage,
 * and a scheme that required it would stop being a property of the firmware and
 * start being a property of the bill of materials.
 *
 * So an update is written IN PLACE, over the running application, and for the
 * duration of the write the device has no bootable application at all.
 *
 * THE RECOVERY IS THE BOOTLOADER ITSELF. Before the first page is erased, the
 * reserved EEPROM boot_state is set to AG_BOOT_STATE_STAGED, and it is only
 * returned to AG_BOOT_STATE_GOOD once the whole image has been measured and its
 * signature has verified. A bootloader that finds STAGED on the next reset
 * knows the application region is not to be trusted, does not jump into it, and
 * stays in OTA waiting for another image. So an interrupted update leaves a
 * device that is not running an application and IS still reachable, updatable
 * and honest about its state -- which is what "must leave the device bootable"
 * can mean on a part with nowhere to stage.
 *
 * The cost is real and is not hidden: between the first erase and a successful
 * commit, a power cycle leaves the device in the bootloader rather than running
 * the old application. A deployment that cannot tolerate that window needs a
 * part with more flash or an external staging medium, and that is a hardware
 * decision, not a firmware one.
 * ------------------------------------------------------------------------
 */
#ifndef AG_BOOT_OTA_H
#define AG_BOOT_OTA_H

#include <stdint.h>
#include "ag/boot.h"
#include "ag/status.h"

typedef enum {
    AG_OTA_IDLE = 0,
    AG_OTA_RECEIVING,
    AG_OTA_COMPLETE,   /* every byte arrived; nothing has been verified yet */
    AG_OTA_FAILED
} ag_ota_state_t;

/*
 * How much of an image is buffered before it reaches flash.
 *
 * Exactly one flash page, because a page is the smallest thing that can be
 * written and erasing one to write half of it would destroy the other half. The
 * buffer is sized for the largest page in the family -- 256 bytes on the Mega,
 * 128 on the 328P -- because the bootloader must link against one number and a
 * per-board buffer would be a per-board RAM budget nobody could check.
 *
 * On the 328P that is 256 bytes of a 2 048-byte SRAM spent on a buffer, and it
 * is unavoidable: the alternative is a read-modify-write of each page through a
 * second buffer, which costs the same RAM and adds a failure mode.
 */
#define AG_OTA_PAGE_MAX 256u

/* Writes one page. Supplied by the caller so this file touches no hardware:
 * the bootloader passes the flash HAL, the tests pass an array. */
typedef ag_status_t (*ag_ota_page_write_fn)(uint32_t addr, const uint8_t *data,
                                            uint16_t len);

typedef struct {
    ag_ota_page_write_fn write_page;
    uint16_t page_size;

    uint8_t  state;
    uint32_t declared_len;     /* what OTA_BEGIN promised */
    uint32_t received;         /* bytes accepted so far */
    uint32_t next_offset;      /* the offset the next chunk must carry */

    uint16_t buf_used;
    uint8_t  buf[AG_OTA_PAGE_MAX];
} ag_ota_t;

/* `page_size` must be non-zero, no larger than AG_OTA_PAGE_MAX and a power of
 * two; ag_ota_init refuses otherwise, because a page size that is none of those
 * turns every address computation below into a silent corruption. */
ag_status_t ag_ota_init(ag_ota_t *o, ag_ota_page_write_fn write_page,
                        uint16_t page_size);

/*
 * OTA_BEGIN. Declares the total image length, trailer included.
 *
 * Refuses a length that is not a whole number of pages, and one that does not
 * reach the trailer: an image shorter than AG_BOOT_TRAILER_LEN has nowhere to
 * carry a signature, and accepting it would mean erasing the application to
 * write something that could never verify.
 *
 * Deliberately takes NO version argument. The version is inside the trailer,
 * under the signature, and a version the host declared separately would be a
 * version an attacker could declare -- the rollback check would then be run
 * against a number nobody signed. The rollback decision happens at commit, on
 * the parsed trailer, and never on anything the wire asserted.
 */
ag_status_t ag_ota_begin(ag_ota_t *o, uint32_t total_len);

/*
 * OTA_DATA. `offset` is carried in every chunk and must match exactly.
 *
 * Not a convenience: a chunk stream with no offset is a stream where one
 * dropped frame silently shifts everything after it, and the image then fails
 * to verify for a reason that looks like a signing bug. An explicit offset
 * turns that into AG_ERR_RANGE on the frame that actually went missing.
 */
ag_status_t ag_ota_data(ag_ota_t *o, uint32_t offset, const uint8_t *data,
                        uint8_t len);

/* OTA_END. Refuses unless exactly declared_len bytes arrived. Flushes a partial
 * final page, padding it with 0xFF -- erased flash -- so the measurement covers
 * the same bytes a verifier computing it from the image file would see. */
ag_status_t ag_ota_end(ag_ota_t *o);

/* Abandons a session. The flash already written is left alone: it is already
 * unbootable and the boot_state that says so is the caller's to maintain.
 * Erasing it here would spend the device's remaining seconds of supply on
 * making a bad image differently bad. */
void ag_ota_abort(ag_ota_t *o);

uint8_t  ag_ota_state(const ag_ota_t *o);
uint32_t ag_ota_received(const ag_ota_t *o);

#endif /* AG_BOOT_OTA_H */
