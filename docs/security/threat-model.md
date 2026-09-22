# Threat model

> **Status: DESIGN** · Scope: the firmware, its protocol and its device identity
> · Last verified: 2026-09-22

This document says what the device defends against, what it does not, and which
gaps are structural rather than unfinished. A trusted-computing claim that does
not say where it stops is not a claim anyone can act on.

## Attacker classes

| Attacker | Has | Defended |
|---|---|---|
| **Serial peer** | Can send arbitrary bytes to the UART | Partly — see below |
| **Network** | Reaches the gateway, not the device | Yes, by construction: the device speaks no IP |
| **Malicious host** | Owns the machine the device is plugged into | No. A host that can command a device can command it |
| **Physical** | Holds the board | Partly. Lock bits raise the cost; they do not stop a determined attacker |
| **Compromised gateway** | Relays for many devices | Partly — see "containment" |

## What the protocol layer defends against today

**Malformed and hostile input.** The framer is a bounded state machine over a
fixed buffer. Ten million random bytes cannot overrun it, wedge it, or make it
report a frame that was not there, and that is asserted under
AddressSanitizer rather than assumed. Every payload read and write is
range-checked with a sticky failure, so a handler that forgets to check still
cannot corrupt anything.

**Structurally impossible frames.** A frame whose opcode claims a status byte
its length leaves no room for is rejected after the CRC and before any accessor
sees it. The CRC does not catch this: a sender can be internally consistent and
still wrong.

**Truncation and desynchronisation.** A partial frame is abandoned after an
idle window rather than absorbing the next good frame's opening bytes.

## What it does not defend against, today

**The serial link is unauthenticated.** Anyone who can reach the UART can drive
every pin. For a lighting controller that is a nuisance; for a pump it is not.
The session authentication that closes this lands with the crypto provider, and
until then the device's security posture is exactly the physical security of its
serial line — which is the honest description of most microcontrollers.

**`RESET` into the bootloader is reachable from the wire.** That is a firmware
replacement entry point. It requires an authenticated session once one exists.

## Structural limits that will not be fixed

**No AVR in this family has a memory protection unit.** Once the application
runs, it can read all of SRAM. A CDI that transits SRAM is readable by
application code, which is why the design keeps key material inside a secure
element and derives per-boot keys that are zeroized rather than storing
long-lived ones in RAM.

**Lock bits are a procedural claim, not a silicon one.** They protect against
external ISP readout, and only if the factory flow actually burns them. They do
nothing about application code reading EEPROM, which is why the EEPROM region
holding platform material is protocol-write-protected rather than merely
documented.

**No secure clock exists.** Certificate validity carries no information and
freshness comes entirely from a verifier-supplied nonce. Any design that needs
the device to know the date is wrong.

**Evidence is bound to a nonce, not to a transport channel.** The platform
binds TLS channel data into a quote's qualifying data so it cannot be replayed
onto another connection. A device speaking serial has no TLS. The transcript
binding covers the serial segment; the gateway's mTLS terminates at the gateway.
This is weaker than an x86_64 node and is stated here rather than discovered.

## Containment: the gateway relays, it never re-signs

Device evidence is signed by the device and passed through verbatim. A
compromised gateway can therefore lie about **which devices exist** — it can
withhold or invent membership — but it cannot forge **what a given device
measured**. That boundary is deliberate and must be preserved by anything built
on top: a gateway that re-signed evidence would collapse it.

## Why DICE devices do not join the public network directly

The network's admission control rests on a manufacturer-issued endorsement that
cannot be minted. Self-provisioned device identities are cheap to mint if the
provisioning CA leaks, which makes them a Sybil vector against a global network
rather than a local one. Devices attest locally and reach the wider network
through a gateway that is itself attested.
