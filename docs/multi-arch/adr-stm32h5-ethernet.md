# ADR: Ethernet transport on the STM32H573I-DK

> **Status: PROPOSED** · Issue: [#540](../../../../issues/540)
> · SPEC rows: `stm32h5`, `ethernet-transport` · Related: the onboarding ADR
> ([`adr-stm32h573i-dk-onboarding.md`](adr-stm32h573i-dk-onboarding.md), #126),
> ADR #14 ([`../cortex-m/adr-reference-part-and-emulation.md`](../cortex-m/adr-reference-part-and-emulation.md)),
> the Ethernet transport ADR
> ([`../ethernet-transport/adr-wire-protocol-over-w5x00.md`](../ethernet-transport/adr-wire-protocol-over-w5x00.md)),
> board page [`../boards/stm32h573i-dk.md`](../boards/stm32h573i-dk.md),
> plan [`../plans/540.md`](../plans/540.md)
> · Scope: trusted-firmware, documentation only · Last verified: 2026-11-17
>
> Ruling 768's completion plan includes the STM32H573I-DK's Ethernet rows:
> link up, a DHCP lease, and tfw-wire over TCP from the bench host, verified
> on the board. Which IP stack runs on the part is an architecture decision,
> so this ADR proposes one and decides nothing: the choice is the owner's
> (open question 1). It changes no code, no `mk/` file, no board profile and
> no `docs/SPEC.md` row.

## Context

- **The `stm32h5` row** (`docs/SPEC.md`) names the STM32H573I-DK as the
  reference board of a Cortex-M33 target "enrolling over Ethernet". The port's
  first slice is the board as data on the generic Cortex-M port: clock,
  watchdog, GPIO and the ST-LINK console, built in CI and not yet run on the
  board ([`../boards/stm32h573i-dk.md`](../boards/stm32h573i-dk.md)).
- **The onboarding ADR defers the Ethernet port.** Its section 4 cables the
  Ethernet port "when a later slice needs it", and its board facts say only
  that the board has 10/100 Ethernet from the MCU's MAC. The board page marks
  the PHY part and the connector **not confirmed**. This is that later slice.
- **The `ethernet-transport` row** carries the existing frames unchanged over
  one TCP connection that the device opens to the control plane's
  device-facing endpoint; the Ethernet link admits only the attestation and
  enrolment commands (the allow-list, 0x68–0x6B); provisioning stays on
  serial. On AVR the TCP/IP stack is the WIZnet controller's, in hardware, and
  the row states "No IP stack, TLS or ACME client of ours runs on the device".
  The H573 has a MAC and no hardware stack, so on this part a software IP
  stack must run on the MCU, and the row's sentence has to be read as "of
  ours": a composed stack keeps it, a stack we write does not.
- **The seam the link already has.** `link_eth.c` reaches the network only
  through `include/tfw/eth.h`: `tfw_eth_start`, `tfw_eth_state`, `tfw_eth_rx`,
  `tfw_eth_tx`, `tfw_eth_send`, `tfw_eth_close` for the TCP connection, and
  `tfw_dhcp_poll`, `tfw_dhcp_due`, `tfw_dhcp_renew` for the address. On AVR
  `src/net/w5x00.c` implements them over the controller; `tfwsim` implements
  them over a host TCP socket
  ([`../ethernet-transport/architecture.md`](../ethernet-transport/architecture.md),
  "On the host"). An H573 implementation of the same calls leaves the link,
  the allow-list, the transport and the dispatcher untouched.
- **The precedent on the ESP32-P4.** `src/hal/esp32/hal_eth.c` composes
  ESP-IDF's Ethernet driver and its lwIP through `esp_netif`, with ESP-IDF's
  DHCP client; nothing of the stack is ours.
- **Compose first.** The SPEC's rows compose the vendor's or community's
  framework before writing one (the `ota` row: "the vendor's or community's
  framework is composed first"; the `optee-trustzone` row: "reimplements none
  of it"). The Cortex-M port links no vendor SDK or CMSIS today
  ([`../cortex-m/architecture.md`](../cortex-m/architecture.md), "Ownership
  boundary"), and its HAL is substituted at link time, with SoC peripherals
  under `src/hal/cortex-m/<SOC>/`.
- **The budget.** The board profile gives the application a 128 KiB slot at
  `0x08008000` and SRAM1, 256 KiB. The figures below for each stack are
  estimates from the projects' published figures, **not measured here**;
  slice 2 measures the chosen row.
- **No ST PDF is in the tree.** UM3143 (the board's user manual), RM0481 and
  DS14053 could not be retrieved (board page). Every board-level value below is
  marked **to confirm on the board**.

## The hardware, as far as it can be stated

| | Value | Status |
|---|---|---|
| MAC | the STM32H573's Ethernet MAC with its own DMA and descriptor rings, 10/100 | MAC present: UM3143 introduction (board page). Its MAC, MTL and DMA registers and descriptors as RM0481 gives them: **verified on the board 2026-11-17** (#581), frames sent and received |
| MII or RMII | RMII | **verified on the board 2026-11-17** (#579): the PHY answers over MDIO with SBS PMCR set to RMII; ST's pin data for the kit, `ETH.MediaInterface=HAL_ETH_RMII_MODE` |
| PHY part | a Microchip LAN8742A: PHYID1 `0x0007`, PHYID2 `0xC131` (model `0x13`, revision 1) | **verified on the board 2026-11-17** (#579), read over MDIO; board page "Ethernet PHY" |
| PHY address on MDIO | 0 | **verified on the board 2026-11-17** (#579): the scan of 0–31 found it at 0; the driver keeps scanning, so the value is never assumed |
| RMII pins | REF_CLK PA1, MDIO PA2, CRS_DV PA7, MDC PC1, RXD0 PC4, RXD1 PC5, TX_EN PG11, TXD1 PG12, TXD0 PG13, each on alternate function 11 | ST's pin data for the kit (`STM32_open_pin_data` at the commit the board page pins, the kit's board file); MDIO and MDC **verified on the board 2026-11-17** (#579); the data pins **verified on the board 2026-11-17** (#581), a DHCP exchange through them |
| Reference clock | 50 MHz to REF_CLK, from the PHY or a crystal on the board | **to confirm** against UM3143 |
| Connector | an RJ45 on the board | **verified on the board 2026-11-17** (#579): cabled, the PHY reports link up |
| IP stack | lwIP 2.2.1 (`STABLE-2_2_1_RELEASE`), `NO_SYS`, raw API, over our MAC and descriptor driver (option (a)); vendored unmodified under `third_party/lwip`, built with `ETH_MAC=1` | **verified on the board 2026-11-17** (#581): a DHCP lease, the console line `eth: dhcp bound`; the `ETH_MAC=1` application measured 35 816 bytes of flash and 45 004 bytes of RAM (lwIP's heap 8 KiB, 8 pool buffers) |

**Provenance of the vendored lwIP.** `third_party/lwip` is pinned to upstream
lwIP `STABLE-2_2_1_RELEASE`, commit `77dcd25a72509eb83f72b033d219b1d40cd8eb95`
(`third_party/lwip/VERSION`). `third_party/lwip/SHA256SUMS` lists every
vendored file with its SHA-256, summed from a fresh fetch of that commit;
`make lwip-check` (and `scripts-test`, through `scripts/test_vendor_sums.py`)
fails on a changed byte, a missing file or a file the list does not name. The
subset is therefore upstream's bytes and needs no line review here; the review
falls on our port files: `src/hal/cortex-m/stm32h5/lwipopts.h`,
`src/hal/cortex-m/stm32h5/lwip-port/arch/cc.h`, `hal_eth_netif.c` (the netif
glue) and `hal_eth_wire_tcp.c` (the raw-API outbound connection).

## Options

### (a) lwIP, `NO_SYS`, raw API, over our own MAC and descriptor driver

lwIP (BSD-3-Clause) is composed at a pinned release, with `NO_SYS=1` (no RTOS:
the firmware's main loop calls `sys_check_timeouts` and the receive poll), the
raw TCP API and lwIP's own DHCP client. Under it, one netif driver of ours in
`src/hal/cortex-m/stm32h5/`: the MAC and DMA set-up, a small descriptor ring
for each direction in SRAM1, an MDIO read and write, and the PHY's link and
auto-negotiation. Above it, `tfw_eth_*` and `tfw_dhcp_*` over `tcp_connect`,
`tcp_recv`, `tcp_write` and `dhcp_start`.

- Composes the community's stack; the code we write is the register driver
  only, the same kind of file the port already has for the UART and the
  flash, behind the same HAL boundary.
- The driver is a few hundred lines that host-test against a register model,
  as `test_boot_stm32h5_flash.c` tests the flash driver.
- lwIP is the stack ESP-IDF composes, so the ESP32-P4 and the H573 share one
  stack's behaviour on the wire.
- No ST source enters the tree, and the port keeps its rule that no vendor
  SDK is linked.
- Cost: our driver carries the register values, which are **to confirm**
  against RM0481 the way the flash driver's are, and must be read from ST's
  HAL source at a pinned commit until then.
- Estimated size: lwIP with TCP, IPv4, ARP and DHCP in roughly 35–45 KiB of
  flash and 15–25 KiB of RAM with small windows and pools, plus the driver.

### (b) lwIP over ST's HAL ETH driver from STM32CubeH5

The same lwIP, over `stm32h5xx_hal_eth.c` and ST's LAN8742 component driver,
both BSD-3-Clause, pinned from ST's GitHub repositories.

- Less code of ours, and ST's own sequence for the MAC and DMA.
- Brings the HAL's handle model, its `HAL_ETH_MspInit` callbacks, its tick
  (`HAL_GetTick`) and `stm32h5xx_hal_conf.h` into the build, so the port
  links a vendor SDK for the first time; the Cortex-M port's ownership
  boundary and `lint-arch-leak` would need an exception.
- Larger: the HAL ETH driver and its dependencies (RCC, GPIO, cortex HAL
  files) add flash beside lwIP's.
- The licence is compatible with AGPL-3.0-or-later; the field-of-use and
  notice terms of each file are **to confirm** when pinned.

### (c) NetX Duo, from X-CUBE-AZRTOS-H5 or the Eclipse ThreadX project

- A complete stack with a DHCP client, which ST ships for this part.
- It runs on ThreadX: the firmware would take an RTOS it does not have
  today, and the main-loop model of every other port would change on this one.
- Its licence (MIT since the Eclipse ThreadX move) is compatible; ST's
  packaging terms are **to confirm**.
- No other board in the tree uses it, so the H573 would be the only target on
  its own stack.

### (d) Our own minimal IPv4, ARP, DHCP and TCP

- The smallest image, and the only option with no third-party code.
- Writes an IP stack of ours, which the `ethernet-transport` row rules out and
  the compose-first rule argues against. A TCP of our own is the largest
  source of risk in the transport: retransmission, windows, the state machine
  and its fuzzing would all be ours to own.
- The `src/net/dhcp.c` client works over the WIZnet's UDP socket and would
  need a UDP and IP layer under it that we also write.

## Recommendation

**(a): lwIP, `NO_SYS`, raw API, with lwIP's DHCP client, over our own small
MAC, DMA-descriptor and PHY driver behind the Cortex-M HAL.** Reasons:

1. **Compose first.** The IP stack, the hard part, is the community's, pinned;
   the row's "no IP stack of ours" holds. Option (d) breaks it.
2. **The HAL boundary.** The code of ours is a peripheral driver in
   `src/hal/cortex-m/stm32h5/`, the shape the UART, the watchdog and tfw-boot's
   flash driver already have. Option (b) would link ST's HAL into the port,
   which the Cortex-M ownership boundary does not allow today.
3. **Licence.** lwIP is BSD-3-Clause, which combines with AGPL-3.0-or-later
   with its notice kept.
4. **Flash and RAM.** `NO_SYS` with the raw API is lwIP's smallest
   configuration and needs no RTOS, unlike (c). Whether it fits the 128 KiB
   slot beside the attestation code is measured in slice 2 (open question 3).
5. **One stack on two parts.** The ESP32-P4 already runs lwIP through ESP-IDF.

If the owner prefers ST's sequence to our register driver, (b) is the
fall-back, at the price of the ownership-boundary exception.

## How the frames ride TCP

Exactly as on the `ethernet-transport` row; nothing on the wire changes.

- The image is built with `ETH=1 DHCP=1` and links `link_eth.c`. The H573's
  `tfw_eth_*` and `tfw_dhcp_*` replace `src/net/w5x00.c` and `src/net/dhcp.c`
  by file selection, never by `#if` in logic.
- `tfw_dhcp_poll` starts lwIP's DHCP client and reports the lease once bound;
  `tfw_dhcp_due` and `tfw_dhcp_renew` defer to lwIP's own T1 and T2 timers.
  A block whose flags ask for a static address uses it, as `dhcp_none.c` does.
- The device opens the one TCP connection to the endpoint in its network
  block; it listens on nothing. The frames, command ids and wire vectors are
  unchanged.
- The `ETH_MAC=1` image built today (`hal_eth_wire.c` in place of
  `link_serial.c`) does the same since #584's fix-584k: `hal_eth_wire_tcp.c`
  dials the block's peer and port once the netif has an address, redials a
  refused or dropped connection after 1 s (doubling per refusal up to 16 s,
  the block read again each time), and binds no port; the inbound listener on
  21574 is gone. `hal_eth_netif.c` takes the address, mask, gateway and MAC
  from the block (`src/net/netcfg.c`), starts lwIP's DHCP client only when the
  block's DHCP flag is set, and with no valid block keeps the device off the
  network while the UART is served, reading the block again later. Before
  each dial the netif is reconciled with the block just read
  (`tfw_h5_eth_reconcile`): a changed static address is set, a switch to or
  from DHCP starts or stops the client, and a changed MAC is reprogrammed with
  the link brought up again; no dial runs until the netif is up on that block.
  The sign yield runs lwIP's timers and the lease-expiry check while ATTEST
  computes a signature (`tfw_h5_eth_service_deadlines`, #584).
- The Ethernet link admits the attestation and enrolment commands only; serial
  (the ST-LINK Virtual COM port) keeps provisioning and every other command.
- The MAC address follows the row: the network block's, or a locally
  administered address derived from the DeviceID until a value is supplied.
  No address, host name or MAC is written in `src/` or in this repository.
- On the bench the host end is `sdk/go`'s `tfw.Listen`, which accepts the
  device's connection; in the field it is go-trusted-platform's device-facing
  endpoint. The hw-test rows take the same direction: `make hw-test
  BOARD=stm32h573i-dk PORT=tcp-listen:<host-port>` listens on the host
  (`sdk/go/internal/peer`, `TCPListenPrefix`) and accepts the board's dial,
  one listener per row, opened before any `HW_RESET` so the rebooted board's
  first dial is accepted. `PORT=tcp:<board>:<port>` still dials a board that
  listens (the P4, the earlier DK image).

## Slices

1. **This ADR** (docs only).
2. **Driver and DHCP, built in CI**, after the owner's ruling: the stack
   pinned, the driver, the `tfw_eth_*` and `tfw_dhcp_*` calls over it, the
   board's `ETH=1 DHCP=1` row built in CI with its flash and RAM figures, and
   host unit tests of the descriptor ring and the PHY sequence against a
   register model. Not run on the board.
3. **The hardware run, after #535**: the board record first (onboarding ADR
   section 4), then link up, a DHCP lease on the dev network, and the
   cross-implementation attestation rows over TCP from the bench host. The
   evidence goes on the board page, with no address, host name or device path.

## Open questions for the owner

1. **The stack.** (a), (b), (c) or (d)? This ADR recommends (a).
2. **The pin.** If (a) or (b): which lwIP release is pinned, and is it vendored
   into the tree or fetched at build time by a pinned commit, as the ST sources
   the board page cites are read?
3. **The slot.** If the `ETH=1 DHCP=1` row does not fit the 128 KiB
   application slot, may slice 2 grow the slot in `boards/stm32h573i-dk/`
   (bank 1 has room), or must the row shed something?
4. **The row's wording.** Should the `ethernet-transport` row's "No IP stack
   ... of ours runs on the device" be reworded to name the composed stack on
   parts with a bare MAC, in the change that lands slice 2?
5. **The board facts.** May slice 2 take the PHY part, its address and the
   RMII pins from ST's board-support or example source at a pinned commit, as
   the board page does for the console, until UM3143 can be read?
6. **The secure world.** Slice 2 runs Non-secure with TrustZone off, as the
   port does today. Whether the MAC belongs to the secure or the non-secure
   world under the onboarding ADR's open question 1 is left to that question.
