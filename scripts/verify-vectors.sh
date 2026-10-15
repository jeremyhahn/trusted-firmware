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
c_vals=$(sed -n 's/^#define TFWV_CRC_\([A-Z_]*\)_RESULT *0x\([0-9A-Fa-f]*\)u$/\1 \2/p' "$C" \
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

# Wire frames: encoded byte strings rather than hex results, compared the same
# way -- by VALUE, not by name, since the two languages spell a byte string
# differently. A frame one side pins and the other does not is the failure.
c_frames=$(sed -n 's/^#define TFWV_FRAME_[A-Z0-9_]* "\(.*\)"$/\1/p' "$C" | sort)
g_frames=$(sed -n 's/^\tFrame[A-Za-z0-9]* *= *"\(.*\)"/\1/p' "$G" | sort)

cf_only=$(comm -23 <(echo "$c_frames") <(echo "$g_frames"))
gf_only=$(comm -13 <(echo "$c_frames") <(echo "$g_frames"))
if [ -n "$cf_only" ]; then
  echo "wire frame(s) in $C with no counterpart in $G:"; echo "$cf_only"; fail=1
fi
if [ -n "$gf_only" ]; then
  echo "wire frame(s) in $G with no counterpart in $C:"; echo "$gf_only"; fail=1
fi

if [ "$fail" = 0 ]; then
  echo "wire vectors agree: $(echo "$c_vals" | wc -l) CRC values and" \
       "$(echo "$c_frames" | wc -l) wire frames, both languages"
fi

# String-literal vectors -- SYS_INFO's reply payloads, #5. These are not hex
# numbers, so they get their own block, but \x escapes are spelled identically
# in a C string literal and a Go one, which means the quoted text can be
# compared directly rather than decoded.
c_str=$(sed -n 's/^#define TFWV_SYSINFO_\([A-Z_]*\)_REPLY *"\(.*\)"$/\1 \2/p' "$C" | sort)
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

# CAPS reply vectors -- the protocol version bytes and the six-byte pin page,
# #12 -- string literals compared the same way, by NAME and VALUE together: the
# version pair and a pin page must not be able to swap places and still agree.
c_caps=$(sed -n 's/^#define TFWV_CAPS_\([A-Z0-9_]*\)_REPLY *"\(.*\)"$/\1 \2/p' "$C" | tr 'A-Z' 'a-z' | sort)
g_caps=$(sed -n 's/^\tCaps\([A-Za-z0-9]*\)Reply *= *"\(.*\)"$/\1 \2/p' "$G" | tr 'A-Z' 'a-z' | sort)

if [ -z "$c_caps" ]; then
  echo "no CAPS reply vectors found in $C"; fail=1
elif [ "$c_caps" != "$g_caps" ]; then
  echo "CAPS reply vectors differ:"; echo "  $C:"; echo "$c_caps" | sed 's/^/    /'
  echo "  $G:"; echo "$g_caps" | sed 's/^/    /'; fail=1
else
  echo "CAPS reply vectors agree: $(echo "$c_caps" | wc -l) values, both languages"
fi

# APP_INFO reply vectors, #51 -- the runner and the descriptor, compared by NAME
# and VALUE together as CAPS is: the example, the overrun and the refused
# descriptor must not be able to swap places and still agree.
c_app=$(sed -n 's/^#define TFWV_APPINFO_\([A-Z0-9_]*\)_REPLY *"\(.*\)"$/\1 \2/p' "$C" | tr 'A-Z' 'a-z' | sort)
g_app=$(sed -n 's/^\tAppInfo\([A-Za-z0-9]*\)Reply *= *"\(.*\)"$/\1 \2/p' "$G" | tr 'A-Z' 'a-z' | sort)

if [ -z "$c_app" ]; then
  echo "no APP_INFO reply vectors found in $C"; fail=1
elif [ "$c_app" != "$g_app" ]; then
  echo "APP_INFO reply vectors differ:"; echo "  $C:"; echo "$c_app" | sed 's/^/    /'
  echo "  $G:"; echo "$g_app" | sed 's/^/    /'; fail=1
else
  echo "APP_INFO reply vectors agree: $(echo "$c_app" | wc -l) values, both languages"
fi

# Image trailer v1 signed spans, #76 -- compared by NAME and VALUE together: a
# board's span and the mismatched or unbound one must not be able to swap
# places and still agree.
c_tr=$(sed -n 's/^#define TFWV_TRAILER_\([A-Z0-9_]*\)_SPAN *"\(.*\)"$/\1 \2/p' "$C" | tr 'A-Z' 'a-z' | sort)
g_tr=$(sed -n 's/^\tTrailer\([A-Za-z0-9]*\)Span *= *"\(.*\)"$/\1 \2/p' "$G" | tr 'A-Z' 'a-z' | sort)

if [ -z "$c_tr" ]; then
  echo "no image trailer vectors found in $C"; fail=1
elif [ "$c_tr" != "$g_tr" ]; then
  echo "image trailer vectors differ:"; echo "  $C:"; echo "$c_tr" | sed 's/^/    /'
  echo "  $G:"; echo "$g_tr" | sed 's/^/    /'; fail=1
else
  echo "image trailer vectors agree: $(echo "$c_tr" | wc -l) values, both languages"
fi

exit $fail
