#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# usage: ccdeps.sh CC ARGS... -o OUT ARGS...
#
# Runs the compile as given, then writes OUT.d: a make fragment naming every
# header the sources of that command include, as prerequisites of OUT.
#
# The host binaries are compiled and linked in ONE cc invocation over many
# sources. gcc's own -MMD cannot serve there: with several sources and one -o it
# writes a single .d file and each source overwrites the one before, so the
# header list of all but the last is silently lost -- the failure this file
# exists to prevent. -MM runs preprocess-only, concatenates every source's
# dependencies on stdout, and -MT pins the target to OUT for all of them.
# -MP adds an empty rule per header so a deleted header is not a build error.

set -e

out=
skip=0
n=$#
while [ "$n" -gt 0 ]; do
	a=$1
	shift
	n=$((n - 1))
	if [ "$skip" = 1 ]; then
		out=$a
		skip=0
	elif [ "$a" = -o ]; then
		skip=1
	elif [ "$a" = -include ] || [ "$a" = -imacros ]; then
		# A header named by an option is the option's argument, not a
		# prerequisite $^ carried in: kept, or the option takes the next
		# source as its file and that source is compiled twice.
		set -- "$@" "$a" "$1"
		shift
		n=$((n - 1))
	else
		# The .d files make headers prerequisites, so $^ carries them into
		# this command; they are dependencies, not inputs.
		case $a in
		*.h | *.def) continue ;;
		esac
		set -- "$@" "$a"
	fi
done

[ -n "$out" ] || { echo "ccdeps.sh: no -o in the command" >&2; exit 2; }

# The real compile first, with -o restored: a failing build leaves no .d behind.
"$@" -o "$out"

# Then the same command again, minus -o and minus the linker inputs (unity.o),
# which a preprocess-only run would only warn about.
n=$#
while [ "$n" -gt 0 ]; do
	a=$1
	shift
	n=$((n - 1))
	case $a in
	*.o) ;;
	*) set -- "$@" "$a" ;;
	esac
done
"$@" -MM -MP -MT "$out" > "$out.d.tmp"
mv "$out.d.tmp" "$out.d"
