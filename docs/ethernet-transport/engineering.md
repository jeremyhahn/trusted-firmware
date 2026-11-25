# Ethernet transport — engineering

> **Status: DRAFT** · Scope: trusted-firmware · Last verified: 2026-11-16

## Requirements

One line per requirement of the row in [`docs/SPEC.md`](../SPEC.md)
(`ethernet-transport`) and of this file's invariants (marked *inv*), audited on
main d1282f61 (#457) and audited again on main fa31d30e (#515). On fa31d30e
every file named was found with `git ls-files`, and every test function and
make variable named was found in that file with one search each. Of the sources,
profiles and makefiles the table names, one changed between d1282f61 and
fa31d30e: `mk/host.mk`. Its hunks add the `test_cortexm_sys` host test, a
"clean" line per cppcheck run in `lint`, the `tfwsim-manifest` and `suit-emit`
simulator recipes (with `tfwsim-otaboot` now building them) and
`image_key_none.c` on the other element simulators' links. The one target the
table cites there, `lint-conditionals`, was reviewed and is unchanged in recipe
and behaviour, so every status and every slice below is unchanged.
The clause table in [the test matrix](test-matrix.md#acceptance-clauses-row-ethernet-transport-431)
says the same with the bench runs beside it.

| Requirement | Status | Code | Test |
|---|---|---|---|
| A device reaches the network, enrols (device-attest-01, through go-trusted-platform) and attests without a serial host | partial: done on `tfwsim` over TCP; no board has enrolled over Ethernet | `src/proto/link_eth.c`, `src/net/w5x00.c` | `sdk/go/cmd/tfwctl/e2e_ethernet_test.go` `TestEthernetDeviceAttestsOverTCP`, `TestEthernetDeviceSignsTheCSRDigestOverTCP`; the board run: slices 1 and 2 |
| The controller's TCP/IP stack carries the existing frames unchanged over a TCP connection the device opens | done | `src/proto/link_eth.c`, `src/net/w5x00.c` | `test/unit/test_link_eth.c` `test_a_frame_over_ethernet_is_answered_over_ethernet`; `e2e_ethernet_test.go` `TestEthernetDeviceAnswersCapsOverTCP` |
| Frames, command ids and wire vectors do not change | done | `src/proto/frame.c`, `src/proto/transport.c` (one framer for both links) | `e2e_ethernet_test.go` `TestEthernetAndSerialAreAnsweredSideBySide` |
| The Ethernet link accepts only attestation and enrolment, and 0x68–0x6B (item 48) | done | the allow-list table in `src/proto/link_eth.c` | `test_link_eth.c` `test_ethernet_admits_the_enrolment_attestation_and_staging_commands_and_nothing_else`, `test_every_id_off_the_list_is_answered_unknown_over_ethernet`; `e2e_ethernet_test.go` `TestEthernetDeviceRefusesWhatIsNotOnTheAllowList`, `TestEthernetDeviceRefusesAuthAndOTAActivateOverTCP` |
| Provisioning and the capabilities stay on serial, which the host SDK keeps | done | `src/proto/link_eth.c` | `test_link_eth.c` `test_serial_admits_every_command`, `test_events_go_to_serial_only`; `e2e_ethernet_test.go` `TestEthernetNetworkBlockReadsBackOverSerial` |
| One driver file per controller, chosen by the board profile, over the HAL's SPI | done | `src/net/w5100.c`, `src/net/w5500.c`; `ETH_CHIP` in `boards/atmega2560/profile.mk` and `boards/atmega328p/profile.mk`, checked by `mk/firmware.mk` | `test/unit/test_w5x00.c` `test_start_refuses_the_other_chip_and_never_writes_to_it` (built as `test_w5100` and `test_w5500`) |
| No IP stack, TLS or ACME client of ours on the device | done (an absence: the Ethernet objects are `LINK_SRCS` in `mk/firmware.mk`, the controller's sockets only) | `include/tfw/eth.h`, `src/net/w5x00.c` | `test_w5x00.c` `test_send_puts_each_reply_on_the_wire` |
| The device's evidence is signed and nonce-bound end to end | done | `src/caps/attest.c`, `src/caps/csr.c` | `e2e_ethernet_test.go` `TestEthernetDeviceAttestsOverTCP`, `TestEthernetDeviceSignsTheCSRDigestOverTCP` |
| Address, MAC and endpoint are configuration, never hardcoded in `src/` | done | `src/net/netcfg.c`, `sdk/go/netcfg.go` | `test_link_eth.c` `test_every_field_the_controller_is_given_comes_from_the_block`, `test_the_block_from_eeprom_is_what_the_controller_is_given`; `sdk/go/cmd/tfwctl/e2e_netcfg_test.go` `TestE2ENetcfgSetPointsTheDeviceAtTheWrittenEndpoint` |
| Static addresses until DHCP; a DHCP block is leased by the RFC 2131 client linked with `DHCP=1`, off by default | done | `src/net/dhcp_none.c`, `src/net/dhcp.c`; `DHCP ?= 0` in `mk/firmware.mk` | `test_link_eth.c` `test_a_block_asking_for_dhcp_stays_down_without_the_client` |
| The client probes the offered address | done | `src/net/dhcp.c` | `test/unit/test_dhcp.c` `test_an_ack_probes_the_address_from_no_address_before_taking_it`, `test_an_address_in_use_is_declined_and_asked_for_again_after_10_s` |
| It renews at T1 and T2 beside the connection | done | `src/net/dhcp.c`, `src/proto/link_eth.c` | `test_dhcp.c` `test_at_t1_the_lease_is_renewed_beside_the_connection`, `test_at_t2_any_server_may_rebind_the_lease`; `test/unit/test_link_dhcp.c` `test_at_t1_the_link_renews_beside_its_connection_and_keeps_its_address` |
| `tfwctl netcfg set --dhcp` writes a DHCP block and refuses `--ip`, `--mask`, `--gateway` beside it | done | `sdk/go/cmd/tfwctl/netcfg.go` | `sdk/go/cmd/tfwctl/netcfg_test.go` `TestNetcfgSetDHCPWritesTheFlagAndNoAddress`, `TestNetcfgSetDHCPRefusesAStaticAddressBesideIt`; `e2e_netcfg_test.go` `TestE2ENetcfgSetDHCPRoundTripsThroughTheDevice` |
| `tfwprov --bake-netblock --dhcp` does the same | done | `sdk/go/cmd/tfwprov/netblock.go` | `sdk/go/cmd/tfwprov/netblock_test.go` `TestNetblockDHCPIsTheFlagAndNoAddress`, `TestNetblockDHCPRefusesAStaticAddressBesideIt` |
| Until the shields' MACs are supplied, a locally administered MAC derived from the DeviceID | partial: the derived MAC is done; the shields' values are the owner's (slice 4) | `sdk/go/netcfg.go` `DerivedMAC` | `sdk/go/netcfg_test.go` `TestDerivedMACIsPinned`, `TestDerivedAndStickerMACsAreAccepted` |
| Reference boards: the Nano (ATmega328P) with a W5100 shield, the Mega 2560 with the Arduino Ethernet shield | partial: both profiles name the W5100; the Mega has run over serial only, and no Nano image builds Ethernet (slices 1 and 3) | `ETH_CHIP ?= w5100` in both boards' `profile.mk` | `test_w5x00.c` `test_the_sd_card_is_parked_and_the_chip_select_is_d10` |
| The Mega enrols on the soft DICE root when no element answers; the Nano only with an element | partial: the tier choice is done; the enrolment run is open (slice 1) | `src/crypto/auto/provider.c`; `BOARD_TIERS` and `BOARD_REFUSED_TIERS` in both boards' `profile.mk` | `test/unit/test_crypto_auto.c` `test_no_element_runs_the_soft_root`; `scripts/test_board_rows.py` `test_the_nanos_one_tier_is_the_element`, `test_crypto_soft_on_the_nano_is_refused` |
| The ATECC608C element tier runs once the owner wires it | not started: the owner's wiring; nothing waits on it (slice 5) | `src/crypto/atecc608a/provider.c` | `test_crypto_auto.c` `test_an_element_that_answers_is_chosen` (host only) |
| `ETH=1` is the ATmega2560 default; the shieldless bench Mega is an explicit `ETH=0` row | done | `ETH ?= 1` and `BOARD_ROWS` in `boards/atmega2560/profile.mk` | `scripts/test_board_rows.py` `test_the_mega_keeps_its_soft_and_auto_tiers`; every row built by `make firmware-matrix` |
| The soft-crypto 328P Ethernet node is withdrawn; no 328P row builds Ethernet | done | `boards/atmega328p/profile.mk` (no `BOARD_ROWS`, `BOARD_REFUSED_TIERS := soft`); the refusal in `mk/firmware.mk` | `scripts/test_board_rows.py` `test_the_matrix_builds_no_soft_nano`, `test_crypto_soft_on_the_nano_is_refused` |
| The 328P element image is over its flash budget with `ETH=1` (W5100) | done, but the row's figure is stale (slice 6) | `size-check` in `mk/firmware.mk` | [Flash and RAM](#flash-and-ram) below: 21 612 / 16 384 bytes and 1 121 / 1 024 of RAM; the row still says 18 036 and 1 652 over |
| `sdk/go` gains a TCP transport beside serial | done | `sdk/go/tcp.go` | `sdk/go/tcp_test.go` `TestAcceptReturnsAWorkingDeviceWithoutWaitingForBoot`; `sdk/go/tfwsim/fake_test.go` `TestStartElementEthernetAcceptsTheConnectionTheDeviceOpens` |
| Serial or network chosen at build time by object list, never `#if` in logic | done | `LINK_SRCS` in `mk/firmware.mk` (`link_eth.c` or `link_serial.c`); `TFW_PIN_ETH` in `include/tfw/board.h` uses the flag as a value | `lint-conditionals` in `mk/host.mk` |
| Seam: go-trusted-platform's device-facing endpoint accepts these connections | not started here: that repository's side (slice 2) | none in this repository | none in this repository |
| *inv* One transport, one framer, one response buffer; a reply goes back on the link that delivered its first byte | done | `src/proto/transport.c`, `src/proto/link_eth.c` | `test_link_eth.c` `test_a_frame_over_serial_is_answered_over_serial_while_ethernet_is_up`; `test/unit/test_link_eth_mutate.c` `test_the_end_of_a_serial_reply_never_sends_on_ethernet` |
| *inv* The allow-list is checked before dispatch and only for Ethernet frames; a test pins all 128 ids | done | `src/proto/link_eth.c` | `test_link_eth.c` `test_every_id_off_the_list_is_answered_unknown_over_ethernet`, `test_serial_admits_every_command` |
| *inv* The driver sets SPI mode 0 before every access | done | `src/net/w5x00.c` | `test_w5x00.c` `test_every_transaction_reapplies_mode_0_msb_first` |
| *inv* Every wait is bounded; chip command completion is polled a bounded number of times | done | `src/net/w5x00.c`, `src/net/w5100.c`, `src/net/w5500.c` | `test_w5x00.c` `test_a_command_the_chip_never_takes_is_given_up`, `test_a_connect_the_chip_never_takes_is_given_up`; `test/unit/test_w5100_spi_mutate.c` `test_init_accepts_rst_clearing_on_the_last_poll` |
| *inv* A failure in the Ethernet path keeps that link down and never blocks serial | done | `src/proto/link_eth.c` | `test_link_eth.c` `test_a_blank_block_never_starts_the_controller_and_serial_still_answers`, `test_a_reply_to_a_dropped_connection_is_abandoned_and_serial_carries_on`, `test_reconnect_backs_off_from_1_s_to_64_s_and_resets_once_up`; `test_link_eth_mutate.c` `test_a_hostile_socket_never_wedges_the_link_and_caps_answers_after` |

### Remaining slices

No requirement is open that a host build or host test can close: every slice
left needs a board, another project or an owner decision.

1. **Hardware.** The Mega 2560 with its Ethernet shield answers on the bench
   network block and enrols over TCP through `make hw-enroll` against
   go-trusted-platform's device-facing endpoint. It needs the board on that
   segment (#171), a published port to the harness, and a soft-tier DeviceID
   handed off by `tfw-boot`. It is recorded in `test-matrix.md` and proved by
   `sdk/go/cmd/tfwctl/e2e_ethernet_test.go`'s `TestEthernet*` rows under
   `make hw-test HW_ETH=… HW_ETH_LISTEN=…` and by `xkms/e2e/hw_enroll_test.go`
   `TestHardwareEnrollment`.
2. **Another project.** go-trusted-platform's device-facing endpoint accepts
   these connections and its SPEC names the seam. That is its repository's
   work, and it is proved there; slice 1 then proves it end to end.
3. **Owner decision.** The row names the Nano with a W5100 shield as a
   reference board, but its one tier with `ETH=1` is 5 228 bytes of flash and
   97 of RAM over budget, so no Nano Ethernet image can be built. The owner
   either drops the Nano as an Ethernet reference board or names the image
   that fits (the #52 ADR's image choice). A ruling to drop it changes
   `docs/SPEC.md` only. A ruling to fit it touches `boards/atmega328p/profile.mk`
   and the sources it links, proved by `make firmware-matrix`.
4. **Owner input.** The shields' MAC values. They go in through
   `tfwctl netcfg set` or `tfwprov --bake-netblock` and never in `src/`, so no
   code changes; `sdk/go/netcfg_test.go` `TestDerivedAndStickerMACsAreAccepted`
   already covers a sticker MAC.
5. **Owner's wiring.** The ATECC608C (MIKROE-6774) element tier on the Mega
   over Ethernet, run as `test-matrix.md`'s "The day the ATECC608C arrives"
   says and recorded there. Nothing waits on it.
6. **Coordinator or owner, docs only.** The row's 328P figure in
   `docs/SPEC.md` (18 036 / 16 384 bytes, 1 652 over) predates the measurement
   in [Flash and RAM](#flash-and-ram) (21 612 / 16 384 bytes, 5 228 over, and
   97 bytes over in RAM). One sentence of `docs/SPEC.md`, docs only, with no
   test to add. The lane's policy guards that file.

When slices 1 and 2 close, and 3 to 5 are ruled on or run, the row's status
change to `shipped` is the owner's.

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

Measured on main 48d85486 with avr-gcc 14.2.0 by `make firmware`'s size check,
against each board's flash and RAM budgets: 16 384 and 1 024 bytes on the
ATmega328P, 65 536 and 2 048 on the ATmega2560. The Mega builds `ETH=1` by
default, so its serial-only rows pass `ETH=0`; `auto` is the default crypto.

| Image | Flash / budget | RAM / budget | Result |
|---|---|---|---|
| ATmega328P, `CRYPTO=atecc608a` | 16 138 / 16 384 | 987 / 1 024 | passes |
| ATmega328P, `CRYPTO=atecc608a ETH=1` (W5100) | 21 612 / 16 384 | 1 121 / 1 024 | **over by 5 228 and 97** |
| ATmega328P, `CRYPTO=atecc608a ETH=1 ETH_CHIP=w5500` | 21 668 / 16 384 | 1 121 / 1 024 | **over by 5 284 and 97** |
| ATmega2560, `CRYPTO=atecc608a ETH=0` | 18 626 / 65 536 | 1 058 / 2 048 | passes |
| ATmega2560, `CRYPTO=atecc608a ETH=1` (W5100) | 28 802 / 65 536 | 1 210 / 2 048 | passes |
| ATmega2560, `CRYPTO=atecc608a ETH=1 ETH_CHIP=w5500` | 28 858 / 65 536 | 1 210 / 2 048 | passes |
| ATmega2560, `CRYPTO=atecc608a ETH=1 DHCP=1` | 32 832 / 65 536 | 1 254 / 2 048 | passes |
| ATmega2560, `ETH=0` | 26 954 / 65 536 | 1 324 / 2 048 | passes |
| ATmega2560, `ETH=1` (W5100) | 37 292 / 65 536 | 1 476 / 2 048 | passes |
| ATmega2560, `ETH=1 DHCP=1` | 41 322 / 65 536 | 1 520 / 2 048 | passes |

Owner ruling 2026-10-17 made the 328P element-only and refuses
`CRYPTO=soft` on it at build time. No 328P row builds Ethernet: its one tier,
the element, is over both its flash and its RAM budget with `ETH=1`.

On the Mega's `CRYPTO=atecc608a` image, Ethernet costs 10 176 bytes of flash and
152 of RAM. The DHCP client costs 4 030 bytes of flash and 44 of RAM on both Mega
images (`DHCP=1` against the same build without it). Its static state is 38
bytes (34, plus 4 for the lease length renewal keeps; the probe and DHCPDECLINE
add none). The Mega's `DHCP=1` image is a firmware-matrix row
(`auto:ETH=1,DHCP=1`), so CI builds it against the budgets; it leaves 24 214
bytes of flash and 528 of RAM.
