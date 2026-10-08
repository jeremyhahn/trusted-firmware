/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "secure_zero.h"

void tfw_secure_zero(void *p, size_t n)
{
    volatile unsigned char *vp = (volatile unsigned char *)p;

    while (n--) {
        *vp++ = 0u;
    }
}
