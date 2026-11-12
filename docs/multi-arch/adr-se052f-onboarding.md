# ADR: onboarding the NXP EdgeLock SE052F

> **Status: PROPOSED** · Issue: none yet · SPEC row: `secure-element-se05x`
> (future) · Scope: trusted-firmware (the on-device driver only); go-enclave
> (the host-side backend, its row `se052f-secure-element`, named, not
> designed) · Related: [`architecture.md`](architecture.md) §6,
> [`adr-optiga-trust-m-onboarding.md`](adr-optiga-trust-m-onboarding.md),
> [`../cortex-m/adr-reference-part-and-emulation.md`](../cortex-m/adr-reference-part-and-emulation.md) (#14),
> [`../cortex-m/adr-trust-model-vs-tf-m.md`](../cortex-m/adr-trust-model-vs-tf-m.md) (#15),
> [`../development/bench/se052f.md`](../development/bench/se052f.md) ·
> Last verified: 2026-11-12
>
> Documentation only. Owner ruling 2026-10-17 05:51Z, queue item 26: each
> arrived board gets one docs-only onboarding ADR. This ADR changes no code, no
> `mk/` file, no board profile and no wire vector, names no command id and no
> status code, and decides nothing the owner has not ruled: every choice is an
> open question below.
>
> Written with the standards filed but not yet read. The SE052F data sheet,
> NXP's APDU specification (AN12413) and the T=1 over I2C specification are
> entered in [`../reference/README.md`](../reference/README.md) at no revision:
> nxp.com answered 404 to each on 2026-11-12. NXP's plug-and-trust middleware
> is pinned there at `v04.07.01`, for cross-checking values only, and its
> `README.rst` and `ChangeLog.md` are read. Every other fact below is what the
> part is *expected* to be, marked *to confirm*, with the document it is
> confirmed against.

## The question

The SE052F is first in ruling 269's hardware order (the SE052F, then the i.MX
95 M7, then the STM32H5), yet it is the one element without an onboarding ADR:
the STM32H5 (#126), the OPTIGA Trust M (#127), the RP2350 (#128) and the
nRF54L15 (#129) each have one. It is a secure element, not a host MCU: an
element tier beside the ATECC608C (MIKROE-6774) and the OPTIGA Trust M, on a
host that already runs the firmware core. What is it for, what is its root of
trust, how is its driver layered, where does it sit against #14 and #15, and
how is it brought onto the bench without changing it?

## 1. What the board is for, and its SPEC row

- **SPEC row `secure-element-se05x` (future):** "The element tier on the NXP
  EdgeLock SE052F, next to the ATECC608C (MIKROE-6774 Click board) tier." It
  serves that row and no other.
- **Purpose here:** a second element vendor after Microchip, first in the
  hardware order. It holds a DeviceID key in hardware and signs the alias
  certificate and UCCS evidence the firmware already emits
  (`docs/dice-attestation/`), as the ATECC608C does today.
- **Ownership, as the row states:** the on-device C driver is ours; the
  host-side Go backend is go-enclave's (its row `se052f-secure-element`).
  go-xkms sees only evidence and the NXP-issued device certificate. This ADR
  designs nothing on the host side and imports nothing from go-enclave.
- **No element becomes mandatory.** CA m435: no element is required, and the
  software DICE root stays the default (owner ruling, 2026-10-17 05:51Z).
- **Which boards, as the row states:** the ATmega2560, the ATmega32U4 and the
  Cortex-M targets, not the ATmega328P (§3, the fit estimate).

## 2. Root of trust

### What the element holds

Vendor documents, all in [`../reference/README.md`](../reference/README.md):

- NXP, *EdgeLock SE052F* data sheet: not read; revision and sections *to
  confirm*.
- NXP, *AN12413, SE05x IoT applet APDU Specification*: not read; the revision
  that matches the SE052F's applet *to confirm*.
- NXP, *UM11225*, or GlobalPlatform's *APDU Transport over SPI/I2C*: not read;
  which one NXP names for the SE052F *to confirm* (open question 8).
- NXP, plug-and-trust `v04.07.01`: `README.rst` and `ChangeLog.md` read.

| Item | What it is expected to be | Status |
|---|---|---|
| Host interface | An I2C target speaking T=1 over I2C | In part: plug-and-trust's `README.rst` ("Introduction") connects to the SE05x "using t1oi2c protocol". Any other interface *to confirm* (data sheet) |
| Factory key | One or more NXP-provisioned key pairs, generated and locked at personalisation, in reserved objects whose policy decides what they may sign | *to confirm* (data sheet; AN12413). Which one is the DeviceID key is open question 2 |
| Factory certificate | An NXP-issued X.509 certificate for that key, in a reserved data object, readable without a session | *to confirm* (data sheet; AN12413) |
| Key and data objects | Objects created by the host, each with an access policy set at creation; a private key never leaves | *to confirm* (AN12413) |
| Monotonic counter | A counter object the host creates and increments; whether one exists from the factory is not known | *to confirm* (AN12413). Creating one is a write (open question 3) |
| Lifecycle and locks | Applet-wide lock and reset operations, and per-object policies; some are irreversible | *to confirm* (AN12413). None is ever run without the owner (open question 4) |
| Bus protection | A platform SCP03 session binding host and element, keyed by secrets the host must hold | *to confirm* (AN12413; data sheet). Not used here (open question 5) |
| Supply | The voltage range the element and the board accept, which decides whether a 5 V AVR host needs a level shifter | *to confirm* (data sheet) |
| Reset and power | A T=1oI2C deep power down for the SE052, and a reset logic chosen by applet version | Confirmed as middleware features only: plug-and-trust `ChangeLog.md`, v04.05.01 and v04.07.00. The part's own behaviour *to confirm* (data sheet) |

### How it compares with the other element tiers

| | ATECC608C (AVR rows today) | OPTIGA Trust M V3 | SE052F (this row) |
|---|---|---|---|
| Bus | I2C | I2C, IFX I2C protocol | I2C, T=1 over I2C (*to confirm* which specification) |
| DeviceID key | hardware-resident P-256 | factory key `0xE0F0` | an NXP-provisioned factory key, which one open question 2 |
| Factory certificate | per the TrustFLEX slot map | Infineon-issued | NXP-issued (*to confirm* where) |
| Public key readable | yes | no (`TFW_ERR_UNSUPPORTED`) | expected yes, by a read of the key object (*to confirm*) |
| Anti-rollback | the element's monotonic counter | the element's counter objects, read only | a counter object, if one exists without a write (open question 3) |
| Locking | per-slot and zone locks | per-object lifecycle state | applet locks and object policies (open question 4) |

### Mapping onto TCG DICE

The DICE standards are listed, not copied, in
[`../reference/README.md`](../reference/README.md). The element maps as the
ATECC608C and the OPTIGA Trust M do
(`docs/cortex-m/adr-trust-model-vs-tf-m.md`, Context): the factory key is a
hardware-resident DeviceID key, *not* derived from a CDI; the alias key is the
DeviceID key; the measurement is bound by the signature over the claim, not by
key derivation. Where the UDS lives, and so where the CDI comes from, is open
question 6.

## 3. The driver's layering

The driver follows `src/crypto/optiga/`, three layers each tested on the host
against recorded frames, under a new `src/crypto/se05x/`. Nothing below is
written; it is the shape a later slice writes to the filed standards.

1. **Transport, T=1 over I2C** (as `optiga_i2c.c`): the block framing, the
   sequence numbers, the waiting-time extension the element asks for, and the
   chaining of an APDU longer than one information field, over the existing
   `tfw_hal_i2c_init`, `tfw_hal_i2c_xfer` and `tfw_hal_delay_us`, so
   `include/tfw/hal.h` gains nothing. One master, no SCP03 (question 5). Its
   frame layout, check sequence, maximum information field, timings and
   address come from the T=1 over I2C specification NXP names (question 8),
   checked against plug-and-trust's T=1oI2C stack, never copied from it.
2. **Command layer, read-only** (as `optiga_cmd.c`): select the applet, then
   the APDUs of AN12413 that read and do not write: get random, read an
   object's data and its public part, read its attributes, sign a digest with
   an existing key, verify. The status words are mapped onto the existing
   status codes; no status code is added. No create, write, delete, lock or
   reset command is reachable, which its tests prove, as
   `test/unit/test_optiga_cmd.c` does for the OPTIGA.
3. **Provider** (as `optiga_crypto.c`): `include/tfw/crypto.h` mapped onto the
   read-only commands:
   - `tfw_crypto_random`: the element's random.
   - `tfw_crypto_sign`: ECDSA over a SHA-256 digest with the factory key the
     owner names (question 2), r‖s decoded from the element's DER signature.
     If no factory key's policy allows a sign over a host digest, sign needs a
     key generated on the element, a write, and stays `TFW_ERR_UNSUPPORTED`
     until the owner rules (question 3).
   - `tfw_crypto_pubkey`: expected readable from the key object, unlike the
     OPTIGA, which would let the alias certificate builder link on this tier
     (*to confirm*, AN12413).
   - `tfw_crypto_counter`: a read of an existing counter object. If the part
     ships none, `TFW_ERR_UNSUPPORTED`, and OTA_INFO's rollback floor stays the
     platform block's, as on the OPTIGA tier.
   - `tfw_crypto_verify_operator`: expected to need the operator's public key
     in an element object, transient or persistent, which is a write. Until the
     owner rules (question 3) it is `TFW_ERR_UNSUPPORTED` on the element.
   - `tfw_crypto_sha256`: the shared software one.
   - `tfw_crypto_hmac` and `tfw_crypto_verify_image`: `TFW_ERR_UNSUPPORTED`.
     HMAC needs a written secret; verify_image needs a stored anchor.
   - `tfw_crypto_tier`: which value the tier reports is question 1.

**Where it fits.** Each line is an estimate, not a measurement: no driver
exists to build, and `make firmware-matrix` measures the row's sizes once it
does, as it measured the OPTIGA's.

| Board | Estimate | Why |
|---|---|---|
| ATmega2560 | fits, as an option beside soft (estimate) | The OPTIGA tier, of the same three layers, has two measurement rows on it, which `make firmware-matrix` gates against its budget |
| ATmega32U4 | tight; fits only with the transport's buffer at the smallest information field (estimate) | A quarter of the 2560's flash and less RAM than its gate; no element tier has a row there yet |
| Cortex-M (`mps2-an505` with `I2C=1`) | fits (estimate) | The MPS2 I2C master already carries the OPTIGA's measurement row |
| ATmega328P | not built | The SPEC row excludes it |

## 4. Against ADR #14 and ADR #15

- **#14 (reference part and CI emulation):** the element is not a part or an
  architecture. It needs no QEMU machine; on CI it is a mock behind the
  link-time HAL, as the ATECC608C and OPTIGA are. It does not change #14's
  reference part or its emulation target.
- **#15 (trust model and TF-M):** #15's option (c) puts the key store in the
  SoC's enclave where there is one and in "the element tier where there is
  not", naming the ATECC608C and the SE052F. This ADR is that element tier's
  SE052F half. On TrustZone-M (#15 options (a) and (b)) the driver sits in the
  secure world, so any SCP03 secret stays out of the non-secure image.
- **The i.MX 95:** its key tier is the EdgeLock secure enclave
  ([`imx95-design.md`](imx95-design.md)), not this element; the SE052F is for
  the parts without one.

## 5. Onboarding on the bench host

The board goes onto the bench under the existing bench rulings (m346/m347):
one device per lane, the factory record and restore the one condition. For an
element that is "write nothing":

1. **Cabling** goes to the owner, through the CA. No lane cables the board.
   The host, the header pins, the supply and any level shifter are the owner's
   call (question 7).
2. **One device per lane.** A lane that touches the SE052F touches nothing
   else on the bench.
3. **The factory record, read-only.** Read once, only for the record, the
   values the bench page's "Factory values" table lists: the identifier, the
   applet version, the NXP-issued certificate, the provisioned objects and
   their policies, and the lifecycle state. Which tool reads them is question 7.
4. **Restore** is "nothing to restore": a lane compares what it reads at the
   end against the record.
5. **Never a lock, lifecycle or reset operation.** No applet lock, no factory
   reset, no object deleted, no policy changed.
6. **Never a write.** No object created or written, no key generated, no
   counter created or incremented, no SCP03 key changed, even in a transient
   object.
7. **No bench identity in the repository:** no serial numbers, certificate
   subjects or serials, unique identifiers, addresses of hosts or host names
   in any file, commit message or PR.

## Open questions for the owner

None is decided here.

1. **The tier value.** Does the SE052F report the existing
   `TFW_CRYPTO_SECURE_ELEMENT`, as the OPTIGA tier does, or a value of its own
   so evidence names the vendor?
2. **The DeviceID key.** Which NXP-provisioned key is the DeviceID key, and
   is its policy one that allows a sign over a host digest? Read from the
   factory record before the owner rules.
3. **Any write command.** May the driver write at all: a transient object for
   the operator's key in verify_operator, a counter object for anti-rollback,
   a generated key for DeviceID? Each is off until ruled.
4. **Lifecycle and lock operations.** Are any ever run, and by whom? Until the
   owner decides, no lane runs one, on the bench or in code.
5. **Platform SCP03.** Is the bus protected on this tier? It needs the host to
   hold SCP03 keys, and changing them is a write.
6. **The CDI source.** Does Layer 0 keep the UDS in the element, or in the host
   as the soft root does, with the element holding only the DeviceID key?
7. **Cabling, host and the factory-record tool.** Which board carries the
   SE052F, with which supply, and does a Linux I2C bus with NXP's own tooling
   read the factory record before our driver exists?
8. **The transport specification.** Which T=1 over I2C does the SE052F speak:
   NXP's UM11225 or GlobalPlatform's? Settled by the data sheet when it is
   read.
9. **The vendor documents.** nxp.com refused every SE05x document to a lane.
   Does the owner supply them (data sheet, AN12413, the transport
   specification) at the revision to be governing, as ruling 109 settled the
   OPTIGA's?

## Follow-ups (not in this ADR)

- Read the three NXP documents and fill what is *to confirm*, here and in the
  bench page's "The part" table, each cell by section.
- The bench factory record, read-only (§5 step 3).
- The driver, in the three slices of §3, with its mock, after the owner's
  answers to questions 1 to 3 and 8.
- The fit table's measured cells, from `make firmware-matrix` once the driver
  builds.
