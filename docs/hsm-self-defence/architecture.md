# HSM self-defence: architecture

> **Status: PROPOSED** · Issue: [#50](../../../../issues/50) · SPEC row:
> `hsm-self-defence` (future) · Scope: trusted-firmware (the M7 enforcement
> and the signature check); go-xkms, go-trusted-platform, trusted-imx95 and
> NXP (seams only) · Last verified: 2026-10-22
>
> No M7 code implements this row yet; slices 1 and 2 (the SUIT reader's
> tenant key, the payload reader and the decision engine) run on the host
> only, under `make host-test`. Slice 4 (the channel commands `0x76`–`0x7A`)
> runs there and in tfwsim-atecc608a, under `make sdk-test`. Every requirement and its
> state is in section 1a. The policy format is decided in
> [`adr-response-policy-format.md`](adr-response-policy-format.md), the tests
> are in [`engineering.md`](engineering.md).

## Sources and how they are cited

Every claim names its source as `path` and section on main, or as a vendor
document and section.

- **This repository:** `docs/SPEC.md` rows `hsm-self-defence`, `imx95`,
  `cortex-m` and `ota`; [`adr-reference-part-and-emulation.md`](../cortex-m/adr-reference-part-and-emulation.md)
  (#14, "ref ADR" below); [`adr-trust-model-vs-tf-m.md`](../cortex-m/adr-trust-model-vs-tf-m.md)
  (#15, "trust ADR"); [`../ota/adr-ota-image-format-and-trust.md`](../ota/adr-ota-image-format-and-trust.md)
  decision 12 ("OTA ADR d12"); `docs/reference/`.
- **NXP, in trusted-imx95's `docs/references/`: not reached.** No copy was
  available to the lanes that wrote this page, so UM12527, RM00284, AN14471 and
  the CST were not read for it, and every vendor fact here is marked "to
  confirm". Facts from UM12527 Rev. 1.0 and RM00284
  Rev. 4.5 are cited **via OTA ADR d12**, where they were checked under #52.
  OTA ADR d12 records that AN14471 covers the i.MX RT1170, not the i.MX 95, so
  nothing is cited from it; the CST signs AHAB containers, which are
  trusted-imx95's. What this page would have read is listed against each open
  question (section 8).
- **Missing and not guessed:** the i.MX 95 Reference Manual (IMX95RM) and
  Security Reference Manual (IMX95SRM), both under NDA; the System Manager
  interface document; the tamper pin and fuse map. Every fact the design needs
  from one of them is an open question.
- **Not in `docs/reference/`, not fetched:** Arm SCMI (DEN0056), the protocol
  the M7 speaks to the System Manager (ref ADR decision 4, `tfw_hal_init` row).
  Slice 5 lists it under "Not redistributable".

## 1. Scope and non-goals

The row, word for word (`docs/SPEC.md` row `hsm-self-defence`):

> HSM self-defence on the i.MX 95: the M7 application of row `imx95`, on the
> `cortex-m` port, enforces the tamper response, so a compromised Linux side on
> the A55 cannot suppress it. Its inputs are deterministic tamper signals: the
> EdgeLock tamper pins, AHAB and measurement failures, IMA violations, hotplug
> events, and loss of the heartbeat from the tamper detector on the A55. It acts
> through NXP's System Manager and the EdgeLock secure enclave: power domains,
> PHYs, key zeroisation and shutdown. A response policy signed by the tenant
> decides which action follows which signal, a custom response script is a
> signed artefact too, and the M7 accepts only a policy or script whose tenant
> signature verifies. The NPU model on the A55 selects among the signed policies
> and never acts on its own. The default response is lockdown, and keys are
> wiped only when a deterministic signal corroborates the model's verdict. Seam
> to go-xkms: the keys the M7 locks or wipes are those of go-xkms, which runs on
> the board as the HSM appliance, and go-xkms's SPEC names the same seam. Seam
> to go-trusted-platform: the signed policies and scripts reach the device
> through go-trusted-platform's control plane, which provisions and attests the
> HSM device (its `hsm-device-lifecycle` row), and go-trusted-platform's SPEC
> names the same seam. This row owns the M7 enforcement and the signature check.
> The A55 detector, the NPU model and the IMA log are outside this project, the
> System Manager stays NXP's, and the model work starts only after the HSM node
> runs.

**In scope** (the row's last sentences): the M7 enforcement, meaning signal
intake, the decision and the dispatch of actions; and the signature check of
the policy and the script.

**Not in scope**, each with its owner (L1, L2):

| Not ours | Owner |
|---|---|
| The keys locked or wiped, their key store and its sessions | go-xkms |
| The policy and the script: authoring, signing request, delivery, provisioning and attestation of the HSM device | go-trusted-platform (`hsm-device-lifecycle`) |
| The policy signing key | off the device, reached through go-trusted-platform (section 4; ADR D3) |
| The System Manager, its SCMI agents and its configuration | NXP (`imx-sm`); configured by trusted-imx95 (row `imx95`) |
| The EdgeLock enclave firmware | NXP |
| The M7 boot container, the platform image | trusted-imx95 (row `imx95`) |
| The A55 tamper detector, the NPU model, the IMA log | outside the project |

**Preconditions owned elsewhere.** The `cortex-m` port (#161) and the `imx95`
board (#49). The M7 is started by the ROM and the System Manager, never by
Linux `remoteproc` (ref ADR, Constraints). Whether the System Manager's
configuration stops the A55 halting, resetting or writing the M7 is the trust
ADR's open question 7; without it nothing on this page holds (question 8).

## 1a. Every requirement, its component and its state

The row split into its requirements, in its own order. **Met** means code on
`main` does it, **unmet** that this project owes it, **outside** that another
owner does. No requirement is met yet, as none runs on the M7. "Host" marks a
requirement whose logic is portable C in `src/core/` (question 16), tested by
`make host-test` (engineering.md, level 1) and not yet linked on any target.

| # | Requirement (row) | Component | Signal source | Action path | State |
|---|---|---|---|---|---|
| H1 | The M7 application of row `imx95`, on the `cortex-m` port | The enforcement engine and loop, on the M7 HAL (§3, "Where the code sits") | — | — | Unmet: needs `cortex-m` (#161) and `imx95` (#49) |
| H2 | A compromised Linux on the A55 cannot suppress the response | The M7 started by the ROM and the System Manager; its TCM and resources closed to the A55 | — | — | Unmet here; the isolation is trusted-imx95's System Manager and TRDC configuration (questions 8, 9) |
| H3 | Input: the EdgeLock tamper pins | Signal intake, S1 | The SoC's tamper logic | — | Unmet (questions 1, 2) |
| H4 | Input: AHAB failures | Signal intake, S2 | The ROM and the enclave | — | Unmet (question 6) |
| H5 | Input: measurement failures | Signal intake, S3 | The enclave's device attestation, compared with the policy's expected values | The enclave, over the M7's messaging unit | Unmet (questions 3, 17) |
| H6 | Input: IMA violations | Signal intake, S4 | The A55 detector over the channel; the detector and the IMA log are outside | — | Intake unmet; the source outside |
| H7 | Input: hotplug events | Signal intake, S5 | Linux over the channel, or a pin the M7 owns | — | Unmet (question 11) |
| H8 | Input: loss of the detector's heartbeat | Signal intake, S6 | The M7's own timer against the policy's period | — | Unmet |
| H9 | Acts on power domains | Action dispatch, A2 | — | SCMI to the System Manager | Unmet (question 7) |
| H10 | Acts on PHYs | Action dispatch, A1 | — | A PHY reset line or power domain, through the System Manager | Unmet (questions 7, 10) |
| H11 | Acts on keys: lock and zeroise | Action dispatch, A3, A4 | — | The EdgeLock enclave | Unmet (questions 3–5); the keys are go-xkms's (H18) |
| H12 | Acts by shutdown | Action dispatch, A5 | — | SCMI system power, through the System Manager | Unmet (question 7) |
| H13 | A tenant-signed policy decides which action follows which signal | The policy verifier and the decision engine (§5) | Channel: the policy set | — | Unmet on the M7. Host: the verifier (`src/core/suit_integrated.c`, slice 1), the payload reader (`src/core/hsm_payload.c`, D5) and the engine (`src/core/hsm_engine.c`, §5), slice 2 |
| H14 | A custom response script is a signed artefact | The verifier, script class (ADR D2, D6) | Channel: the script | The actions it lists | Unmet on the M7. Host: D6's reader, and the engine holds a script only while the set names it (slice 2) |
| H15 | Only a policy or script whose tenant signature verifies is accepted | The verifier (ADR D3, D7) | — | — | Unmet on the M7. Host: slices 1 and 2 |
| H16 | The NPU model selects among the signed policies and never acts on its own | Selection by index into the verified set (ADR D4; §5 R4) | Channel: selection and verdict *V*; the model is outside | — | Unmet on the M7; host: `tfw_hsm_select`, R4 (slice 2); the model outside |
| H17 | The default response is lockdown; keys are wiped only when a deterministic signal corroborates the model's verdict | The decision engine, R1, R3, R5, R6 | S1–S6 with *V* | Lockdown (A1, A2, A3, A6); wipe (A4) | Unmet on the M7. Host: R1, R3, R5, R6 (slice 2) |
| H18 | Seam to go-xkms: the keys locked or wiped are go-xkms's, which runs on the board as the HSM appliance | §6 | — | A3, A4 on go-xkms's key store | Outside: go-xkms (question 19) |
| H19 | Seam to go-trusted-platform: policies and scripts arrive through its control plane, which provisions and attests the device (`hsm-device-lifecycle`) | §6 | Channel, from go-trusted-platform's agent on the A55 | — | Outside: go-trusted-platform (ADR questions 1, 5) |
| H20 | This row owns the M7 enforcement and the signature check | H1, H13–H17 | — | — | Unmet |
| H21 | The A55 detector, the NPU model and the IMA log are outside; the System Manager stays NXP's | — | — | — | Outside |
| H22 | The model work starts only after the HSM node runs | — | — | — | Outside this row's slices (engineering.md) |

### Where the enforcement runs, and what was rejected

| Alternative | Why not |
|---|---|
| In Linux on the A55, beside the detector | The row's premise: a compromised A55 suppresses it |
| In the System Manager on the M33 | The System Manager stays NXP's (row `imx95`); code of ours on the M33 only where it leaves room |
| In the EdgeLock enclave | NXP's firmware; it runs no application code of ours |
| The model acting directly | The row: the model selects, never acts (H16) |

## 2. Signals in

"M7-observed" means the M7 sees the signal without the A55: a compromised A55
can neither forge nor suppress it. "A55-reported" means it arrives from the A55
over the inter-core channel (question 9): a compromised A55 can forge it and
suppress it.

| # | Signal (row) | Source | How the M7 learns of it | Class |
|---|---|---|---|---|
| S1 | EdgeLock tamper pins | The SoC's tamper logic | **Open:** an interrupt routed to the M7, a System Manager notification over SCMI, or an enclave event (questions 1, 2) | M7-observed, if routed to the M7 or the System Manager |
| S2 | AHAB failure | The ROM and the enclave, authenticating a container | A failure of the M7's own container means the M7 never runs, and the response is the ROM's. For a container loaded later for the A55, the result goes to the core that asked: **open** whether and how the M7 hears of it (question 6) | M7-observed only if the enclave or the System Manager tells the M7 |
| S3 | Measurement failure | The M7 itself, at boot: the enclave's device attestation reports the lifecycle, the firmware hash and the OEM SRK revocation status (RM00284 §3.12, §3.13, via OTA ADR d12, to confirm), compared against the expected values in the policy | A call over the M7's messaging unit to the enclave, **if the M7 has one** (question 3; OTA ADR d12, "needed from IMX95RM") | M7-observed |
| S4 | IMA violation | The A55 detector, from the IMA log (outside the project) | A message on the inter-core channel | A55-reported |
| S5 | Hotplug | A USB, SD-card or PCIe event | Through Linux: a message on the channel. Directly: a pin the System Manager assigns to the M7, **if one exists** (question 11) | A55-reported, or M7-observed per event |
| S6 | Heartbeat loss | The M7's own timer | No heartbeat message within the policy's period (section 5) | M7-observed. A compromised A55 can force it (which only locks down) but can also keep beating: a heartbeat proves liveness, not integrity |

Two further inputs are **not signals**, because the row says the model "never
acts on its own": the model's **verdict** (V) and its **policy selection**.
Both are A55-reported, on the same channel.

**The channel carries the wire protocol.** Messages from the A55 are commands
of this project's wire protocol (`docs/wire-protocol/`), framed as on every
other transport, with new command ids that extend and never renumber. No
second protocol is designed for one link (L1). Slice 4 allocated them:
`HSM_HEARTBEAT` `0x76`, `HSM_REPORT` `0x77`, `HSM_VERDICT` `0x78`,
`HSM_SELECT` `0x79` and `HSM_LOAD` `0x7A`, each replying with the engine's
state ([`../wire-protocol/api.md`](../wire-protocol/api.md)).

## 3. Actions out

| # | Action (row) | What performs it | Cited, or open |
|---|---|---|---|
| A1 | PHYs off: the network is cut | A PHY reset line or power domain, through the System Manager | **Open:** which line or domain holds the board's Ethernet PHYs off (question 10), and whether the M7's agent may drive it (question 7) |
| A2 | Power domains off: the A55 stops | SCMI power-domain management or the System Manager's logical-machine stop, as an SCMI agent (Arm DEN0056; `imx-sm`) | **Open:** which protocol and which permission the System Manager configuration gives the M7 (question 7) |
| A3 | Keys locked | The enclave | **Open.** The enclave identifies a caller by its domain ID and messaging unit (RM00284 §3.22.1, via OTA ADR d12, to confirm), and go-xkms opens its key store from the A55. Whether the M7's domain can disable or close it is question 4. Until it is answered, A3 is A2: with the A55 off, no session uses the keys |
| A4 | Key zeroisation | The enclave's key or key-group deletion | **Open:** the call (question 5). Cited: the enclave writes the key store to NVM only after a strict operation (RM00284 §2.4, via OTA ADR d12, to confirm), so a wipe that is not committed by a strict operation could come back from NVM at the next boot. The wipe is defined as complete only once the strict operation is acknowledged |
| A5 | Shutdown | SCMI system power, through the System Manager | **Open:** permission for the M7's agent (question 7) |
| A6 | Record | The M7 keeps what it saw and did, for go-trusted-platform to read after recovery | **Open:** the M7 has no non-volatile store of its own (ref ADR open question 4; question 12) |

**Lockdown** is A1 then A2 (A3 once it exists), then A6. It is latched: nothing
but a reset ends it, and no later policy, selection or message lowers it
(section 5, rule R6).

**Where the code sits.** Privileged M7 code holds the policy-signature check
and the only mapping of the enclave's interface (trust ADR option (c)); the
SCMI and enclave clients are the board's HAL under `src/hal/cortex-m/imx95/`
(ref ADR decision 2). The actions are reached through one port header,
implemented once per SoC and by the mock at link time, never through a vtable
(CLAUDE.md). The payload reader and the decision engine are portable C in
`src/core/` (question 16); `src/core/hsm_port.h` is that port header.

## 4. The signed policy and the signed script

Decided in [`adr-response-policy-format.md`](adr-response-policy-format.md);
the shape, in brief:

- **Envelope** (ADR D1): a SUIT envelope (draft-ietf-suit-manifest-37) with the
  payload integrated, authenticated by a `COSE_Sign1_Tagged` (RFC 9052), ES256
  and SHA-256 (draft-ietf-suit-mti-23). One reader, `src/core/suit.c`, for
  images, policy sets and scripts (L1).
- **Separation** (D2): a policy class id and a script class id per board, beside
  the image class id, so an image manifest never passes as a policy.
- **Key** (D3): the tenant's ECDSA P-256 public key, read-only in the M7 image
  inside its AHAB container. The private half is never on the device it
  defends: go-xkms runs on this board, and a compromised board must not sign
  itself a laxer policy.
- **Delivery and replay** (D4): envelopes arrive on the inter-core channel from
  go-trusted-platform's agent on the A55 and are kept in TCM. One envelope
  carries the tenant's whole policy set; its sequence number is checked against
  a floor; scripts are named by digest from the set.
- **Withheld:** the built-in default applies (section 5, R1), which only locks
  down.
- **Selected:** the model selects an entry of the verified set. A compromised
  A55 can select the laxest of them, so every signed policy is one the tenant
  accepts.
- **Payload** (D5, D6): deterministic CBOR under this row's CDDL; a script is a
  bounded list of actions, never code.
- **Check** (D7): signature before the payload is parsed, fail closed, existing
  status codes only.

## 5. The default, and the decision table

**Inputs at a decision:** the signal *s*; the set *D* of signals seen in the
corroboration window *W*; the verdict *V* last received in *W*; the selected,
verified policy *P*, or none.

- **R1.** No *P*: the built-in default. Any signal locks down. It never wipes.
- **R2.** With *P*: the actions *P* names for *s*, except a wipe unless R3
  holds. A signal *P* does not name locks down.
- **R3. Wipe** only if *P* names (*s*, wipe), *V* is "compromised" in *W*, and
  *s* is one of the row's deterministic signals (S1–S6). Then lockdown follows.
- **R4.** *V* with no signal acts on nothing: it selects, it is recorded.
- **R5.** Heartbeat loss (S6) locks down. No verdict arrives after it, so it
  never wipes on its own.
- **R6.** Lockdown is latched until reset; nothing lowers it.
- **R7.** An envelope that fails verification, or is below the floor, is
  refused and counted; the previous policy stays. Only those are counted: a
  load chunk out of sequence, or longer than is left, drops the pending
  envelope with the same refusal status and is not counted.

| Signal | *P* | *V* in *W* | *P* names | Response |
|---|---|---|---|---|
| any | none | any | — | lockdown (R1) |
| S1–S6 | yes | none, or not compromised | any | *P*'s actions, wipe excluded; lockdown if *P* names nothing (R2) |
| S1–S6 | yes | compromised | wipe | wipe, committed by a strict operation, then lockdown (R3, A4) |
| S1–S6 | yes | compromised | no wipe | *P*'s actions (R2) |
| none | any | any | — | none; selection only (R4) |
| S6 | any | — | — | lockdown, never wipe (R5) |
| bad envelope | any | any | — | refused, counted (R7) |

**The risk the row leaves.** S4 and S5 through Linux are A55-reported, and so
is *V*. A compromised A55 that forges S4 and a "compromised" verdict can
trigger a wipe wherever the tenant's policy pairs S4 with wipe: a denial of
service by key destruction. The design implements the row as written and the
built-in default never wipes; whether the M7 should refuse a policy that lets
an A55-reported signal corroborate a wipe is question 15.

## 6. Seams

| Seam | Other side | Its SPEC row | Ours | What crosses |
|---|---|---|---|---|
| Keys locked or wiped | go-xkms | the seam row its SPEC names for this one (name not read here; question 19) | `hsm-self-defence` | Which key store and key groups the M7 acts on, and the domain that opens them |
| Policy and script delivery, device provisioning and attestation | go-trusted-platform | `hsm-device-lifecycle` | `hsm-self-defence` | The SUIT envelopes, the payload CDDL, the tenant key value, its rotation, the record A6 |
| Policy signing | go-xkms's signing service, reached by go-trusted-platform's control plane, off the device | its signing-service row (as `ota`'s seam) | `hsm-self-defence` | Signatures over the envelopes; no private key here |
| M7 logical machine, TCM, MU, TRDC, SCMI permissions, the container with the M7 image and the tenant key | trusted-imx95 | its platform-image row (not read here) | `imx95` | The configuration questions 7–9 depend on |
| System Manager | NXP `imx-sm` | — | `imx95`, `hsm-self-defence` | SCMI calls A1, A2, A5 |
| EdgeLock enclave | NXP | — | `imx95`, `hsm-self-defence` | S3, A3, A4 (RM00284) |
| Detector, model, IMA log | outside the project | — | `hsm-self-defence` | Channel messages S4, S5, S6, *V*, selection |
| The SUIT reader | this repository | `ota` | `hsm-self-defence` | One reader for images and envelopes (L1) |
| The port | this repository | `cortex-m` (#161), `imx95` (#49) | `hsm-self-defence` | The HAL and boot HAL the M7 runs on |

## 7. Slices and tests

The slices, their gates and the test plan (host, QEMU, board) are in
[`engineering.md`](engineering.md).

## 8. Open questions

Each names the document that answers it. The policy format's own questions
are in the ADR; 13 and 14 here are its questions 1 and 5.

1. **Tamper pins:** how many, which pins on the board, how they are armed,
   whether arming burns fuses. IMX95SRM (tamper chapter), the fuse map;
   UM12527 (not read here).
2. **How a tamper event reaches the M7:** interrupt, SCMI notification or
   enclave event. IMX95RM (interrupt map), the System Manager interface
   document, IMX95SRM.
3. **Whether the M7 has a messaging unit to the enclave.** IMX95RM (open since
   OTA ADR d12).
4. **Whether the M7's domain can lock or delete keys in a key store go-xkms
   opened from the A55.** RM00284 (key store and key management chapters; not
   read here), IMX95SRM.
5. **The deletion call, and the strict operation that makes a wipe durable.**
   RM00284 (key management; §2.4 cited via OTA ADR d12, to confirm).
6. **How the M7 hears of an AHAB failure of a container loaded for the A55.**
   RM00284 (not read here), IMX95RM (AHAB chapter).
7. **Which SCMI protocols and resources the M7's agent may use**: A55 power
   domains or logical machine, PHY lines, system shutdown. The System Manager
   interface document; trusted-imx95's configuration.
8. **Whether the configuration stops the A55 halting, resetting or writing the
   M7 or its TCM.** IMX95RM (TRDC chapter), the System Manager interface
   document, trusted-imx95 (trust ADR open question 7).
9. **The A55–M7 channel:** which messaging unit and shared memory, and who
   assigns them. IMX95RM (MU chapter), trusted-imx95. What it carries is
   settled (slice 4, §2): the wire-protocol commands `0x76`–`0x7A`; only the
   transport is open.
10. **How the Ethernet PHYs are held off.** UM12527 (not read here), IMX95RM.
11. **Which hotplug events the M7 can see without Linux.** UM12527, IMX95RM.
12. **Durable storage for the lockdown latch, the policy floor and the record**,
    on an M7 with no NVM of its own. IMX95SRM, IMX95RM; RM00284 §3.8 rules out
    enclave data storage for the floor.
13. **The payload CDDL, and whether a script is a list of actions or code.**
    go-trusted-platform's `hsm-device-lifecycle` row; the owner.
14. **In-band rotation of the tenant key.** go-trusted-platform's
    `hsm-device-lifecycle` row.
15. **May an A55-reported signal corroborate a wipe?** The owner; the SPEC row
    lists IMA violations among the deterministic signals.
16. **Where the enforcement lives:** here, as the row says, or in a
    `trusted-firmware-<application>` repository on the application hooks, as
    `docs/application-sdk/architecture.md` ("Problem") says of application
    logic. **Decided (ruling 109, slice 2):** here, in `src/core/` beside the
    SUIT reader it consumes: `hsm_payload.c`, `hsm_engine.c` and `hsm.h`,
    with the actions behind one port header, `src/core/hsm_port.h`, that the
    M7 implements later. The engine is portable C with no HAL call, so
    nothing in it is the application's, and the row owns the enforcement and
    the signature check (H20). Only its tests build it until slice 3 links it
    on QEMU.
17. **The expected values S3 compares, and whether device attestation is
    callable from the M7.** RM00284 §3.12, §3.13 (via OTA ADR d12, to confirm); IMX95RM.
18. **The trusted-imx95 references themselves:** UM12527, RM00284 and the CST
    were not read for this page (no route to the forge). The next slice that
    reaches them re-checks every "via OTA ADR d12" citation here.
19. **The name of go-xkms's seam row for this one.** go-xkms's SPEC.
