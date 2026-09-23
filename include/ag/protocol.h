/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The wire contract, shared by the firmware, the host SDK and the test vectors.
 *
 *   off  size  field
 *    0    1    SOF      0xA5
 *    1    1    LEN      2 + payload_len   (covers SEQ, OP and the payload)
 *    2    1    SEQ      request token, echoed verbatim in the response
 *    3    1    OP       request  CMD        0x00..0x7F
 *                       response CMD|0x80   0x80..0xFE
 *                       event    0xFF
 *    4    1    STATUS   responses and events only; absent from a request
 *    5+   N    PAYLOAD
 *    ..   2    CRC16    little-endian, over bytes [1 .. end of payload]
 *
 * Total on the wire is LEN + 4.
 *
 * LENGTH PREFIX, NOT BYTE STUFFING. COBS or SLIP would make the worst-case
 * encoded size twice the payload, so a 64-byte payload would have to reserve
 * 132 bytes on a part with 2 KB of SRAM. A length prefix gives a buffer whose
 * size is known at compile time, which is what lets every allocation here be
 * static. The cost is that 0xA5 can occur inside a payload; the framer's
 * resync handles it and the CRC catches the rest.
 *
 * THE CRC COVERS LEN. A corrupted length byte is otherwise undetectable until
 * it has already mis-framed the following frame.
 *
 * LITTLE-ENDIAN, and the codec never memcpy's a struct -- every multi-byte
 * field is assembled by shift and mask. AVR is little-endian and so is every
 * plausible host, so this costs nobody a byte swap, and the explicit form keeps
 * the logic layer free of any endianness assumption at all.
 */
#ifndef AG_PROTOCOL_H
#define AG_PROTOCOL_H

#include <stdint.h>

/* ---------------------------------------------------------------- framing */

#define AG_SOF              0xA5u  /* start of frame */
#define AG_OP_RESPONSE_BIT  0x80u  /* set on OP to mark a response */
#define AG_OP_EVENT         0xFFu  /* unsolicited event; never a response */

#define AG_HDR_SOF          0u
#define AG_HDR_LEN          1u
#define AG_HDR_SEQ          2u
#define AG_HDR_OP           3u
#define AG_HDR_STATUS       4u

#define AG_FRAME_OVERHEAD   4u  /* SOF + LEN + CRC16 */
#define AG_LEN_MIN          2u  /* SEQ + OP, an empty request */

/* A SEQ of zero means the host wants no correlation; the device still echoes
 * it, so a response to SEQ 0 is not evidence of which request it answered. */
#define AG_SEQ_NONE         0u

/* ------------------------------------------------------------- versioning */

/* Major changes when a frame cannot be parsed by an older host; minor when
 * fields are added that an older host can ignore. Hosts accept an equal major
 * and any minor at or above their own, so a fleet may run mixed firmware. */
#define AG_PROTO_VERSION_MAJOR 1u
#define AG_PROTO_VERSION_MINOR 0u

/* ------------------------------------------------------------- command ids */
/* Grouped by subsystem with gaps, so a later addition lands beside its
 * relatives instead of at the end. Ids extend and never renumber. */

/* system */
#define AG_CMD_CAPS              0x00u
#define AG_CMD_PING              0x01u
#define AG_CMD_RESET             0x02u
#define AG_CMD_SYS_INFO          0x03u
#define AG_CMD_WDT_SET           0x04u
#define AG_CMD_SLEEP             0x05u

/* gpio and the pin scheduler */
#define AG_CMD_PIN_MODE          0x10u
#define AG_CMD_DIGITAL_READ      0x11u
#define AG_CMD_DIGITAL_WRITE     0x12u
#define AG_CMD_TIMER_SET         0x14u
#define AG_CMD_TIMER_CANCEL      0x15u
#define AG_CMD_TIMER_QUERY       0x16u

/* analog, pwm, tone */
#define AG_CMD_ANALOG_READ       0x20u
#define AG_CMD_ADC_REF           0x21u
#define AG_CMD_PWM_WRITE         0x22u
#define AG_CMD_TONE              0x23u
#define AG_CMD_NO_TONE           0x24u

/* pulse */
#define AG_CMD_PULSE_IN          0x30u
#define AG_CMD_PULSE_COUNT_START 0x31u
#define AG_CMD_PULSE_COUNT_READ  0x32u
#define AG_CMD_PULSE_COUNT_STOP  0x33u
#define AG_CMD_PULSE_CAPTURE     0x34u

/* storage */
#define AG_CMD_EEPROM_READ       0x40u
#define AG_CMD_EEPROM_WRITE      0x41u

/* buses */
#define AG_CMD_I2C_XFER          0x50u
#define AG_CMD_SPI_XFER          0x51u
#define AG_CMD_ONEWIRE_RESET     0x52u
#define AG_CMD_ONEWIRE_WRITE     0x53u
#define AG_CMD_ONEWIRE_READ      0x54u

/* trust */
#define AG_CMD_ATTEST            0x60u
#define AG_CMD_AUTH_CHALLENGE    0x61u
#define AG_CMD_AUTH_RESPONSE     0x62u

/* provisioning -- PRESENT ONLY IN A PROVISIONING BUILD
 *
 * AG_FEATURE_PROVISION defaults to 0 and a production image therefore contains
 * no handler for any of these: the dispatch table has no entry, the translation
 * unit is not compiled, and a fielded device answers AG_ERR_UNKNOWN_CMD. That
 * is the point. Provisioning writes a device's identity, and a device that
 * could be re-provisioned over its own command channel has no identity worth
 * attesting to -- the attacker who reaches the serial port becomes the
 * manufacturer.
 *
 * So these ids are reserved on the wire, permanently, and unimplemented almost
 * everywhere. Reserving them rather than reusing the range keeps a provisioning
 * host and a production device from ever misunderstanding each other.
 */
#define AG_CMD_PROV_INFO         0x70u  /* element revision, serial, lock state */
#define AG_CMD_PROV_WRITE_CONFIG 0x71u  /* a 32-byte block of the config zone */
#define AG_CMD_PROV_LOCK         0x72u  /* irreversible; zone in the payload */
#define AG_CMD_PROV_GENKEY       0x73u  /* DeviceID keypair in its slot */
#define AG_CMD_PROV_PUBKEY       0x74u  /* read back the public point */
#define AG_CMD_PROV_SEED_UDS     0x75u  /* UDS from the element's own TRNG */

/* ------------------------------------------------------ signature algorithms */
/* Carried in the AlgID byte of an attestation frame. Both ECDSA P-256 (r||s)
 * and Ed25519 (R||S) are exactly 64 raw bytes, so the frame layout does not
 * change with the algorithm -- only its interpretation does. */
#define AG_ALG_NONE              0x00u
#define AG_ALG_ECDSA_P256_SHA256 0x01u
#define AG_ALG_ED25519           0x02u
/* Symmetric identity, per the TCG DICE Symmetric spec -- the path that spec
 * defines for parts too small for asymmetric crypto. Honest about what it is:
 * a verifier holding the key can forge the response, so this tier is for local
 * attestation behind an attested gateway, never for public admission. */
#define AG_ALG_HMAC_SHA256       0x10u

/*
 * The algorithm BITMAP, which CAPS reports and which is not the same thing as
 * the identifiers above.
 *
 * An ID names one algorithm in one byte of one frame; the bitmap says which
 * ones a provider can perform. They need separate numbers because the IDs are
 * sparse on purpose -- 0x10 leaves room for more asymmetric algorithms below it
 * -- and a bitmap indexed by ID would need seventeen bits to express three
 * algorithms.
 *
 * Bits extend and never renumber, exactly as the status codes do. A host built
 * before an algorithm existed sees an unknown bit and negotiates down, rather
 * than misreading it as one it knows.
 */
#define AG_ALGBIT_ECDSA_P256_SHA256 0x0001u
#define AG_ALGBIT_ED25519           0x0002u
#define AG_ALGBIT_HMAC_SHA256       0x0004u

/* --------------------------------------------------- attestation evidence */
/* The frame go-trusted-platform's docs/firmware/avr.md specifies, with one
 * amendment: an algorithm byte, so the evidence says which primitive signed it
 * rather than the verifier having to know out of band. Both ECDSA P-256 (r||s)
 * and Ed25519 (R||S) are exactly 64 raw bytes, so the layout does not change
 * with the algorithm -- only its interpretation does.
 *
 *   [Version:1][Nonce:16][DeviceID:16][FWHash:32][BootCount:4]
 *   [UptimeMs:4][AlgID:1][Signature:64][Reserved:26]
 *
 * Timestamp is UptimeMs, not wall-clock: no part in this family has an RTC.
 * Freshness is ALWAYS the verifier's nonce. Any design that needs the device
 * to know the date is wrong. */
#define AG_ATTEST_VERSION   1u
#define AG_ATTEST_NONCE_LEN 16u
#define AG_ATTEST_ID_LEN    16u
#define AG_ATTEST_LEN       164u

/* The evidence is larger than a frame payload on a part this size, so it is
 * paged: ATTEST(nonce) computes and returns page 0, ATTEST(page) returns the
 * rest. Paging rather than enlarging every frame buffer, because the cost of a
 * bigger buffer is paid by every command on every board, and the cost of paging
 * is paid only by the one command that needs it. */
#define AG_ATTEST_PAGE      48u

/* ------------------------------------------------------------------ events */
#define AG_EVENT_BOOT            0x01u  /* sent once at startup; an unexpected
                                         * one means the device reset */
#define AG_EVENT_DEADMAN         0x02u  /* the link went quiet and scheduled
                                         * pins were driven to safety */
#define AG_EVENT_COUNTER_WRAP    0x03u  /* a pulse counter wrapped */

#endif /* AG_PROTOCOL_H */
