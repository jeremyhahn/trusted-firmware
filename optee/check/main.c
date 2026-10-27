/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw-ta-check's entry point, in the guest: exit 0 when every case passed.
 */

#include "check.h"

int main(void)
{
    return tfw_ta_check(stdout) == 0 ? 0 : 1;
}
