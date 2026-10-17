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
for an actuator it is not. That is unchanged and is not what the auth session
below closes — it protects two specific resources, not the command channel in
general.

**One resource now requires an authenticated session, element tier only.**
`AUTH_CHALLENGE`/`AUTH_RESPONSE` (0x61/0x62, issue #4) let a host prove it
holds an operator private key: the device issues a single-use nonce, the host
signs `domain||device_serial||nonce` with the operator key, and the device
verifies the P-256 signature in the ATECC608A against the operator public key
`tfwprov` baked into the platform-reserved EEPROM block. A session granted this
way gates `EEPROM_READ` below `EEPROMReservedEnd`:

This was previously unconditional, since the region's contents (a hash, a
serial, and now an operator public key) were considered fine to disclose. A
public key does not need secrecy once trusted, but a host that has not yet
proven it holds the matching private key has no business confirming what the
anchor even is.

`RESET` mode 1 ("bootloader") is gated by the same session on the element tier
(`TFW_ERR_AUTH_REQUIRED` without one, checked before anything else about the
request, so an unauthenticated peer learns nothing from the answer). Mode 1
leaves a one-shot request that makes tfw-boot open its OTA session on a healthy
device (see [../secure-boot/api.md](../secure-boot/api.md#update-session-request)).
Mode 0 is never gated. What the gate protects is the entry; the session itself
is unauthenticated, and is bounded instead: it ends after an idle timeout -- in
empty polls and, independently, in bytes received, so a flood of junk is bounded
too -- that only accepted `OTA_*` frames restart, **and** after a ceiling nothing
restarts (60 s of loop iterations, or the application size plus framing and slack
in bytes), **and** after the first `OTA_END` that fails to verify. The ceiling
matters because an accepted frame is well-formed, not authenticated: a peer that
speaks the grammar can drip valid frames inside every idle window. The session
then falls through to a normal verified boot, and grants nothing, since every image is signature- and floor-checked. What a hostile peer can still do on the soft and `CRYPTO=none` tiers is destroy
the running application: pages written before the session ends are not restored
(single slot), so the device falls through into recovery and needs a signed
image. It can never make the device run an unsigned one. The
auth session is lost with the reset it triggers. On the soft and `CRYPTO=none`
tiers the gate is the permissive no-op, so any peer on the serial link can
request a session; that is the same accepted gap as the rest of this section
and its worst case is a 10 second delay per request plus a refused update.

A session lasts until the device resets; there is no separate inactivity
timeout.

> Item 79 (owner ruling 2026-10-17): the tfw-boot verifier is P-256 on every
> row; the 2560 soft row verifies with `BOOT_SIG=p256-soft` (`mk/boot.mk:350`).
> The paragraph below predates it.

**This is element tier only, and that is a real gap on the other two, not a
temporary one.** The software tier's only available primitive for an
external-key signature check, ed25519-donna (already vendored for
`BOOT_SIG=ed25519-donna`), does not fit this device family: its verify path
alone measures 72,930–85,452 bytes of flash and a single ~2.95 KB stack
frame, which overflows an ATmega328P's entire 32 KB of flash before the rest
of the firmware is counted, and is 31–47% over this project's own 64 KB
application-flash budget on an ATmega2560. No smaller alternative is vendored.
So on the soft and `CRYPTO=none` tiers, `AUTH_CHALLENGE`/`AUTH_RESPONSE` are
not compiled in at all (the same `TFW_ERR_UNKNOWN_CMD` any other absent
feature gets), and `EEPROM_READ` is consequently ungated there,
exactly as they always were. A verifier reading a soft-tier device's `CAPS`
already knows this — the feature bit is clear — and should not expect these
two resources to be protected on it.

## What the DeviceID key will sign

Whoever holds the serial line can ask the DeviceID key for a signature, so
what it signs is the whole of what that line can obtain. It signs three
structures and no others:

1. the `ATTEST` evidence frame, bytes `[0, 74)`, with a 16-byte host nonce;
2. the alias `TBSCertificate` (`ATTEST_CERT`), with a 32-byte host nonce;
3. a PKCS#10 `CertificationRequestInfo` (`ATTEST_CSR`, issue #30), with a
   32-byte host `b`.

**Only fixed templates with fixed-width host fields are ever signed.** Each
structure is built on the device from a template in its own flash and its own
state; the host's only input is a nonce whose width the dispatcher enforces,
inside a field that no value of it can resize or retag. No command signs a
host-supplied digest, and none takes a host-supplied subject, name or
extension — a signing oracle over arbitrary bytes is what this rule exists to
prevent. Adding a fourth structure, or any host field that is not fixed-width,
changes this section first.

The three cannot be confused for one another. The frame opens `0x01`, which no
DER SEQUENCE does; the TBS has its `[0]` version tag at offset 4 where the CRI
has INTEGER 0; and the CRI's empty subject `30 00` at offset 7 lies where a
TBS must carry an AlgorithmIdentifier with a mandatory OID. So a signature over
one is never a valid signature over another (`test/unit/test_csr.c` checks all
six directions).

What a hostile host *can* obtain from `ATTEST_CSR` is a CSR naming this device
— its own key, a subjectAltName `permanentIdentifier` holding the hex of its
own UEID, its own measurement — for any `b` it likes. That is a statement the
device makes about itself and nothing else. It proves possession of the key,
not authority to enrol it: the ACME server must still check that the SAN is
the identifier being finalized and that `b` is the value its own challenge
bound (`docs/dice-attestation/adr-possession-at-acme-finalize.md`). Only the
ATmega2560's element tier builds the command. The ATmega32U4 and ATmega328P do
not (#6: excluded where it puts the part over budget), so neither can prove possession at finalize at all.

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
