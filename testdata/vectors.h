/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * THE WIRE VECTORS.
 *
 * These bytes are the contract between the C firmware and the Go host SDK.
 * Both assert against them, and the two co-merge only when both are green.
 *
 * They are LITERALS ON PURPOSE. A vector computed from the code it is meant to
 * pin moves whenever the code moves, and would agree with a regression as
 * readily as with the contract. Every value below was produced once, checked
 * against an independent implementation, and then frozen.
 *
 * A FAILURE HERE IS A WIRE BREAK, NOT A TEST TO UPDATE. Changing any of these
 * values makes every already-deployed device fail to talk to an updated host.
 * Changing the contract means a new protocol major version, not new literals.
 *
 * The Go mirror is sdk/go/internal/vectors/vectors.go and CI checks that the
 * two files carry identical values.
 */
#ifndef TFW_TESTDATA_VECTORS_H
#define TFW_TESTDATA_VECTORS_H

#include <stdint.h>

/* ---------------------------------------------------------------- CRC-16 */
/* CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no final XOR. */

/* The published check value for this polynomial and initial value: the CRC of
 * the nine ASCII bytes "123456789". Every CRC-16/CCITT-FALSE implementation in
 * the world agrees on this number, which is what makes it worth pinning. */
#define TFWV_CRC_CHECK_INPUT   "123456789"
#define TFWV_CRC_CHECK_LEN     9u
#define TFWV_CRC_CHECK_RESULT  0x29B1u

/* The empty input. Folding nothing must leave the initial value untouched --
 * an implementation that special-cases length zero usually gets this wrong. */
#define TFWV_CRC_EMPTY_RESULT  0xFFFFu

/* A single zero byte. Distinguishes "init returned unchanged" from "one byte
 * processed", which the empty case alone cannot. */
#define TFWV_CRC_ZERO_RESULT   0xE1F0u

/* Two zero bytes, to catch a shift that drops the high half on the second
 * round rather than the first. */
#define TFWV_CRC_ZEROZERO_RESULT 0x1D0Fu

/* 0xFF, the all-ones byte, which exercises every branch of the bitwise loop
 * on its first fold. */
#define TFWV_CRC_FF_RESULT     0xFF00u

/* ORDER SENSITIVITY. These two inputs are byte-permutations of each other, so
 * an implementation that sums or XORs bytes instead of folding them through
 * the polynomial produces the same value for both. The real-path tests cannot
 * catch that on their own -- both ends would call the same broken function and
 * agree -- so the vector set carries the discriminator explicitly, for the
 * benefit of whoever writes the next implementation. */
#define TFWV_CRC_ORDER_A       "\x01\x02"
#define TFWV_CRC_ORDER_A_RESULT 0x0E7Cu
#define TFWV_CRC_ORDER_B       "\x02\x01"
#define TFWV_CRC_ORDER_B_RESULT 0x6B4Cu

/* ---------------------------------------------------------------- SHA-256 */
/* FIPS 180-4. The two vectors every implementation is checked against, plus
 * the empty input -- which catches a padding routine that special-cases a
 * length of zero, and a great many do. */

#define TFWV_SHA256_ABC_INPUT  "abc"
#define TFWV_SHA256_ABC \
    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"

#define TFWV_SHA256_EMPTY \
    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"

/* 448 bits: the length that forces the padding to spill into a SECOND block.
 * An implementation that only ever hashes short inputs passes everything above
 * and fails here. */
#define TFWV_SHA256_448_INPUT \
    "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
#define TFWV_SHA256_448 \
    "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"

/* ----------------------------------------------------------- HMAC-SHA256 */
/* RFC 4231 test case 1. The DICE one-way function is HMAC, so this is the
 * primitive the whole identity chain rests on. */

#define TFWV_HMAC_TC1_KEY_BYTE 0x0bu   /* twenty repetitions */
#define TFWV_HMAC_TC1_KEY_LEN  20u
#define TFWV_HMAC_TC1_DATA     "Hi There"
#define TFWV_HMAC_TC1 \
    "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"

/* -------------------------------------------------------------- SYS_INFO -- */
/* #5: err_crc and err_format are APPENDED after reset_cause, never inserted,
 * so a reply built for an older host and a reply built for this one differ
 * only in whether the last four bytes exist. Both are literal wire bytes, not
 * built from tfw_write_u32/u16/u8 the way the handler is: a vector derived from
 * the code that pins it would move whenever that code did.
 *
 * Fields, little-endian: uptime_ms=0x12345678, free_ram=0x0321,
 * reset_cause=TFW_RESET_WATCHDOG(4), and in the new reply err_crc=5,
 * err_format=7 -- the same uptime/RAM/cause triple test_dispatch.c's
 * pre-existing SYS_INFO test already used, so the two are checked against one
 * scenario rather than two.
 */
#define TFWV_SYSINFO_OLD_REPLY     "\x78\x56\x34\x12\x21\x03\x04"
#define TFWV_SYSINFO_OLD_REPLY_LEN 7u

#define TFWV_SYSINFO_NEW_REPLY     "\x78\x56\x34\x12\x21\x03\x04\x05\x00\x07\x00"
#define TFWV_SYSINFO_NEW_REPLY_LEN 11u

/* #12: free_ram widened by APPENDING a u32 after err_format; the u16 stays where
 * it was, saturating at 0xFFFF. WIDE is what the firmware sends today and
 * begins with NEW, byte for byte: free_ram=0x00000321 appended.
 *
 * BIG is the case the widening exists for: free_ram=0x00012345, which the u16
 * cannot hold and so reads 0xFFFF there, with the exact value in the tail. */
#define TFWV_SYSINFO_WIDE_REPLY "\x78\x56\x34\x12\x21\x03\x04\x05\x00\x07\x00\x21\x03\x00\x00"
#define TFWV_SYSINFO_WIDE_REPLY_LEN 15u

#define TFWV_SYSINFO_BIG_REPLY "\x78\x56\x34\x12\xff\xff\x04\x05\x00\x07\x00\x45\x23\x01\x00"
#define TFWV_SYSINFO_BIG_REPLY_LEN 15u

/* ------------------------------------------------------------------ CAPS -- */
/* #12 made the pin page six bytes and so bumped the protocol to 2.0: a 1.x host
 * reading the page would take the bit for the ADC channel and the ADC channel
 * for the PWM state. Both are pinned here as literal wire bytes.
 *
 * PROTO is the board header's first two bytes, major then minor. A host refuses
 * a device whose major differs from its own, so this pair is what turns the
 * page's layout change into a refusal rather than a misread.
 *
 * PIN13 is the 328P's pin page for its LED: pin 13, flags DIGITAL|LED (0x81),
 * port 0 (PORTB), bit 5, no ADC channel, no compare unit. */
#define TFWV_CAPS_PROTO_REPLY     "\x02\x00"
#define TFWV_CAPS_PROTO_REPLY_LEN 2u

#define TFWV_CAPS_PIN13_REPLY     "\x0d\x81\x00\x05\xff\x00"
#define TFWV_CAPS_PIN13_REPLY_LEN 6u

/* ------------------------------------------------------------ wire frames */
/* One full encoded request -- SOF through CRC16 -- for I2C_CONFIG and
 * SPI_CONFIG (issue #21), each at a fixed SEQ and payload. Produced once by
 * tfw_frame_encode_request and cross-checked against an independent CRC16
 * implementation, then frozen: a change to either command's id, payload
 * layout or byte order breaks this instead of shipping silently. */

/* TFW_CMD_I2C_CONFIG (0x55), seq 0x01, payload hz=100000 (0x000186A0 LE). */
#define TFWV_FRAME_I2C_CONFIG_REQ "\xa5\x06\x01\x55\xa0\x86\x01\x00\x93\x73"

/* TFW_CMD_SPI_CONFIG (0x56), seq 0x02, payload mode=3 clkdiv=2 lsb_first=1. */
#define TFWV_FRAME_SPI_CONFIG_REQ "\xa5\x05\x02\x56\x03\x02\x01\x23\x74"

/* ---------------------------------------------------------- ATTEST_CSR -- */
/* The CertificationRequestInfo ATTEST_CSR (0x64, issue #30) signs, and its
 * SHA-256. The command sends only the signature; a host rebuilds these bytes
 * from b, the DeviceID, the key and the measurement, so they are as much wire
 * contract as a frame is.
 *
 * Inputs, all from testdata/uccs_vectors.h: b = TFWV_UCCS_NONCE, DeviceID =
 * TFWV_UCCS_UEID, key = TFWV_ALIAS_PUBKEY, measurement = TFWV_UCCS_FWHASH.
 *
 * The device is named by the lowercase hex of its whole UEID, type byte
 * included -- 34 digits, go-xkms#1441's profile -- not by the 32-digit DeviceID.
 *
 * Produced by an independent encoder -- a TLV and CBOR helper that computed
 * every length itself and took nothing from the firmware, and that reproduces
 * this vector's earlier 32-digit form byte for byte -- then read back by
 * `openssl req -text` (version 0, empty subject, P-256 key, a critical SAN
 * otherName of type id-on-permanentIdentifier, the critical 2.23.133.5.4.6
 * evidence extension) and by `openssl asn1parse`, whose -strparse of the SAN
 * reads the UTF8String "01a1b2c3d4e5f60718293a4b5c6d7e8f90". The same bytes
 * with the test DeviceID key's own point, signed with that key and wrapped as
 * a CertificationRequest, pass `openssl req -verify`. The digest is from
 * Python's hashlib, not from src/crypto/sha256.c. */
#define TFWV_CSR_CRI_LEN 365u
#define TFWV_CSR_CRI \
    "\x30\x82\x01\x69\x02\x01\x00\x30\x00\x30" \
    "\x59\x30\x13\x06\x07\x2A\x86\x48\xCE\x3D" \
    "\x02\x01\x06\x08\x2A\x86\x48\xCE\x3D\x03" \
    "\x01\x07\x03\x42\x00\x04\x79\x89\x53\xE7" \
    "\xE8\x13\x4F\xDF\x3C\x13\x9F\x63\xD3\xFB" \
    "\xCC\xC2\x52\xA2\x8B\x6C\xA5\x05\x9E\x61" \
    "\x83\x74\xA8\x12\x31\x24\x0F\x3F\xC8\x32" \
    "\x67\xAE\xC7\x25\xE1\x8B\x66\x17\x6C\x36" \
    "\x85\xD1\x25\x72\x01\xA6\x70\x33\x81\x95" \
    "\x85\xA2\x2A\x29\x63\x50\x15\x9A\xE7\x0B" \
    "\xA0\x82\x01\x05\x30\x82\x01\x01\x06\x09" \
    "\x2A\x86\x48\x86\xF7\x0D\x01\x09\x0E\x31" \
    "\x81\xF3\x30\x81\xF0\x30\x40\x06\x03\x55" \
    "\x1D\x11\x01\x01\xFF\x04\x36\x30\x34\xA0" \
    "\x32\x06\x08\x2B\x06\x01\x05\x05\x07\x08" \
    "\x03\xA0\x26\x30\x24\x0C\x22\x30\x31\x61" \
    "\x31\x62\x32\x63\x33\x64\x34\x65\x35\x66" \
    "\x36\x30\x37\x31\x38\x32\x39\x33\x61\x34" \
    "\x62\x35\x63\x36\x64\x37\x65\x38\x66\x39" \
    "\x30\x30\x81\xAB\x06\x06\x67\x81\x05\x05" \
    "\x04\x06\x01\x01\xFF\x04\x81\x9D\x30\x81" \
    "\x9A\x04\x81\x97\xA3\x0A\x58\x20\x00\x01" \
    "\x02\x03\x04\x05\x06\x07\x08\x09\x0A\x0B" \
    "\x0C\x0D\x0E\x0F\x10\x11\x12\x13\x14\x15" \
    "\x16\x17\x18\x19\x1A\x1B\x1C\x1D\x1E\x1F" \
    "\x19\x01\x00\x51\x01\xA1\xB2\xC3\xD4\xE5" \
    "\xF6\x07\x18\x29\x3A\x4B\x5C\x6D\x7E\x8F" \
    "\x90\x19\x01\x11\x81\x82\x19\x01\x02\x58" \
    "\x54\xA5\x00\x50\xBC\xC7\x10\x07\x1A\x66" \
    "\x53\xC1\x96\x7E\xF0\xD3\xD5\xED\xE4\x21" \
    "\x01\x63\x74\x66\x77\x02\xA2\x18\x1F\x63" \
    "\x74\x66\x77\x18\x21\x01\x03\xA1\x11\xA2" \
    "\x07\x82\x01\x58\x20\xBA\x78\x16\xBF\x8F" \
    "\x01\xCF\xEA\x41\x41\x40\xDE\x5D\xAE\x22" \
    "\x23\xB0\x03\x61\xA3\x96\x17\x7A\x9C\xB4" \
    "\x10\xFF\x61\xF2\x00\x15\xAD\x18\x18\x63" \
    "\x61\x70\x70\x0C\x00"
#define TFWV_CSR_CRI_DIGEST \
    "\x32\xF4\x87\x9D\xEE\xFE\x71\x6B\xFE\xAA" \
    "\x46\x84\x94\xA4\xEB\x19\xEB\x6A\x13\x07" \
    "\x5D\xA2\x0E\xC1\x57\xC2\x32\xD8\xB6\x3F" \
    "\x37\x65"

#endif /* TFW_TESTDATA_VECTORS_H */
