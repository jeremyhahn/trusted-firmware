# ADR: the wire protocol over a WIZnet W5100 or W5500

> **Status: PROPOSED (2026-10-15)** · Issue: #55 · Scope: trusted-firmware (device
> and `sdk/go`), go-trusted-platform (device-facing endpoint, seam only)
> · Last verified: 2026-10-15
>
> Every decision below is taken and implemented on the branch except one: how
> the ATmega328P carries Ethernet within its flash. That needs the owner's choice
> ("The 328P budget", below). Nothing shipped changes until then: Ethernet is off
> in every default image.

## The question

The SPEC row `ethernet-transport` says an AVR board reaches the network through a
WIZnet W5100 or W5500 on SPI, and enrols and attests over a TCP connection that
it opens to go-trusted-platform's device-facing endpoint. The frames, command ids
and wire vectors do not change. This ADR decides how the connection behaves, what
the Ethernet link may carry, where the device's address comes from, how the
drivers are built and selected, whether the 328P can carry it, what `sdk/go`
offers a host, and what go-trusted-platform's SPEC needs to say.

## Context

- The transport (`src/proto/transport.c`) is strict stop-and-wait: one framer, one
  response buffer, one frame per poll. It is the only layer that touches the byte
  link. That property is what lets everything above it run on a host.
- The W5100 and W5500 each hold a complete TCP/IP stack in silicon. The MCU
  programs registers over SPI and moves bytes in and out of a per-socket buffer.
  No IP, ARP or TCP code runs on the AVR.
- The two chips have the same socket register set at the same offsets and the
  same common registers for gateway, mask, MAC and IP. They differ in SPI framing
  (W5100: opcode, address, one data byte; W5500: address, control byte, data), in
  where a socket's registers and buffers live, and in how each identifies itself.
- The RAM budget is the tightest constraint on both reference boards. Before this
  work the ATmega2560 element image used 988 of its 1 024-byte RAM budget, and the
  ATmega328P element image used 922. A second transport instance (a framer plus a
  frame buffer) would not fit on either.
- The ATmega328P element image used 16 052 of its 16 384-byte flash budget: 332
  bytes free.
- Evidence (UCCS, the alias certificate, the CSR) is signed by the element and
  bound to the verifier's nonce. Its integrity does not depend on the transport.

## Constraints

- No `delay()`; nothing in the transport blocks.
- No new `#if` outside the whitelisted files; features are chosen by linking whole
  files.
- The HAL is substituted at link time; no vtable.
- Command ids, status codes and wire vectors do not change. No provisioning
  vector moves.
- No shipped capability is dropped to make room.

## Decisions

### 1. Connection

- **One device-initiated TCP connection** on socket 0, to one provisioned
  endpoint. The device never listens. That keeps it reachable from behind NAT and
  from a VLAN that only permits outbound connections, and it means a device on the
  network exposes no port at all.
- **Reconnect without `delay()`.** The Ethernet link keeps a deadline in
  `tfw_hal_millis()` time. Each attempt resets the controller, checks its identity,
  programs the addresses and connects. A failed or dropped connection doubles the
  wait, from 1 s to a 64 s ceiling; an established connection resets it to 1 s.
  The connect itself is the controller's: its SYN retry and timeout run in
  silicon and the link reads the socket state on each pass.
- **A fresh source port each attempt**, taken from the millisecond clock in the
  ephemeral range, so a reconnect is never mistaken for the old connection by the
  server's TCP.
- **The frames go over the stream unchanged.** The framer already resynchronises
  on a byte stream with no message boundaries, which is what TCP delivers. The
  partial-frame idle guard (`TFW_FRAME_IDLE_MS`) applies to both links.
- **Serial and Ethernet are both live, and share one transport.** Each frame is
  bound to the link that delivered its first byte, and its reply goes back on
  that link. While a frame is arriving on one link the other is not read; its
  bytes wait in the UART ring or in the controller's 2 KB socket buffer, so
  nothing is lost. When neither link is mid-frame, the link that did not carry the
  last frame is read first, so a busy host on one link cannot starve the other.
  This keeps a single framer and response buffer: the RAM cost of the second link
  is the link's own state, about a dozen bytes.
- **Events go to serial only.** BOOT and the scheduler's events are for the bench
  host that owns the device's pins. The Ethernet peer does not receive them and
  does not wait for a BOOT greeting.
- **A reply owed to a connection that has gone is dropped**, so a dead peer can
  never hold the transport and starve serial.

### 2. Allow-list

The Ethernet link accepts exactly these commands:

| Id | Command | Why it is needed over Ethernet |
|---|---|---|
| `0x00` | `CAPS` | Enrolment: the xkms adapter decides what the device can do (tier, algorithms, board) from `CAPS`, and refuses a key the part cannot hold. Read-only. |
| `0x60` | `ATTEST` | Attestation: UCCS evidence over the verifier's nonce. |
| `0x63` | `ATTEST_CERT` | Attestation and enrolment: the alias certificate over the nonce. |
| `0x64` | `ATTEST_CSR` | Enrolment: the device-templated PKCS#10 for ACME finalize. |

- Every other command received on Ethernet is answered with the existing status
  `TFW_ERR_UNKNOWN_CMD` (4) and is never dispatched. That is the answer the device
  already gives for a command not in its image, and from the network's side the
  command is not in the image. A new status would tell a probing peer which
  commands exist; it would also be a new code an older host has to read as
  unrecognised. Nothing is renumbered.
- A command on the list that the image does not carry (`ATTEST_CSR` on a 328P
  image today) answers `TFW_ERR_UNKNOWN_CMD` from the dispatcher, as it does on
  serial.
- `PING`, `SYS_INFO` and `RESET` are left off. Liveness is TCP's; `SYS_INFO` is
  diagnostics for the bench host; `RESET` from the network is a denial of service.
  The provisioning commands, the auth-session pair and every capability stay on
  serial.
- The check runs in the transport before dispatch, from a table in the Ethernet
  link's file. A serial-only image has no table and no check. A unit test pins all
  128 command ids against the literal list above.

### 3. Addressing

- **Static, provisioned, in EEPROM.** The MAC, IP, mask, gateway, endpoint IP and
  endpoint port are a 32-byte network block at EEPROM `0x0C0`, the first byte
  after the 192-byte platform block. It carries a magic, a version and a CRC-16
  (the frame CRC). A blank or corrupt block keeps the Ethernet link down; serial
  is unaffected. The block is read at each connection attempt and never held in
  RAM.
- **Board-independent address.** `0x0C0` is the same on every board, so a host
  needs no per-board table. The platform block is not touched, so no
  provisioning vector moves.
- **Written over serial with `EEPROM_WRITE`**, which already permits that range
  and which the Ethernet link refuses. The network cannot reconfigure the
  device's addressing; anyone holding the serial port already owns the device.
  `sdk/go` encodes the block. The next connection attempt picks up a new block
  without a reset.
- **No address in the image.** Nothing under `src/` carries an address, a MAC or
  a port, and no board profile supplies a default. The block is the only source,
  and a device without one keeps its Ethernet link down. A board-profile default
  would cost flash on every image of that board and would put a site's
  addressing into a build.
- **The MAC is configuration too.** A W5100 or W5500 has no MAC ROM: the address
  is whatever the block says. An authentic Arduino shield carries its assigned
  MAC on a sticker. When that value is supplied, the host writes it. Until then,
  the host writes `tfw.DerivedMAC(id)`, a locally administered unicast address:
  - **Derivation:** the first six bytes of SHA-256(`"tfw-eth-mac-v1"` ‖ `id`),
    with bit 1 of the first octet set (locally administered) and bit 0 cleared
    (unicast).
  - **`id` on the element tier:** the DeviceID public key, the 64-byte X‖Y of
    its P-256 point.
  - **`id` on the software tier:** that tier has no public key, since its
    identity is a symmetric secret, so `id` is the 16-byte serial, the DeviceID
    field of its `ATTEST` evidence.

  The derivation is the host's, not the device's: the device holds the one MAC
  the block gives it and costs no flash for this. A sticker value replaces a
  derived one by being written over it. `MarshalBinary` refuses a multicast MAC.
- **No gateway is allowed.** A network with no router takes `0.0.0.0` as the
  gateway. The controller then reaches on-link hosts only, which is all a
  device on the control plane's own segment needs.
- **The reference values for the hardware run.** The bench network has no DHCP
  yet, and the control plane's endpoint is on the same segment. These are the
  values the hardware run writes into each board's block over serial. They
  live here and in the [test matrix](test-matrix.md), not in code:

  | Board | IP / mask | Gateway | Endpoint |
  |---|---|---|---|
  | Nano (ATmega328P) | `192.0.2.21/24` | none (`0.0.0.0`) | `192.0.2.1`, at the port the endpoint listens on |
  | Mega (ATmega2560) | `192.0.2.22/24` | none (`0.0.0.0`) | `192.0.2.1`, at the port the endpoint listens on |

- **DHCP is not in the image. It arrives with the boot node.** A DHCP client on
  the AVR is a UDP state machine (DISCOVER, OFFER, REQUEST, ACK, lease renewal)
  in our code; neither the W5100 nor the W5500 has DHCP in silicon. The bench
  network gets a DHCP server when the boot node starts serving it, and only then
  is there anything for a client to talk to.
  - **Flash cost:** about 3 KB. That is an estimate from the size of the
    Arduino Ethernet library's DHCP client, not a measurement of a client in
    this tree.
  - **RAM cost:** a second socket's state and a lease deadline, about 20 bytes.
    The 548-byte message streams through the controller's socket buffer.
  - **Fit:** on the Mega it fits, with about 45 KB of flash free in the Ethernet
    image. On the 328P it does not fit under any of the budget options below
    except (a).
  - **Shape when it comes:** a separate file, linked on boards with room. It is
    selected by a reserved bit in the block's flags byte, so the block's layout
    does not change.
  - **A static address stays valid** after DHCP arrives. It also lets an
    operator pin the device's address in the control plane's allow-list.

### 4. Drivers

- **One file per controller over the HAL SPI**: `src/net/w5100.c` and
  `src/net/w5500.c`. Each defines the same three primitives: read a byte, write a
  byte, identify the chip. They are given a register space and an offset. The
  socket logic common to both is `src/net/w5x00.c`, written once against those
  primitives. The board's `profile.mk` names its shield's controller
  (`ETH_CHIP`); `make ETH=1` links the link, the socket logic and that one
  driver file. No vtable, no runtime dispatch.
- **Identity is checked before the chip is trusted, and the wrong chip fails
  closed.** Each driver resets the chip through `MR` and then reads:
  - **W5500:** `VERSIONR` at `0x0039` must read `0x04`.
  - **W5100:** it has no version register, so the driver checks behaviour. It
    requires the `0x00 0x01 0x02` the chip shifts out during every frame header.
    After the reset it requires `RTR` (`0x0017`) to read `0x07D0` and `RCR`
    (`0x0019`) to read `0x08`. This is the pattern the probe read off the
    reference Mega's shield ([wiznet-probe](../development/wiznet-probe.md)),
    which also saw `0x00` at `0x001F`, the W5500's `VERSIONR` offset.
  - **W5200:** there is no driver. It reads `0x03` at `0x001F` and fails both
    checks.

  A W5100 ignores the W5500's framing. A W5500 reads the W5100's framing as a
  buffer read, with one exception: a frame addressed to `0x0017` decodes on a
  W5500 as a write. The W5100 driver therefore reads `RTR`'s high byte last,
  once the header echo has already ruled out a W5500, so probing the wrong chip
  writes nothing. A unit test runs each driver against the other chip's model
  and counts writes. A failed check keeps the Ethernet link down and retries on
  the backoff.
- **W5100 or W5500 on the Mega.** The Arduino Ethernet Shield Rev3 carries a
  W5100 and the Ethernet Shield 2 a W5500. The reference Mega's shield is a
  W5100, probed. The Mega's profile defaults to `ETH_CHIP=w5100`, and
  `make ETH=1 ETH_CHIP=w5500` builds the other image.
  Detecting at run time would link both drivers and a dispatch between them. That
  is the vtable the HAL rule forbids, and it doubles the driver flash. The
  identity check makes a mismatch a clean, visible failure instead of a silent
  one.
- **The bus is re-configured before every transaction.** A serial host may
  change the SPI mode with `SPI_CONFIG`. The driver sets mode 0, MSB first, at
  the controller's clock before each access, so a host's setting never reaches
  the controller.
- **Pins.** The shield's W5x00 chip select is D10 and its SD card's is D4. In an
  Ethernet image both are flagged `TFW_PIN_IN_USE`, as are the SPI pins (D11–D13 on
  the 328P, D50–D53 on the Mega). That flag already makes GPIO, the scheduler, PWM,
  tone and `SPI_XFER` refuse them. D4 is driven high at start so the SD card never
  answers on the bus. The flags come from one expression in `include/tfw/board.h`
  that is zero in a serial-only image, so `CAPS` and every vector are unchanged
  there.
- **Tone.** Each board's tone pin is one the shield uses: D10 (OC2A) on the Mega
  is the chip select, and D11 (OC2A) on the 328P is MOSI. In an Ethernet image
  tone is refused with `TFW_ERR_RESERVED_PIN`, because driving the pin would
  select the controller or corrupt the bus. This is why Ethernet is not on by
  default in either image: turning it on removes tone, and that is the owner's
  call. With the shield fitted, tone is physically unusable in any image.

### 5. sdk/go TCP transport

- `tfw.Listen(addr string, opts ...Option) (*Listener, error)` opens a TCP
  listener. `(*Listener).Accept() (*TCPDevice, error)` waits for the next device
  to connect and returns it as a `*Device` on that connection, with its remote
  address and a `Close`. `tfw.NewListener(net.Listener, ...)` wraps a listener the
  caller already owns, such as a TLS-terminating proxy's.
- The host accepts. It does not dial: the device opens the connection. A
  `TCPDevice` is an ordinary `Device`; the same `Caps`, `Attest`, `AttestCert` and
  `AttestCSR` calls work on it. `New` is called with no BOOT wait, because the
  device sends no events on Ethernet.
- Who the device is comes from its evidence, never from its address. A host
  identifies the connection by `ATTEST_CERT` or `ATTEST`, verified by go-xkms.
- `tfw.NetConfig` and its `MarshalBinary` encode the network block, and
  `(*Device).WriteNetConfig` writes it over serial. Standard library only; the
  module keeps zero in-house dependencies.

### 6. Seam to go-trusted-platform

go-trusted-platform owns the device-facing endpoint, enrolment and the registry.
This project owns the device side and the `sdk/go` listener. The row go-trusted-
platform's SPEC needs, in its own words:

> **device-endpoint-tcp** — The control plane's device-facing TCP endpoint for
> trusted-firmware devices on Ethernet. It accepts connections the device opens
> (the device never listens), through `sdk/go`'s `tfw.Listener`, and speaks the
> trusted-firmware wire frames as the host. It issues only `CAPS`, `ATTEST`,
> `ATTEST_CERT` and `ATTEST_CSR`, the device's Ethernet allow-list. It identifies
> the device from its signed, nonce-bound evidence verified through go-quicbaas
> and go-xkms, never from its source address. It enrols through device-attest-01
> as for a serial-attached device. The endpoint address and port are what an
> operator writes into the device's network block. trusted-firmware's SPEC
> (`ethernet-transport`) names the same seam.

Filing that row is go-trusted-platform's work; this ADR only reports it.

## The 328P budget

Measured with avr-gcc 14.2.0, element tier (`CRYPTO=atecc608a`), against each
board's `TFW_FLASH_BUDGET`:

| Image | Flash / budget | Free | RAM (budget 1 024) |
|---|---|---|---|
| ATmega328P, serial only | 16 064 / 16 384 | 320 | 923 |
| ATmega328P, `ETH=1` (W5100) | 18 036 / 16 384 | **−1 652** | 943 |
| ATmega2560, serial only | 18 356 / 65 536 | 47 180 | 989 |
| ATmega2560, `ETH=1` (W5100) | 20 328 / 65 536 | 45 208 | 1 009 |

The W5500 images are 44 bytes larger; every row is in
[engineering.md](engineering.md#flash-and-ram). The Mega fits. **The 328P's
element-tier Ethernet image is 1 652 bytes over its budget and does not build.**
Its RAM fits. The options for the owner are:

- **(a) Raise the 328P's flash budget.** `TFW_FLASH_BUDGET` is 16 384, half the
  part, a policy gate and not a physical limit: the application region is
  28 672 bytes (32 KB less the 4 KB boot section), and OTA writes in place with
  no staging slot. Cost: none in flash. It spends headroom the gate was set to
  protect, and the 328P would then also have room for `ATTEST_CSR` (500 bytes over
  today), which enrolment needs.
- **(b) An Ethernet profile for the 328P that leaves capabilities out.** A
  networked attestation node needs none of tone, pulse, the scheduler or PWM. Cost:
  those capabilities are absent from that image, and it is a second 328P image to
  build and test.
- **(c) Ethernet on the Mega only.** Cost: the SPEC's dev-complete criterion names
  the Nano, so the criterion changes.
- **(d) Ethernet on the 328P's software tier only.** `CRYPTO=soft ETH=1` builds
  at 14 850 bytes and fits. Cost: no element, so no `ATTEST_CERT` or `ATTEST_CSR`;
  its `ATTEST` evidence is an HMAC under a shared secret, which a verifier can
  only check by holding that secret. It cannot enrol through device-attest-01.

Whichever is chosen, the 328P cannot enrol through device-attest-01 until it
also carries `ATTEST_CSR`, which the owner excluded on flash grounds. Option (a)
is the only one that also fits that.

No option is applied. Nothing is dropped: the 328P's profile names its controller
so the image can be measured, and its default image is unchanged.

## Rejected alternatives

- **A second transport instance per link.** Simplest code, but it doubles the
  framer and the frame buffer, about 160 bytes of RAM that neither board has.
- **The device listens and the host dials.** Needs the device reachable from the
  control plane, an inbound firewall rule per device, and a listening port on every
  device on the network.
- **Runtime detection between the two chips.** It needs both drivers linked
  and a dispatch between them; see Drivers.
- **TLS on the device.** No TLS stack fits, and a W5x00 has none. The evidence is
  signed and nonce-bound, so a network attacker can delay or drop it but not
  forge or replay it. What TLS would add is confidentiality of the evidence and
  of the CSR, which carry no secret.
- **A new status code for "refused on this link".** It would leak the command set
  to the network and add a code older hosts must treat as unknown, for no
  behaviour a host can act on differently.
- **DHCP in this change.** See Addressing: it arrives with the boot node.
- **The device derives its own MAC.** It would cost flash on every image to save
  the host one call, and it would put a second source of the MAC beside the
  block.
- **An address or MAC default in the board profile.** See Addressing.

## Follow-ups

- The owner's choice for the 328P (above).
- The owner's call on Ethernet by default in the Mega's image, which removes tone
  there, and the CI matrix entry for whichever Ethernet images ship.
- The Nano's shield, which could not be probed. A W5100 is expected.
- The sticker MAC of each shield, which replaces the derived one when supplied.
- DHCP, when the boot node serves the bench network (see Addressing).
- go-trusted-platform's `device-endpoint-tcp` row (above).
- A hardware run on both boards, under the physical end-to-end issue, as the
  [test matrix](test-matrix.md) lays out.
- Whether `SYS_INFO` should join the allow-list so the adapter's health check
  works over Ethernet.
