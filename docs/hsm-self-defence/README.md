# HSM self-defence

> **Status: PROPOSED** · SPEC row: `hsm-self-defence` (future) · Issue:
> [#50](../../../../issues/50) · Last verified: 2026-10-20

The tamper response of the i.MX 95 HSM appliance, enforced by our firmware on
the SoC's Cortex-M7 so that a compromised Linux on the A55 cannot suppress it.
Deterministic tamper signals come in; the M7 acts through NXP's System Manager
and the EdgeLock enclave; a tenant-signed policy decides which action follows
which signal. Implemented: slice 1, the SUIT reader for an integrated payload
under the caller's key ([engineering.md](engineering.md), slices). The rest is
not implemented yet.

| Document | What it holds |
|---|---|
| [`architecture.md`](architecture.md) | Every requirement of the row with its component, signal source, action path and state; the signals, the actions, the decision table, the seams and the open questions |
| [`adr-response-policy-format.md`](adr-response-policy-format.md) | **PROPOSED**: the signed policy set and script as SUIT envelopes with an integrated payload, ES256 under the tenant's key, the floor, the payload CDDL, the check in order, and the owner's questions |
| [`engineering.md`](engineering.md) | The invariants, the error mapping, and the test plan: host tests of the verifier and the engine, the QEMU loop, then the board; the slices |

This repository owns the M7 enforcement and the signature check. The keys are
go-xkms's, the policies arrive through go-trusted-platform's control plane, the
platform image and System Manager configuration are trusted-imx95's, and the
A55 detector, the NPU model and the IMA log are outside the project.

Related: rows `imx95` ([`../multi-arch/imx95-design.md`](../multi-arch/imx95-design.md))
and `cortex-m` ([`../cortex-m/`](../cortex-m/README.md)).
