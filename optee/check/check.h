/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw-ta-check: calls the TA through the GlobalPlatform TEE Client API and
 * compares every answer to optee/testdata/vectors.h.
 */
#ifndef TFW_TA_CHECK_H
#define TFW_TA_CHECK_H

#include <stdio.h>

/*
 * Runs every case, printing one line a case and a summary to out, each
 * starting "tfw-ta-check: ". Returns the number of cases that failed, or -1
 * when no context or no session could be had, which is printed too.
 */
int tfw_ta_check(FILE *out);

#endif /* TFW_TA_CHECK_H */
