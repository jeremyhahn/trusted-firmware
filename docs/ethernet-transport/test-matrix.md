# Ethernet transport — hardware test matrix

> **Status: DRAFT** · Scope: trusted-firmware · Last verified: 2026-10-15
> What the hardware run of [the ADR](adr-wire-protocol-over-w5x00.md) covers on
> each board, now and when the ATECC608A arrives. No run has happened yet.

## Values the run writes

Each board's network block is written over serial with `sdk/go`'s
`WriteNetConfig` before the Ethernet link is tested. None of these values is
in the firmware.

| Board | IP / mask | Gateway | Endpoint | MAC |
|---|---|---|---|---|
| Nano | `192.0.2.21/24` | none (`0.0.0.0`) | `192.0.2.1`, at the port the endpoint listens on | the shield's sticker address if supplied, else `tfw.DerivedMAC` |
| Mega | `192.0.2.22/24` | none (`0.0.0.0`) | `192.0.2.1`, at the port the endpoint listens on | the shield's sticker address if supplied, else `tfw.DerivedMAC` |

## Matrix

| Board | Port | Shield / controller | ATECC608A | Tier | What is tested |
|---|---|---|---|---|---|
| Mega (ATmega2560) | USB serial; Ethernet on the bench network | W5100 (probed, [docs/development/wiznet-probe.md](../development/wiznet-probe.md)) | Not fitted (ordered) | Software (`CRYPTO=soft ETH=1`) | Now: everything below except the element tier and enrolment. |
| Mega (ATmega2560) | USB serial; Ethernet on the bench network | W5100 (probed) | Fitted | Element (`CRYPTO=atecc608a ETH=1`) | The day the ATECC608A arrives: the element tier and device-attest-01 enrolment. |
| Nano (ATmega328P) | USB serial; Ethernet on the bench network | W5100 expected; not probed (board held, owner decision) | Not fitted (ordered) | Software (`CRYPTO=soft ETH=1`) | Now, once the owner releases the board: everything below except the element tier and enrolment. The software-tier Ethernet image fits the 328P's flash budget. |
| Nano (ATmega328P) | USB serial; Ethernet on the bench network | W5100 expected; not probed (board held, owner decision) | Fitted | Element (`CRYPTO=atecc608a ETH=1`) | Its Ethernet image waits for the owner's budget choice: it is over the 328P's flash budget today ([the options](adr-wire-protocol-over-w5x00.md#the-328p-budget)). The day the ATECC608A arrives and that choice is made: the element tier and device-attest-01 enrolment. |
| ATmega32U4, ATtiny85 | — | None | — | — | Covered by the simulator. |

## What is tested now, on each board

1. **Flash.** The Ethernet image is built for the board's shield and flashed;
   its size-check passes.
2. **Firmware tests over serial.** The serial suite runs as on any image, with
   the shield's pins reported in use and tone refused.
3. **The Ethernet link with a static address.** The block above is written over
   serial. The device brings the link up from it without a reset.
4. **TCP to the control plane's endpoint.** The device opens the connection to
   `192.0.2.1` and a `tfw.Listener` there accepts it.
5. **Attestation commands over TCP on the tier the board has.** `CAPS` reports
   the software tier. `ATTEST` returns evidence over the host's nonce, and it
   verifies under the device's key. `ATTEST_CERT` and `ATTEST_CSR` answer
   "unknown command" on this tier, as they do over serial.
6. **The off-allow-list refusal.** `PING` and `EEPROM_WRITE` over TCP answer
   "unknown command", and the EEPROM is unchanged; the same commands work over
   serial.
7. **The same identity after a reset.** After a reset the device reconnects
   with the same MAC. Its `ATTEST` evidence carries the same DeviceID and
   verifies under the same key.

## The day the ATECC608A arrives

The element tier and device-attest-01 enrolment, on each board whose element
image builds:

- `CAPS` reports the element tier.
- `ATTEST`, `ATTEST_CERT` and `ATTEST_CSR` work over TCP and verify under the
  DeviceID key.
- The device enrols through device-attest-01 over the TCP connection.
- The MAC, unless a sticker address was supplied, is re-derived from the
  DeviceID public key and rewritten. The element tier's DeviceID differs from
  the software tier's serial-based one.

## What the simulator covers today

`make sdk-test` runs the element-tier firmware, compiled for the host, as an
Ethernet device on a loopback socket (`tfwsim --eth`). It covers `CAPS`,
`ATTEST`, `ATTEST_CERT` and `ATTEST_CSR` over TCP, the off-allow-list refusal,
and serial and Ethernet answering side by side.
