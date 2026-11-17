#!/bin/sh
# boot_link_report.sh LINK_ERR BOARD
#
# Reports a failed tfw-boot link (mk/boot.mk, the $(BOOT_TARGET).elf recipe)
# by the cause the linker actually gave (#530).
#
#   exit 0  the image does not fit: link.err has a region overflow or a
#           "will not fit" line. The caller runs the size recovery
#           (boot-core-size and the budget text).
#   exit 1  any other failure (an -Werror at the LTO link such as
#           frame-larger-than, vla or alloca, an undefined reference, ...):
#           its error lines are printed and sizing is not the answer.
#   exit 2  usage.
#
# Plain POSIX sh with no grep or sed, so it runs in any build image.

[ $# -eq 2 ] || { echo "usage: $0 LINK_ERR BOARD" >&2; exit 2; }
err=$1
board=$2

size=0
if [ -r "$err" ]; then
	while IFS= read -r line || [ -n "$line" ]; do
		case $line in
		*overflowed* | *"will not fit"*) size=1 ;;
		esac
	done <"$err"
fi

if [ $size -eq 1 ]; then
	echo "tfw-boot does not link for $board: it does not fit in the boot section."
	echo
	while IFS= read -r line || [ -n "$line" ]; do
		case $line in
		*overflow* | *"will not fit"* | *overlaps* | *"undefined reference"*)
			echo "  ld: $line" ;;
		esac
	done <"$err"
	exit 0
fi

echo "tfw-boot does not link for $board:"
echo
shown=0
if [ -r "$err" ]; then
	while IFS= read -r line || [ -n "$line" ]; do
		case $line in
		*error:* | *frame-larger-than* | *vla* | *alloca* | *"undefined reference"*)
			echo "  ld: $line"
			shown=1 ;;
		esac
	done <"$err"
	# Nothing matched: the whole log is the only account of the failure.
	if [ $shown -eq 0 ]; then
		while IFS= read -r line || [ -n "$line" ]; do
			echo "  ld: $line"
		done <"$err"
	fi
fi
exit 1
