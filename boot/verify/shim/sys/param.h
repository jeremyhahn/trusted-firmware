/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * avr-libc has no <sys/param.h>, and ed25519-donna's portability detection
 * includes it unconditionally on anything that is not Windows -- to reach BSD's
 * endian macros, which this target does not have and does not need.
 *
 * Supplying an empty one is how the vendored tree stays byte-identical to the
 * pinned commit. The alternative is editing ed25519-donna-portable-identify.h,
 * which would mean the hash recorded for it pins our edit rather than
 * upstream's file.
 *
 * On the AVR bootloader build only: mk/boot.mk adds this directory to the
 * include path for the vendored translation unit and for nothing else. The host
 * test build uses glibc's real header, so this cannot shadow it.
 */
