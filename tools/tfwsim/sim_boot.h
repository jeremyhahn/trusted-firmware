/*
 * tools/tfwsim/sim_boot.h -- tfw-boot in front of the simulated firmware (#52).
 *
 * Two implementations, one per binary, chosen by the link as the tier is:
 * boot_host.c runs the real tfw-boot (boot/boot_main.c) through the host boot
 * HAL before the firmware starts and at every reset the firmware asks for;
 * boot_none.c is a simulator with no bootloader, the firmware starting as it
 * always has. Without --boot-key, boot_host.c behaves as boot_none.c.
 */
#ifndef TFWSIM_SIM_BOOT_H
#define TFWSIM_SIM_BOOT_H

/* The options this binary's bootloader takes: the count of argv consumed, 0
 * when argv[0] is not one of them, -1 when it is and is wrong (reported). */
int         tfwsim_boot_option(int argc, char **argv);
const char *tfwsim_boot_usage(void);

/* Power on, after the mock is reset and seeded: the boot pass, if there is a
 * bootloader. 0 starts the firmware; anything else is tfwsim's exit status. */
int tfwsim_boot_power_on(void);

/* The firmware asked for a reset: the RAM-only state is gone, the EEPROM, the
 * handoff, the flash and the store are not; then the boot pass. As above. */
int tfwsim_boot_reset(void);

/* main.c: back to the top of the firmware's startup, from inside any poll. */
_Noreturn void tfwsim_reboot(void);

#endif /* TFWSIM_SIM_BOOT_H */
