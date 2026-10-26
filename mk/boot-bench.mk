# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# The soft-rooted bench Mega's signing key (#7; CA rulings 188 and 189).
#
# DEVELOPMENT ONLY. A soft-tier tfw-boot boots only an application signed with
# the P-256 key whose public half is in its tfw_boot_p256_key slot
# (boot/verify/p256_key_slot.c), and the built image holds the erased
# placeholder there. On a bench board that key is a bench key: a P-256 key the
# developer makes once, kept outside the repository, never in a log, a record
# or a commit. A device that leaves the bench is signed by go-xkms's key
# (docs/ota/architecture.md), never this one.
#
#   make bench-key                   creates BENCH_KEY, mode 600, if it is not
#                                    there. Nothing else ever creates it.
#   make boot-bench BOARD=atmega2560 $(BOOT_TARGET)-bench.hex: tfw-boot with
#                                    the key's public half in its key slot,
#                                    and nothing else changed
#   make bench-sign BOARD=... CRYPTO=... IMAGE_VERSION=<n>
#                                    signs image-tbs's .tbs with the key and
#                                    writes the 64-byte r||s image-seal takes
#                                    as IMAGE_SIG
#   make boot-flash-uds BOARD=atmega2560 BOOT_TIER=soft ...
#                                    the bench image over ISP, through
#                                    boot-flash, with a fresh UDS that is
#                                    never kept
#
# BENCH_KEY names the private key. Its default is under $(HOME), outside the
# checkout, and a path inside the repository tree is refused before anything
# runs, whichever goal reads it.
BENCH_KEY ?= $(HOME)/.config/trusted-firmware/bench-p256.pem

BENCH_KEY_GOALS := $(filter bench-key boot-bench bench-sign boot-flash-uds,$(MAKECMDGOALS))
ifneq ($(BENCH_KEY_GOALS),)
ifneq ($(words $(BENCH_KEY)),1)
$(error $(firstword $(BENCH_KEY_GOALS)): REFUSED. BENCH_KEY="$(BENCH_KEY)" is not one path without spaces)
endif
BENCH_KEY_REAL := $(shell realpath -m -- '$(BENCH_KEY)')
BENCH_TREE     := $(shell realpath -- '$(CURDIR)')
ifneq ($(filter $(BENCH_TREE) $(BENCH_TREE)/%,$(BENCH_KEY_REAL)),)
$(error $(firstword $(BENCH_KEY_GOALS)): REFUSED. BENCH_KEY=$(BENCH_KEY) is inside the repository tree ($(BENCH_TREE)); the bench key is kept outside it, under $$HOME by default)
endif
endif

# The goals that read the key need it made already: make bench-key, never a
# side effect of a build.
$(BENCH_KEY):
	@echo "$(firstword $(BENCH_KEY_GOALS) $@): there is no bench key at $@." >&2
	@echo "  make bench-key creates one (development only); nothing else does." >&2
	@exit 1

## bench-key: create the development-only bench P-256 signing key at BENCH_KEY, outside the tree
# umask 077 from the first byte, so the key is never readable by anyone else,
# not even between its creation and the chmod. Only its public half's SHA-256 is
# printed. A key already there is kept, not replaced, unless its mode lets
# anyone but its owner read it.
bench-key:
	@if [ -e '$(BENCH_KEY)' ]; then \
	   mode=$$(stat -c %a '$(BENCH_KEY)'); \
	   if [ "$$mode" != 600 ] && [ "$$mode" != 400 ]; then \
	     echo "bench-key: REFUSED. $(BENCH_KEY) is mode $$mode; a private key is mode 600" >&2; exit 1; \
	   fi; \
	   echo "bench-key: $(BENCH_KEY) is there already, mode $$mode; kept"; exit 0; \
	 fi; \
	 umask 077; \
	 mkdir -p "$$(dirname '$(BENCH_KEY)')" && \
	 openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out '$(BENCH_KEY).new' 2>/dev/null && \
	 chmod 600 '$(BENCH_KEY).new' && mv '$(BENCH_KEY).new' '$(BENCH_KEY)' || \
	   { rm -f '$(BENCH_KEY).new'; echo "bench-key: FAILED creating $(BENCH_KEY)" >&2; exit 1; }; \
	 pub=$$(openssl pkey -in '$(BENCH_KEY)' -pubout -outform DER | sha256sum | cut -d' ' -f1); \
	 echo "bench-key: wrote $(BENCH_KEY), mode 600, development only"; \
	 echo "  public key (DER) SHA-256 $$pub"

# A P-256 SubjectPublicKeyInfo in DER is these 27 bytes, the last the
# uncompressed point's 0x04, then X || Y: the 64 bytes tfw_boot_p256_key holds.
# Any other key is refused rather than cut to size.
BENCH_SPKI_P256 := 3059301306072a8648ce3d020106082a8648ce3d03010703420004

# $(call BENCH_KEY_PUB,<out>): the bench key's public half, X || Y, to <out>.
BENCH_KEY_PUB = \
  der=$$(openssl pkey -in '$(BENCH_KEY)' -pubout -outform DER | od -An -v -tx1 | tr -d ' \n'); \
  case "$$der" in $(BENCH_SPKI_P256)*) ;; \
    *) echo "$@: $(BENCH_KEY) is not a P-256 key" >&2; exit 1;; esac; \
  if [ $${\#der} -ne 182 ]; then echo "$@: $(BENCH_KEY)'s public key is not 91 bytes of DER" >&2; exit 1; fi; \
  printf '%s' "$${der\#$(BENCH_SPKI_P256)}" | tr a-f A-F | basenc --base16 -d > $(1)

# $(call BOOT_SLOT_PATCH,<elf>,<symbol>,<bytes>,<out elf>): <elf> with the
# bytes of the file <bytes> in place of the flash slot <symbol>, written to
# <out elf>; <elf> is not touched. The objcopy path of mk/boot.mk's
# cdi_soft_uds.o, on the linked image: the section that holds the slot comes
# out with objcopy, the bytes go in at the slot's offset, and --update-section
# puts it back. The symbol's address and size are the ELF's own (nm -S), the
# section is the one whose span holds it (size -A). Refused, and nothing
# written, unless the file is the slot's size and the slot is the erased
# placeholder, all 0xFF: a slot already patched is never patched over.
BOOT_SLOT_PATCH = \
  set -- $$($(ARCH_NM) -S $(1) | awk '$$4 == "$(2)" { print $$1, $$2 }'); \
  if [ $$\# -ne 2 ]; then echo "$@: $(1) has no $(2) slot" >&2; exit 1; fi; \
  at=$$(( 0x$$1 )); len=$$(( 0x$$2 )); \
  if [ $$len -ne $$(stat -c %s $(3)) ]; then echo "$@: $(2) is $$len bytes and $(3) is not" >&2; exit 1; fi; \
  set -- $$($(ARCH_SIZE) -A -d $(1) | awk -v at=$$at '$$3 ~ /^[0-9]+$$/ && $$3 <= at && at < $$3 + $$2 { print $$1, $$3 }'); \
  if [ $$\# -ne 2 ]; then echo "$@: no section of $(1) holds $(2)" >&2; exit 1; fi; \
  sec=$$1; off=$$(( at - $$2 )); \
  $(ARCH_OBJCOPY) -O binary --only-section=$$sec $(1) $(4).sec || { rm -f $(4).sec; exit 1; }; \
  if [ $$(( off + len )) -gt $$(stat -c %s $(4).sec) ] || \
     [ "$$(dd if=$(4).sec bs=1 skip=$$off count=$$len 2>/dev/null | tr -d '\377' | wc -c)" != 0 ]; then \
    rm -f $(4).sec; echo "$@: $(2) in $(1) is not the erased placeholder; nothing patched" >&2; exit 1; \
  fi; \
  dd if=$(3) of=$(4).sec bs=1 seek=$$off conv=notrunc 2>/dev/null && \
    $(ARCH_OBJCOPY) --update-section $$sec=$(4).sec $(1) $(4); \
  rc=$$?; rm -f $(4).sec; if [ $$rc -ne 0 ]; then rm -f $(4); exit 1; fi

# The bench image is the soft tier's, P-256 in software: the only row with a
# key slot in flash to put a public half in.
BOOT_BENCH := $(BOOT_TARGET)-bench
BOOT_BENCH_GOAL := $(firstword $(filter boot-bench boot-flash-uds,$(MAKECMDGOALS)))
ifneq ($(BOOT_BENCH_GOAL),)
ifneq ($(BOOT_TIER):$(BOOT_SIG),soft:p256-soft)
$(error $(BOOT_BENCH_GOAL): BOOT_TIER=$(BOOT_TIER) BOOT_SIG=$(BOOT_SIG) has no key slot in flash; the bench key goes in the soft tier's, BOOT_TIER=soft BOOT_SIG=p256-soft)
endif
endif

$(BOOT_BENCH).elf: $(BOOT_TARGET).elf $(BENCH_KEY)
	@set -o pipefail; trap 'rm -f $@.pub' EXIT; \
	 $(call BENCH_KEY_PUB,$@.pub); \
	 $(call BOOT_SLOT_PATCH,$<,tfw_boot_p256_key,$@.pub,$@)

# The recipe of mk/boot.mk's $(BOOT_TARGET).hex.
$(BOOT_BENCH).hex: $(BOOT_BENCH).elf
	@$(ARCH_OBJCOPY) -O ihex -R .eeprom $< $@

## boot-bench: tfw-boot with the bench key's public half in its key slot (soft tier, development only)
boot-bench: $(BOOT_BENCH).hex
	@echo "built $<: the bench key's public half in tfw_boot_p256_key, development only"

## bench-sign: sign image-tbs's .tbs with the bench key, writing the r||s image-seal takes (development only)
# ES256 as go-xkms signs it: ECDSA P-256 over the SHA-256 of the 48-byte span.
# openssl writes the signature in DER; image-seal takes r || s, each 32 bytes
# big-endian, so each INTEGER loses DER's sign byte and is padded to 32.
bench-sign: $(BENCH_KEY)
	@if [ ! -f $(SUIT_OUT).tbs ]; then \
	   echo "bench-sign: there is no $(SUIT_OUT).tbs to sign: make image-tbs first" >&2; exit 1; \
	 fi
	@set -o pipefail; \
	 openssl dgst -sha256 -sign '$(BENCH_KEY)' -out $(SUIT_OUT).sig.der $(SUIT_OUT).tbs || exit 1; \
	 set -- $$(openssl asn1parse -inform DER -in $(SUIT_OUT).sig.der | awk -F: '/ INTEGER / { print $$NF }'); \
	 rm -f $(SUIT_OUT).sig.der; \
	 if [ $$# -ne 2 ]; then echo "bench-sign: openssl's signature is not two INTEGERs" >&2; exit 1; fi; \
	 rs=""; \
	 for v in "$$1" "$$2"; do \
	   v=$${v#"$${v%%[!0]*}"}; \
	   if [ $${#v} -gt 64 ]; then echo "bench-sign: an INTEGER of the signature is over 32 bytes" >&2; exit 1; fi; \
	   rs="$$rs$$(printf '%64s' "$$v" | tr ' ' 0)"; \
	 done; \
	 printf '%s' "$$rs" | basenc --base16 -d > $(SUIT_OUT).sig
	@echo "  $(SUIT_OUT).sig"
	@echo "  then: make image-seal BOARD=$(BOARD) CRYPTO=$(CRYPTO) IMAGE_SIG=$(SUIT_OUT).sig"

# The soft tier's UDS (boot/tier/cdi_soft.c's uds_slot): one per image, placed
# at provisioning. On the bench, at flash time: 32 bytes from the host's CSPRNG
# go into a temporary copy of the bench image, the objcopy path above, and
# boot-flash writes that copy over ISP with every check it makes. The copy and
# its UDS are in a directory of their own under $TMPDIR, mode 700, and the EXIT
# trap removes it whether the flash passes, fails or is interrupted. Nothing
# prints the UDS or keeps it: only the flashed image's SHA-256, printed and
# added to BOOT_UDS_RECORD once the flash has passed. The bench image and
# tfw-boot's own are left as they were. Run it again and the part gets a new
# UDS, so a new identity.
#
# boot-flash runs through BOOT_SUBMAKE rather than $(MAKE) itself: make runs a
# recipe that names $(MAKE) even under -n, and this one must do nothing then.
BOOT_UDS_RECORD := $(BOOT_DIR_OUT)/uds-flashed.sha256
BOOT_SUBMAKE     = $(MAKE)

## boot-flash-uds: flash the bench tfw-boot over ISP with a fresh UDS that is never kept (soft tier, development only)
boot-flash-uds: $(BOOT_BENCH).elf
	@set -o pipefail; \
	 tmp=$$(mktemp -d "$${TMPDIR:-/tmp}/tfw-uds.XXXXXX") || exit 1; \
	 trap 'rm -rf "$$tmp"' EXIT; trap 'exit 1' INT TERM HUP; \
	 (umask 077; head -c 32 /dev/urandom > "$$tmp/uds") && [ "$$(stat -c %s "$$tmp/uds")" = 32 ] || \
	   { echo "$@: could not read 32 bytes from /dev/urandom; nothing was flashed" >&2; exit 1; }; \
	 $(call BOOT_SLOT_PATCH,$<,uds_slot,$$tmp/uds,$$tmp/tfw-boot.elf); \
	 rm -f "$$tmp/uds"; \
	 $(ARCH_OBJCOPY) -O ihex -R .eeprom "$$tmp/tfw-boot.elf" "$$tmp/tfw-boot.hex" || exit 1; \
	 sum=$$(sha256sum "$$tmp/tfw-boot.hex" | cut -d' ' -f1); \
	 echo "$@: the bench tfw-boot with a fresh UDS, image SHA-256 $$sum; the UDS is not kept"; \
	 if ! $(BOOT_SUBMAKE) --no-print-directory boot-flash BOOT_FLASH_HEX="$$tmp/tfw-boot.hex"; then \
	   echo "$@: FAILED. The image and its UDS are deleted, nothing kept; what boot-flash" >&2; \
	   echo "  says to run again, run as make $@ with the same variables" >&2; \
	   exit 1; \
	 fi; \
	 echo "$$(date -u +%Y-%m-%dT%H:%M:%SZ) $$sum $(BOARD) tfw-boot-bench, UDS not kept" >> $(BOOT_UDS_RECORD); \
	 echo "$@: flashed; image SHA-256 $$sum, added to $(BOOT_UDS_RECORD)"

.PHONY: bench-key boot-bench bench-sign boot-flash-uds
