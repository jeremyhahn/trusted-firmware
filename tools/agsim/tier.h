/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The seam between agsim's shared loop (main.c) and the crypto tier it was
 * linked with.
 *
 * Exactly one implementation is linked, chosen by the Makefile the way the
 * firmware's own crypto provider is: tier_soft.c for `agsim`, tier_atecc608a.c
 * for `agsim-atecc608a`. There is no #if here and no runtime switch -- a
 * binary cannot be talked into a tier it was not built for.
 */
#ifndef AGSIM_TIER_H
#define AGSIM_TIER_H

/*
 * Offers argv[0..argc) to the tier. Returns how many entries it consumed, or
 * 0 if argv[0] is not an option this tier has. A malformed value is reported
 * on stderr and returned as -1, and the caller exits: a simulator that quietly
 * ignored a bad identity file would start unprovisioned and let a test pass or
 * fail for a reason nobody printed.
 */
int  agsim_tier_option(int argc, char **argv);

/* What the options above select, as the usage line shows it. */
const char *agsim_tier_usage(void);

/*
 * Applies the options to the freshly reset mock HAL and the tier's own state.
 * Called once, after ag_mock_reset() and before the firmware's startup
 * sequence, so the firmware reads what a real device would find at power-on.
 */
void agsim_tier_seed(void);

#endif /* AGSIM_TIER_H */
