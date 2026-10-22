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
#
# APP_DEFS turns on TFW_FEATURE_APP beside the real runner, and only beside it:
# the flag gives APP_INFO its commands.def entry, and the handler is defined in
# app_runner.c. An image with no application has neither, and answers APP_INFO
# TFW_ERR_UNKNOWN_CMD.

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
APP_DEFS         := -DTFW_FEATURE_APP=1
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
APP_DEFS         :=
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

## tfwsim-app-example: build the software- and element-tier simulators with the example application linked
# A make of its own because APP_DIR names one application per invocation; the
# cross-implementation tests need these images beside the ones without one. The
# element tier's is the only image whose alias certificate carries an
# application's evidence (#51).
tfwsim-app-example:
	@$(MAKE) --no-print-directory tfwsim APP_DIR=test/app/example
	@$(MAKE) --no-print-directory tfwsim-atecc608a APP_DIR=test/app/example

## app-firmware-check: build the example application's firmware image for BOARD from outside the tree
app-firmware-check:
	@t=$$(mktemp -d) && trap 'rm -rf "$$t"' EXIT && \
	cp -a test/app/example/. "$$t/" && \
	$(MAKE) --no-print-directory firmware APP_DIR="$$t" BOARD=$(BOARD)

## app-header-check: every public header compiles on its own, with an application's flags, against every board
# An application repository sees include/tfw/, its board's board_profile.h and
# the HAL's types, and nothing of src/. Each header is compiled as the whole of
# a translation unit, included twice so its guard is exercised too, against
# each board directory, with the warning set an application is held to: a
# header that leans on one included before it, or on src/, fails here rather
# than in an application's repository. The host's HAL types
# (test/mock/hal_types.h) stand in for the architecture's; an architecture's
# own are compiled when the example builds as firmware (app-firmware-check).
#
# Two plants first, so the gate is seen to fail: a header that names uint8_t
# without <stdint.h>, and a unit that includes src/core/app_runner.h, which an
# application must not. An empty header or board list is a failure, not a pass.
# Run by host-test.
APP_HEADER_ROOT   ?= .
APP_HEADER_CFLAGS  = -std=c11 $(APP_WARN_CFLAGS) -Iinclude -Itest/mock \
                     -DTFW_BOARD_HEADER='"board_profile.h"' -fsyntax-only
app-header-check:
	@cd $(APP_HEADER_ROOT) && t=$$(mktemp -d) && trap 'rm -rf "$$t"' EXIT && \
	hdrs=$$(ls include/tfw/*.h 2>/dev/null); boards=$$(ls -d boards/*/ 2>/dev/null); \
	[ -n "$$hdrs" ] && [ -n "$$boards" ] || \
	  { echo "app-header-check: no include/tfw/*.h or no boards/*/ under $(APP_HEADER_ROOT)"; exit 1; }; \
	b0=$$(printf '%s\n' $$boards | head -n 1); \
	mkdir -p "$$t/plant/tfw" && \
	printf '#ifndef TFW_PLANT_H\n#define TFW_PLANT_H\nuint8_t tfw_plant(void);\n#endif\n' > "$$t/plant/tfw/plant.h" && \
	printf '#include "tfw/plant.h"\n' > "$$t/plant.c" && \
	if $(CC) $(APP_HEADER_CFLAGS) -I"$$t/plant" -I"$$b0" "$$t/plant.c" > /dev/null 2>&1; then \
	  echo "app-header-check: a header that needs <stdint.h> included before it was accepted"; exit 1; fi; \
	for inc in core/app_runner.h src/core/app_runner.h; do \
	  printf '#include "%s"\n' "$$inc" > "$$t/internal.c"; \
	  if $(CC) $(APP_HEADER_CFLAGS) -I"$$b0" "$$t/internal.c" > /dev/null 2>&1; then \
	    echo "app-header-check: an application reached $$inc"; exit 1; fi; \
	done; \
	fail=0; n=0; \
	for b in $$boards; do for h in $$hdrs; do \
	  i=$${h#include/}; n=$$((n + 1)); \
	  printf '#include "%s"\n#include "%s"\n' "$$i" "$$i" > "$$t/h.c"; \
	  $(CC) $(APP_HEADER_CFLAGS) -I"$$b" "$$t/h.c" > "$$t/h.log" 2>&1 || \
	    { echo "app-header-check: $$i does not compile on its own against $$b"; head -n 5 "$$t/h.log"; fail=1; }; \
	done; done; \
	[ $$fail -eq 0 ] || exit 1; \
	echo "app-header-check: $$(printf '%s\n' $$hdrs | wc -l) headers compile on their own against $$(printf '%s\n' $$boards | wc -l) boards ($$n units)"

## app-header-check-selftest: the header gate rejects a header that is not self-contained, and names it
# A copy of include/ and one board with a planted header that needs <stddef.h>
# from whoever includes it: the gate must fail and print the header's name. The
# copy unplanted must pass. Run by the security stage, beside the other gates'
# selftests.
app-header-check-selftest:
	@t=$$(mktemp -d) && trap 'rm -rf "$$t"' EXIT && \
	mkdir -p "$$t/boards" "$$t/test" && cp -a include "$$t/" && cp -a test/mock "$$t/test/" && \
	cp -a boards/$(HOST_BOARD) "$$t/boards/" && \
	$(MAKE) --no-print-directory app-header-check APP_HEADER_ROOT="$$t" > /dev/null || \
	  { echo "selftest: the unplanted copy was rejected"; exit 1; }; \
	printf '#ifndef TFW_LEAN_H\n#define TFW_LEAN_H\nsize_t tfw_lean(void);\n#endif\n' > "$$t/include/tfw/lean.h" && \
	if out=$$($(MAKE) --no-print-directory app-header-check APP_HEADER_ROOT="$$t" 2>&1); then \
	  echo "selftest: the gate passed tfw/lean.h, which needs <stddef.h>"; exit 1; fi; \
	case "$$out" in *'tfw/lean.h does not compile on its own'*) ;; \
	  *) echo "selftest: the failure did not name tfw/lean.h"; printf '%s\n' "$$out"; exit 1 ;; esac; \
	echo "app-header-check rejects a header that is not self-contained and names it"

host-test: app-header-check
security: app-header-check-selftest

.PHONY: app-sdk-check app-firmware-check tfwsim-app-example app-header-check app-header-check-selftest
