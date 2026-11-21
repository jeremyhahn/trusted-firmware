# lwIP import audit

The record of the check that `third_party/lwip` is upstream lwIP, unmodified,
at the release it claims (#584, Codex round 2). `third_party/lwip/README.tfw.md`
keeps the pin, the port files, the release archive, the upstream check, CVE
tracking and the upgrade rule; this page is the audit.

## What the import claims

- Release lwIP 2.2.1, tag `STABLE-2_2_1_RELEASE`, commit
  `77dcd25a72509eb83f72b033d219b1d40cd8eb95` (`third_party/lwip/VERSION`).
- Upstream `https://github.com/lwip-tcpip/lwip`, the GitHub mirror of
  `https://git.savannah.nongnu.org/git/lwip.git`.
- 145 vendored files, unmodified, each listed with its SHA-256 in
  `third_party/lwip/SHA256SUMS`.

## How it was checked

2026-11-19, in lane fix-584g, from a fresh download into an empty directory:

1. The tag's identity: `git ls-remote https://github.com/lwip-tcpip/lwip
   'refs/tags/STABLE-2_2_1_RELEASE*'` gives the annotated tag object
   `009c2256469004009488b3385ba269461e8eb616`, which peels
   (`STABLE-2_2_1_RELEASE^{}`) to commit
   `77dcd25a72509eb83f72b033d219b1d40cd8eb95`, the commit `VERSION` names.
2. The tag's source: `https://github.com/lwip-tcpip/lwip/archive/refs/tags/STABLE-2_2_1_RELEASE.tar.gz`
   (SHA-256 of the download
   `ce0b7461c0ad9602c376f0bf07c5eb7253b48c7bf66f011c6bf3e2a96731c539`),
   unpacked to `lwip-STABLE-2_2_1_RELEASE/`.
3. Every vendored file under `third_party/lwip` (the 145 that `SHA256SUMS`
   lists, not the three of ours named below) was compared with the file
   at the same path in that tree by SHA-256 (`sha256sum` of each side).
4. `SHA256SUMS` was checked against both trees: `sha256sum -c SHA256SUMS`
   in `third_party/lwip` and in the unpacked upstream tree.

## Result

PASS.

- 145 of 145 vendored files have the same SHA-256 as upstream's at that tag.
  None differs and none is missing upstream.
- `SHA256SUMS` holds for the vendored tree and for the upstream tree, so the
  sums are upstream's bytes, not only the vendored copy's.
- Three files in `third_party/lwip` are ours, not upstream's, and were left
  out of the comparison: `VERSION` (the pin), `SHA256SUMS` (the sums) and
  `README.tfw.md` (pin, CVE tracking, upgrade rule).
- No port file lives under `third_party/lwip`. The port is outside it and is
  reviewed as our code: `src/hal/cortex-m/stm32h5/lwipopts.h`,
  `src/hal/cortex-m/stm32h5/lwip-port/arch/cc.h`, `hal_eth_netif.c` and
  `hal_eth_wire_tcp.c`.

`make lwip-check` runs `make lwip-upstream-check` first and then re-checks
`SHA256SUMS` (`scripts/vendor_sums.py`), so it is not an offline check; no
build depends on it, it is run by hand. The offline re-check is
`make scripts-test` (`scripts/test_vendor_sums.py`).
`make lwip-upstream-check` (`scripts/lwip_upstream_check.py`) runs two modes:
archive mode downloads `LWIP_ARCHIVE_URL`, refuses it unless its SHA-256 is
`LWIP_ARCHIVE_SHA256`, and compares every vendored file; git mode fetches
`LWIP_UPSTREAM_COMMIT`, checks the fetched id and compares every vendored
file. With no network it prints SKIP and its reason, never PASS.
