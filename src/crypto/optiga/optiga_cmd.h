/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * OPTIGA Trust M command layer, the commands that change nothing in the
 * element -- SPEC row optiga-trust-m.
 *
 * The command set is Infineon's *OPTIGA Trust M Solution Reference Manual*,
 * Revision 3.60 (2023-12-04), listed in docs/reference/README.md. Every
 * section and table number below is that document's. Four commands are
 * written, each as its table gives it:
 *
 *   OpenApplication   §4.4.1.1, Table 40   Param 0x00 only
 *   CloseApplication  §4.4.1.2, Table 41   Param 0x00 only
 *   GetDataObject     §4.4.1.3, Table 42   data (0x00) and metadata (0x01)
 *   GetRandom         §4.4.1.6, Table 45   TRNG (0x00) only
 *
 * and nothing that writes. SetDataObject, SetObjectProtected, the key
 * generators, counting, the lifecycle and the platform binding are not here
 * and no path in this file emits their command codes: the onboarding ADR
 * (docs/multi-arch/adr-optiga-trust-m-onboarding.md, §4 steps 5 and 6) forbids
 * every write. The Params left out are writes too: CloseApplication 0x01
 * saves the application context persistently, OpenApplication 0x01 restores
 * one, and GetRandom 0x04 stores into a session context.
 *
 * The APDUs ride tfw_optiga_transceive (optiga_i2c.h), which moves bytes and
 * checks checksums; this file builds the commands and reads the answers, the
 * split atecc.c and provider.c keep for the ATECC608.
 */
#ifndef TFW_OPTIGA_CMD_H
#define TFW_OPTIGA_CMD_H

#include <stdint.h>
#include "tfw/status.h"

/* Command codes (§4.4.1, Table 36). With the most significant bit set the
 * element flushes its last error code before it runs the command (Table 36,
 * footnote 3), so after a failure the code read back is this command's. */
#define TFW_OPTIGA_CMD_GET_DATA_OBJECT   0x01u
#define TFW_OPTIGA_CMD_GET_RANDOM        0x0Cu
#define TFW_OPTIGA_CMD_OPEN_APPLICATION  0x70u
#define TFW_OPTIGA_CMD_CLOSE_APPLICATION 0x71u
#define TFW_OPTIGA_CMD_FLUSH             0x80u

/* The response's first byte, Sta (Table 38). */
#define TFW_OPTIGA_STA_OK     0x00u
#define TFW_OPTIGA_STA_ERROR  0xFFu

/* Cmd, Param, InLen (2), then InData; Sta, UnDef, OutLen (2), then OutData
 * (Table 37). Two-byte fields go high byte first, as Table 42's InData
 * lists an OID's high byte before its low. */
#define TFW_OPTIGA_APDU_HEADER 4u

/* Data objects this layer names (§5.4, Tables 68 and 70). */
#define TFW_OPTIGA_OID_COPROCESSOR_UID  0xE0C2u
#define TFW_OPTIGA_OID_MAX_COM_BUFFER   0xE0C6u
#define TFW_OPTIGA_OID_DEVICE_CERT      0xE0E0u  /* Infineon-issued, 1728 B max (Table 73) */
#define TFW_OPTIGA_OID_LAST_ERROR       0xF1C2u  /* cleared on read (Table 70, note 100) */

/* The metadata constructed TLV object's tag (§5.5, Table 71). */
#define TFW_OPTIGA_METADATA_TAG 0x20u

/*
 * The most data one GetDataObject asks for. Table 42 takes any length from 1
 * to 0xFFFF and cuts it to what the object holds; this layer asks for at most
 * this many bytes so the response fits one static buffer, and reads a larger
 * object in pieces at increasing offsets, the partial read Table 42's note
 * describes. Each piece still spans several transport frames at the
 * transport's 64-byte DATA register (optiga_i2c.h).
 */
#define TFW_OPTIGA_READ_CHUNK 64u

/* GetRandom's length bounds (Table 45: 0x0008-0x0100), the upper one cut to
 * the chunk for the same reason. */
#define TFW_OPTIGA_RANDOM_MIN 8u
#define TFW_OPTIGA_RANDOM_MAX TFW_OPTIGA_READ_CHUNK

/*
 * The wait for each command's answer, in ms. Table 64 (§4.4.3) gives no time
 * for these four commands; this is ours, to confirm on the bench.
 */
#define TFW_OPTIGA_EXEC_MS 200u

/*
 * Every call returns TFW_OK, a transport status as tfw_optiga_transceive
 * returned it, or one of these:
 *
 *   TFW_ERR_FRAMING  the response is shorter than its header, its Sta is
 *                    neither 0x00 nor 0xFF, its OutLen disagrees with the
 *                    bytes that came, or its OutData is not what the command
 *                    returns
 *   TFW_ERR_TOO_LONG the data would overflow the caller's buffer
 *   TFW_ERR_RANGE    an argument outside what the command takes
 *
 * and, when Sta is 0xFF, the element's last error code (Table 39), read back
 * from TFW_OPTIGA_OID_LAST_ERROR and mapped by tfw_optiga_error_status.
 */

/* Table 39's code onto a status. Codes are never renumbered here: each maps
 * to an existing status, and a code the table does not list, or 0x00 behind a
 * 0xFF Sta, is TFW_ERR_CRYPTO. */
tfw_status_t tfw_optiga_error_status(uint8_t code);

/* OpenApplication, Param 0x00: a clean context for the authentication
 * application, addressed by its identifier (Table 78). */
tfw_status_t tfw_optiga_open_application(void);

/* CloseApplication, Param 0x00: close without saving the context. */
tfw_status_t tfw_optiga_close_application(void);

/* GetDataObject, Param 0x00 with InLen 6: up to `want` (1 to
 * TFW_OPTIGA_READ_CHUNK) bytes of `oid` from `offset`. The element returns
 * fewer at the end of the object; `*len` is what came. */
tfw_status_t tfw_optiga_get_data(uint16_t oid, uint16_t offset, uint8_t want,
                                 uint8_t *out, uint16_t *len);

/* The used size of a data object from its metadata `md`: tag 0xC5, or the
 * maximum size 0xC4 where 0xC5 is absent (§5.5, Table 71 and its implicit
 * rules). TFW_ERR_FRAMING if `md` is not one constructed TLV object holding
 * either. */
tfw_status_t tfw_optiga_metadata_size(const uint8_t *md, uint16_t len,
                                      uint16_t *size);

/* The whole data object `oid`: its metadata first for its used size, then
 * GetDataObject pieces of TFW_OPTIGA_READ_CHUNK from offset 0, so no read
 * asks past the object's end (Table 42 gives offsets up to its length - 1).
 * TFW_ERR_TOO_LONG, before any data is read, if it does not fit `cap`. */
tfw_status_t tfw_optiga_read_object(uint16_t oid, uint8_t *out, uint16_t cap,
                                    uint16_t *len);

/* GetDataObject, Param 0x01: the metadata of `oid`, a constructed TLV object
 * whose first byte is TFW_OPTIGA_METADATA_TAG. */
tfw_status_t tfw_optiga_get_metadata(uint16_t oid, uint8_t *out, uint16_t cap,
                                     uint16_t *len);

/* GetRandom, Param 0x00: `n` (TFW_OPTIGA_RANDOM_MIN to _MAX) bytes from the
 * TRNG, with no session context. */
tfw_status_t tfw_optiga_get_random(uint8_t *out, uint8_t n);

#endif /* TFW_OPTIGA_CMD_H */
