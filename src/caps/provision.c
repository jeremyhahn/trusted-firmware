/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Provisioning -- the commands that write a device's identity.
 *
 * THIS FILE IS NOT IN A PRODUCTION IMAGE. TFW_FEATURE_PROVISION defaults to 0,
 * the Makefile does not compile this translation unit, commands.def emits no
 * dispatch entries, and a fielded device answers TFW_ERR_UNKNOWN_CMD to every id
 * below. The gate is file selection, not a runtime check, because a runtime
 * check is a thing an attacker argues with.
 *
 * That is the security property, and it is worth stating plainly: a device that
 * can be re-provisioned over its own command channel has no identity worth
 * attesting to. Whoever reaches the serial port would become the manufacturer.
 * So provisioning happens on a jig, with an image that is replaced by the
 * production image before the part leaves the bench.
 *
 * This file is the wire, and only the wire: it reads the arguments, calls the
 * backend the object list linked beside it (provision.h), and writes what the
 * backend returned. One wire for every identity root, so a host's tfwprov
 * speaks to an ATECC608 board and an eFuse-UDS board alike. A backend's error
 * reaches the host as the status alone: dispatch sends no payload with it.
 */

#include "provision.h"
#include "tfw/protocol.h"

tfw_status_t tfw_prov_cmd_info(tfw_reader_t *r, tfw_writer_t *w)
{
    uint8_t      rev[4];
    uint8_t      cfg[32];
    uint8_t      lock[2];
    tfw_status_t st;

    (void)r;
    st = tfw_prov_info(rev, cfg, lock);
    if (st == TFW_OK) {
        tfw_write_blob(w, rev, 4u);
        tfw_write_blob(w, cfg, 32u);
        tfw_write_blob(w, lock, 2u);
    }
    return st;
}

tfw_status_t tfw_prov_cmd_write_config(tfw_reader_t *r, tfw_writer_t *w)
{
    uint8_t        block = tfw_read_u8(r);
    const uint8_t *data  = tfw_read_blob(r, 32u);

    (void)w;
    if (!data) {
        return TFW_ERR_BAD_ARGC;
    }
    return tfw_prov_write_config(block, data);
}

tfw_status_t tfw_prov_cmd_lock(tfw_reader_t *r, tfw_writer_t *w)
{
    uint8_t  zone = tfw_read_u8(r);
    uint16_t crc  = tfw_read_u16(r);

    (void)w;
    return tfw_prov_lock(zone, crc);
}

tfw_status_t tfw_prov_cmd_genkey(tfw_reader_t *r, tfw_writer_t *w)
{
    uint8_t      slot = tfw_read_u8(r);
    uint8_t      pub[64];
    tfw_status_t st;

    st = tfw_prov_genkey(slot, pub);
    if (st == TFW_OK) {
        tfw_write_blob(w, pub, 64u);
    }
    return st;
}

tfw_status_t tfw_prov_cmd_pubkey(tfw_reader_t *r, tfw_writer_t *w)
{
    uint8_t      slot = tfw_read_u8(r);
    uint8_t      pub[64];
    tfw_status_t st;

    st = tfw_prov_pubkey(slot, pub);
    if (st == TFW_OK) {
        tfw_write_blob(w, pub, 64u);
    }
    return st;
}

tfw_status_t tfw_prov_cmd_seed_uds(tfw_reader_t *r, tfw_writer_t *w)
{
    (void)w;
    return tfw_prov_seed_uds(tfw_read_u8(r));
}
