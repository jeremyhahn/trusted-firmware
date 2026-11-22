/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw_status_t — the one result type in this codebase.
 *
 * Every layer returns it: the HAL, the capability modules, the dispatcher and
 * the wire. There is no second error representation, no per-module errno and
 * no in-band sentinel value, because a project with two error vocabularies
 * eventually has a site that translates one into the other and loses a case.
 *
 * THESE NUMBERS ARE WIRE VALUES. They are carried in the STATUS byte of a
 * response frame, so they extend and never renumber. A host built before a
 * status existed reads the new number as unrecognised and falls through to its
 * generic failure path -- degraded to exactly the behaviour it had before the
 * status was added, never a misclassification. Renumbering instead silently
 * turns one failure into a different one on every host that has not been
 * rebuilt, which is the failure mode this comment exists to prevent.
 */
#ifndef TFW_STATUS_H
#define TFW_STATUS_H

#include <stdint.h>

typedef enum {
    TFW_OK                   = 0,  /* success */

    /* framing and transport -- raised below the dispatcher */
    TFW_ERR_CRC              = 1,  /* frame CRC mismatch */
    TFW_ERR_FRAMING          = 2,  /* implausible LEN, or idle timeout mid-frame */
    TFW_ERR_TOO_LONG         = 3,  /* request exceeds TFW_MAX_LEN, or a response overflowed */

    /* dispatch -- the request never reached a capability module */
    TFW_ERR_UNKNOWN_CMD      = 4,  /* no table entry for this command id */
    TFW_ERR_BAD_ARGC         = 5,  /* payload length wrong for this command */

    /* argument validation -- the capability module refused the request */
    TFW_ERR_BAD_PIN          = 6,  /* pin index out of range for this board */
    TFW_ERR_UNSUPPORTED      = 7,  /* compiled out, or this pin lacks the capability */
    TFW_ERR_BUSY             = 8,  /* peripheral already in use */
    TFW_ERR_TIMEOUT          = 9,  /* a bus, a pulse, an ADC conversion,
                                    * a secure element or the external
                                    * store did not answer in time */
    TFW_ERR_NACK             = 10, /* i2c address or data NACK */
    TFW_ERR_RANGE            = 11, /* value outside the permitted interval */
    TFW_ERR_NO_SLOT          = 12, /* scheduler or counter table full */
    TFW_ERR_RESERVED_PIN     = 13, /* pin is TFW_PIN_RESERVED (/RESET,
                                    * a software peripheral's) or
                                    * TFW_PIN_IN_USE (claimed by this
                                    * build) */

    /* trust -- raised once a crypto provider is present */
    TFW_ERR_CRYPTO           = 14, /* the crypto provider failed the operation */
    TFW_ERR_NOT_PROVISIONED  = 15, /* no device identity has been provisioned */
    TFW_ERR_PROTECTED_REGION = 16, /* the target is platform-reserved */
    TFW_ERR_AUTH_REQUIRED    = 17, /* command needs an authenticated session */

    TFW_ERR_INTERNAL         = 18, /* a condition the firmware believes impossible */
    TFW_ERR_BAD_STATE        = 19, /* the command is not valid in the current OTA state */
    TFW_ERR_NO_STORE         = 20  /* the external store did not answer its JEDEC ID, or latch WEL */
} tfw_status_t;

/*
 * The widest status value the wire can carry. The STATUS field is one byte, so
 * a value above this cannot be encoded and is a programming error rather than a
 * runtime condition.
 */
#define TFW_STATUS_MAX 0xFEu

/* Returns a short stable name, or "?" for a value this build does not know.
 * Declared only for a build that defines TFW_STATUS_NAMES and supplies the
 * string table; no build on main does, so no image carries it and the table
 * costs a release image nothing. */
#if defined(TFW_STATUS_NAMES) && TFW_STATUS_NAMES
const char *tfw_status_name(tfw_status_t s);
#endif

#endif /* TFW_STATUS_H */
