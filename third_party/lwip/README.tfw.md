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
`LWIP_UPSTREAM_COMMIT` in `mk/firmware.mk`, and updates this file's table.
`make lwip-check` and `make lwip-upstream-check` must both pass on it.
