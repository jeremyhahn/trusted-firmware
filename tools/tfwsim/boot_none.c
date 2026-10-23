/*
 * tools/tfwsim/boot_none.c -- a simulator with no bootloader (sim_boot.h).
 *
 * The firmware starts with whatever the tier seeded, and a reset it asks for
 * is the mock HAL's count, as it always was.
 */
#include "sim_boot.h"

int tfwsim_boot_option(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return 0;
}

const char *tfwsim_boot_usage(void) { return ""; }

int tfwsim_boot_power_on(void) { return 0; }

int tfwsim_boot_reset(void) { return 0; }
