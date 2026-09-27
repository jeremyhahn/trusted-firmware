#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# The C and Go mirrors of the wire vectors must carry identical values.
#
# They are two files on purpose -- neither language can include the other's --
# and two files that must agree will eventually not. This is the check that
# makes the disagreement a build failure instead of a field incident.
set -euo pipefail

C=testdata/vectors.h
G=sdk/go/internal/vectors/vectors.go
fail=0

# name -> value, from each side, normalised to bare lowercase hex.
c_vals=$(sed -n 's/^#define AGV_CRC_\([A-Z_]*\)_RESULT *0x\([0-9A-Fa-f]*\)u$/\1 \2/p' "$C" \
         | tr 'A-F' 'a-f' | sort)
g_vals=$(sed -n 's/^\tCRC\([A-Za-z]*\) *= *0x\([0-9A-Fa-f]*\).*$/\1 \2/p' "$G" \
         | sed 's/^Check /CHECK /; s/^Empty /EMPTY /; s/^Zero /ZERO /; s/^ZeroZero /ZEROZERO /; s/^FF /FF /; s/^OrderA /ORDER_A /; s/^OrderB /ORDER_B /' \
         | tr 'a-z' 'A-Z' | sed 's/ \([0-9A-F]*\)$/ \L\1/' | sort)

# Compare the value sets. Names differ by language convention; the VALUES are
# the contract, and a value present on one side and absent on the other is the
# failure this exists to catch.
c_only=$(comm -23 <(echo "$c_vals" | awk '{print $2}' | sort -u) <(echo "$g_vals" | awk '{print $2}' | sort -u))
g_only=$(comm -13 <(echo "$c_vals" | awk '{print $2}' | sort -u) <(echo "$g_vals" | awk '{print $2}' | sort -u))

if [ -n "$c_only" ]; then
  echo "value(s) in $C with no counterpart in $G:"; echo "$c_only" | sed 's/^/  0x/'; fail=1
fi
if [ -n "$g_only" ]; then
  echo "value(s) in $G with no counterpart in $C:"; echo "$g_only" | sed 's/^/  0x/'; fail=1
fi
if [ "$fail" = 0 ]; then
  echo "wire vectors agree: $(echo "$c_vals" | wc -l) values, both languages"
fi

# String-literal vectors -- SYS_INFO's reply payloads, #5. These are not hex
# numbers, so they get their own block, but \x escapes are spelled identically
# in a C string literal and a Go one, which means the quoted text can be
# compared directly rather than decoded.
c_str=$(sed -n 's/^#define AGV_SYSINFO_\([A-Z_]*\)_REPLY *"\(.*\)"$/\1 \2/p' "$C" | sort)
g_str=$(sed -n 's/^\tSysInfo\([A-Za-z]*\) *= *"\(.*\)"$/\1 \2/p' "$G" | sort)

c_str_only=$(comm -23 <(echo "$c_str" | awk '{print $2}' | sort -u) <(echo "$g_str" | awk '{print $2}' | sort -u))
g_str_only=$(comm -13 <(echo "$c_str" | awk '{print $2}' | sort -u) <(echo "$g_str" | awk '{print $2}' | sort -u))

if [ -n "$c_str_only" ]; then
  echo "string vector(s) in $C with no counterpart in $G:"; echo "$c_str_only"; fail=1
fi
if [ -n "$g_str_only" ]; then
  echo "string vector(s) in $G with no counterpart in $C:"; echo "$g_str_only"; fail=1
fi
if [ -z "$c_str_only" ] && [ -z "$g_str_only" ]; then
  echo "SYS_INFO reply vectors agree: $(echo "$c_str" | wc -l) values, both languages"
fi

exit $fail
