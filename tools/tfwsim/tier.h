/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The seam between tfwsim's shared loop (main.c) and the crypto tier it was
 * linked with.
 *
 * Exactly one implementation is linked, chosen by the Makefile the way the
 * firmware's own crypto provider is: tier_soft.c for `tfwsim`, tier_atecc608a.c
 * for `tfwsim-atecc608a`. There is no #if here and no runtime switch -- a
 * binary cannot be talked into a tier it was not built for.
 */
#ifndef TFWSIM_TIER_H
#define TFWSIM_TIER_H

/*
 * Offers argv[0..argc) to the tier. Returns how many entries it consumed, or
 * 0 if argv[0] is not an option this tier has. A malformed value is reported
 * on stderr and returned as -1, and the caller exits: a simulator that quietly
 * ignored a bad identity file would start unprovisioned and let a test pass or
 * fail for a reason nobody printed.
 */
int  tfwsim_tier_option(int argc, char **argv);

/* What the options above select, as the usage line shows it. */
const char *tfwsim_tier_usage(void);

/*
 * Applies the options to the freshly reset mock HAL and the tier's own state.
 * Called once, after tfw_mock_reset() and before the firmware's startup
 * sequence, so the firmware reads what a real device would find at power-on.
 */
void tfwsim_tier_seed(void);

/*
 * Non-zero when a RESET restarts the firmware in place, keeping the EEPROM and
 * the element. main.c's weak definition answers 0; tools/tfwsim/reboot.c, in
 * tfwsim-prov's object list only, answers 1.
 */
int tfwsim_reboot_on_reset(void);

#endif /* TFWSIM_TIER_H */
