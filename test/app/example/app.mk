# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# The example application's build description, read by mk/app.mk. An
# application repository carries one of these at its root.
APP_NAME := heartbeat
APP_SRCS := example.c
# example.c defines tfw_app_command (APP_CMD op 0x00, the blink count).
APP_COMMANDS := yes
