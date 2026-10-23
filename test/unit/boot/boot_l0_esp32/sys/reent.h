/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/* Stand-in for newlib's sys/reent.h, for test_boot_l0_esp32_mutate.c: the
 * host's libc has none, and CONFIG_LIBC_NEWLIB is 0 here. */
#ifndef TFW_TEST_L0_SYS_REENT_H
#define TFW_TEST_L0_SYS_REENT_H

struct _reent;

#endif
