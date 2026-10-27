# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# tfw-ta-check for the guest. mk/optee.mk calls it with CROSS_COMPILE (the
# toolchain the pinned OP-TEE build fetched), TEEC_EXPORT (the usr directory
# where Buildroot staged libteec and tee_client_api.h) and O (an output
# directory in OPTEE_CACHE, so nothing is written in the checkout).
CC      := $(CROSS_COMPILE)gcc
CFLAGS  := -std=c11 -O2 -Wall -Wextra -Werror -Wconversion \
           -I../ta/include -I$(TEEC_EXPORT)/include
LDFLAGS := -L$(TEEC_EXPORT)/lib
SRCS    := main.c check.c

$(O)/tfw-ta-check: $(SRCS) check.h ../ta/include/tfw_ta.h ../testdata/vectors.h
	@mkdir -p $(O)
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS) -lteec
