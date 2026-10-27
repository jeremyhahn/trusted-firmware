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
#ifndef TFW_PROTOCOL_H
#define TFW_PROTOCOL_H

#include <stdint.h>

/* ---------------------------------------------------------------- framing */

#define TFW_SOF              0xA5u  /* start of frame */
#define TFW_OP_RESPONSE_BIT  0x80u  /* set on OP to mark a response */
#define TFW_OP_EVENT         0xFFu  /* unsolicited event; never a response */

#define TFW_HDR_SOF          0u
#define TFW_HDR_LEN          1u
#define TFW_HDR_SEQ          2u
#define TFW_HDR_OP           3u
#define TFW_HDR_STATUS       4u

#define TFW_FRAME_OVERHEAD   4u  /* SOF + LEN + CRC16 */
#define TFW_LEN_MIN          2u  /* SEQ + OP, an empty request */

/* A SEQ of zero means the host wants no correlation; the device still echoes
 * it, so a response to SEQ 0 is not evidence of which request it answered. */
#define TFW_SEQ_NONE         0u

/* ------------------------------------------------------------- versioning */

/* Major changes when a frame cannot be parsed by an older host; minor when
 * fields are added that an older host can ignore. Hosts accept an equal major
 * and any minor at or above their own, so a fleet may run mixed firmware.
 *
 * 2.0 (#12): the CAPS pin page went from five bytes to six, port and bit each a
 * whole byte, which shifted the ADC and PWM fields. A 1.x host would misread
 * them, so it must refuse a 2.x device rather than parse it. */
#define TFW_PROTO_VERSION_MAJOR 2u
#define TFW_PROTO_VERSION_MINOR 0u

/* ------------------------------------------------------------- command ids */
/* Grouped by subsystem with gaps, so a later addition lands beside its
 * relatives instead of at the end. Ids extend and never renumber.
 *
 * 0x7F is never assigned: its response OP would be 0x7F | 0x80, which is
 * TFW_OP_EVENT, so a host would read the answer as an event. test_dispatch pins
 * that no table entry reaches it. */

/* system */
#define TFW_CMD_CAPS              0x00u
#define TFW_CMD_PING              0x01u
#define TFW_CMD_RESET             0x02u
#define TFW_CMD_SYS_INFO          0x03u
#define TFW_CMD_WDT_SET           0x04u   /* reserved, not implemented */
#define TFW_CMD_SLEEP             0x05u   /* reserved, not implemented */
#define TFW_CMD_APP_INFO          0x06u   /* only in an image with an application */

/* gpio and the pin scheduler */
#define TFW_CMD_PIN_MODE          0x10u
#define TFW_CMD_DIGITAL_READ      0x11u
#define TFW_CMD_DIGITAL_WRITE     0x12u
#define TFW_CMD_TIMER_SET         0x14u
#define TFW_CMD_TIMER_CANCEL      0x15u
#define TFW_CMD_TIMER_QUERY       0x16u

/* analog, pwm, tone */
#define TFW_CMD_ANALOG_READ       0x20u
#define TFW_CMD_ADC_REF           0x21u
#define TFW_CMD_PWM_WRITE         0x22u
#define TFW_CMD_TONE              0x23u
#define TFW_CMD_NO_TONE           0x24u

/* pulse */
#define TFW_CMD_PULSE_IN          0x30u
#define TFW_CMD_PULSE_COUNT_START 0x31u   /* reserved, not implemented */
#define TFW_CMD_PULSE_COUNT_READ  0x32u   /* reserved, not implemented */
#define TFW_CMD_PULSE_COUNT_STOP  0x33u   /* reserved, not implemented */
#define TFW_CMD_PULSE_CAPTURE     0x34u

/* storage */
#define TFW_CMD_EEPROM_READ       0x40u
#define TFW_CMD_EEPROM_WRITE      0x41u

/* buses */
#define TFW_CMD_I2C_XFER          0x50u
#define TFW_CMD_SPI_XFER          0x51u
#define TFW_CMD_ONEWIRE_RESET     0x52u
#define TFW_CMD_ONEWIRE_WRITE     0x53u
#define TFW_CMD_ONEWIRE_READ      0x54u
/* Neither bus had a way for the host to choose its clock, mode or bit order --
 * every board booted the generic I2C/SPI primitives, if it booted them at all,
 * at whatever the crypto provider or nothing had left the registers in. These
 * two let a host ask for a specific configuration; tfw_bus_init() (main.c)
 * still applies a sane default at boot so the bus works before either is
 * ever sent. [hz:4] and [mode:1][clkdiv:1][lsb_first:1] respectively. */
#define TFW_CMD_I2C_CONFIG        0x55u
#define TFW_CMD_SPI_CONFIG        0x56u

/* trust */
#define TFW_CMD_ATTEST            0x60u
/* The operator-signature auth session -- issue #4, element tier only. See
 * src/core/auth_session.h for the handshake and docs/security/threat-model.md
 * for why it does not exist on the soft or CRYPTO=none tiers. */
#define TFW_CMD_AUTH_CHALLENGE    0x61u
#define TFW_CMD_AUTH_RESPONSE     0x62u
/* The alias certificate carrying tcg-dice-UCCS-evidence. A SECOND command
 * rather than a mode of ATTEST: the two answer different questions and return
 * differently shaped pages, and an id costs nothing where a mode byte would
 * cost every existing host a reparse. Ids extend and never renumber. */
#define TFW_CMD_ATTEST_CERT       0x63u
/* Possession at ACME finalize -- issue #30. [b:32] in, [sig:64] r||s out: the
 * DeviceID key's signature over a CertificationRequestInfo the device fills
 * from its own state, with b as the UCCS nonce. The host rebuilds the same
 * bytes; the signature is the only thing that crosses the wire. See
 * src/caps/csr.c. */
#define TFW_CMD_ATTEST_CSR        0x64u

/* AUTH_CHALLENGE's reply and AUTH_RESPONSE's request payload widths. Wire-level
 * constants, deliberately separate from TFW_SIG_LEN in tfw/crypto.h: that one
 * names a crypto PRIMITIVE's width (any P-256 signature, on any path), this
 * one names what THIS COMMAND carries, and the two are only equal because the
 * primitive in use happens to produce a 64-byte signature today. */
#define TFW_AUTH_NONCE_LEN 16u
#define TFW_AUTH_SIG_LEN   64u

/* over-the-air update -- tfw-boot writes flash; the application only stages
 *
 * The application cannot write its own flash: SPM executes only from the boot
 * section, and the one way to give the application access would be a
 * trampoline into the boot section -- which voids the BLB mode 3 read barrier
 * the software tier's UDS sits behind. An image built with OTA_STAGE=1 answers
 * these four ids by streaming into the external store instead (issue #75, ADR
 * decision 8), with the same payloads; tfw-boot installs from there. An image
 * without the stager answers TFW_ERR_UNKNOWN_CMD, which tells the host to reset
 * into the bootloader and ask there.
 *
 * Reserved in the application's range rather than in a range of their own,
 * because a host discovers which it is talking to by the answer it gets and
 * that only works if the id means the same thing to both.
 */
#define TFW_CMD_OTA_BEGIN         0x68u  /* [total_len:4] */
#define TFW_CMD_OTA_DATA          0x69u  /* [offset:4][bytes:1..] */
#define TFW_CMD_OTA_END           0x6Au  /* no payload; verification follows */
#define TFW_CMD_OTA_STATUS        0x6Bu  /* -> [state:1][received:4] */
/* ADR decision 8 (owner ruling 2026-10-17, items 21 and 55), served by the
 * application on serial only: the Ethernet allow-list stays 0x68-0x6B. */
#define TFW_CMD_OTA_INFO          0x6Cu  /* [slot:1] -> 44 bytes, see ota_ctl.c */
#define TFW_CMD_OTA_ACTIVATE      0x6Du  /* [mode:1]: 0 next reset, 1 reset now */
#define TFW_CMD_OTA_CONFIRM       0x6Eu  /* no payload; TRIAL -> CONFIRMED */
#define TFW_CMD_OTA_AUTH          0x6Fu  /* [sig:64]: held for the next command (#137) */

/* provisioning -- PRESENT ONLY IN A PROVISIONING BUILD
 *
 * TFW_FEATURE_PROVISION defaults to 0 and a production image therefore contains
 * no handler for any of these: the dispatch table has no entry, the translation
 * unit is not compiled, and a fielded device answers TFW_ERR_UNKNOWN_CMD. That
 * is the point. Provisioning writes a device's identity, and a device that
 * could be re-provisioned over its own command channel has no identity worth
 * attesting to -- the attacker who reaches the serial port becomes the
 * manufacturer.
 *
 * So these ids are reserved on the wire, permanently, and unimplemented almost
 * everywhere. Reserving them rather than reusing the range keeps a provisioning
 * host and a production device from ever misunderstanding each other.
 */
#define TFW_CMD_PROV_INFO         0x70u  /* element revision, serial, lock state */
#define TFW_CMD_PROV_WRITE_CONFIG 0x71u  /* a 32-byte block of the config zone */
#define TFW_CMD_PROV_LOCK         0x72u  /* irreversible; zone in the payload */
#define TFW_CMD_PROV_GENKEY       0x73u  /* DeviceID keypair in its slot */
#define TFW_CMD_PROV_PUBKEY       0x74u  /* read back the public point */
#define TFW_CMD_PROV_SEED_UDS     0x75u  /* UDS from the element's own TRNG */

/* ------------------------------------------------ HSM self-defence channel */
/* The A55's half of the channel to the M7's response engine
 * (docs/hsm-self-defence/). Every reply that is not a refusal carries the
 * engine's state: [flags:1][selected:1][refused:4 LE]. Present only where
 * TFW_FEATURE_HSM is built; elsewhere the answer is TFW_ERR_UNKNOWN_CMD. */
#define TFW_CMD_HSM_HEARTBEAT     0x76u  /* S6's input: the A55 is alive */
#define TFW_CMD_HSM_REPORT        0x77u  /* [signal:1], S4 or S5 only */
#define TFW_CMD_HSM_VERDICT       0x78u  /* [verdict:1], V from the verifier */
#define TFW_CMD_HSM_SELECT        0x79u  /* [index:1], a policy of the set */
#define TFW_CMD_HSM_LOAD          0x7Au  /* [total:2][offset:2][bytes] of P/F */

/* Provisioning again, after the HSM ids: [slot:1][half:1][coordinate:32], half
 * 0 for X and 1 for Y, since X||Y and the slot do not fit one payload. Each
 * success is answered with the slot's X||Y as read back, 64 bytes. Present
 * where TFW_FEATURE_PROVISION is. */
#define TFW_CMD_PROV_WRITE_PUBKEY 0x7Bu  /* a P-256 public key into a data slot */

/* State flags in the reply. */
#define TFW_HSM_STATE_SET         0x01u  /* a verified policy set is held */
#define TFW_HSM_STATE_LOCKED      0x02u  /* the device keys are locked */
#define TFW_HSM_STATE_WIPED       0x04u  /* the device keys are wiped */
#define TFW_HSM_STATE_LEN         6u
#define TFW_HSM_LOAD_HDR          4u     /* total + offset */

/* ------------------------------------------------------ signature algorithms */
/* Carried in the AlgID byte of an attestation frame. Both ECDSA P-256 (r||s)
 * and Ed25519 (R||S) are exactly 64 raw bytes, so the frame layout does not
 * change with the algorithm -- only its interpretation does. */
#define TFW_ALG_NONE              0x00u
#define TFW_ALG_ECDSA_P256_SHA256 0x01u
#define TFW_ALG_ED25519           0x02u
/* Symmetric identity, per the TCG DICE Symmetric spec -- the path that spec
 * defines for parts too small for asymmetric crypto. Honest about what it is:
 * a verifier holding the key can forge the response, so this tier is for local
 * attestation behind an attested gateway, never for public admission. */
#define TFW_ALG_HMAC_SHA256       0x10u
/* ECDSA P-256 over SHA-256, the same 64-byte r||s as 0x01, signed by the
 * software DICE root (issue #130). The 0x1_ values are the software root's:
 * the evidence names its root by its AlgID, and where the soft root signs
 * ES256 as the element does, the algorithm alone would not. Written only into
 * an ATTEST frame (src/caps/attest.c); the provider contract's alg_id stays
 * 0x01, so the alias certificate and the CSR still sign ecdsa-with-SHA256. */
#define TFW_ALG_ECDSA_P256_SHA256_SOFT 0x11u

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
#define TFW_ALGBIT_ECDSA_P256_SHA256 0x0001u
#define TFW_ALGBIT_ED25519           0x0002u
#define TFW_ALGBIT_HMAC_SHA256       0x0004u

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
#define TFW_ATTEST_VERSION   1u
#define TFW_ATTEST_NONCE_LEN 16u
#define TFW_ATTEST_ID_LEN    16u
#define TFW_ATTEST_LEN       164u

/* The evidence is larger than a frame payload on a part this size, so it is
 * paged: ATTEST(nonce) computes and returns page 0, ATTEST(page) returns the
 * rest. Paging rather than enlarging every frame buffer, because the cost of a
 * bigger buffer is paid by every command on every board, and the cost of paging
 * is paid only by the one command that needs it. */
#define TFW_ATTEST_PAGE      48u

/* ------------------------------------------- the alias certificate, ATTEST_CERT
 *
 * TCG DICE Attestation Architecture r23 section 6.1.4 carries the verifier's
 * challenge in tcg-dice-UCCS-evidence (2.23.133.5.4.6), an extension that has
 * no signature of its own because "the certificate signature authenticates, and
 * integrity protects UCCS contents". So the answer to a challenge is a whole
 * X.509 certificate, minted on demand and signed by the DeviceID key.
 *
 * It is 488 to 490 bytes -- the spread is the DER encoding of the ECDSA
 * signature, whose two INTEGERs vary between 32 and 33 bytes each -- which is
 * seven times a frame payload and longer than a byte can count. So it is paged
 * like the evidence, with a TWO-byte total:
 *
 *   request   ATTEST_CERT(nonce[32])  mint, and return page 0
 *             ATTEST_CERT(page[1])    return a later page of the same document
 *   response  [page:1][total:2 little-endian][bytes:1..TFW_ALIAS_PAGE]
 *
 * A host reads total from page 0 and asks for ceil(total / TFW_ALIAS_PAGE)
 * pages; a page at or past the end is TFW_ERR_RANGE rather than an empty
 * success. The pages are REGENERATED, not cached, so they are consistent only
 * until the next nonce arrives -- the same contract ATTEST has, for the same
 * reason: no part in this family has room to hold the document.
 *
 * TFW_ALIAS_PAGE leaves room for the three header bytes inside one payload.
 */
#define TFW_ALIAS_PAGE       60u

/*
 * THE ENROLMENT CHALLENGE IS 32 BYTES, AND IT IS NOT TFW_ATTEST_NONCE_LEN.
 *
 * Two nonces of different widths in one firmware looks like an oversight and is
 * not, so this says why before someone unifies them.
 *
 *   TFW_ATTEST_NONCE_LEN (16)  RUNTIME FRESHNESS. It rides in the 164-byte
 *                             evidence frame and answers "is this device
 *                             reporting now". Sixteen bytes is a wire contract
 *                             with every deployed verifier and cannot move.
 *
 *   TFW_ALIAS_NONCE_LEN  (32)  ENROLMENT. It rides in the eat_nonce claim of the
 *                             alias certificate and answers "is this key on
 *                             this device". ACME device-attest-01 compares the
 *                             attestation nonce against the key authorization,
 *                             "<token>.<RFC 7638 thumbprint>" -- 87 bytes,
 *                             because the thumbprint alone is 43 base64url
 *                             characters. RFC 9711 bounds eat_nonce at 8..64,
 *                             so the raw form cannot be carried by a conforming
 *                             claims set at all. The profile therefore carries
 *                             SHA-256 of the key authorization, which is 32
 *                             bytes, and the HOST does that hashing: the device
 *                             never sees a key authorization and never hashes
 *                             one. It receives 32 bytes and places them
 *                             verbatim.
 *
 * Merging them would either truncate an enrolment challenge to 16 bytes or
 * widen an evidence frame that every deployed host parses at a fixed offset.
 */
#define TFW_ALIAS_NONCE_LEN  32u

/* ------------------------------------------------------------------ APP_INFO
 *
 * The linked application's descriptor and the runner's state, one fixed
 * 63-byte reply, little-endian like every other:
 *
 *   [state:1][overruns:2][abi:1][ota_slots:1]
 *   [version_major:1][version_minor:1][version_patch:2]
 *   [config_len:2][required_caps:4][name:16][profile:32]
 *
 * The descriptor's fields are its own bytes, in its own order, minus the magic:
 * a descriptor the runner refused is reported as written, and `state` says it
 * was refused. name and profile are their whole fields, NUL-padded. The request
 * is empty; a later page byte could extend it without breaking this shape. */
#define TFW_APP_INFO_LEN     63u

/* ------------------------------------------------------------------ events */
#define TFW_EVENT_BOOT            0x01u  /* sent once at startup; an unexpected
                                         * one means the device reset */
#define TFW_EVENT_DEADMAN         0x02u  /* the link went quiet and scheduled
                                         * pins were driven to safety */
#define TFW_EVENT_COUNTER_WRAP    0x03u  /* a pulse counter wrapped */

#endif /* TFW_PROTOCOL_H */
