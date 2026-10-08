# DICE attestation — architecture

> **Status: NORMATIVE** · Scope: trusted-firmware · Last verified: 2026-09-23
> Conflicts with this document are defects. Fix them here first.

## Ownership boundary

This repo owns the AVR wire protocol, the firmware that speaks it (`src/caps/attest.c`,
`src/caps/alias.c`, `src/caps/uccs.c`, `src/crypto/`), and the host SDK for it
(`sdk/go/attest.go`). It does **not** own DICE ASN.1, certificate profiles or
attestation verification — those belong to go-xkms. It does not own device
enrolment, the device registry or fleet policy — those belong to
go-trusted-platform. A gap in either is filed against that repo, never worked
around here.

Language discipline: this firmware implements **DICE-based device identity
with a hardware root of trust**. It is never described as "TCG DICE
compliant" — several DICE Hardware Requirements are met only procedurally on
a classic AVR (lock bits, a build convention), and "compliant" would entitle
a verifier to assume otherwise.

## Components

| File | Responsibility |
|---|---|
| `include/tfw/crypto.h` | the provider contract: `tfw_crypto_init/tier/algorithms/random/sha256/hmac/sign/pubkey/counter` |
| `src/crypto/soft/provider.c` | DICE-SW: HMAC-SHA256 signing, UDS in boot-section flash (`uds_flash.c`) |
| `src/crypto/atecc608a/provider.c` | DICE-HW: ECDSA P-256 signing, UDS in a never-readable element slot |
| `src/crypto/none/provider.c` | a fourth, build-time tier for silicon too small to hold the software tier's contract honestly (ATtiny85 has no boot section, no BLB lock bits) — always `TFW_CRYPTO_NONE`, every operation `TFW_ERR_UNSUPPORTED` |
| `src/caps/attest.c` | `ATTEST`: builds and pages the 164-byte evidence frame |
| `src/caps/alias.c` | `ATTEST_CERT`: builds the TBSCertificate, mints and pages the alias certificate |
| `src/caps/uccs.c` | encodes the UCCS claims set carried inside the certificate |
| `include/tfw/protocol.h` | wire constants for both commands, both nonce widths, frame header |
| `sdk/go/attest.go` | host-side paging and nonce-width validation for both commands |

**Substituted at link time, like the HAL, never through a vtable.** One
provider is linked per image; a vtable would cost RAM and defeat
`--gc-sections`, and an image built for one tier cannot be talked into using
a root of trust it does not have. See `include/tfw/crypto.h`.

## The three profiles

| | DICE-HW | DICE-SW |
|---|---|---|
| Condition | an ATECC608A | neither element nor TPM |
| UDS | never-readable element slot | boot-section flash, BLB mode 3 |
| CDI | `HMAC(UDS, H(firmware))` inside the element | software `HMAC(UDS, H(firmware))` |
| Signs with | ECDSA P-256 (element) | HMAC-SHA256 (verifier holds the key) |
| `CAPS` tier | `TFW_CRYPTO_SECURE_ELEMENT` (2) | `TFW_CRYPTO_SOFTWARE` (1) |
| Built with | `CRYPTO=atecc608a` | `CRYPTO=soft` (default) |

`tfw_crypto_tier_t` also reserves `TFW_CRYPTO_TPM2` (3) for a discrete TPM; no
`src/crypto/tpm2/` implementation exists in this tree today. That value is a
reserved wire slot, not a shipped tier — do not document behavior for it.

Conformance is precise about what is **not** met: no classic AVR has an MPU,
so "first mutable code has exclusive access to the CDI" (DICE HW Requirements
r78 §6.4) cannot be enforced by hardware on either tier — the mitigation is
that the handoff is zeroized promptly, not that the requirement is met.
Immutability (§6.2.1) rests on BLB lock bits and a fuse setting verified at
provisioning, which is procedural. Full conformance detail moves to
`engineering.md`; do not restate the table there.

## What an unprovisioned device does

It refuses and says which kind of nothing it has: `TFW_CRYPTO_NONE` reports an
empty algorithm bitmap and every crypto operation returns `TFW_ERR_CRYPTO`; an
element that is present but unseeded still reports `TFW_CRYPTO_SECURE_ELEMENT`
(truthfully — a host's decision depends on knowing an element is there) and
signing returns `TFW_ERR_NOT_PROVISIONED`. A device that signed anyway would
produce evidence that verifies against a key every build of this source
shares — evidence that proves nothing while looking like evidence that
proves something.

## The DeviceID/Alias split

DICE layering asks for a per-boot Alias key, derived from the CDI, whose
derivation itself binds the measurement into the key. The ATECC608A cannot do
that: it has no command that turns an arbitrary 32-byte secret into a P-256
private key inside the part, and a key derived outside it is a key that left
it — the one thing this tier exists to prevent (`src/crypto/atecc608a/provider.c`).

So this firmware follows DICE's own layering instead of inventing a
derivation: the DeviceID key is element-resident (slot 0), never leaves the
part, and acts as the DICE ECA key. **The alias certificate's subject public
key is that same DeviceID point** (`tfw_alias_parts_t.pubkey`, filled by
`tfw_crypto_pubkey()` in `src/caps/alias.c`) — there is no distinct, separately
derived Alias private key in this implementation. "Alias" here names a
certificate role (`CN=tfw Alias`, minted fresh per challenge with its own
serial and UCCS claims), not a second key.

The consequence is stated, not hidden: TCG DICE Attestation Architecture
r23 §6.1.4 asks that non-constant elements of the UCCS extension contributed
to the CDI that generated the subject key. The `ueid` and firmware
measurement do — they are DICE inputs, fixed before the CDI exists. **The
nonce does not**, because the subject key is not re-derived per challenge.
What binds the nonce instead is the certificate **signature** over a TBS that
contains it, which is the protection §6.1.4 itself names for UCCS contents.
The freshness property a verifier needs holds; the key-derivation property
the letter of the sentence asks for does not. See `api.md` for the exact TBS
layout this produces.

## The nonce channel: `tcg-dice-UCCS-evidence`

The verifier's enrolment challenge travels in **`tcg-dice-UCCS-evidence`**
(OID `2.23.133.5.4.6`, TCG DICE Attestation Architecture r23 §6.1.4), inside
the alias certificate, as the CWT claim `nonce` (key `10`). Alongside it:
`ueid` (key `256`, a type byte plus the 16-byte device id) and `measurements`
(key `273`, a CoSWID naming the firmware hash). All three keys were read from
the IANA CWT/EAT claims registry via RFC 9711 §10.2, not assumed —
`src/caps/uccs.c`. `api.md` has the exact CBOR.

## The evidence frame

```
[Version:1][Nonce:16][DeviceID:16][FWHash:32][BootCount:4][UptimeMs:4][AlgID:1][Signature:64][Reserved:26]
```

164 bytes total, `include/tfw/protocol.h`. Exact offsets in `engineering.md`
and `api.md`; not restated here.

## Freshness is always the verifier's nonce

No board in this family has an RTC. `UptimeMs` is elapsed time since boot,
not a timestamp, and the certificate's `notBefore`/`notAfter` are the RFC
5280 §4.1.2.5 "no well-defined expiration" sentinel values — they carry no
information. Any design that needs the device to know the date is wrong for
this hardware; freshness comes from the nonce alone, checked by the verifier
that issued it.

## Data flow status: the bootloader handoff is wired up

`include/tfw/boot.h` specifies a CDI handoff block (`TFW_BOOT_HANDOFF_*`): a
trusted bootloader computes the real firmware measurement and CDI and hands
them to the application once, over a fixed SRAM block, then both sides
zeroize their copy. `src/core/handoff.c` is the application's read-only side
of it (`tfw_boot_handoff_init`, called once at startup, before
`tfw_crypto_init`): it parses the block with `boot/boot_handoff.c`'s own
decoder, copies out what a valid one held, and zeroizes the raw block. Fixed
SRAM placement is a linker fact, not a struct: `mk/arch/avr.mk` derives the same
address `mk/boot.mk` does from the board's `RAM_END`, and moves the
application's own stack below it — a board with no boot section (the
ATtiny85) links a zero-cost stub instead, since there is no bootloader there
to hand anything off.

`src/caps/attest.c`'s `tfw_attest_device_id()` reads the platform-reserved
EEPROM block through `src/core/platform_id.c` (a read-only accessor; see
`docs/device-provisioning/` for how `tfwprov --bake-eeprom` puts a serial
there before the device is ever flashed), and `tfw_attest_fw_hash()` reads
`tfw_boot_handoff_measurement()` directly rather than recomputing anything.
Both stay zero, as they always did, wherever their source is absent — an
unprovisioned platform block, a stock bootloader, or no bootloader at all.

The crypto providers' own CDI derivation is a SEPARATE concern from
`FWHash`, and only the software tier changed: `src/crypto/soft/provider.c`
now prefers `tfw_boot_handoff_take_cdi()` — the real
`HMAC(UDS, the measurement tfw-boot verified)` — falling back to its own
UDS-plus-placeholder derivation only when no valid handoff exists (issue
#7). The element tier's own CDI probe is unaffected: it never signed with
the CDI to begin with (see "The DeviceID/Alias split" above), so there was
nothing to wire up there beyond zeroizing it — see `engineering.md`'s
invariants.

**Not wired up: an Alias keypair on the software tier.** Issue #7 asked for
one, Ed25519, derived from the CDI, held in SRAM, certified by
`ATTEST_CERT`. It is not implemented: the alias certificate's fixed 232-byte
TBS template (`src/caps/alias.c`) hard-codes an ECDSA P-256
`subjectPublicKeyInfo` (a 65-byte uncompressed point at a fixed offset) and
an `ecdsa-with-SHA256` `signatureAlgorithm` — an Ed25519 key (32-byte raw
point, OID `1.3.101.112`) and an Ed25519 signature (64 bytes raw, not an
ASN.1 `SEQUENCE` of two integers) cannot be expressed inside that template
without a different certificate profile, and this repo does not own DICE
ASN.1 profiles (see the top-level `CLAUDE.md`). Raised against the owner
rather than guessed at; `ATTEST_CERT` on the software tier still answers
`TFW_ERR_UNSUPPORTED`, exactly as it did before, for exactly the reason
stated where `mint()` fetches the subject key.

## Alternatives rejected

- **Nonce in `DiceTcbInfo.vendorInfo` instead of UCCS.** `vendorInfo` is an
  OCTET STRING whose meaning is the vendor's, so a nonce there is a private
  convention every verifier must be told about. `tcg-dice-UCCS-evidence` is a
  registered extension carrying registered EAT claims, readable by a verifier
  that has never heard of this product. `docs/dice-attestation/architecture.md`
  (superseded by `api.md` in this folder) has the full argument.
- **A static, randomly generated attestation key instead of DICE.** A key
  generated once and never bound to a measurement proves the device's
  identity but not what it is running: a device reflashed with different
  firmware attests identically. DICE's CDI gates signing on
  `HMAC(UDS, H(firmware))`, so the root of trust the device reports in `CAPS`
  and the evidence it signs come from the same measurement chain.

## Trust boundaries

A DICE device does not join the wider network directly — admission there
needs a manufacturer-issued endorsement this firmware cannot mint, and a
self-provisioned identity is cheap to mint if the provisioning CA leaks.
Devices attest locally and reach the network through a gateway that **relays
device-signed evidence verbatim and never re-signs it**: a compromised
gateway can lie about which devices exist, never about what a given device
measured. The gateway's own attestation, admission policy and the public
verification path are go-xkms's and go-trusted-platform's, not this repo's —
see `docs/security/threat-model.md` for the boundary as drawn on this side.

## What this deliberately does not do

- **No key material crosses the wire, ever.** No command exports a private
  key; the protocol transports evidence, public keys and certificates only.
- **No CryptoAuthLib.** A minimal purpose-built ATECC608A driver is linked
  instead — licence-incompatible with this project's AGPL-3.0-or-later, and
  would not fit regardless. See `../attestation/cryptoauthlib-evaluation.md`.
- **No end-to-end channel binding.** The gateway's transport security
  terminates at the gateway; the binding covers the device-to-gateway segment
  only. Recorded in the threat model rather than papered over here.
