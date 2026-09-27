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
#ifndef AG_TESTDATA_VECTORS_H
#define AG_TESTDATA_VECTORS_H

#include <stdint.h>

/* ---------------------------------------------------------------- CRC-16 */
/* CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no final XOR. */

/* The published check value for this polynomial and initial value: the CRC of
 * the nine ASCII bytes "123456789". Every CRC-16/CCITT-FALSE implementation in
 * the world agrees on this number, which is what makes it worth pinning. */
#define AGV_CRC_CHECK_INPUT   "123456789"
#define AGV_CRC_CHECK_LEN     9u
#define AGV_CRC_CHECK_RESULT  0x29B1u

/* The empty input. Folding nothing must leave the initial value untouched --
 * an implementation that special-cases length zero usually gets this wrong. */
#define AGV_CRC_EMPTY_RESULT  0xFFFFu

/* A single zero byte. Distinguishes "init returned unchanged" from "one byte
 * processed", which the empty case alone cannot. */
#define AGV_CRC_ZERO_RESULT   0xE1F0u

/* Two zero bytes, to catch a shift that drops the high half on the second
 * round rather than the first. */
#define AGV_CRC_ZEROZERO_RESULT 0x1D0Fu

/* 0xFF, the all-ones byte, which exercises every branch of the bitwise loop
 * on its first fold. */
#define AGV_CRC_FF_RESULT     0xFF00u

/* ORDER SENSITIVITY. These two inputs are byte-permutations of each other, so
 * an implementation that sums or XORs bytes instead of folding them through
 * the polynomial produces the same value for both. The real-path tests cannot
 * catch that on their own -- both ends would call the same broken function and
 * agree -- so the vector set carries the discriminator explicitly, for the
 * benefit of whoever writes the next implementation. */
#define AGV_CRC_ORDER_A       "\x01\x02"
#define AGV_CRC_ORDER_A_RESULT 0x0E7Cu
#define AGV_CRC_ORDER_B       "\x02\x01"
#define AGV_CRC_ORDER_B_RESULT 0x6B4Cu

/* ---------------------------------------------------------------- SHA-256 */
/* FIPS 180-4. The two vectors every implementation is checked against, plus
 * the empty input -- which catches a padding routine that special-cases a
 * length of zero, and a great many do. */

#define AGV_SHA256_ABC_INPUT  "abc"
#define AGV_SHA256_ABC \
    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"

#define AGV_SHA256_EMPTY \
    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"

/* 448 bits: the length that forces the padding to spill into a SECOND block.
 * An implementation that only ever hashes short inputs passes everything above
 * and fails here. */
#define AGV_SHA256_448_INPUT \
    "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
#define AGV_SHA256_448 \
    "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"

/* ----------------------------------------------------------- HMAC-SHA256 */
/* RFC 4231 test case 1. The DICE one-way function is HMAC, so this is the
 * primitive the whole identity chain rests on. */

#define AGV_HMAC_TC1_KEY_BYTE 0x0bu   /* twenty repetitions */
#define AGV_HMAC_TC1_KEY_LEN  20u
#define AGV_HMAC_TC1_DATA     "Hi There"
#define AGV_HMAC_TC1 \
    "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"

/* -------------------------------------------------------------- SYS_INFO -- */
/* #5: err_crc and err_format are APPENDED after reset_cause, never inserted,
 * so a reply built for an older host and a reply built for this one differ
 * only in whether the last four bytes exist. Both are literal wire bytes, not
 * built from ag_write_u32/u16/u8 the way the handler is: a vector derived from
 * the code that pins it would move whenever that code did.
 *
 * Fields, little-endian: uptime_ms=0x12345678, free_ram=0x0321,
 * reset_cause=AG_RESET_WATCHDOG(4), and in the new reply err_crc=5,
 * err_format=7 -- the same uptime/RAM/cause triple test_dispatch.c's
 * pre-existing SYS_INFO test already used, so the two are checked against one
 * scenario rather than two.
 */
#define AGV_SYSINFO_OLD_REPLY     "\x78\x56\x34\x12\x21\x03\x04"
#define AGV_SYSINFO_OLD_REPLY_LEN 7u

#define AGV_SYSINFO_NEW_REPLY     "\x78\x56\x34\x12\x21\x03\x04\x05\x00\x07\x00"
#define AGV_SYSINFO_NEW_REPLY_LEN 11u

#endif /* AG_TESTDATA_VECTORS_H */
