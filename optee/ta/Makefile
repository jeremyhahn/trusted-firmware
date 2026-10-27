# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# The TA, built with the pinned optee_os's TA dev kit. mk/optee.mk calls it with
# TA_DEV_KIT_DIR, CROSS_COMPILE and O (an output directory in OPTEE_CACHE, so
# nothing is written in the checkout); the dev kit's own makefile does the rest,
# signing included. BINARY is the TA's UUID: optee/ta/include/tfw_ta.h.
BINARY = 23751b49-3a76-4e49-b63c-e5853742fa1f

include $(TA_DEV_KIT_DIR)/mk/ta_dev_kit.mk
