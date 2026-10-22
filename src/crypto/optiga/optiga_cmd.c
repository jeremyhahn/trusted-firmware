/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * OPTIGA Trust M command layer: OpenApplication, CloseApplication,
 * GetDataObject and GetRandom, and nothing that writes. Section and table
 * numbers are the Solution Reference Manual's, Revision 3.60; see
 * optiga_cmd.h.
 */
#include "optiga_cmd.h"

#include <string.h>
#include "optiga_i2c.h"

/* The response buffer: the header and the largest OutData asked for. */
static uint8_t rsp[TFW_OPTIGA_APDU_HEADER + TFW_OPTIGA_READ_CHUNK];

/*
 * The last error code read (Table 73, "Last error code"): GetDataObject of
 * 0xF1C2, one byte from offset 0. Its Cmd is 0x01 WITHOUT the flush bit: the
 * flush runs before the command (Table 36, footnote 3) and would clear the
 * code this read is for. The read itself clears it (Table 70, note 100).
 */
static const uint8_t last_error_q[] = {
    TFW_OPTIGA_CMD_GET_DATA_OBJECT, 0x00u, 0x00u, 0x06u,
    (uint8_t)(TFW_OPTIGA_OID_LAST_ERROR >> 8), (uint8_t)(TFW_OPTIGA_OID_LAST_ERROR & 0xFFu),
    0x00u, 0x00u, 0x00u, 0x01u,
};

/* The unique application identifier (§5.7, Table 78): RID D2 76 00 00 04,
 * PIX 'GenAuthAppl'. */
static const uint8_t app_id[16] = {
    0xD2u, 0x76u, 0x00u, 0x00u, 0x04u,
    0x47u, 0x65u, 0x6Eu, 0x41u, 0x75u, 0x74u, 0x68u, 0x41u, 0x70u, 0x70u, 0x6Cu,
};

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFFu);
}

/* One APDU out, one in, its header checked (Tables 37 and 38): at least the
 * header, an OutLen equal to the bytes after it, a Sta of 0x00 or 0xFF, and
 * no OutData behind 0xFF ("In this case, the OutData field is absent"). */
static tfw_status_t exchange(const uint8_t *q, uint16_t qlen, uint8_t *sta,
                             uint16_t *olen)
{
    uint16_t     n = 0u;
    tfw_status_t st = tfw_optiga_transceive(q, qlen, rsp, (uint16_t)sizeof rsp,
                                            &n, TFW_OPTIGA_EXEC_MS);

    if (st != TFW_OK) {
        return st;
    }
    if (n < TFW_OPTIGA_APDU_HEADER) {
        return TFW_ERR_FRAMING;
    }
    *olen = (uint16_t)(((uint16_t)rsp[2] << 8) | rsp[3]);
    if (*olen != (uint16_t)(n - TFW_OPTIGA_APDU_HEADER)) {
        return TFW_ERR_FRAMING;
    }
    *sta = rsp[0];
    if (*sta == TFW_OPTIGA_STA_OK) {
        return TFW_OK;
    }
    if (*sta == TFW_OPTIGA_STA_ERROR && *olen == 0u) {
        return TFW_OK;
    }
    return TFW_ERR_FRAMING;
}

/* A command, and on Sta 0xFF the element's own reason for it. */
static tfw_status_t command(const uint8_t *q, uint16_t qlen, uint16_t *olen)
{
    uint8_t      sta = 0u;
    tfw_status_t st  = exchange(q, qlen, &sta, olen);

    if (st != TFW_OK || sta == TFW_OPTIGA_STA_OK) {
        return st;
    }
    st = exchange(last_error_q, (uint16_t)sizeof last_error_q, &sta, olen);
    if (st != TFW_OK) {
        return st;
    }
    if (sta != TFW_OPTIGA_STA_OK) {
        return TFW_ERR_CRYPTO;
    }
    if (*olen != 1u) {
        return TFW_ERR_FRAMING;
    }
    return tfw_optiga_error_status(rsp[TFW_OPTIGA_APDU_HEADER]);
}

tfw_status_t tfw_optiga_error_status(uint8_t code)
{
    switch (code) {
    case 0x01u:  /* Invalid OID */
    case 0x03u:  /* Invalid parameter field */
    case 0x04u:  /* Invalid length field */
    case 0x05u:  /* Invalid parameter in data field */
    case 0x08u:  /* Data object boundary exceeded */
        return TFW_ERR_RANGE;
    case 0x07u:  /* Access conditions not satisfied */
        return TFW_ERR_PROTECTED_REGION;
    case 0x09u:  /* Metadata truncation error */
    case 0x0Du:  /* Insufficient buffer/memory */
        return TFW_ERR_TOO_LONG;
    case 0x0Au:  /* Invalid command field */
        return TFW_ERR_UNSUPPORTED;
    case 0x0Bu:  /* Command out of sequence */
    case 0x0Cu:  /* Command not available: application closed or terminated */
        return TFW_ERR_BAD_STATE;
    case 0x2Fu:  /* Authorization failure */
        return TFW_ERR_AUTH_REQUIRED;
    default:     /* 0x06 Internal process error, the rest, and unlisted codes */
        return TFW_ERR_CRYPTO;
    }
}

tfw_status_t tfw_optiga_open_application(void)
{
    uint8_t      q[TFW_OPTIGA_APDU_HEADER + sizeof app_id];
    uint16_t     olen = 0u;
    tfw_status_t st;

    q[0] = TFW_OPTIGA_CMD_OPEN_APPLICATION | TFW_OPTIGA_CMD_FLUSH;
    q[1] = 0x00u;  /* initialise a clean application context */
    put16(&q[2], (uint16_t)sizeof app_id);
    memcpy(&q[TFW_OPTIGA_APDU_HEADER], app_id, sizeof app_id);
    st = command(q, (uint16_t)sizeof q, &olen);
    if (st == TFW_OK && olen != 0u) {
        return TFW_ERR_FRAMING;  /* OutLen 0x0000 (Table 40) */
    }
    return st;
}

tfw_status_t tfw_optiga_close_application(void)
{
    static const uint8_t q[] = {
        TFW_OPTIGA_CMD_CLOSE_APPLICATION | TFW_OPTIGA_CMD_FLUSH,
        0x00u,  /* close without saving the application context */
        0x00u, 0x00u,
    };
    uint16_t     olen = 0u;
    tfw_status_t st = command(q, (uint16_t)sizeof q, &olen);

    if (st == TFW_OK && olen != 0u) {
        return TFW_ERR_FRAMING;  /* OutLen 0x0000 for Param 0x00 (Table 41) */
    }
    return st;
}

tfw_status_t tfw_optiga_get_data(uint16_t oid, uint16_t offset, uint8_t want,
                                 uint8_t *out, uint16_t *len)
{
    uint8_t      q[TFW_OPTIGA_APDU_HEADER + 6u];
    uint16_t     olen = 0u;
    tfw_status_t st;

    if (want == 0u || want > TFW_OPTIGA_READ_CHUNK) {
        return TFW_ERR_RANGE;
    }
    q[0] = TFW_OPTIGA_CMD_GET_DATA_OBJECT | TFW_OPTIGA_CMD_FLUSH;
    q[1] = 0x00u;  /* read data */
    put16(&q[2], 6u);
    put16(&q[4], oid);
    put16(&q[6], offset);
    put16(&q[8], want);
    st = command(q, (uint16_t)sizeof q, &olen);
    if (st != TFW_OK) {
        return st;
    }
    if (olen > want) {
        return TFW_ERR_FRAMING;  /* more than was asked for */
    }
    memcpy(out, &rsp[TFW_OPTIGA_APDU_HEADER], olen);
    *len = olen;
    return TFW_OK;
}

tfw_status_t tfw_optiga_get_metadata(uint16_t oid, uint8_t *out, uint16_t cap,
                                     uint16_t *len)
{
    uint8_t      q[TFW_OPTIGA_APDU_HEADER + 2u];
    uint16_t     olen = 0u;
    tfw_status_t st;

    q[0] = TFW_OPTIGA_CMD_GET_DATA_OBJECT | TFW_OPTIGA_CMD_FLUSH;
    q[1] = 0x01u;  /* read metadata */
    put16(&q[2], 2u);
    put16(&q[4], oid);
    st = command(q, (uint16_t)sizeof q, &olen);
    if (st != TFW_OK) {
        return st;
    }
    if (olen == 0u || rsp[TFW_OPTIGA_APDU_HEADER] != TFW_OPTIGA_METADATA_TAG) {
        return TFW_ERR_FRAMING;
    }
    if (olen > cap) {
        return TFW_ERR_TOO_LONG;
    }
    memcpy(out, &rsp[TFW_OPTIGA_APDU_HEADER], olen);
    *len = olen;
    return TFW_OK;
}

tfw_status_t tfw_optiga_metadata_size(const uint8_t *md, uint16_t len,
                                      uint16_t *size)
{
    uint16_t i;
    uint16_t used = 0u;
    uint16_t max  = 0u;
    uint8_t  have = 0u;  /* bit 0: 0xC4 seen, bit 1: 0xC5 seen */

    /* 0x20, one length byte, then the simple TLV objects inside it. */
    if (len < 2u || md[0] != TFW_OPTIGA_METADATA_TAG || md[1] != (uint8_t)(len - 2u)) {
        return TFW_ERR_FRAMING;
    }
    for (i = 2u; i < len;) {
        uint8_t  tag;
        uint16_t vlen;

        if ((uint16_t)(len - i) < 2u) {
            return TFW_ERR_FRAMING;  /* a tag with no length byte */
        }
        tag  = md[i];
        vlen = md[i + 1u];
        if (vlen > (uint16_t)(len - i - 2u)) {
            return TFW_ERR_FRAMING;
        }
        if (tag == 0xC4u || tag == 0xC5u) {
            uint16_t v;

            if (vlen == 1u) {
                v = md[i + 2u];
            } else if (vlen == 2u) {
                v = (uint16_t)(((uint16_t)md[i + 2u] << 8) | md[i + 3u]);
            } else {
                return TFW_ERR_FRAMING;
            }
            if (tag == 0xC4u) {
                max = v;
                have |= 1u;
            } else {
                used = v;
                have |= 2u;
            }
        }
        i = (uint16_t)(i + 2u + vlen);
    }
    if (have & 2u) {
        *size = used;
    } else if (have & 1u) {
        *size = max;  /* "the used size is same as maximum size" (§5.5) */
    } else {
        return TFW_ERR_FRAMING;
    }
    return TFW_OK;
}

tfw_status_t tfw_optiga_read_object(uint16_t oid, uint8_t *out, uint16_t cap,
                                    uint16_t *len)
{
    uint16_t     size = 0u;
    uint16_t     off;
    uint16_t     got  = 0u;
    tfw_status_t st   = tfw_optiga_get_metadata(oid, out, cap, &got);

    if (st != TFW_OK) {
        return st;
    }
    st = tfw_optiga_metadata_size(out, got, &size);
    if (st != TFW_OK) {
        return st;
    }
    if (size > cap) {
        return TFW_ERR_TOO_LONG;
    }
    for (off = 0u; off < size; off = (uint16_t)(off + got)) {
        const uint16_t left = (uint16_t)(size - off);
        const uint8_t  want = (uint8_t)(left < TFW_OPTIGA_READ_CHUNK ? left : TFW_OPTIGA_READ_CHUNK);

        st = tfw_optiga_get_data(oid, off, want, &out[off], &got);
        if (st != TFW_OK) {
            return st;
        }
        if (got != want) {
            return TFW_ERR_FRAMING;  /* shorter than the metadata said */
        }
    }
    *len = size;
    return TFW_OK;
}

tfw_status_t tfw_optiga_get_random(uint8_t *out, uint8_t n)
{
    uint8_t      q[TFW_OPTIGA_APDU_HEADER + 2u];
    uint16_t     olen = 0u;
    tfw_status_t st;

    if (n < TFW_OPTIGA_RANDOM_MIN || n > TFW_OPTIGA_RANDOM_MAX) {
        return TFW_ERR_RANGE;
    }
    q[0] = TFW_OPTIGA_CMD_GET_RANDOM | TFW_OPTIGA_CMD_FLUSH;
    q[1] = 0x00u;  /* TRNG */
    put16(&q[2], 2u);
    put16(&q[4], n);
    st = command(q, (uint16_t)sizeof q, &olen);
    if (st != TFW_OK) {
        return st;
    }
    if (olen != n) {
        return TFW_ERR_FRAMING;  /* the random stream is the length asked */
    }
    memcpy(out, &rsp[TFW_OPTIGA_APDU_HEADER], n);
    return TFW_OK;
}
