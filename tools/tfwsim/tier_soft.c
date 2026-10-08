/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The software tier's side of tfwsim. It has nothing to seed: the software
 * provider's UDS comes from the mock (test/mock/uds_mock.c), unset, so the
 * device is unprovisioned and says so.
 *
 * It offers no options, and an option it does not offer is an error in main.c
 * rather than silently ignored -- `tfwsim --eeprom x` must not quietly start a
 * device that never saw the file.
 */

#include "tier.h"

int tfwsim_tier_option(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return 0;
}

const char *tfwsim_tier_usage(void)
{
    return "";
}

void tfwsim_tier_seed(void)
{
}
