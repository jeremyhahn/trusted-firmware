# Ethernet transport — engineering

> **Status: DRAFT** · Scope: trusted-firmware · Last verified: 2026-10-20

## Files

| File | Role |
|---|---|
| `include/tfw/link.h` | The transport's byte link |
| `src/proto/link_serial.c` | Link for serial-only images: the UART, admits every command |
| `src/proto/link_eth.c` | Link for Ethernet images: UART and socket 0, binding, allow-list, reconnect |
| `include/tfw/eth.h` | Socket API a controller provides |
| `src/net/w5x00.c` | Socket logic shared by both chips |
| `src/net/w5100.c`, `src/net/w5500.c` | One per chip: SPI framing and identity check |
| `src/net/netcfg.c` | Reads and checks the network block |
| `src/net/dhcp.c` | DHCP client (RFC 2131) on socket 1 in UDP mode, beside the connection; linked with `DHCP=1` |
| `src/net/dhcp_none.c` | The block's own addresses; refuses a block that asks for DHCP |
| `tools/tfwsim/eth_socket.c` | Host implementation of `eth.h` over a TCP socket; on with `tfwsim --eth` |
| `sdk/go/tfwsim/ethernet.go` | `StartElementEthernet`: the element simulator dialling a loopback `tfw.Listener` |
| `test/model/w5x00_model.c` | Register model of both chips behind the mock SPI |
| `sdk/go/tcp.go`, `sdk/go/netcfg.go` | Listener and network block |

## Invariants

- One transport, one framer, one response buffer. A frame's reply goes back on
  the link that delivered its first byte.
- The allow-list is checked before dispatch and only for Ethernet frames. Its
  table lives in `link_eth.c`; a test pins all 128 ids.
- The driver sets SPI mode 0 before every access. A host's `SPI_CONFIG` never
  reaches the controller.
- Every wait is bounded and nothing calls a delay. Command completion on the
  chip is polled a bounded number of times.
- A failure in the Ethernet path keeps that link down; it never blocks serial.

## Tests

- `test_w5100` and `test_w5500`: the socket logic and each chip's driver against
  the register model, including the wrong-chip refusal.
- `test_link_eth`: the allow-list over every id, frame binding, events to serial
  only, a dropped connection, the reconnect backoff, and a block asking for
  DHCP refused by an image without the client.
- `test_dhcp_w5100` and `test_dhcp_w5500`: the DHCP client against the register
  model with the test as the server: Table 5's fields, the first wait and the
  retransmission schedule, each reply it must discard, DHCPNAK, the lease
  counted from the DHCPREQUEST, the ARP probe and DHCPDECLINE, and renewal at
  T1 and rebinding at T2 on socket 1 while socket 0 stays established.
- `test_link_dhcp`: `link_eth.c` over `dhcp.c`, both sockets faked at `eth.h`,
  on the mock clock (RFC 2131 §4.4.5): nothing sent before T1, a unicast
  renewal at T1 that keeps the address and the connection, a broadcast
  rebinding at T2 that accepts any server, a DHCPNAK or the lease's end
  dropping the connection and asking again from no address, and a connection
  lost while renewing redialled on the lease still running.
- `sdk/go/cmd/tfwctl` and `sdk/go/cmd/tfwprov`: `--dhcp` sets flags bit 0 and
  is refused beside `--ip`, `--mask` or `--gateway`; the CLI E2E round-trips a
  DHCP block through `tfwsim`'s EEPROM.
- `sdk/go/tfwsim`: the cross-implementation test drives `tfwsim-atecc608a` over a
  real TCP socket through `CAPS`, `ATTEST`, `ATTEST_CERT` and `ATTEST_CSR`, and
  checks a refused command.

## Flash and RAM

Measured with avr-gcc 14.2.0 by `make firmware`'s size check, against each
board's `TFW_FLASH_BUDGET` and its 1 024-byte RAM budget. "Serial only" is the
default image, after the link refactor in this work.

| Image | Flash / budget | RAM | Result |
|---|---|---|---|
| ATmega328P, `CRYPTO=atecc608a` | 16 064 / 16 384 | 923 | passes |
| ATmega328P, `CRYPTO=atecc608a ETH=1` (W5100) | 18 036 / 16 384 | 943 | **over by 1 652** |
| ATmega328P, `CRYPTO=atecc608a ETH=1 ETH_CHIP=w5500` | 18 080 / 16 384 | 943 | **over by 1 696** |
| ATmega328P, `CRYPTO=soft` | 12 886 / 16 384 | 717 | passed; refused since m522 |
| ATmega328P, `CRYPTO=soft ETH=1` (W5100) | 14 850 / 16 384 | 737 | passed; refused since m522 |
| ATmega2560, `CRYPTO=atecc608a` | 18 356 / 65 536 | 989 | passes |
| ATmega2560, `CRYPTO=atecc608a ETH=1` (W5100) | 20 328 / 65 536 | 1 009 | passes |
| ATmega2560, `CRYPTO=atecc608a ETH=1 ETH_CHIP=w5500` | 20 372 / 65 536 | 1 009 | passes |

The two `CRYPTO=soft` rows were measured before owner ruling 2026-10-17 (m522),
which made the 328P element-only and refuses `CRYPTO=soft` on it at build time.
No 328P row builds Ethernet now: its one tier, the element, is 1 652 bytes over
the flash budget with `ETH=1`.

Ethernet costs about 2 KB of flash and 20 bytes of RAM on either board. The
link refactor costs serial-only images 12 bytes of flash and 1 byte of RAM. The
Mega's Ethernet image leaves 15 bytes of its RAM budget.

`DHCP=1` is not measured: no avr-gcc ran with it. Its static state is 38
bytes (34, plus 4 for the lease length renewal keeps; the probe and
DHCPDECLINE add none), more than the 15 the table leaves, so the table above is out of date
or the Mega's `DHCP=1` image is over its RAM budget; `make firmware` decides.
