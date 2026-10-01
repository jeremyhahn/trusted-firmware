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

**The serial link is unauthenticated for driving pins.** Anyone who can reach
the UART can drive every pin. For a lighting controller that is a nuisance;
for a pump it is not. That is unchanged and is not what the auth session
below closes — it protects two specific resources, not the command channel in
general.

**One resource now requires an authenticated session, element tier only.**
`AUTH_CHALLENGE`/`AUTH_RESPONSE` (0x61/0x62, issue #4) let a host prove it
holds an operator private key: the device issues a single-use nonce, the host
signs `domain||device_serial||nonce` with the operator key, and the device
verifies the P-256 signature in the ATECC608A against the operator public key
`agprov` baked into the platform-reserved EEPROM block. A session granted this
way gates `EEPROM_READ` below `EEPROMReservedEnd`:

This was previously unconditional, since the region's contents (a hash, a
serial, and now an operator public key) were considered fine to disclose. A
public key does not need secrecy once trusted, but a host that has not yet
proven it holds the matching private key has no business confirming what the
anchor even is.

`RESET` mode 1 ("bootloader") is deliberately not gated. It is not yet a
distinct path: the firmware takes the same watchdog reset for both modes, and
ag-boot opens its OTA session only through recovery conditions, so a gate
would protect nothing. Issue #28 makes mode 1 a real bootloader entry and adds
the gate there.

A session lasts until the device resets; there is no separate inactivity
timeout.

**This is element tier only, and that is a real gap on the other two, not a
temporary one.** The software tier's only available primitive for an
external-key signature check, ed25519-donna (already vendored for
`BOOT_SIG=ed25519-donna`), does not fit this device family: its verify path
alone measures 72,930–85,452 bytes of flash and a single ~2.95 KB stack
frame, which overflows an ATmega328P's entire 32 KB of flash before the rest
of the firmware is counted, and is 31–47% over this project's own 64 KB
application-flash budget on an ATmega2560. No smaller alternative is vendored.
So on the soft and `CRYPTO=none` tiers, `AUTH_CHALLENGE`/`AUTH_RESPONSE` are
not compiled in at all (the same `AG_ERR_UNKNOWN_CMD` any other absent
feature gets), and `EEPROM_READ` is consequently ungated there,
exactly as they always were. A verifier reading a soft-tier device's `CAPS`
already knows this — the feature bit is clear — and should not expect these
two resources to be protected on it.

## Structural limits that will not be fixed

**No AVR in this family has a memory protection unit.** Once the application
runs, it can read all of SRAM. A CDI that transits SRAM is readable by
application code. The element tier therefore keeps its signing key inside the
ATECC608A and zeroizes its CDI buffer as soon as derivation is done. The soft
tier cannot: its CDI is the HMAC key for every signature, so it stays resident
in SRAM for the life of the process. Neither tier derives a per-boot Alias key
(see `docs/dice-attestation/architecture.md`).

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
