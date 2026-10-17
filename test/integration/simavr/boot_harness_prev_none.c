/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * A board with no PREV in its store (the 2560 falls back to its secondary
 * slot): linked instead of boot_harness_prev.c (mk/boot.mk). The harness's
 * prev row fails on it, never passes by running nothing.
 */
long boot_harness_prev_addr(void);

long boot_harness_prev_addr(void)
{
    return -1L;
}
