# ADR: the element tier on the shipped ATECC608C-TFLXTLS

> **Status: PROPOSED** · Scope: trusted-firmware, #7 · Last verified: 2026-10-15
> An owner decision. Nothing here changes code, vectors, SPEC or row status.

## Context

The boards take the ATECC608C-TFLXTLS at `0x36` on MikroE's ECC608 Trust Click
(MIKROE-6774) ([engineering](engineering.md#the-atecc608c-mikroe-6774-click-board)).
That part ships pre-provisioned. Its configuration and data zones are locked and
Microchip has set its slot policies (DS40002515B page 1, table 3-1). The
provisioning flow assumes a blank part, so `tfwprov --commit` refuses this one.
The element tier then answers `NOT_PROVISIONED`, because slot 1 does not hold
an HMAC key. This ADR records the slots and settings our tier depends on, the
slot map Microchip ships, and the ways to reconcile the two.

## What the element tier relies on

| Role | Where | Element operation | Why it is there |
|---|---|---|---|
| DeviceID private key | slot 0 (`TFW_ATECC_SLOT_DEVICEID`, `src/crypto/atecc608a/atecc.h`) | `Nonce` pass-through, then `Sign` external (`tfw_crypto_sign`); `GenKey` public mode for the point (`tfw_crypto_pubkey`) | The DICE ECA key. It never leaves the part. The alias certificate's subject key is the same point. There is no separate Alias private key ([architecture](../dice-attestation/architecture.md#the-deviceidalias-split)) |
| UDS | slot 1 (`TFW_ATECC_SLOT_UDS`) | `SHA` in HMAC mode, keyed by the slot | tfw-boot derives CDI = HMAC(UDS, H(fw)) (`boot/tier/cdi_atecc.c`). The application recomputes an HMAC on slot 1 and then zeroizes it. A success sets `have_cdi`, and `tfw_crypto_sign` refuses to sign without it (`src/crypto/atecc608a/provider.c`). On this tier the application uses the UDS only as the gate that says "this part went through our ceremony" |
| Boot image-signing public key | slot 2 (`SLOT_IMAGE_SIGNING`, `boot/verify/verify_atecc608a.c`) | `Verify`, stored mode, P-256 | The build system's signing key. It never enters the boot section as a value ([secure boot](../secure-boot/architecture.md#trust-boundaries)). No provisioning command writes it today, and no document says how it reaches slot 2 |
| Boot counter | counter 0 (`TFW_ATECC_COUNTER_BOOT`) | `Counter` read and increment | The monotonic anti-rollback floor. `tfw_crypto_counter` also reads counters 0 and 1 |
| Operator key | none | `Nonce` pass-through, then `Verify` external | The public key arrives in the packet (`AUTH_RESPONSE`, #4) |
| Image measurement | none | `SHA` | `boot/measure/measure_atecc608a.c` |

These dependencies are pinned in three places:

- **On the wire.** `sdk/go/provision.go` pins `SlotDeviceID = 0` and `SlotUDS = 1`.
  `PROV_GENKEY` and `PROV_PUBKEY` accept slot 0 only. `PROV_SEED_UDS` accepts
  slot 1 only (`src/caps/provision.c`).
- **In the flow.** `--commit` writes config blocks ≥ 2, locks the config zone
  under its CRC, runs `GenKey` on slot 0, seeds the UDS into slot 1, and then
  locks the data zone. It refuses a part whose data zone is already locked.
  `PROV_SEED_UDS` runs `Random`, then a pass-through `Nonce`, then a clear
  32-byte `Write`. The UDS therefore crosses the I2C bus once, and the code
  comment says so.
- **In verification.** `tfwprov --verify` checks that slot 0 holds a key and
  that slot 1 is not readable ([fuses and lock bits](../security/fuses-and-lockbits.md#verification)).

The template `sdk/go/templates/atecc608a-dice.conf` still holds placeholder
blocks and CRCs. So the blank-part flow cannot run on any part until real values
are generated. This is true whichever option is chosen.

## The shipped TFLXTLS layout

Source: DS40002515B, ATECC608C-TFLXTLS data sheet, revision B, November 2023,
§3.2.3 and tables 3-3 to 3-16
([PDF](https://ww1.microchip.com/downloads/aemDocuments/documents/SCBU/ProductDocuments/DataSheets/ATECC608C-TFLXTLS-CryptoAuthentication-Data-Sheet-DS40002515.pdf)).
§11 says the configuration and slot values are the same as on the 608A- and
608B-TFLXTLS.

Many slots come in one of two variants:

- **Option 1 (slot-lockable).** The slot stays writable, under its policy, until
  a `Lock` in SlotLock mode clears its bit in config bytes 88–89. That happens
  once per slot and cannot be undone (§3.1.1).
- **Option 2 (permanent).** Microchip fixes the slot at manufacture.

Which option applies to each slot is chosen through the Trust Platform Design
Suite (TPDS) when the part is ordered. Prototype units have every slot
slot-lockable, no secure-boot option, and an I2C-only interface (§3.2.4).

| Slot | Purpose | Key type | How it gets its value | Writable after Microchip's lock |
|---|---|---|---|---|
| 0 | Primary private key | P-256 private; external sign, ECDH; random nonce required | Fixed at manufacture | Never (permanent). In production units it can be tied to the Persistent Latch, so it works only after a successful SecureBoot |
| 1 | Internal-sign key | P-256 private; signs only `GenKey`/`GenDig` output (key attestation); random nonce required | Fixed at manufacture | Never |
| 2–4 | Secondary keys 1–3 | P-256 private; external sign, ECDH; random nonce required | `GenKey` by the user; "key attestation is required" when they change (§3.2.1) | Option 1: `GenKey` until SlotLock. Option 2: never |
| 5 | Secret key | Symmetric, two AES-128 keys or a 32-byte secret. Usable with `GenDig`, `MAC`, `CheckMac`, `KDF`, `SHA`/HMAC and `AES` (§3.2.1) | Encrypted `Write` under the slot-6 key | Option 1: until SlotLock. Option 2: never |
| 6 | I/O protection key | Symmetric; random nonce required; not readable. Can hold other data if I/O protection is unused | Clear `Write`. §3.2.1.7: the MCU generates the value, keeps it, writes it, then slot-locks | Option 1: until SlotLock. Option 2: never |
| 7 | Secure-boot digest | — | Not directly writable or readable | — |
| 8 | General data, 416 bytes | Data, clear read | Clear `Write` | Option 1: until SlotLock. Option 2: never |
| 9 | AES key | Symmetric, four AES-128 keys; secret | Clear `Write` | Always; no lock option is listed |
| 10 | Device certificate, compressed | Data, clear read | Microchip / TPDS | Production units: permanent or slot-lockable. Prototype units: slot-lockable |
| 11 | Signer public key | ECC public, clear read | Microchip / TPDS | As slot 10 |
| 12 | Signer certificate, compressed | Data, clear read | Microchip / TPDS | As slot 10 |
| 13 | Parent public key, or 72 bytes of data | Clear read | Clear `Write` | Until SlotLock |
| 14 | Validated public key | ECC public, clear read | `Write` in PubInvalid mode, validated by slot 13 | Only after invalidation |
| 15 | Secure-boot public key | ECC public, clear read | Clear `Write` | Option 1: until SlotLock. Option 2: never |

Other shipped facts:

- **Configuration zone.** Locked and read-only, but always readable (table 3-1).
- **OTP zone.** Locked (table 3-1).
- **I/O protection.** Enabled, with its key in slot 6 (§3.1).
- **SecureBoot.** Enabled in FullStore digest mode (§3.1).
- **Counters 0 and 1.** They start at zero and are not attached to any key.
  They are free for the system (§3.1, §3.1.1).
- **Lock command.** Its SlotLock mode is usable (§6).
- **`Random`.** Its output always goes over the bus and cannot be stored in a
  slot (§6).

The Click's other two parts:

- **TNGTLS.** DS40002515B §7.3 says the TFLXTLS has the "same locked
  configuration" and the "same data slot definition" as the Trust&GO
  ATECC608C-TNGTLS. The TrustFLEX part adds the ability to provision a customer
  secure-boot key, symmetric keys and a custom PKI.
- **TMNGTLS.** Microchip markets the ECC608-TMNGTLS as TrustMANAGER for its
  keySTREAM service.

### Not recorded here, and why

- **Only in the full ATECC608C data sheet, which is under NDA:**
  - the exact rule behind "random nonce is required" (ReqRandom), and whether it
    blocks `Sign` external after a pass-through `Nonce`;
  - whether a slot whose KeyType is not a SHA key (slots 6 and 9) can key the
    `SHA` command's HMAC mode;
  - the bit encodings of SlotConfig and KeyConfig;
  - which command outputs I/O protection encrypts beyond `Verify`'s optional MAC;
  - `KDF` details, which DS40002515B §6 refers to Microchip Sales;
  - the ChipMode clock divider.

  These are not guessed. The bench questions below answer the ones the tier
  needs.
- **Public, but not retrieved this session:**
  - the ATECC608C-TNGTLS data sheet (a guessed DS40002514 URL returned HTML);
  - the ECC608-TMNGTLS data sheet;
  - the microchip.com and mikroe.com product pages, which sit behind a bot
    challenge;
  - the blank 608C's ordering code.

  Nothing below depends on their contents beyond what DS40002515B §7.3 states.
- **Not in any document:**
  - which option (1 or 2) each slot of the parts soldered on our Click has;
  - whether those parts are production or prototype units.

  Config bytes 88–89 and the SlotConfig and KeyConfig bytes answer both, and a
  config read is always allowed.

## Options

### (a) Map our roles onto the TFLXTLS's slots

Each role moves to a slot whose policy fits it:

- **DeviceID: slot 0, as shipped.** It signs external messages, and its public
  point can always be generated, so `PROV_PUBKEY` works. `PROV_GENKEY` is not
  used.
- **UDS: two candidate slots.**
  - **(a1) Slot 6.** Seeded with today's `PROV_SEED_UDS` sequence, aimed at
    slot 6, and then slot-locked. The slot gives up its I/O-protection role. The
    firmware uses no command whose output that role protects.
  - **(a2) Slot 5.** An encrypted `Write` under an I/O protection key in slot 6.
    The AVR generates that key, writes it in the clear, keeps it, and slot-locks
    slot 6 (§3.2.1.7). Slot 5 is then slot-locked too.
- **Boot image-signing key: slot 15.** It is written in the clear and
  slot-locked. tfw-boot's `SLOT_IMAGE_SIGNING` becomes 15.
- **Counter 0.** Unchanged.
- **Config and data locks.** Already set by Microchip. The per-slot SlotLock
  takes their place.

Rejected UDS homes:

- **Slot 9.** It has no lock option, so the application could overwrite the
  UDS with a value an attacker knows and then forge any CDI.
- **Slots 8 and 13.** They are readable in the clear.
- **Slots 2–4.** They hold private keys, not secrets.

**Flash.**

- **Runtime images:** a slot number is a constant, so moving one adds no code to
  the application or to tfw-boot. The numbers become board-profile data, as the
  I2C address did, so no `#if` enters the logic. Nothing is added to the 328P's
  application budget, which is already full.
- **Provisioning image (`TFW_FEATURE_PROVISION`):**
  - **(a1)** adds a SlotLock mode to the existing `Lock` path, a slot-15 public
    key write, and a full config read.
  - **(a2)** adds `GenDig` and the encrypted write as well. That code computes
    the write's mask and MAC with SHA-256, and it must hold and protect the I/O
    key.

  None of this is measured. The provisioning image's 328P size has to be checked
  before choosing.

**Wire, extend only.**

- **New commands** at unused ids (`0x76` onward is free in
  `include/tfw/protocol.h`): a SlotLock, a public-key write for slot 15, and a
  full config read. `PROV_INFO` forwards only block 0 and the lock bytes.
- **Wider argument ranges.** `PROV_SEED_UDS` also accepts slot 6 under (a1) or
  slot 5 under (a2); `PROV_PUBKEY` keeps slot 0. An older firmware refuses the
  new slot with the status it uses today, so nothing changes meaning.
- **SDK.** `sdk/go` adds new constants beside `SlotDeviceID` and `SlotUDS`;
  those two keep their values.
- **Host flow.** `tfwprov` gains a TrustFLEX plan, because there are no config
  blocks to write and no zone locks.
- **Unchanged:** wire vectors and status codes.

**Security.**

- **DeviceID provenance.** The key was generated by Microchip at manufacture,
  not by our ceremony. The public data sheet does not say whether any record of
  it exists outside the part.
- **UDS.**
  - **(a1)** crosses the bus once, in the clear, as on a blank part today.
  - **(a2)** sends the I/O key across the bus in the clear and leaves it in AVR
    memory. A bus observer during provisioning therefore recovers the UDS under
    (a2) as well. Against that attacker (a2) adds nothing. After SlotLock both
    are fixed.
  - **(a2) only:** slot 5 is the slot the data sheet documents for HMAC.
- **Boot key.** Slot 15 with SlotLock gives the same "locked slot named by
  number" property as slot 2 on a blank part.
- **Persistent Latch.** If slot 0 shipped tied to the Persistent Latch, it signs
  only after a SecureBoot, which tfw-boot does not run. The bench must rule this
  out first.

**Provisioning steps (a1).**

1. Read the whole config zone, record it, and stop if any of these hold:
   - slots 6 or 15 are option 2;
   - slot 0 is latched;
   - bytes 88–89 show slot 6 or slot 15 already locked.
2. Read the slot-0 public point into the provisioning record.
3. Seed the UDS into slot 6.
4. Before any lock, check that HMAC keyed by slot 6 works.
5. Write the image-signing key to slot 15. Check `Verify` stored mode against a
   signed image.
6. SlotLock slots 6 and 15. This step is irreversible.
7. Run `tfwprov --verify` against the TrustFLEX expectations:
   - slot 0 holds a key;
   - slot 6 is not readable;
   - slot 15 holds our key;
   - the bits for slots 6 and 15 are cleared.

(a2) inserts the I/O-key write and lock before step 3, and makes step 3 an
encrypted write to slot 5.

### (b) Use the Click's other parts

- **(b1) TNGTLS at `0x35`.** It has the same slot definition and the same locked
  configuration (§7.3), and it lacks TrustFLEX's customer secure-boot key and
  symmetric-key provisioning.
  - **Behaviour.** Slot 0 would serve as the DeviceID with Microchip's
    certificate chain in slots 10–12. Whether slots 5, 6 or 15 can be written is
    not recorded, because its data sheet was not retrieved. §7.3 implies they
    cannot.
  - **Flash.** Choosing the part is a board-profile address change. If no UDS
    slot is writable, the tier can only run as in (d2).
  - **Boot.** Without a writable slot 15, `BOOT_SIG=atecc608a` has no key to
    verify against, and boot falls back to the existing `BOOT_SIG=ed25519-donna`
    row, at that row's boot-section cost.
  - **Wire.** Nothing new beyond (d2).
- **(b2) TMNGTLS at `0x38`.** Its slot map is not recorded: its data sheet was
  not retrieved. Its key lifecycle is meant to run through Microchip's keySTREAM
  service. Fleet and device lifecycle belong to go-trusted-platform, so building
  the tier on a third-party key manager conflicts with L1. Not pursued without
  its data sheet and an owner ruling.

### (c) A blank ATECC608C, or TrustCUSTOM

- **Behaviour.** The existing flow works as designed:
  - `--dry-run`, `--verify`, `--commit` on a spare part first;
  - slot 0 is generated by our `GenKey`;
  - slot 1 is a never-readable HMAC key;
  - slot 2 holds the image key.
- **Flash.** None.
- **Wire.** None.
- **Security.** Our own policy, our own ceremony, and the UDS bus exposure
  accepted today.
- **Cost.** It needs:
  - real template values and CRCs, which are needed anyway;
  - a part off the MIKROE-6774, since the Click's three parts are soldered and
    pre-provisioned;
  - lead time.
- **TrustCUSTOM.** It means a configuration agreed with Microchip. Its terms are
  not in the public documents.
- **Effect on the plan.** It moves the "day it arrives" run off the Click that
  was ordered.

### (d) Other choices the data sheet supports

- **(d1) DeviceID in a secondary slot (2–4).**
  - **How.** `GenKey` regenerates the key, then SlotLock fixes it, so the
    DeviceID comes from our ceremony rather than Microchip's. This works only if
    the slot is option 1.
  - **Requirements.** The data sheet requires key attestation when a key
    changes. Slot 1 can sign `GenKey` output, which gives a Microchip-rooted
    attestation of the new key. Producing or verifying that attestation is
    go-xkms's to define, not ours.
  - **Wire.** `PROV_GENKEY` and `PROV_PUBKEY` accept the new slot.
  - **Combining.** It combines with (a) for the UDS and the boot key.
- **(d2) An HMAC-free element tier.**
  - **How.** Use slot 0 only and replace the slot-1 gate. The application uses
    the CDI only as that gate (above).
  - **What is lost.** The CDI binds the measurement in tfw-boot. Without it the
    DICE claim that the measurement fed the CDI no longer holds. A stock part
    would sign as soon as it was flashed, so the evidence that the part passed
    our ceremony is gone.
  - **Cost.** A different provider `.c`, no new wire. It weakens the tier's
    claims.

## Recommendation

**(a1), gated on the bench reads.**

- DeviceID in slot 0 as shipped.
- UDS seeded into slot 6, checked, then slot-locked.
- Image-signing key in slot 15, slot-locked.
- Counter 0 unchanged.
- Slot numbers as board-profile data.
- The new provisioning commands at new ids.

Reasons:

- It uses the part already ordered.
- It adds no bytes to either runtime image.
- It matches today's UDS exposure. (a2) costs more provisioning code and gives
  no extra protection against the attacker who matters at provisioning.

Fallbacks:

- **(a2)** if slot 6 cannot key an HMAC but slot 5 can.
- **(c)** if the bench shows slots 6 and 15 permanent, or slot 0 latched.

(d1) is a separate yes/no for the owner on top of (a).

## Open questions for the owner

1. Is a Microchip-generated, permanent DeviceID acceptable? Or should the
   DeviceID come from our ceremony, through (d1) or (c)?
2. Slot 6 or slot 5 for the UDS? Should the UDS gate stay at all on this tier
   (d2)?
3. Should we order blank 608C parts now as a fallback? That needs an ordering
   code and a carrier board.
4. Do the Microchip certificates in slots 10–12 play any part in enrolment?
   That is go-xkms's call (certificate profiles), so it is filed there if
   wanted.
5. Do the new command ids and SDK constants of (a) fit the provisioning image on
   the 328P?

Bench questions. Each is a read, or runs before any lock:

1. Config bytes 88–89, plus SlotConfig and KeyConfig for slots 0, 5, 6 and 15.
   Production or prototype part?
2. Does slot 0 sign after a pass-through `Nonce` despite "random nonce is
   required"? A failure would show as an error status, not as a bad signature.
3. Does HMAC keyed by slot 6 succeed after a clear write?
4. Does `Verify` in stored mode accept slot 15?
