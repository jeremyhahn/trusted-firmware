# lwIP in trusted-firmware

This file is ours, not upstream's. Nothing else in this directory has been
edited: every other file is upstream's bytes, as `VERSION` and `SHA256SUMS`
record. `make lwip-check` and `make lwip-upstream-check` verify those bytes.

## Pin

- Release: lwIP 2.2.1, tag `STABLE-2_2_1_RELEASE`, commit
  `77dcd25a72509eb83f72b033d219b1d40cd8eb95` (`VERSION`).
- Checksums: `SHA256SUMS` lists every vendored file, summed from a fresh
  fetch of that commit. `scripts/vendor_sums.py` fails on a changed, missing
  or unlisted file. `VERSION`, `SHA256SUMS` and this file are exempt.
- Vendored subset: `src/core/*.c`, `src/core/ipv4/*.c`,
  `src/netif/ethernet.c` and their headers. Not vendored: `src/apps/` (httpd,
  SNMP, MQTT, makefsdata and the rest), `src/api/` (sockets, netconn),
  `src/core/ipv6/`, `src/netif/ppp/` sources (PPP headers only, with PPP
  off), and the other netifs (zepif, slipif, 6LoWPAN, bridgeif).
- Port, which code review covers: `src/hal/cortex-m/stm32h5/lwipopts.h`,
  `lwip-port/arch/cc.h`, `hal_eth_netif.c` (with the RFC 6528 TCP ISN hook,
  keyed from `hal_rng.c`), `hal_eth_wire_tcp.c`. Codex reviews our port and
  integration, not upstream's lines.

## Release archive

- Tag: `STABLE-2_2_1_RELEASE` (commit above).
- Archive: upstream's tag archive on its GitHub repository,
  https://github.com/lwip-tcpip/lwip/archive/refs/tags/STABLE-2_2_1_RELEASE.tar.gz
- SHA-256: `ce0b7461c0ad9602c376f0bf07c5eb7253b48c7bf66f011c6bf3e2a96731c539`
  (`LWIP_ARCHIVE_SHA256` in `mk/firmware.mk`).
- Why that archive: upstream's Savannah release zip,
  https://download.savannah.nongnu.org/releases/lwip/lwip-2.2.1.zip (SHA-256
  `7b622662dba2383d71f874f2e494b54ae948531559c17acbe75797966d646878`),
  stores CRLF line endings. The archive check against it on 2026-11-19
  found all 145 vendored files equal to it except for line endings, so it is
  not byte-identical. The tag archive is, and is the one checked.

## Local patches

None. `third_party/lwip/patches/` does not exist. A future patch goes there
as its own diff, listed here, and the upgrade rule below changes with it.

## Upstream check

`make lwip-upstream-check` (`scripts/lwip_upstream_check.py`) runs two modes:

- Archive mode downloads the release archive above, refuses it unless its
  SHA-256 matches, and compares every vendored file byte for byte with the
  same path in the archive.
- Git mode fetches commit `77dcd25a72509eb83f72b033d219b1d40cd8eb95` from
  the upstream named in `VERSION`, checks the fetched id and compares every
  vendored file byte for byte.

Each mode names each file that differs or is missing. With no network it
reports SKIP, never PASS.

- 2026-11-19: PASS. All 145 vendored files are byte-identical to upstream at
  that commit.
- 2026-11-19: PASS, archive mode. All 145 vendored files are byte-identical
  to the tag archive above, and its SHA-256 matched.

## CVE tracking

Sources to watch:

- The lwIP bug and patch trackers on Savannah:
  https://savannah.nongnu.org/bugs/?group=lwip and the lwip-devel list.
- NVD, CPE `cpe:2.3:a:lwip_project:lwip`:
  https://nvd.nist.gov/products/cpe/search (search the CPE above).
- Upstream's `CHANGELOG` for each release after 2.2.1.

Known CVEs against lwIP and their status for this subset at 2.2.1. This list
was written without an NVD query. The next check against the sources above
confirms it and adds any new entries.

| CVE | Where | Status here |
|-----|-------|-------------|
| CVE-2020-22283 | ICMPv6, `core/ipv6/icmp6.c` | Fixed upstream before 2.2.1 (2.2.0). Also not vendored, and IPv6 is off. |
| CVE-2020-22284 | ZEP netif, `netif/zepif.c` | Fixed upstream before 2.2.1 (2.2.0). Also not vendored. |
| CVE-2023-49287 | TinyDir, in `apps/http/makefsdata` | Not applicable: `apps/` is not vendored. |

The rows this image does build are IPv4, ARP, ICMP, UDP (DHCP) and TCP, as
set in `lwipopts.h`. A new CVE in one of those modules gets an issue the same
day. The fix is then an upgrade, under the rule below.

## Upgrade rule

An upgrade replaces the vendored subset whole, from a fresh fetch of the new
release tag: no cherry-picked files and no local patches. The same change
updates `VERSION`, regenerates `SHA256SUMS`, updates
`LWIP_UPSTREAM_COMMIT`, `LWIP_ARCHIVE_URL` and `LWIP_ARCHIVE_SHA256` in
`mk/firmware.mk`, and updates this file's archive section and table.
`make lwip-check` and `make lwip-upstream-check` must both pass on it.
