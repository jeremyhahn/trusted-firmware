/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The CDI handoff block's output section, genuinely NOLOAD -- fixed at the
 * source, in the one place both images' link steps share, rather than by
 * excluding ".handoff" at every point an image later gets derived from the
 * ELF (the *.hex rules, and mk/release.mk's provenance .bin, which is
 * SIGNED-EVIDENCE-ADJACENT: a verifier's image_len and recomputed FWHash
 * must cover application flash only).
 *
 * This is a linker script FRAGMENT, not a replacement for avr-gcc's default
 * one: `INSERT AFTER` splices it into the default script immediately after
 * `.noinit`, which is the standard-recognised uninitialised-but-not-zeroed
 * section this handoff block is conceptually the same kind of thing as. Every
 * *_LDFLAGS that places `.handoff` (mk/arch/avr.mk) passes this file
 * with `-T` alongside its own `--section-start=.handoff=<addr>` -- NOLOAD and
 * the address are orthogonal linker concerns, and the command-line flag still
 * sets where this section lands.
 */
SECTIONS
{
  .handoff (NOLOAD) : { *(.handoff) }
}
INSERT AFTER .noinit;
