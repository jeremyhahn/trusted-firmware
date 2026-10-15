# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# The application hook (docs/application-sdk/). An application lives in its own
# repository and is linked into this firmware by naming its directory:
#
#   make firmware APP_DIR=../trusted-firmware-heartbeat BOARD=atmega328p
#   make tfwsim   APP_DIR=../trusted-firmware-heartbeat
#
# APP_DIR/app.mk sets APP_NAME, APP_SRCS (relative to APP_DIR) and optionally
# APP_CFLAGS. Nothing in this repository is edited to add one.
#
# Included BEFORE mk/host.mk and mk/firmware.mk: both name their outputs after
# the application, so a build with one never overwrites the image without it.
# Only variables are set here; the rules that compile an application's files
# are beside the ones that compile the firmware's, in those two files.
#
# APP_RUNNER_SRCS is the runner the image links. With an application it is
# src/core/app_runner.c; with no APP_DIR it is src/core/app_runner_none.c,
# whose entry points are empty, so a build with no application links no
# runner, no descriptor and no configuration code and costs no flash.

APP_DIR ?=

ifneq ($(strip $(APP_DIR)),)
APP_ABS := $(abspath $(APP_DIR))
ifeq ($(wildcard $(APP_ABS)/app.mk),)
$(error APP_DIR=$(APP_DIR) has no app.mk -- an application directory sets APP_NAME and APP_SRCS there)
endif
APP_NAME   :=
APP_SRCS   :=
APP_CFLAGS :=
include $(APP_ABS)/app.mk

# The name becomes a directory and a binary name, so it is held to a shape no
# path or shell can misread.
ifneq ($(shell printf '%s' '$(APP_NAME)' | grep -Eqx '[a-z0-9][a-z0-9-]{0,14}' && echo ok),ok)
$(error $(APP_DIR)/app.mk: APP_NAME "$(APP_NAME)" must be [a-z0-9][a-z0-9-]*, at most 15 characters)
endif
ifeq ($(strip $(APP_SRCS)),)
$(error $(APP_DIR)/app.mk: APP_SRCS is empty -- name the application's .c files, relative to its directory)
endif
# Relative and inside the directory, or the object path would leave the build
# directory with it.
ifneq ($(filter /% ../% %/../%,$(APP_SRCS)),)
$(error $(APP_DIR)/app.mk: APP_SRCS must be relative paths inside the application directory: $(filter /% ../% %/../%,$(APP_SRCS)))
endif
ifneq ($(filter-out %.c,$(APP_SRCS)),)
$(error $(APP_DIR)/app.mk: APP_SRCS lists files that are not .c: $(filter-out %.c,$(APP_SRCS)))
endif
APP_MISSING := $(filter-out $(wildcard $(addprefix $(APP_ABS)/,$(APP_SRCS))),$(addprefix $(APP_ABS)/,$(APP_SRCS)))
ifneq ($(APP_MISSING),)
$(error $(APP_DIR)/app.mk: APP_SRCS names files that do not exist: $(APP_MISSING))
endif

APP_SUFFIX       := -app-$(APP_NAME)
APP_RUNNER_SRCS  := src/core/app_runner.c
# After the CURDIR map, so it is the one that applies to an application inside
# this checkout too: the image does not record where the application was built.
APP_PREFIX_MAP   := -ffile-prefix-map=$(APP_ABS)=app
else
APP_ABS          :=
APP_NAME         :=
APP_SRCS         :=
APP_CFLAGS       :=
APP_SUFFIX       :=
APP_RUNNER_SRCS  := src/core/app_runner_none.c
APP_PREFIX_MAP   :=
endif

# The project's warning set, applied to an application as to the firmware: it
# is linked into the same image and measured with it.
APP_WARN_CFLAGS := -Wall -Wextra -Werror -Wconversion -Wshadow \
                   -Wpointer-arith -Wcast-qual -Wstrict-prototypes

## app-sdk-check: build the example application from outside the tree and run it on tfwsim
# From a copy in a temporary directory, so the example can only be finding what
# any application repository finds: include/tfw and the build hook.
app-sdk-check:
	@t=$$(mktemp -d) && trap 'rm -rf "$$t"' EXIT && \
	cp -a test/app/example/. "$$t/" && \
	$(MAKE) --no-print-directory tfwsim APP_DIR="$$t" > "$$t.log" 2>&1 || \
	  { cat "$$t.log"; rm -f "$$t.log"; echo "app-sdk-check: the example did not build from $$t"; exit 1; }; \
	rm -f "$$t.log"; \
	name=$$(sed -n 's/^APP_NAME *:*= *//p' test/app/example/app.mk); \
	./$(HOST_DIR)/tfwsim-app-$$name < /dev/null > /dev/null || \
	  { echo "app-sdk-check: tfwsim-app-$$name did not exit cleanly"; exit 1; }; \
	echo "app-sdk-check: the example builds out of tree and runs"

## app-firmware-check: build the example application's firmware image for BOARD from outside the tree
app-firmware-check:
	@t=$$(mktemp -d) && trap 'rm -rf "$$t"' EXIT && \
	cp -a test/app/example/. "$$t/" && \
	$(MAKE) --no-print-directory firmware APP_DIR="$$t" BOARD=$(BOARD)

.PHONY: app-sdk-check app-firmware-check
