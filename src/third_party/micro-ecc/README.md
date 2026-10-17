# micro-ecc, vendored

- Upstream: https://github.com/kmackay/micro-ecc
- Tag: `v1.2`
- Commit: `541b3a78026420a3e369c4c9281c396b5e531113` (2024-11-13)
- Licence: BSD-2-Clause, upstream's `LICENSE.txt` committed unchanged as
  `LICENSE.micro-ecc.txt` (Unity owns the name `LICENSE.txt` in
  `make verify-vendor`).
- Pinned by hash in `test/third_party/MANIFEST.md`. This one copy also serves the
  host ATECC608A model (`test/model/atecc608a.c`, `mk/host.mk`).

Files are copied from that commit, byte for byte except the local change below: `uECC.c`, `uECC.h`,
`uECC_vli.h`, `types.h`, `curve-specific.inc`, `platform-specific.inc`,
`asm_avr.inc`. The unrolled `asm_avr_mult_square.inc` (930 KB, only used at
`uECC_OPTIMIZATION_LEVEL` 3) is not vendored.

tfw-boot links it as `BOOT_SIG=p256-soft` (`boot/verify/verify_p256_soft.c`,
issue #130): secp256r1 only, verify only, no compressed points. The
configuration is set by `-D` flags in `mk/boot.mk`; no file here is edited for
it.

## Local changes

- `patches/0001-verify-shamir-doubling-and-infinity.patch`, in `uECC.c`
  `uECC_verify`. The joint u1·G + u2·Q loop adds with the co-Z formula
  `XYcZ_add`, which handles neither R == T nor R at infinity. Upstream
  rejects two valid Wycheproof P-256 / SHA-256 P1363 signatures (FIPS 186-5
  §6.4.2 requires a verifier to accept every valid one):
  - tcId 60 (EdgeCaseShamirMultiplication): R == -T at bit 8 makes R the
    point at infinity, and the additions at bits 1 and 0 are then made to it;
  - tcId 210 (extreme value for k and s^-1): R == T at bit 0.

  The patch takes the identity path (R + T = T when Z = 0) and the doubling
  path (R == T). Verify takes public inputs only, so the branches leak nothing.
  Upstream: open issues https://github.com/kmackay/micro-ecc/issues/141 and
  https://github.com/kmackay/micro-ecc/issues/196; no fix in its history up to
  541b3a7 (checked 2026-10-17).
