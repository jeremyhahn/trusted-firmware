# ADR: onboarding the OPTIGA Trust M V3 shield

> **Status: PROPOSED** · Issue: [#127](../../../../issues/127) · SPEC row:
> `optiga-trust-m` (future) · Scope: trusted-firmware (the on-device driver
> only); go-enclave (the host-side backend, named, not designed) · Related:
> [`architecture.md`](architecture.md) §6,
> [`../cortex-m/adr-reference-part-and-emulation.md`](../cortex-m/adr-reference-part-and-emulation.md) (#14),
> [`../cortex-m/adr-trust-model-vs-tf-m.md`](../cortex-m/adr-trust-model-vs-tf-m.md) (#15) ·
> Last verified: 2026-10-22
>
> Documentation only. Owner ruling 2026-10-17 05:51Z, queue item 26: each
> arrived board gets one docs-only onboarding ADR, written now; the port itself
> stays behind the AVR dev-complete gate (#7). This ADR changes no code, no
> `mk/` file, no board profile and no wire vector, and decides nothing the owner
> has not ruled: every choice is an open question below.
>
> Written when no vendor document was readable. Since then the *Solution
> Reference Manual* is pinned in
> [`../reference/README.md`](../reference/README.md), at Revision 3.70 since
> question 9 was decided, and the §2 items it settles carry its section
> (2026-10-22). The datasheet is still unread, so the supply item stays *to
> confirm*.

## The question

The OPTIGA Trust M V3 shield has arrived (CA m311). It is a secure element, not
a host MCU: it adds an element tier beside the ATECC608C (MIKROE-6774) and the
SE052F (`secure-element-se05x`), on a host that already runs the firmware core.
What is it for, what is its root of trust, how does that map onto TCG DICE,
where does it sit against #14 and #15, and how is it brought onto the bench
without changing it?

## 1. What the board is for, and its SPEC row

- **SPEC row `optiga-trust-m` (future):** "an element tier on the Infineon
  OPTIGA Trust M, the plain OPTIGA Trust M V3 shield". It serves that row and
  no other.
- **Purpose here:** a third element vendor for the element tier, so the tier is
  not tied to one part. It holds the DeviceID key in hardware and signs the
  alias certificate and UCCS evidence the firmware already emits
  (`docs/dice-attestation/`), as the ATECC608C does today.
- **Ownership (L1, L2), as the row states:** the on-device driver is ours; the
  host-side backend is go-enclave's, the same split as the SE052F. go-xkms sees
  only evidence and the Infineon-issued device certificate. This ADR designs
  nothing on the host side.
- **No element becomes mandatory.** CA m435: the ATECC608C is never required,
  and the software DICE root stays the default (owner ruling, 2026-10-17 05:51Z). This ADR
  adds an element; it requires none.
- **Order of work.** SPEC Rules: the OPTIGA Trust M comes after the SE052F and
  the STM32H5, once AVR is proven on the Nano and the Mega (#7).

## 2. Root of trust

### What the element holds

Vendor documents:

- Infineon, *OPTIGA™ Trust M (SLS32AIA) Datasheet*, the V3 revision: not yet
  read; revision and sections *to confirm*.
- Infineon, *OPTIGA™ Trust M Solution Reference Manual*, Revision 3.70
  (2024-10-09), pinned by SHA-256 in
  [`../reference/README.md`](../reference/README.md). Sections below are its own
  (question 9).

Item by item:

| Item | What it is expected to be | Status |
|---|---|---|
| Factory key | An ECC NIST P-256 private key generated and locked at Infineon's personalisation, in a key object (`0xE0F0`, "device private key 1") | Confirmed in part, Rev 3.70 §5.4 Table 69: `0xE0F0` is "Device private ECC key 1", read NEV, and GetDataObject and SetDataObject are refused on its data part whatever its metadata says. The curve is not given there; P-256 *to confirm* from its metadata on the bench (algorithm tag, Table 71) |
| Factory certificate | An X.509 certificate for that key, issued by an Infineon CA, in a data object (`0xE0E0`) | Confirmed, Rev 3.70 §5.4 Table 68: `0xE0E0` is "Public key certificate 1, issued by Infineon", read ALW, change NEV, data type "device identity", at most 1728 bytes (Table 73). Read by `tfw_optiga_read_object` |
| Key object slots | Further ECC key objects (`0xE0F1`-`0xE0F3`) and RSA key objects, generated on chip; the private key never leaves | Confirmed, Rev 3.70 §5.4 Table 69: `0xE0F1`-`0xE0F3` ECC and `0xE0FC`-`0xE0FD` RSA key objects, read NEV |
| Data object slots | General data objects, monotonic counters (`0xE120`-`0xE123`), and objects that can hold a secret for HMAC or a platform binding secret (`0xE140`) | Confirmed, Rev 3.70 §5.4 Tables 68 and 70: counters `0xE120`-`0xE123`, the platform binding secret `0xE140`, and the arbitrary data objects `0xF1D0`-`0xF1DB` and `0xF1E0`-`0xF1E1` |
| Lifecycle state | Each object carries a lifecycle state (LcsO: creation, initialisation, operational, termination) and access conditions in its metadata; the state only moves forward | Confirmed, Rev 3.70 §5.3 and §5.5 Table 71 (tag `0xC0`, LcsO): cr, in, op, te, forward only; an absent access condition reads as NEV |
| Shielded connection | The I2C link can be protected by a session derived from the platform binding secret shared between host and element, so the bus carries no plaintext command or response | Confirmed, Rev 3.70 §2.3 and §6.6: the session is keyed by the platform binding secret (`0xE140`), and a `Conf` access condition makes an object readable or writable only through it. Not written here (question 4) |
| Supply | The voltage range the element and the shield accept, which decides whether a 5 V AVR host needs a level shifter | *to confirm* (datasheet) |

The read-only command layer (`src/crypto/optiga/optiga_cmd.c`) names only the
objects it reads: `0xE0C2`, `0xE0C6`, `0xE0E0` and the last error code
`0xF1C2`.

### How it compares with the ATECC608C element tier

| | ATECC608C (AVR rows today) | OPTIGA Trust M V3 (this row) |
|---|---|---|
| Bus | I2C | I2C, with an optional shielded connection (Rev 3.70 §6.6) |
| DeviceID key | hardware-resident P-256, not derived from the CDI | hardware-resident ECC factory key, `0xE0F0` (Rev 3.70 Table 69; P-256 *to confirm*) |
| Factory certificate | provisioned per the TrustFLEX slot map (`docs/device-provisioning/adr-trustflex-608c-slot-map.md`) | Infineon-issued, in data object `0xE0E0` (Rev 3.70 Table 68) |
| CDI source | `boot/tier/cdi_atecc.c`: HMAC(UDS, H(code)) with the UDS in the element | not decided (open question 3) |
| Bus protection | none on the AVR rows | shielded connection, if the owner rules it in (open question 4) |
| Anti-rollback | the element's monotonic counter (`docs/ota/adr-ota-image-format-and-trust.md`) | the element's monotonic counter object, as the ATECC608C (same table) |
| Locking | per-slot and zone locks | per-object lifecycle state (Rev 3.70 §5.3) |

### Mapping onto TCG DICE

The DICE standards are listed, not copied, in
[`../reference/README.md`](../reference/README.md) ("Not redistributable"):

- TCG *Hardware Requirements for a Device Identifier Composition Engine*,
  Revision 78. The firmware's Layer 0 CDI is HMAC(UDS, H(code)), form (2) of
  §5, as cited there.
- TCG *DICE Attestation Architecture*, Revision 23: the
  `tcg-dice-UCCS-evidence` extension (§6.1.4, as cited there) in the alias
  certificate.

Read against those, the element maps as the ATECC608C does
(`docs/cortex-m/adr-trust-model-vs-tf-m.md`, Context): the factory key is a
hardware-resident DeviceID key, *not* derived from a CDI; the alias key is the
DeviceID key; the measurement is bound by the signature over the claim, not by
key derivation. Where the UDS lives, and so where the CDI comes from, is open
question 3. The DICE texts themselves are not in `docs/reference/` (their
licence forbids a copy); see open question 7.

## 3. Against ADR #14 and ADR #15

- **#14 (reference part and CI emulation):** the element is not a part or an
  architecture. It needs no QEMU machine; on CI it would be a mock behind the
  link-time HAL, as the ATECC608C is. It does not change #14's reference part
  (the i.MX 95 M7) or its emulation target.
- **#15 (trust model and TF-M):** #15's option (c) puts the key store in the
  SoC's enclave where there is one and in "the element tier where there is not",
  naming the ATECC608C and SE052F. The OPTIGA Trust M is a third candidate for
  that element tier on an M7 or M33 without an enclave. Whether it is one is not
  decided here (open question 6). On TrustZone-M (#15 options (a) and (b)) the
  driver would sit in the secure world, so the shielded-connection secret stays
  out of the non-secure image.
- **AVR:** the row is AVR-first like every element tier; whether it fits the
  ATmega328P, as the SE052F row says it does not, is open question 5.

## 4. Onboarding on the bench host

The board goes onto the bench under the existing bench rulings (m346/m347): full
read and write on every dev board on the bench host, one device per lane, the factory
record and restore the one condition. For an element that is "write nothing":

1. **Cabling** goes to the owner, through the CA. No lane cables the shield.
   The host, the I2C pins, the supply voltage and any level shifter are the
   owner's call (open question 1).
2. **One device per lane.** A lane that touches the shield touches nothing else
   on the bench.
3. **The factory record, read-only.** Read, once, only for the record:
   - the factory certificate (the `0xE0E0` data object, Rev 3.70 Table 68);
   - the metadata of every key and data object: its lifecycle state, its access
     conditions, its type (the objects Rev 3.70 §5.4, Tables 68 to 70, lists).
   The record is kept off the repository, where the bench records are kept;
   which tool reads it is open question 2.
4. **Restore** is "nothing to restore": the record is proof the element was not
   changed. A lane compares the metadata it reads at the end against the record.
5. **Never a lifecycle-state change.** No object moves to operational or
   termination, and no metadata is written.
6. **Never a write to a key or data object.** No key generation, no certificate
   write, no counter increment, no platform binding secret written, even into
   an object that is still in its creation state.
7. **No bench identity in the repository:** no serial numbers, certificate
   subjects or serials, UIDs, MACs, IPs or host names in any file, commit
   message or PR.

## Open questions for the owner

Question 9 is decided, by ruling 109; none of the others is decided here.

1. **Cabling and host.** Which board on the bench host does the shield go on (the Mega, the
   Nano or another), and does it need a level shifter at 5 V? (Supply range *to
   confirm* from the datasheet.)
2. **The factory-record tool.** Which tool reads the certificate and metadata
   read-only before our driver exists: Infineon's host tooling on a Linux I2C
   bus, or a read-only firmware build on the AVR host?
3. **The CDI source.** Does Layer 0 on this tier keep the UDS in the element
   (an HMAC secret object, as `cdi_atecc.c` does with the ATECC608C), or in the
   host as the soft root does, with the element holding only the DeviceID key?
4. **The shielded connection.** Is it required on this tier? It needs a
   platform binding secret written into the element, which is a write and a
   lifecycle decision the onboarding steps forbid until the owner rules.
5. **Which boards the tier fits.** The ATmega2560, ATmega32U4 and Cortex-M as
   the SE052F, or also the ATmega328P? The ATmega2560's measurement rows
   (follow-ups, the fifth part) measure the tier's cost there; nothing else
   is measured yet.
6. **#15's element tier.** Is the OPTIGA Trust M an element-tier option for an
   M7 or M33 without an enclave, beside the ATECC608C and SE052F?
7. **The DICE texts.** The TCG DICE specifications are not in
   `docs/reference/`; they are listed by title, revision and URL because their
   licence forbids a copy. Is that listing enough for this tier, or does the
   owner want a licence check for anything more?
8. **The Infineon host library.** The SPEC row makes the on-device driver ours.
   Does "ours" mean written here, or composed from Infineon's own host library
   (L2), subject to its licence?
9. **The vendor documents. Decided (ruling 109, 2026-10-21):** the governing
   revision of each is the newest Infineon publishes for the part. For the
   solution reference manual that is Revision 3.70 (2024-10-09), from
   Infineon's `optiga-trust-m-overview` repository, now pinned in place of
   3.60. Its revision history lists "Editorial changes" only, and a
   word-by-word comparison of §4.4 (Tables 36 to 64) and §5.4 to §5.7
   (Tables 65 to 79) against 3.60 finds the same command codes, fields,
   lengths, error codes, object identifiers and access conditions: the
   differences are page layout, footnote placement and wording ("OPTIGA™"
   becomes "OPTIGA™ Trust M", "KHz" becomes "kHz"). Every section and table
   number cited from 3.60 is the same in 3.70, so the citations stand under
   the new revision. The datasheet's revision is decided the same way when it
   is read.

## Follow-ups (not in this ADR)

- Confirm what is still *to confirm*: the supply range against the datasheet,
  and the factory key's curve from its metadata on the bench. The other §2
  items are confirmed against the Solution Reference Manual Rev 3.70.
- The driver and its mock, after #7 and the SE052F and STM32H5, per SPEC order.
  Its first part is written: the I2C transport, `src/crypto/optiga/optiga_i2c.c`,
  to Infineon's *IFX I2C Protocol, Protocol Specification* v2.03
  ([`../reference/README.md`](../reference/README.md)), tested against recorded
  frames in `test/unit/test_optiga_i2c.c`. It is written here, not taken from
  Infineon's host library, which keeps question 8 open: the owner may still
  choose the library. It refuses the shielded connection (question 4). Bench
  facts it takes from the host library and marks to confirm: the address 0x30,
  the FCS byte order, and the timing values.
- Its second part is written: the read-only command layer,
  `src/crypto/optiga/optiga_cmd.c`, to the Solution Reference Manual Rev 3.70:
  OpenApplication, CloseApplication, GetDataObject (data and metadata) and
  GetRandom (§4.4.1, Tables 40-42 and 45), with Table 39's error codes mapped
  onto existing status codes. No write command is reachable, which
  `test/unit/test_optiga_cmd.c` proves; `test/unit/test_optiga_cmd_i2c.c`
  reads the `0xE0E0` certificate across transport frames. It is the read
  side §4 step 3's factory record needs (question 2). Its command wait, 200 ms,
  is ours, and covers Table 64 (§4.4.3): about 30 ms for a 256-byte
  GetDataObject and about 65 ms for CalcSign. Both are to confirm on the bench.
- Its third part is written: CalcSign (§4.4.1.12, Table 51), ECDSA over a
  SHA-256 digest with the factory key `0xE0F0` (Table 69). The response is two
  DER INTEGERs, r then s, with no SEQUENCE around them. The layer decodes them
  to a raw 64-byte r‖s and refuses any other shape.
  Signing writes no data object. It does raise the element's security event
  counter (Table 65), and nothing on the host side is written.
- Its fourth part is written: the provider, `src/crypto/optiga/optiga_crypto.c`,
  with `tfw_crypto_*` mapped onto the read-only commands:
  - random is GetRandom.
  - sign is CalcSign with `0xE0F0`.
  - counter is GetDataObject on the monotonic counters `0xE120`-`0xE123`
    (Table 68, Table 73's value bytes).
  - SHA-256 is the shared software one.
  - verify_operator is VerifySign (§4.4.1.13, Table 52), ECDSA (Table 62)
    with the operator's key sent in the command: Table 57's NIST P-256 as
    tag 0x05, the key as tag 0x06, a DER BIT STRING of the uncompressed point
    (note 70). r and s go as two minimal DER INTEGERs (note 68, §6.2.3), a
    sign byte in front of a value above 0x7F. No object is read or written.
    A signature that does not verify, under any key, is Table 39's 0x2C,
    `TFW_ERR_CRYPTO` as `include/tfw/crypto.h` gives every tier; a transport
    failure is returned as it came.
  - HMAC, pubkey and verify_image return the existing
    `TFW_ERR_UNSUPPORTED`. HMAC needs a written secret. The public key is
    not readable, and reading it from the certificate is X.509, which go-xkms
    owns. verify_image needs a stored anchor.
  - No status code was added.
  - `test/unit/test_optiga_crypto.c` tests it on the host.
- Its fifth part is written: the gate. `CRYPTO=optiga` in `mk/firmware.mk`
  links the transport, the command layer, the provider and the shared
  SHA-256, chosen by the object list alone. It is off by default: no board's
  `BOARD_TIERS` names it, so question 5 stays open and no default image
  changes. The ATmega2560's `BOARD_ROWS` build it twice, `optiga:ETH=1` and
  `optiga:ETH=0`, so `make firmware-matrix` size-checks it and prints its
  flash and RAM. A row reuses the existing mechanism; a measurement-only
  target would be new code for the same build. No Cortex-M row: the one
  Cortex-M board, `mps2-an505`, has its I2C off and the Cortex-M HAL has no
  bus driver, so the transport cannot link there. The ATmega328P is not
  measured, because question 5 asks about it. Decided under ruling 109 for
  this tier:
  - No alias certificate. Its builder names the element's public key, which
    the provider cannot read.
  - The auth session is linked, under the element tier's profile knob. It
    needs random, SHA-256 and verify_operator, and takes the operator key
    from the platform block.
  - OTA_INFO's rollback floor is the platform block's (`ota_floor_eeprom.c`,
    as on the soft tier). The element's monotonic counters are read, never
    incremented, under onboarding rule 6.
  - `PROVISION=1` stops the build. Every provisioning backend writes the
    element, which rule 6 and question 4 forbid until the owner rules.
  - VerifySign's request, up to 187 bytes, stays on the caller's stack. The
    element tier stages its 136-byte External Verify there for the same
    reason (`atecc.h`, `TFW_ATECC_MAX_DATA`): a static buffer is a permanent
    cost to the RAM the size gate counts, and on the element tier it blew the
    ATmega2560's budget by itself. On the stack it is paid only while the
    auth session's gate runs, 51 bytes deeper than the element tier's own
    Verify on the same path, and the transport below it frames from its own
    static buffer.
- Left in the row:
  - Which boards the tier fits, from the rows' measured cost (question 5).
  - A Cortex-M measurement, once a Cortex-M board has an I2C driver.
  - The bench factory record.
  - On the bench: the counter value's byte order and the factory key's curve.
- Not in the row: pubkey from the factory certificate `0xE0E0`. Taking the
  key out of it is certificate parsing, which go-xkms owns, not this
  repository; pubkey stays `TFW_ERR_UNSUPPORTED` on this tier, and a gap
  there is filed against go-xkms.
