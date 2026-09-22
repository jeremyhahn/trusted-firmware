/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * ag_status_t — the one result type in this codebase.
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
#ifndef AG_STATUS_H
#define AG_STATUS_H

#include <stdint.h>

typedef enum {
    AG_OK                   = 0,  /* success */

    /* framing and transport -- raised below the dispatcher */
    AG_ERR_CRC              = 1,  /* frame CRC mismatch */
    AG_ERR_FRAMING          = 2,  /* implausible LEN, or idle timeout mid-frame */
    AG_ERR_TOO_LONG         = 3,  /* request exceeds AG_MAX_LEN, or a response overflowed */

    /* dispatch -- the request never reached a capability module */
    AG_ERR_UNKNOWN_CMD      = 4,  /* no table entry for this command id */
    AG_ERR_BAD_ARGC         = 5,  /* payload length wrong for this command */

    /* argument validation -- the capability module refused the request */
    AG_ERR_BAD_PIN          = 6,  /* pin index out of range for this board */
    AG_ERR_UNSUPPORTED      = 7,  /* compiled out, or this pin lacks the capability */
    AG_ERR_BUSY             = 8,  /* peripheral already in use */
    AG_ERR_TIMEOUT          = 9,  /* pulse_in, i2c or onewire timed out */
    AG_ERR_NACK             = 10, /* i2c address or data NACK */
    AG_ERR_RANGE            = 11, /* value outside the permitted interval */
    AG_ERR_NO_SLOT          = 12, /* scheduler or counter table full */
    AG_ERR_RESERVED_PIN     = 13, /* pin is /RESET, or claimed by the soft UART */

    /* trust -- raised once a crypto provider is present */
    AG_ERR_CRYPTO           = 14, /* the crypto provider failed the operation */
    AG_ERR_NOT_PROVISIONED  = 15, /* no device identity has been provisioned */
    AG_ERR_PROTECTED_REGION = 16, /* the target is platform-reserved */
    AG_ERR_AUTH_REQUIRED    = 17, /* command needs an authenticated session */

    AG_ERR_INTERNAL         = 18  /* a condition the firmware believes impossible */
} ag_status_t;

/*
 * The widest status value the wire can carry. The STATUS field is one byte, so
 * a value above this cannot be encoded and is a programming error rather than a
 * runtime condition.
 */
#define AG_STATUS_MAX 0xFEu

/* Returns a short stable name, or "?" for a value this build does not know.
 * Present in host builds and debug firmware only -- AG_STATUS_NAMES is off by
 * default so the string table costs a release image nothing. */
#if defined(AG_STATUS_NAMES) && AG_STATUS_NAMES
const char *ag_status_name(ag_status_t s);
#endif

#endif /* AG_STATUS_H */
