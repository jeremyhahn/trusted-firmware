# HSM self-defence: engineering

> **Status: PROPOSED** · SPEC row: `hsm-self-defence` (future) · Scope:
> trusted-firmware · Last verified: 2026-10-21
>
> The implementation contract and the test plan. No code implements the row
> yet; nothing here names an identifier that does not exist, except where it is
> marked proposed.

Design: [architecture.md](architecture.md). Policy format and check:
[adr-response-policy-format.md](adr-response-policy-format.md) ("ADR").

## Surface reused

| Code | Today | What the row needs from it |
|---|---|---|
| `tfw_suit_read` (`src/core/suit.h:66`) | Reads and authenticates an envelope against the board's image key; detached payload only | A key argument and the integrated payload (ADR D1, D3), slice 1. **Met:** `tfw_suit_read_integrated` (`src/core/suit_integrated.c`), beside `tfw_suit_read` so no image link grows; vectors `SUIT_POLICY_*` and `SUIT_TENANT_PUB`; `test/unit/test_suit_integrated.c` |
| `tfw_suit_gate` (`src/core/suit.h:80`) | The image gate before activation | Unchanged, with its vectors |
| `TFW_SUIT_VENDOR_ID` (`include/tfw/config.h`), `TFW_SUIT_CLASS_ID` (`boards/*/board_profile.h`) | Vendor id; one class id per board | Two more class ids per board that runs the row (ADR D2), in its board profile. **Met:** `TFW_SUIT_POLICY_CLASS_ID` and `TFW_SUIT_SCRIPT_CLASS_ID` in every profile with an image class id (328P, 2560, 32U4, ESP32 QEMU, ESP32-P4), all zero by default in `include/tfw/config.h` (no class); `tfw_suit_read_integrated` holds them and returns the kind |
| `tools/suit-emit/` | Emits image envelopes for the vectors | Emits policy sets and scripts, signed with a throwaway test key |
| `testdata/suit_vectors.h` | Literal SUIT vectors | New literal vectors beside the old ones; no old vector changes |
| micro-ecc (`src/third_party/micro-ecc/`) | ES256 verify in software (`boot/verify/verify_p256_soft.c`) | The policy signature on the M7 |

## Invariants

- **Fail closed.** A refused envelope changes nothing: the previous set, its
  selection and the floor stay (ADR D7). The refusal is counted.
- **Authenticate first.** No payload byte is parsed before the manifest digest
  and the signature hold (ADR D7, steps 1–3).
- **Lockdown is latched.** Nothing but a reset ends it, and no later set,
  selection or message lowers it (architecture.md §5, R6).
- **A wipe needs corroboration.** A4 runs only when R3 holds, and counts as
  done only once the enclave's strict operation is acknowledged
  (architecture.md §3, A4).
- **The default never wipes** (R1, R5).
- **The image key never verifies a policy**, and the tenant key never an image
  (ADR D3).
- **No new status code.** Every refusal maps to an existing one (ADR D7).
  Status codes and command ids extend and never renumber (`CLAUDE.md`).
- **No `#if` in the engine.** The engine is portable C. The SoC's actions sit
  behind one port header, implemented per SoC and by the mock, chosen by the
  object list and linked, never through a vtable (`CLAUDE.md`).
- **No `delay()`.** The heartbeat (S6) and the window *W* are counted from the
  HAL's time base, never by busy waiting.

## Error semantics

ADR D7 maps each step of the check to `TFW_ERR_RANGE`, `TFW_ERR_UNSUPPORTED`,
`TFW_ERR_CRYPTO` or `TFW_ERR_PROTECTED_REGION` (`include/tfw/status.h`), the
same mapping `tfw_suit_gate` uses for images. When the channel commands land
(slice 4) they return these codes on the wire.

## Persistence

TCM only: the verified set, the selection, the floor since reset, the latch and
the record. Nothing survives a reset until durable storage exists
(architecture.md question 12; ADR question 8).

## Test strategy

Three levels, in this order. A level starts only when the one before it is
green in CI. Every test is in `make ci`, with no skip guard: a test that cannot
run fails.

### 1. Host: the verifier and the engine (`make host-test`)

Unit tests under `test/unit/`, against the mock HAL. Vectors are literals in
`testdata/`, emitted once by `tools/suit-emit` with a throwaway test key and
never recomputed by the code they pin.

**The verifier** (slice 1), one case per row of ADR D7:

| Case | Expect |
|---|---|
| A policy set signed with the tenant key | `TFW_OK`; set, floor and selection replaced |
| The same set, verified with the image key | `TFW_ERR_CRYPTO` |
| An image manifest presented as a policy set; another board's policy class; another vendor; a class of all zero | `TFW_ERR_UNSUPPORTED` (class id). **Met** |
| A script presented as a policy set, and the reverse: a kind the caller does not expect, and a class that claims one kind while its payload carries the other | `TFW_ERR_UNSUPPORTED`. **Met** |
| A board with no policy or script class (all zero) | Every envelope `TFW_ERR_UNSUPPORTED`. **Met:** `test_suit_integrated_unbound` (the ATtiny85's profile) |
| One flipped bit in the signature, the manifest, the payload | `TFW_ERR_CRYPTO` |
| The integrated payload missing, or its size wrong | `TFW_ERR_RANGE` |
| Truncated at every byte; larger than the buffer | `TFW_ERR_RANGE` |
| Sequence below the floor | `TFW_ERR_PROTECTED_REGION` |
| Sequence equal to the floor, same digest; different digest | `TFW_OK`; `TFW_ERR_PROTECTED_REGION` |
| Every existing image vector | Unchanged results |

A mutation test (`test/unit/test_suit_mutate.c` is the pattern) checks that
removing any single comparison in the check fails a case. **Met:**
`test/unit/test_suit_integrated_mutate.c` reaches every refusal path of
`src/core/suit_integrated.c` with one case each, pinning the exact status;
a manifest is behind the signature, so its cases are signed at run time by
`tools/suit-emit/` with the throwaway tenant key. Of 83 source mutants of
the reader (each check deleted, each status swapped, each bound moved, the
class and kind checks weakened or reordered), the three tests kill 82; the one
left drops `members != 3u` from the payload check, where a detached envelope
is still refused with the same status by its empty name. Then the seeded mutation of
`test_suit_mutate.c` over the integrated vectors. The kind is read from the
class id and checked against the payload's top-level type (a map is a policy
set, ADR D5; an array a script, D6); the rest of the payload is the engine's.
The sequence floor (the two rows above) is the engine's, in slice 2.

**The payload reader and the decision engine** (slice 2):

| Case | Expect |
|---|---|
| Every row of architecture.md §5's table | That row's response |
| R1–R7, each on its own | The rule's response |
| An unknown map key, signal or action; a non-deterministic encoding | `TFW_ERR_UNSUPPORTED`, set unchanged |
| Each bound (policies, rules, actions) at its limit and one past it | Accepted; `TFW_ERR_RANGE` |
| A selection out of range | Refused, selection unchanged |
| A script named by a digest the set does not hold | Lockdown |
| A wipe with *V* outside *W*; with *V* not "compromised"; on S6 | No wipe |
| Lockdown, then any input | Still locked down |

The actions are recorded by the mock in order, so a test asserts the exact
sequence, not just the last action.

### 2. QEMU: the loop (slice 3)

The engine and its loop on the generic Cortex-M port, on the QEMU machine #161
lands (`mps2-an505` per the `cortex-m` row; ref ADR decision 3 names
`mps2-an500` for the M7; the engine runs on either). The board's port maps the
FPGAIO switches to S1 and S5, the LEDs to the actions, and a UART to the
channel. Its make target is the one #161 adds for the port.

| Case | Expect |
|---|---|
| No set loaded, any switch | Lockdown |
| A forged set, a replayed set | Refused; the default stays |
| A valid set, then silence past the heartbeat period | Lockdown, no wipe |
| A deterministic signal with a "compromised" verdict inside *W*, on a set that pairs them with wipe | Wipe, then lockdown |
| The same signal with no verdict | The set's actions, no wipe |
| Lockdown, then a laxer valid set | Still locked down |

The wipe on QEMU is the mock's record; no enclave is emulated.

### 3. The board (slices 5–8)

On the bench host's FRDM-IMX95-PRO, once rows `imx95` (#49) and trusted-imx95's
M7 logical machine exist.

- The SCMI actions (A1, A2, A5) are first tested on the host against recorded
  SCMI messages, then on the board.
- The enclave (S3, A3, A4) is first tested on the host against a model of the
  messaging unit, then on the board with throwaway keys only.
- The board's factory state is recorded before any write. Any fuse or tamper
  configuration is the owner's decision; nothing is fused by a lane.
- End to end: a stand-in detector on the A55 sends the heartbeat, the verdict
  and the selection.

## Slices

Each is one lane and one PR. **Gates:** every slice waits on #7, the AVR
end-to-end gate (`docs/multi-arch/architecture.md`, header), unless the owner
lifts it for this row as for `esp32-p4` and the Cortex-M ADRs. Slices 5–8 also
wait on row `imx95` (#49) and trusted-imx95's M7 logical machine.

| # | Slice | Files | Test | Gate |
|---|---|---|---|---|
| 0 | **This folder.** | `docs/hsm-self-defence/`, `docs/reference/rfc8610.txt` | `make releasability` | — |
| 1 | **The SUIT reader takes a key and an integrated payload.** | `src/core/suit.c`, `suit.h`; `tools/suit-emit/`; `test/unit/` SUIT tests; literal vectors in `testdata/`; the OTA ADR subset table | Host, level 1 verifier table | #7; `make ci` |
| 2 | **The payload reader and the decision engine.** | The engine's `.c` files and port header (where they live: architecture.md question 16); `test/unit/` | Host, level 1 engine table | #7; `make ci` |
| 3 | **The loop on QEMU.** | The loop; the `mps2` board port; the harness | Level 2 | #7, #161 |
| 4 | **Channel commands.** | `include/tfw/protocol.h` (ids extend), `sdk/go`, literal vectors in `testdata/` and the Go mirror; `docs/wire-protocol/` | Host unit and `tfwsim`: heartbeat, report, verdict, select, load | #7 |
| 5 | **SCMI actions A1, A2, A5.** | `src/hal/cortex-m/imx95/`; the mock; `docs/reference/README.md` (Arm SCMI, DEN0056, not redistributable) | Host against recorded SCMI messages; then the board | #49; architecture.md questions 7, 10 |
| 6 | **Enclave: S3, A3, A4.** | `src/hal/cortex-m/imx95/` | Host against a model of the messaging unit; the board, throwaway keys | #49; questions 3–5, 17 |
| 7 | **Silicon signals S1, S2, S5.** | `src/hal/cortex-m/imx95/`, `boards/frdm-imx95-m7/` | The board | #49; questions 1, 2, 6, 11 |
| 8 | **Bench.** | The board page | Level 3, end to end | #49, trusted-imx95 |

Slices 1, 2 and 4 need no board and no Cortex-M port; 3 needs #161. The model
work is not a slice here: the row starts it only after the HSM node runs.
