/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The P-256 DeviceID derived from a UDS: src/crypto/deviceid_p256.c.
 *
 * The caller's root computes the seed, HMAC-SHA256(UDS, TFW_DEVICEID_LABEL),
 * where its UDS lives -- the ESP32's HMAC peripheral, the AVR soft root's
 * EEPROM -- and zeroes it after the call. Nothing here keeps state: every call
 * derives the scalar again and zeroes it before it returns.
 */
#ifndef TFW_DEVICEID_P256_H
#define TFW_DEVICEID_P256_H

#include <stdint.h>

/* The HMAC message that turns a UDS into the DeviceID seed. Versioned: a new
 * label is a new DeviceID on every device, so it is changed only on purpose. */
#define TFW_DEVICEID_LABEL     "tfw-deviceid-v1"
#define TFW_DEVICEID_LABEL_LEN 15u

/* The scalar d in [1, n-1] from the seed, big-endian. The caller zeroes it. */
void tfw_deviceid_p256_scalar(const uint8_t seed[32], uint8_t d[32]);

/* The DeviceID's public point d.G, X || Y big-endian: the wire's PUBKEY. */
void tfw_deviceid_p256_public(const uint8_t seed[32], uint8_t pub[64]);

/* ES256 over a SHA-256 digest with the DeviceID: r || s, big-endian. */
void tfw_deviceid_p256_sign(const uint8_t seed[32], const uint8_t digest[32],
                            uint8_t sig[64]);

/* ECDSA P-256 with k from RFC 6979 section 3.2 (HMAC-SHA256), for any scalar
 * in [1, n-1]. The sign above is this with the derived scalar. */
void tfw_p256_sign_rfc6979(const uint8_t d[32], const uint8_t digest[32],
                           uint8_t sig[64]);

#endif /* TFW_DEVICEID_P256_H */
