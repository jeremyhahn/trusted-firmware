# DICE profiles

> **Status: NORMATIVE** · Scope: `src/crypto/`, `include/ag/crypto.h`,
> `src/caps/attest.c` · Last verified: 2026-09-22
>
> The conformance tables below are claims about what this firmware meets. A gap
> listed as "partial" is partial; do not round it up in a datasheet, a README or
> a certificate.

## The vocabulary problem, first

This firmware implements **DICE-based device identity with a hardware root of
trust**. It is not "TCG DICE compliant" and that phrase must not appear.

The difference is not modesty. Several of the DICE Hardware Requirements are met
only procedurally on a classic AVR — by lock bits and a build convention rather
than by architecture — and a verifier that was told "compliant" would be
entitled to assume otherwise. `DiceTcbInfo.OperationalFlags.notSecure` is set
whenever the tier is DICE-SW, and the tier itself is reported in `CAPS` **before**
a host decides what any evidence is worth.

## Three profiles

| | DICE-TPM | DICE-HW | DICE-SW |
|---|---|---|---|
| Condition | a discrete TPM 2.0 | an ATECC608A | neither |
| UDS | TPM seed | a never-readable element slot | boot-section flash, BLB mode 3 |
| CDI | PCRs + seed | HMAC inside the element | software HMAC-SHA256 |
| DeviceID | `TPM2_CreatePrimary` | element slot 0, non-exportable — the DICE **ECA** key | — |
| Alias | n/a | software, from the CDI, per boot, SRAM only, zeroized | symmetric HMAC |
| Signs with | per the TPM | ECDSA P-256 | HMAC-SHA256 |
| `AttestLevel` | `Hardware` | `Hardware` | `Software` |
| `CAPS` tier | `AG_CRYPTO_TPM2` | `AG_CRYPTO_SECURE_ELEMENT` | `AG_CRYPTO_SOFTWARE` |
| Built with | — | `CRYPTO=atecc608a` | `CRYPTO=soft` (default) |

The tier is a **link-time** choice, exactly as the HAL is. One provider is
linked per image, there is no runtime selection and no vtable, so an image built
for the software tier cannot be talked into using an element it does not have —
and `--gc-sections` keeps working.

## Why DICE-HW is shaped the way it is

The obvious design derives the signing key from the CDI, as DICE describes. The
ATECC608A cannot do that: it has no command that turns an arbitrary 32-byte
secret into a P-256 private key inside the part, and a key derived outside it
would be a key that left it — the one thing this tier exists to prevent.

DICE's own layering routes around it, and we follow that rather than invent
something:

> *"The DeviceID key … is used to sign one or more certificates for keys
> generated on behalf of a higher TCB layer. Therefore, the DeviceID key is an
> ECA Key."* — DICE Layering Architecture §8.1.1.5

So the DeviceID key is element-resident, never leaves slot 0, and acts as the
ECA. The measurement binding lives where DICE puts it — in the CDI, computed as
`HMAC(UDS, H(firmware))` by the element's own SHA/HMAC engine, so the UDS never
crosses the bus. That is form (2) of DICE HW Requirements r78 §5, which TCG
calls the stronger of the two: an HMAC's security is the full width of its key
where a bare hash's is half its digest.

**DeviceID sub-options.** A1 (default) generates the DeviceID randomly in the
element at provisioning; the binding lives in the per-boot Alias key and a
bootloader update does not break identity. A2
(`CONFIG_DICE_STRICT_DEVICEID`) derives it from the CDI at provisioning and
`PrivWrite`s it into a locked slot — full layer-0 binding, at the cost that a
bootloader update forces re-provisioning.

## Conformance, met and unmet

| DICE HW Requirements r78 | DICE-HW | DICE-SW |
|---|---|---|
| §6.2 exclusive read access to a UDS | **met** | not met |
| §6.2.1 the DICE **SHALL** be immutable | **partial** — BLB mode 3, lock bits, `DWEN` off; procedural | partial |
| §6.4 UDS access disabled before first mutable code | **met in effect** — it never leaves the element | not met |
| §6.4 erase values that could determine the UDS | **met** — the CDI is zeroized after Alias derivation | best effort |
| §6.4 CDI where first mutable code has exclusive access | **partial** — it transits SRAM; no classic AVR has an MPU | not met |
| Layering §6.2.4.1(6) secure entropy source in the HRoT | **met** — the element's TRNG | **not met** |
| §8.2.1 key generation deterministic from the CDI | **met** for Alias; A2 for DeviceID | met |

The two partials are the honest cost of the part. There is no MPU on a classic
AVR, so "first mutable code has exclusive access to the CDI" cannot be enforced
by hardware; the CDI is zeroized immediately after use and that is the whole
mitigation. Immutability rests on lock bits and a fuse setting verified at
provisioning, which is a procedure someone can skip.

## What an unprovisioned device does

It refuses, and it says which kind of nothing it has.

- **No element fitted**: `ag_crypto_tier()` reports `AG_CRYPTO_NONE`, the
  algorithm bitmap is empty, and every operation returns `AG_ERR_CRYPTO`.
- **Element fitted, no UDS**: the tier is still `AG_CRYPTO_SECURE_ELEMENT` —
  truthfully, because the host's decision depends on knowing an element is there
  and empty — and signing returns `AG_ERR_NOT_PROVISIONED`.
- **Software tier, default UDS**: refuses to sign at all.

A device that signed anyway would produce evidence verifying against a key every
build of this source shares: evidence that proves nothing while looking exactly
like evidence that proves something.

## Algorithms

`CAPS` reports a bitmap of what the provider can actually perform, and the
device reports it truthfully. DICE-HW advertises ECDSA P-256 and HMAC-SHA256 and
**not Ed25519**, because the ATECC608A has no hardware Ed25519.

This matters more than it looks. go-xkms's `BuildAlgorithmsResponse` advertises
Ed25519 for every backend whose `caps.Keys` is true — which falsely includes
TPM2 and three cloud KMSes — and the consequence of that class of bug is that a
host negotiates down to an algorithm the device then fails to perform, at the
point where it matters most.

Ed25519 was retained for one job: **bootloader image verification**. That
choice rested on a claim this document used to make -- "ed25519-donna's verify
path is about 2 KB" -- and the claim was wrong by a factor of thirty-six.

Measured with `make bootloader-budget`: avr-gcc 14.2.0 at `-Os` with
`--gc-sections` and nothing reachable but `ed25519_sign_open` produces **72 930
bytes** of flash and a **2 950-byte** stack frame. The ATmega328P's boot section
is 4 096 bytes -- which is its largest `BOOTSZ`, so there is nothing to raise --
and the part has 2 048 bytes of SRAM in total.

No signature primitive currently ships in the boot section, and no substitute
has been quietly chosen. `docs/security/bootloader.md` records the measurement,
what else does not fit alongside it, and the options with their costs.

## Evidence

```
[Version:1][Nonce:16][DeviceID:16][FWHash:32][BootCount:4][UptimeMs:4][AlgID:1][Signature:64][Reserved:26]
```

164 bytes, delivered in 48-byte pages because no frame this protocol defines
carries it whole.

- **`UptimeMs`, not a timestamp.** No AVR here has a real-time clock. Freshness
  is *always* the verifier's nonce; any design that needs the device to know the
  date is wrong.
- **`BootCount`** is authoritative from the element's monotonic counter, and the
  same counter is the firmware version floor for anti-rollback. The part has two
  counters of about two million increments each — a documented device-lifetime
  bound, not an unbounded one.
- **`AlgID`** says which primitive signed, so a verifier does not have to know
  out of band. Both ECDSA P-256 `r‖s` and Ed25519 `R‖S` are exactly 64 raw
  bytes, so the layout does not change with the algorithm — only its
  interpretation does.

## `FWHash` is why the build must be reproducible

The hash is signed evidence, so a verifier needs a golden measurement it can
compute **from source**. That is a requirement on the build system, not a nice
property: `SOURCE_DATE_EPOCH`, `-ffile-prefix-map`, `-frandom-seed`, a sorted
deterministic link order, and no `__DATE__` or `__TIME__` anywhere — enforced by
a grep in `make lint`. `make reproducible-check` builds twice in different
directories and fails if the images differ.

## Protected resources

"Generic primitives" and "hardware root of trust" conflict unless a set of
resources is carved out. Without this, the generic EEPROM write would let any
host overwrite the bootloader's public key hash. Lock bits stop external ISP
access only — never application code.

| Resource | Rule |
|---|---|
| EEPROM `0x000`–`0x07F` | platform-reserved; writes refused `AG_ERR_PROTECTED_REGION` |
| EEPROM `0x080`+ | user area, freely accessible |
| I²C address `0x60` | refused by the generic `I2C_XFER`, fitted or not — a build that allowed it when absent would allow it the moment one was added |
| TPM chip-select | refused by `SPI_XFER` and by GPIO writes |
| Boot-section flash | unreachable by any primitive; **no BLS callable is exposed**, because one trampoline voids the BLB read barrier |
| `RESET(1)` into the bootloader | authenticated session once attestation exists |

## What this deliberately does not do

- **No key material crosses the wire, ever.** The protocol transports evidence
  and public keys. There is no command that exports a private key, and adding
  one would end the tier's meaning.
- **No DICE device joins the public DHT directly.** Admission there needs a
  manufacturer EK that cannot be minted, and a self-provisioned DICE identity is
  cheap to mint if the provisioning CA leaks. DICE devices attest locally and
  reach the network through a gateway that **relays device-signed evidence
  verbatim and never re-signs it** — so a compromised gateway can lie about
  which devices exist, never about what a given device measured.
- **No end-to-end channel binding.** The gateway's mTLS terminates at the
  gateway, so the binding covers the serial segment. Recorded here and in the
  threat model rather than papered over.
