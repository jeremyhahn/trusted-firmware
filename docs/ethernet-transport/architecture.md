# Ethernet transport — architecture

> **Status: DRAFT** · Scope: trusted-firmware · Last verified: 2026-10-20
> The decisions and their reasons are in
> [adr-wire-protocol-over-w5x00.md](adr-wire-protocol-over-w5x00.md). This page
> describes the shape they produce.

In this folder: [`api.md`](api.md), [`user.md`](user.md),
[`cli.md`](cli.md) (`tfwctl netcfg set|get` and the `tcp://` device address),
[`engineering.md`](engineering.md) and [`test-matrix.md`](test-matrix.md).

## Ownership boundary

This repo owns the device side: the W5100 and W5500 drivers, the Ethernet link
beside serial, the network block in EEPROM, and the `sdk/go` TCP listener a host
uses to accept a device's connection.

It does not own the device-facing endpoint, enrolment, the device registry or
fleet policy. Those are go-trusted-platform's. Evidence verification is go-xkms's.
The seam is the row in the ADR's "Seam to go-trusted-platform" section. Where
the Ethernet link sits in the whole onboarding sequence is
[`../device-onboarding/architecture.md`](../device-onboarding/architecture.md).

## Layers

```
 dispatcher (tfw_dispatch)          unchanged
        |
 transport (src/proto/transport.c)  one framer, one response buffer
        |
 link (include/tfw/link.h)          chosen at link time
   |                         |
 link_serial.c            link_eth.c
   UART only                UART + Ethernet, allow-list, reconnect
                             |
                     socket logic (src/net/w5x00.c)
                             |
                 w5100.c  or  w5500.c   (one linked, by ETH_CHIP)
                             |
                     HAL SPI (tfw_hal_spi_*)
```

- The transport reads and writes bytes through the link layer, never the UART
  directly. A serial-only image links `link_serial.c`, whose functions are the
  UART calls the transport used to make, and admits every command.
- An Ethernet image links `link_eth.c` instead. It serves the UART and the
  controller's socket 0 to the same transport. It binds each frame to the link
  that delivered its first byte, refuses non-allow-listed commands arriving on
  Ethernet, and runs the reconnect schedule.
- `w5x00.c` is the socket state machine both chips share: open, connect, read
  the receive buffer, write the transmit buffer, send, close. It reaches the chip
  through three primitives that `w5100.c` and `w5500.c` each define:
  read a byte, write a byte, check identity.
- `netcfg.c` reads and checks the network block from EEPROM.
- The address comes from `tfw_dhcp_poll`, chosen at link time like the chip:
  `dhcp.c` (`DHCP=1`) leases one by RFC 2131 on socket 1 in UDP mode before the
  TCP connection opens, and renews it there beside the connection; `dhcp_none.c` returns the block's own and refuses a
  block whose flags ask for DHCP.

## Data flow

1. At each attempt the link reads the network block and, if valid, asks for the
   address: the block's own, or a DHCP lease where its flags ask for one. While
   the lease is being asked for, the link stays down and polls again. Then it
   asks the socket logic to start: reset and identify the controller, program
   MAC, IP, mask and gateway, open socket 0 as TCP and connect to the endpoint.
   While connected, the link asks `tfw_dhcp_due()` on each poll and, when it
   says so, `tfw_dhcp_renew()` renews the lease at T1 or rebinds it at T2 on
   socket 1. The connection stays up. A DHCPNAK, or a lease that runs out,
   drops the connection, and the next attempt asks again.
2. On each `tfw_transport_poll` the link reads the socket's state. Once it is
   established, received bytes are offered to the framer exactly as UART bytes
   are.
3. A complete frame is checked against the allow-list if it came from Ethernet,
   then dispatched. Its response is written back to the link that carried it.
4. A dropped connection, a missing chip or a missing block keeps the Ethernet link
   down and schedules the next attempt on the backoff. Serial is never affected.

## Build selection

- `make ETH=1` selects the Ethernet link. The board's `profile.mk` names its
  controller in `ETH_CHIP`; `ETH_CHIP=w5500` overrides it.
- Ethernet images build into their own directory, so they never overwrite the
  default image.
- `make ETH=1 DHCP=1` adds the DHCP client, into a `-dhcp` directory. It is off
  by default until it is measured on a board.
- No `#if` is added outside the whitelisted files. The one compile-time value is
  `TFW_FEATURE_ETHERNET` in `include/tfw/config.h`, used by `include/tfw/board.h`
  as an expression to flag the shield's pins in use.

## On the host

- `tfwsim` links `link_eth.c` with a host driver that implements the same socket
  API over a real TCP socket. Given a network block, it connects to the endpoint
  and carries frames over it, so the cross-implementation suite exercises the real
  link and allow-list code.
- `sdk/go` adds `tfw.Listen` and `tfw.Listener.Accept`, which return a `Device`
  on the accepted connection.
