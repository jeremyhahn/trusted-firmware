# ADR: the signed response policy, the signed script and their check

> **Status: PROPOSED** · SPEC row: `hsm-self-defence` (future) · Scope:
> trusted-firmware (the M7's verifier and the payload vocabulary);
> go-trusted-platform and go-xkms (seams only) · Last verified: 2026-10-20
>
> Docs only. It adds no code, no command id and no status code. The owner
> decides the questions at the end.

## The question

The row says a response policy signed by the tenant decides which action
follows which signal, that a custom response script is a signed artefact too,
and that the M7 accepts only a policy or script whose tenant signature
verifies. This ADR decides:

- the envelope a policy and a script travel in, and the standard it is;
- the signature algorithm, and the key that verifies it;
- how a policy, a script and a firmware image are kept apart;
- anti-replay;
- the payload vocabulary;
- the check, step by step, and what each refusal returns.

Shape, signals, actions and the decision table: [architecture.md](architecture.md).
Tests: [engineering.md](engineering.md).

## Standards implemented

All are copied in [`../reference/`](../reference/README.md).

| Standard | What this ADR takes from it |
|---|---|
| draft-ietf-suit-manifest-37 (pinned) | The envelope (§8.2), the authentication wrapper (§8.3), the sequence number (§8.4.2), integrated payloads (§5.5, §7.5), vendor and class ids |
| draft-ietf-suit-mti-23 (pinned) | The authentication half of `suit-sha256-es256-ecdh-a128ctr`: SHA-256 and ES256 |
| RFC 9052 | `COSE_Sign1_Tagged` and the `Sig_structure` (§4.2, §4.4) |
| RFC 9053 | ES256 (`-7`), the raw r‖s signature (§2.1) |
| RFC 9054 | SHA-256 (`-16`) |
| RFC 8949 | CBOR, with deterministic encoding (§4.2.1) for the payload |
| RFC 8610 | CDDL, the notation of the payload schema (D5) |

No standard in `docs/reference/` defines a tamper-response policy, and none is
known to this project. So the payload's vocabulary is this
row's, and everything around it is the standards above.

## Context

- `src/core/suit.c` already reads a SUIT envelope with a `COSE_Sign1_Tagged`
  authentication, ES256 and SHA-256. `tfw_suit_read` (`src/core/suit.h:66`)
  verifies against the board's image key only. Its subset has a detached
  payload and no integrated one ([`../ota/adr-ota-image-format-and-trust.md`](../ota/adr-ota-image-format-and-trust.md), subset table).
- `tfw_suit_gate` (`src/core/suit.h:80`) binds a manifest to the vendor id and
  the board's class id, and maps each refusal to a status that already exists:
  binding `TFW_ERR_UNSUPPORTED`, digest `TFW_ERR_CRYPTO`, size `TFW_ERR_RANGE`,
  sequence `TFW_ERR_PROTECTED_REGION` (`include/tfw/status.h`).
- A board's class id is `UUIDv5(TFW_SUIT_VENDOR_ID, "<board>")`, a literal in
  its `boards/*/board_profile.h`; the vendor id is in `include/tfw/config.h`.
- Software ES256 verification is micro-ecc (`src/third_party/micro-ecc/`), as in
  `boot/verify/verify_p256_soft.c`.
- On the M7 the check runs privileged ([trust ADR](../cortex-m/adr-trust-model-vs-tf-m.md), M7 option (c)).
- The M7 has no non-volatile store of its own
  ([ref ADR](../cortex-m/adr-reference-part-and-emulation.md), open question 4).

## Decisions

### D1. A SUIT envelope with the payload integrated — PROPOSED

A policy set and a script each travel as a `SUIT_Envelope_Tagged` (-37 §8.2).
Its authentication wrapper (§8.3) holds one `COSE_Sign1_Tagged` over the
manifest digest. The payload is integrated (§5.5, §7.5): an envelope member
under a text-string key, serialized as a bstr. The manifest names it with a
fragment-only URI and holds its SHA-256 digest and size as the image digest
and image size. The signature therefore covers the payload through the
manifest digest. Algorithms are SHA-256 and ES256, the authentication half of
`suit-sha256-es256-ecdh-a128ctr` (draft-ietf-suit-mti-23). There is no
encryption (question 4).

Rejected:

| Alternative | Why not |
|---|---|
| A bare `COSE_Sign1_Tagged` with an attached payload (RFC 9052 §4.2) | Anti-replay becomes a field of ours inside the payload; a second reader beside `src/core/suit.c`; no class id stops an image manifest passing as a policy |
| CMS `SignedData` (RFC 5652) or an X.509-carried policy | A second parser and an ASN.1 surface in privileged M7 code, where the repository already reads CBOR and COSE |
| A format of ours | Handbook `documentation.md` §9: where a standard exists, implement it |

### D2. Class ids keep policy, script and image apart — PROPOSED

The vendor id is the project's, as for images. Each board that runs the row
gets two more class ids beside its image class id, derived the same way:

| Artefact | Class id |
|---|---|
| Firmware image | `UUIDv5(TFW_SUIT_VENDOR_ID, "<board>")`, as today |
| Policy set | `UUIDv5(TFW_SUIT_VENDOR_ID, "<board>/policy")` |
| Script | `UUIDv5(TFW_SUIT_VENDOR_ID, "<board>/script")` |

They are literals in the board profile, like the image class id. The verifier
is told which class it expects and refuses any other with
`TFW_ERR_UNSUPPORTED`, as `tfw_suit_gate` refuses a foreign binding. Binding
to one device rather than one board (`suit-parameter-device-identifier`, -37
§8.4.8.5) is question 7.

### D3. The tenant's P-256 public key, in the M7 image — PROPOSED

One ES256 public key, the tenant's, is read-only data in the M7 image inside
its AHAB container. Only a new container changes it, and AHAB authenticates
that. The check runs on the M7 with micro-ecc. The private half is never on
the board: go-xkms's signing service holds it, reached through
go-trusted-platform's control plane (architecture.md §6). The verifier
takes its key as an argument: the image key never verifies a policy, and the
tenant key never verifies an image.

Rejected:

| Alternative | Why not |
|---|---|
| The key in the EdgeLock enclave's key store | Whether the M7 can reach the enclave, and who may change the key there, are open (architecture.md questions 3, 4); not testable on QEMU |
| The key delivered over the channel | The A55 delivers it, so a compromised A55 substitutes its own |
| The board's image key | One key would sign both code and policy; whoever may ship an image could ship a laxer policy, and the tenant is not the image signer |

### D4. One policy set per envelope; the sequence number is its floor — PROPOSED

The model selects among signed policies (row). So one envelope carries the
tenant's whole **policy set**: a list of policies, entry 0 the tenant's
default. A newer set replaces the old one whole. The model's selection names
an entry of the current set by its index.

- **Floor.** `suit-manifest-sequence-number` (-37 §8.4.2). The floor is the
  higher of a minimum built into the M7 image and the highest number verified
  since reset. A set below it is refused with `TFW_ERR_PROTECTED_REGION`, as
  `tfw_suit_gate` refuses an old image. A set equal to it is accepted only if
  its manifest digest equals the one that set the floor (a re-send).
- **Scripts** have no floor of their own. A policy names a script by the
  SHA-256 digest of the script's payload, so an old script runs only while the
  current set names it. The cost: a new script needs a new policy set
  (question 3).
- **After a reset** the floor falls back to the built-in minimum, unless
  storage the A55 cannot roll back exists (question 8). Until then an attacker
  who resets the board and replays an older set gets that older set, which the
  tenant once signed.

Rejected:

| Alternative | Why not |
|---|---|
| One envelope per policy, a floor per policy | The M7 keeps a floor per policy id in TCM, and a withheld revocation leaves a lax policy selectable (question 2) |
| A floor per class, one envelope per policy | Verifying the newest policy would refuse every older one, so there would be nothing left to select among |

### D5. The payload: deterministic CBOR under this row's CDDL — PROPOSED

The payload is CBOR in deterministic encoding (RFC 8949 §4.2.1), and its
schema is CDDL (RFC 8610). This is the proposal the go-trusted-platform seam
reviews (question 1):

```cddl
policy-set = {
  1 => 1,                       ; format version
  2 => [1*8 policy],            ; selectable; entry 0 is the tenant's default
  3 => expected-values,         ; what S3 compares (architecture.md §2)
  4 => uint,                    ; heartbeat period, ms (S6)
  5 => uint,                    ; corroboration window W, ms (R3)
}
policy  = [1*16 rule]
rule    = [signal, response, wipe: bool]
response = [1*16 action] / script-ref
script-ref = bstr .size 32      ; SHA-256 of a script payload (D4)
script  = [1*16 action]         ; D6
signal  = &(tamper-pin: 1, ahab-failure: 2, measurement-failure: 3,
            ima-violation: 4, hotplug: 5, heartbeat-loss: 6)
action  = &(phy-off: 1, power-off: 2, keys-lock: 3, keys-wipe: 4,
            shutdown: 5, record: 6)
expected-values = { * uint => bstr }   ; keys and sizes to confirm (question 9)
```

- The signal and action numbers are the S1–S6 and A1–A6 of architecture.md
  §2 and §3. They extend and are never renumbered.
- `keys-wipe` in a response acts only when R3 holds (architecture.md §5), and
  `wipe: true` is how a rule asks for it. A signal no rule names gets
  lockdown.
- A map key, enum value or member the M7 does not know refuses the whole set
  (`TFW_ERR_UNSUPPORTED`), and the previous set stays. An older M7 never reads
  a newer vocabulary as something else.
- The bounds (8 policies, 16 rules, 16 actions) are proposed. They size the
  TCM buffer and are fixed in slice 2 against the M7's memory map.

### D6. A script is data, never code — PROPOSED

A script is an ordered, bounded list of actions from the vocabulary of D5,
run as listed: no branch, no loop, no code. The M7 never runs downloaded code,
because AHAB alone authenticates code on the i.MX 95.

Rejected:

| Alternative | Why not |
|---|---|
| Native code or a bytecode interpreter | A second code-signing path beside AHAB, and an interpreter in privileged M7 code |
| SUIT command sequences as the script language | They sequence fetch, copy and invoke; response actions would be `suit-command-custom` (-37 §8.4.11), whose meaning we would design anyway |

### D7. The check, in order — PROPOSED

Each step fails closed. On any refusal the previous set and the previous
selection stay, and the refusal is counted (architecture.md §5, R7).

| # | Step | Refused with |
|---|---|---|
| 1 | The envelope fits the TCM buffer | `TFW_ERR_RANGE` |
| 2 | Its structure is the profile's exactly (as `tfw_suit_read`) | `TFW_ERR_UNSUPPORTED`, or `TFW_ERR_RANGE` when truncated |
| 3 | The manifest digest holds, and ES256 verifies with the tenant key (D3) | `TFW_ERR_CRYPTO` |
| 4 | Vendor id and the expected class id (D2) | `TFW_ERR_UNSUPPORTED` |
| 5 | The integrated payload is present under the manifest's URI, with the manifest's size and SHA-256 | `TFW_ERR_RANGE` (size), `TFW_ERR_CRYPTO` (digest) |
| 6 | The sequence number against the floor (D4; policy sets only) | `TFW_ERR_PROTECTED_REGION` |
| 7 | The payload against D5: deterministic, known, within bounds | `TFW_ERR_UNSUPPORTED`, `TFW_ERR_RANGE` |
| 8 | Accept: the set replaces the old one, the floor advances, the selection returns to entry 0 | — |

Steps 1–3 run before any byte of the payload is parsed. Only the SUIT reader,
which the image gate already exposes to unauthenticated input, sees bytes that
have not been verified. Every status above exists today; none is added and
none is renumbered.

## Consequences

- One SUIT reader for images, policy sets and scripts (L1). Slice 1 gives
  `tfw_suit_read` a key argument and the integrated-payload member. The image
  gate and its vectors do not change.
- The OTA ADR's subset table gains the integrated payload and the two class
  ids.
- `tools/suit-emit` emits policy sets and scripts for test vectors, signed with
  a throwaway test key; the vectors are literals in `testdata/`.
- go-trusted-platform's `hsm-device-lifecycle` row produces these envelopes,
  and go-xkms's signing service signs them. Neither changes in this slice: a
  gap there is a question below, naming the repository.

## Open questions for the owner

1. **Who owns the payload CDDL (D5)**: this row, as the device's requirement,
   or go-trusted-platform's `hsm-device-lifecycle` row? Either way both SPECs
   name the seam. *go-trusted-platform.*
2. **One envelope per policy set (D4)**, or one per policy with a floor per
   policy and a revocation list?
3. **Scripts bound by digest (D4)**, so a new script means a new policy set; or
   scripts with a sequence floor of their own?
4. **Encryption.** A policy reveals the tenant's response to whoever reads the
   channel. Is it encrypted (the `ecdh-a128ctr` half of the same MTI profile),
   or is a signed plaintext enough?
5. **Key rotation in band** (the old key signing the new) or only by a new
   AHAB container (D3)? *go-trusted-platform*, `hsm-device-lifecycle`.
6. **One tenant per board.** The row says "the tenant". Is a board ever shared
   by several tenants, each with a key?
7. **Bind a policy set to one device** (`suit-parameter-device-identifier`) or
   to the board class (D2)?
8. **Durable floor.** A floor that survives a reset needs storage the A55
   cannot roll back; the M7 has none (architecture.md question 12).
9. **The expected values S3 compares**: which fields of the enclave's device
   attestation, and their sizes. RM00284 §3.12, §3.13, cited via the OTA ADR
   decision 12, to confirm (architecture.md question 17).
10. **May a rule pair an A55-reported signal (S4, S5 through Linux) with a
    wipe?** If not, step 7 refuses such a set (architecture.md question 15).
11. **The signing service and the key.** Which go-xkms row signs with the
    tenant key, and is it the same service that signs images under `ota`?
    *go-xkms.*
