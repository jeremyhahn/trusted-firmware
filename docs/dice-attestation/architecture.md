# DICE attestation — architecture

> **Status: NORMATIVE** · Scope: trusted-firmware · Last verified: 2026-09-23
> Conflicts with this document are defects. Fix them here first.

## Ownership boundary

This repo owns the AVR wire protocol, the firmware that speaks it (`src/caps/attest.c`,
`src/caps/alias.c`, `src/caps/csr.c`, `src/caps/uccs.c`, `src/crypto/`), the host SDK for it
(`sdk/go`, Layer 0, zero in-house dependencies), and the `xkms` adapter module
(`github.com/jeremyhahn/trusted-firmware/xkms`).

go-xkms and go-quicbaas implement standards (TPM2, PKCS#11, TCG DICE, EAT/UCCS,
device-attest-01, RFC 9334) and define the plug-in interfaces. A firmware we wrote ships its xkms integration as an adapter
module in its own repo, registered through go-xkms's registry hook. The AVR
xkms backend is therefore this repo's, not go-xkms's.

It does **not** own DICE ASN.1, certificate profiles or
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
| `src/caps/csr.c` | `ATTEST_CSR`: fills the CertificationRequestInfo template and signs it, for possession at ACME finalize |
| `src/caps/tmpl.c` | the one template walker both DER documents are emitted through: a table of fixed runs and holes, hashed or windowed |
| `src/caps/uccs.c` | encodes the UCCS claims set carried inside the certificate and the request |
| `include/tfw/protocol.h` | wire constants for the three commands, both nonce widths, frame header |
| `sdk/go/attest.go` | host-side paging and nonce-width validation for both commands |

**Substituted at link time, like the HAL, never through a vtable.** One
provider is linked per image; a vtable would cost RAM and defeat
`--gc-sections`, and an image built for one tier cannot be talked into using
a root of trust it does not have. See `include/tfw/crypto.h`.

## The three profiles

| | DICE-HW | DICE-SW |
|---|---|---|
| Condition | an ATECC608 (the boards take the ATECC608C (MIKROE-6774 Click board)) | neither element nor TPM |
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

## What the DeviceID key signs

Three structures, and only these:

| | Command | Host contributes | Built in |
|---|---|---|---|
| the evidence frame, bytes `[0, 74)` | `ATTEST` 0x60 | a 16-byte nonce | `src/caps/attest.c` |
| the alias `TBSCertificate`, 383 bytes | `ATTEST_CERT` 0x63 | a 32-byte nonce | `src/caps/alias.c` |
| a PKCS#10 `CertificationRequestInfo`, 365 bytes | `ATTEST_CSR` 0x64 | a 32-byte `b` | `src/caps/csr.c` |

**The rule: only fixed templates with fixed-width host fields are ever
signed.** The device builds every byte it signs from a template in its own
flash and its own state; the host's only input is a nonce of a width the
dispatcher enforces, placed where no value of it can change a length, a tag
or the document's shape. No command signs a host-supplied digest, and none
takes a host-supplied subject, name or extension. A fourth structure, or a
host field of variable width, is a threat-model change first
(`docs/security/threat-model.md`).

The three are mutually unparseable, so a signature over one can never be
presented as a signature over another: the frame opens `0x01`, which is not a
SEQUENCE; the TBS carries its `[0]` version (`A0`) at offset 4, where the
CRI must have INTEGER 0; and the CRI's empty subject `30 00` at offset 7 sits
where a TBS needs its signature AlgorithmIdentifier, whose OID is mandatory.

`ATTEST_CSR` exists because ACME finalize wants a CSR signed by the key being
certified, and the device never sees the finalize request
([adr-possession-at-acme-finalize.md](adr-possession-at-acme-finalize.md),
option (c)). `sdk/go` hands back the 64-byte signature and stops there. go-xkms
rebuilds the CRI from the alias certificate and `b`, verifies the signature,
wraps it and submits it; the CRI names the device only by a
subjectAltName `permanentIdentifier` holding the lowercase hex of its full UEID
(type byte + DeviceID, 34 digits, go-xkms#1441), so a CSR for some
other name cannot be assembled from it. The CRI reads its key and evidence
headers out of the alias template, so the two documents cannot describe either
differently.

**Which parts can enrol.** Only the ATmega2560's element tier builds it. A part
is excluded from ACME enrolment (#6) where the
command puts it over budget: the ATmega328P's element image measured 16 884 /
16 384 bytes with it, so its profile turns it off (the alias certificate stays);
the ATmega32U4's profile turns it off too; and it is never built anywhere
`ATTEST_CERT` is not.

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

## Accepted: possession at ACME finalize

How the DeviceID key proves possession when an ACME order is finalized is
decided (issue #6) in
[`adr-possession-at-acme-finalize.md`](adr-possession-at-acme-finalize.md):
a device-templated PKCS#10, implemented as `ATTEST_CSR` (0x64, `src/caps/csr.c`).

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

## The Mega's runtime tier

Owner ruling of 2026-10-17 (CA m435, issue #130 criterion 1): the ATECC608C is
never required on an AVR board. The Mega 2560 links both the element provider
and the software DICE root, and at runtime runs the element when one answers,
else the soft root. The boot side (`feat/130-soft-root`) is not this section's.

Until now the tier was a build choice: `mk/firmware.mk:136`–`:207` picks one of
`CRYPTO=soft|none|atecc608a`, and each tier's source list brings its own
`tfw_crypto_init()` (`src/crypto/soft/provider.c:65`,
`src/crypto/atecc608a/provider.c:72`). `CRYPTO=auto` is a fourth value, the
2560 profile's default.

**One image, two providers, no vtable.** Both provider files define the same
eleven `tfw_crypto_*` symbols of `include/tfw/crypto.h`. Under `CRYPTO=auto`
each is compiled with a per-object `-D` rename list (`tfw_crypto_init` →
`tfw_crypto_soft_init` / `tfw_crypto_element_init`, and so on for all eleven),
so neither provider's source changes and neither carries a conditional. The
public names are then defined once, by `src/crypto/auto/provider.c`: one
`uint8_t` chosen at init, and in each entry a single branch to the soft or
element function. That is cheaper than a `const` table in flash: a table makes
every entry a live reference through a pointer load, the branch costs a few
words per entry and leaves calls the compiler can see. `--gc-sections` keeps
both providers' entries (both are reachable from the dispatch) and still drops
every peripheral and capability nothing calls, as it does today. The cost is
one byte of RAM and the dispatch's branches.

**The probe runs once, at `tfw_crypto_init`, and the choice never changes until
reset.** The dispatch probes before either provider's init runs, because the
element provider drains the boot handoff's CDI unconditionally
(`src/crypto/atecc608a/provider.c:106`) and the soft provider needs that CDI
when tfw-boot derived it from the soft UDS (`src/crypto/soft/provider.c:115`).
The probe is the element provider's own presence test: the I2C bus at
`TFW_ATECC_I2C_HZ`, the wake answer (`src/crypto/atecc608a/atecc.c:77`), and an
`Info(Revision)` that must return with a good CRC, then sleep. All three pass →
the element provider's init runs, and the element is chosen only if it then
reports `TFW_CRYPTO_SECURE_ELEMENT`. Any of them fails — no bus, no wake, a
wake with a failed or corrupt Info, the half-way case — → only the soft
provider's init runs. There is no mix: every later call goes to the one
provider chosen, and an element that disappears afterwards fails its calls
with `TFW_ERR_CRYPTO` rather than falling over to the soft key. The one race,
an element that answers the probe and not its own init a moment later, falls
to soft after the element provider has drained the handoff's CDI. It cannot
sign under a wrong key: the drain clears only the CDI, the handoff's tier
stays readable (`src/core/handoff.c:89`–`:102`), so the soft provider either
finds a handoff naming its own tier with no CDI left and stays unprovisioned,
finds one naming the element and refuses it, or finds none and derives from
its own UDS (`src/crypto/soft/provider.c:116`–`:129`).

**The evidence states the tier.** `SYS_INFO` writes `tfw_crypto_tier()`
(`src/caps/sys.c:112`), which the dispatch answers from the chosen provider:
`TFW_CRYPTO_SOFTWARE` from the soft root (`src/crypto/soft/provider.c:162`),
`TFW_CRYPTO_SECURE_ELEMENT` only from an element that answered
(`src/crypto/atecc608a/provider.c:169`). `tfw_crypto_algorithms()` is the
chosen provider's too, so a soft-rooted Mega advertises HMAC-SHA256 only and
signs with `TFW_ALG_HMAC_SHA256`. Its `tfw_crypto_pubkey` refuses, so no alias
certificate can name an element key for it.

**The element path is whole.** An `auto` image links everything the
`atecc608a` image links (`mk/firmware.mk`, the `atecc608a auto` branch): the
alias certificate, the CSR, the auth session and, where the profile opts in,
the SUIT reader. Each reaches the element through the dispatch, so with the
soft root chosen they refuse as that tier must (no public key, no image key,
no TRNG). The auth gate is `src/core/auth_session_auto.c`: `auth_session.c`'s
operator-signature gate, renamed per object, when the dispatch chose the
element, and the soft tier's permissive answer when it chose soft. The ATTEST
refusal of a CDI handed off for the other tier stays where it is, in the soft
provider's init (`src/crypto/soft/provider.c:116`–`:129`), and runs only when
the dispatch chose soft; the element provider never derives from the handoff.

No image writes DiceTcbInfo vendorInfo today (the alias template carries none),
so the `.24.1`/`.24.2` statement is not yet emitted by any tier. No wire byte and no status code changes.

**Cost.** `make size-check BOARD=atmega2560` (the default is now `CRYPTO=auto`)
against `make size-check BOARD=atmega2560 CRYPTO=soft` and `CRYPTO=atecc608a`
gives the 2560's flash and RAM for each, measured in lane-verify.

**The boot harness.** `integration-test-boot`'s first row (`mk/boot.mk`) builds
its application `CRYPTO=soft` by name: `test_signed_image_boots` proves a soft
application refuses the element-derived CDI tfw-boot hands off, and an `auto`
image on that bus finds the element and rightly takes the element's path
(one more element HMAC at init, and an ATTEST that signs).

The bridge row (`boot-bridge-run`) is `CRYPTO=soft` as well. Its test,
`TestOTAStageThroughTfwBoot`, checks the stager through tfw-boot. On an element
tier, OTA_ACTIVATE takes the operator session gate (`tfw_auth_gate()`, called
by the handler itself). There is no list of gated commands. The harness bakes
no operator key, so no session could be opened there, and the gate stays.

**The rollback floor.** OTA_INFO's floor follows the boot's tier, not the
application's dispatch. `src/core/ota_floor_auto.c` reads the `tier` byte
tfw-boot wrote into the handoff (offset 5, `include/tfw/boot.h:285`):

- an element boot reports the element's boot counter;
- a soft boot reports the platform block's EEPROM floor;
- with no handoff, or an element counter that cannot be read, the answer is
  `TFW_ERR_CRYPTO`, never the EEPROM copy.

The reason is that tfw-boot enforces the floor of its own tier, so the floor
OTA_INFO reports must be the one enforced. Suppose an element boot is followed
by an application probe that fails transiently and chooses soft. That
application still reports the counter, because the EEPROM copy can be stale
(`src/core/ota_floor.h`) while the counter is the floor in force.
`ota_floor_atecc.c` and `ota_floor_eeprom.c` are linked side by side with
`FLOOR_RENAME`.

The application only reads the floor. tfw-boot enforces it and advances it at
settle (decision 5), on its own build-time tier, and it never decrements either
store. If the tier changes between boots (an element fitted or removed), the
boot's tier, and with it the store OTA_INFO reads, is still tfw-boot's. The
floor that is enforced does not move, and it never moves backwards.

## What this deliberately does not do

- **No key material crosses the wire, ever.** No command exports a private
  key; the protocol transports evidence, public keys and certificates only.
- **No CryptoAuthLib.** A minimal purpose-built ATECC608A driver is linked
  instead — licence-incompatible with this project's AGPL-3.0-or-later, and
  would not fit regardless. See `../attestation/cryptoauthlib-evaluation.md`.
- **No end-to-end channel binding.** The gateway's transport security
  terminates at the gateway; the binding covers the device-to-gateway segment
  only. Recorded in the threat model rather than papered over here.
