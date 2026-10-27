# HSM self-defence: engineering

> **Status: PROPOSED** · SPEC row: `hsm-self-defence` (future) · Scope:
> trusted-firmware · Last verified: 2026-10-22
>
> The implementation contract and the test plan. Slices 1, 2 and 4 are built
> and their tables met (marked **Met**); nothing here names an identifier that
> does not exist, except where it is marked proposed.

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
same mapping `tfw_suit_gate` uses for images. The channel commands (slice 4)
return these codes on the wire unchanged, and `TFW_ERR_RANGE` for a signal the
A55 may not report, a verdict past compromised, a selection past the set and a
load chunk out of sequence ([wire-protocol api](../wire-protocol/api.md)).

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
| A policy set signed with the tenant key | `TFW_OK`; set, floor and selection replaced. **Met:** `test_a_set_signed_with_the_tenant_key_replaces_set_floor_and_selection` (slice 2) |
| The same set, verified with the image key | `TFW_ERR_CRYPTO`. **Met:** `test_the_same_set_with_the_image_key_is_crypto` (slice 2) |
| An image manifest presented as a policy set; another board's policy class; another vendor; a class of all zero | `TFW_ERR_UNSUPPORTED` (class id). **Met** |
| A script presented as a policy set, and the reverse: a kind the caller does not expect, and a class that claims one kind while its payload carries the other | `TFW_ERR_UNSUPPORTED`. **Met** |
| A board with no policy or script class (all zero) | Every envelope `TFW_ERR_UNSUPPORTED`. **Met:** `test_suit_integrated_unbound` (the ATtiny85's profile) |
| One flipped bit in the signature, the manifest, the payload | `TFW_ERR_CRYPTO`. **Met:** `test_a_flipped_bit_or_a_truncation_is_refused` (slice 2) |
| The integrated payload missing, or its size wrong | `TFW_ERR_RANGE`. **Met:** `test/unit/test_suit_integrated_mutate.c` (slice 1) |
| Truncated at every byte; larger than the buffer | `TFW_ERR_RANGE`. **Met:** truncation by `test_a_flipped_bit_or_a_truncation_is_refused` (slice 2); larger than the buffer by `test_hsm_cmd`'s `test_an_envelope_larger_than_the_buffer_is_refused` (slice 4), where HSM_LOAD's first chunk names a total past the 1024-byte buffer |
| Sequence below the floor | `TFW_ERR_PROTECTED_REGION`. **Met:** `test_a_sequence_below_the_floor_is_protected_region` (slice 2) |
| Sequence equal to the floor, same digest; different digest | `TFW_OK`; `TFW_ERR_PROTECTED_REGION`. **Met:** `test_a_sequence_equal_to_the_floor_needs_the_same_digest` (slice 2); the digest is SHA-256 of the bstr-wrapped manifest, `tfw_suit_integrated_digest` |
| Every existing image vector | Unchanged results. **Met:** slice 2 changes no vector and no existing test, and all pass |

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
| Every row of architecture.md §5's table | That row's response. **Met:** `test_row_*`, one per row |
| R1–R7, each on its own | The rule's response. **Met:** `test_r1_*` to `test_r7_*`, three for R3 |
| An unknown map key, signal or action; a non-deterministic encoding | `TFW_ERR_UNSUPPORTED`, set unchanged. **Met:** `test/unit/test_hsm_payload.c`; through the engine, `test_r7_a_refused_envelope_leaves_the_previous_policy` |
| Each bound (policies, rules, actions) at its limit and one past it | Accepted; `TFW_ERR_RANGE`. **Met:** `test_each_bound_at_its_limit_and_one_past`, with the expected values (8 keys, 64 bytes) and a period past 32 bits |
| A selection out of range | Refused, selection unchanged. **Met** |
| A script named by a digest the set does not hold | Lockdown. **Met:** `test_a_script_the_engine_does_not_hold_locks_down` |
| A wipe with *V* outside *W*; with *V* not "compromised"; on S6 | No wipe. **Met:** `test_r3_a_wipe_needs_a_compromised_verdict_inside_w`, `test_row_heartbeat_loss_locks_down_and_never_wipes` |
| Lockdown, then any input | Still locked down. **Met:** `test_r6_lockdown_then_any_input_is_still_locked_down` |

The actions are recorded by the mock in order, so a test asserts the exact
sequence, not just the last action.

**Met** (slice 2): the reader is `src/core/hsm_payload.c`, the engine
`src/core/hsm_engine.c`, both portable C behind `src/core/hsm.h` and the port
header `src/core/hsm_port.h` (architecture.md question 16), with no dynamic
allocation. Every input is literal CBOR, wrapped by `tools/suit-emit/` and
signed at run time with the throwaway tenant key. Of 81 source mutants of the
two files (each comparison flipped or moved by one, each conjunct dropped, each
assignment deleted), the suites kill 78. The three left are equivalent:
dropping `h->set.n == 0u` from R1 (an empty set names no signal, so R2 locks
down the same), dropping `r->n == 0u` where a script is matched by digest (an
action list never equals a SHA-256), and dropping `c->n != 0u` before the
response's type byte (an empty input is refused as a bstr with the same status).

How the engine reads the rows where architecture.md leaves a choice:

- The floor starts at the built-in minimum, and a set at that sequence is
  accepted while no set has been verified (ADR D4).
- A script loads only while the current set names its digest
  (`TFW_ERR_UNSUPPORTED` otherwise); eight are held (`TFW_ERR_NO_SLOT` past
  that); a new set drops those it no longer names.
- A decision is taken at a signal only; a verdict alone acts on nothing (R4).
  A verdict other than "compromised" counts as not compromised.
- S6 locks down whatever *P* says, and never wipes (R5). After lockdown a
  signal still runs *P*'s actions, but the latch never clears and the
  heartbeat timer stops.
- A script-ref of 31 bytes is not the CDDL (`TFW_ERR_UNSUPPORTED`); one of 33
  is past the bound (`TFW_ERR_RANGE`).

Open: R3's deterministic signal is implicit, as the reader admits S1–S6 only,
and *D* (the signals seen within *W*) is not kept, as no row reads it yet.

**The channel commands** (slice 4): HSM_HEARTBEAT `0x76`, HSM_REPORT `0x77`,
HSM_VERDICT `0x78`, HSM_SELECT `0x79` and HSM_LOAD `0x7A`
([wire-protocol api](../wire-protocol/api.md)), handled in `src/core/hsm_cmd.c`
over the engine, gated by `TFW_FEATURE_HSM` in `src/core/commands.def`. Every
reply that is not a refusal is the state `[flags][selected][refused u32]`.
Each handler ticks the engine first, so a late heartbeat is S6 at the next
message, and takes the tenant key from `hsm_port.h`, never from the channel.
LOAD reassembles `[total:2][offset:2][≤60 bytes]` chunks in the 1024-byte
envelope buffer and verifies the whole envelope at the last.

| Case | Expect |
|---|---|
| The envelope in chunks of every size, an empty first chunk, a chunk out of sequence, offset 0 again | Held once whole; a chunk out of sequence, or one longer than is left, drops the envelope, `TFW_ERR_RANGE`, and is not counted (only an envelope that fails verification, or is below the floor, counts: R7). **Met:** `test/unit/test_hsm_cmd.c` |
| A total of 0 or past the buffer; an envelope that fills it | `TFW_ERR_RANGE`, counted; read. **Met** |
| One flipped signature byte | `TFW_ERR_CRYPTO`, counted, the held set kept. **Met** |
| SELECT with no set, in the set, past it | `TFW_ERR_RANGE`; the state; `TFW_ERR_RANGE`. **Met** |
| REPORT of S1–S3, S6 or an unknown signal; S4 or S5 with no set; S4 under a set naming S1 alone | `TFW_ERR_RANGE`; lockdown (R1); lockdown (R2). **Met** |
| VERDICT clear, compromised, past them | The state; the state; `TFW_ERR_RANGE`. **Met** |
| HEARTBEAT in time; late; any message late | No action; S6 lockdown at that message. **Met** |
| The wire vectors of the five commands | The literal frames in `testdata/vectors.h` and `sdk/go/internal/vectors`. **Met:** `test/unit/test_frame.c`, `sdk/go/codec_test.go` |

Against `tfwsim-atecc608a`, the one simulator built with `TFW_FEATURE_HSM`
(`sdk/go/hsm_tfwsim_test.go`, `make sdk-test`), on the literal
`SUIT_POLICY_ENVELOPE_CLASS_328P` (tfwsim is built for the 328P's profile):
the load holds the set; byte 57 flipped is `TFW_ERR_CRYPTO`; a total past the
buffer is `TFW_ERR_RANGE`; select 0 succeeds and 1 is `TFW_ERR_RANGE`; a
verdict, a heartbeat and a report of S4 lock down. The heartbeat period is one
second of the host's clock and nothing waits it out: a case asserts the
set-held flag, which S6 never clears, and the lockdown flag only after S4.
**Met.**

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
| 1 | **The SUIT reader takes a key and an integrated payload.** Done: `tfw_suit_read_integrated` and `tfw_suit_integrated_digest` (`src/core/suit.h`), the level 1 verifier table met | `src/core/suit.c`, `suit.h`; `tools/suit-emit/`; `test/unit/` SUIT tests; literal vectors in `testdata/`; the OTA ADR subset table | Host, level 1 verifier table | #7; `make ci` |
| 2 | **The payload reader and the decision engine.** Done: the level 1 engine table met | `src/core/hsm_payload.c`, `hsm_engine.c`, `hsm.h`, `hsm_port.h` (architecture.md question 16); `test/unit/test_hsm_payload.c`, `test_hsm_engine.c` | Host, level 1 engine table | #7; `make ci` |
| 3 | **The loop on QEMU.** Done: the engine, `hsm_cmd.c` and the loop in the `mps2-an505` image, level 2 met | `src/core/hsm_loop.c`, `hsm_loop_none.c`, `src/hal/cortex-m/mps2/hsm_port.c`, `src/crypto/soft/verify_p256.c`; `test/integration/cortex-m/hsm_sets.c`, `qemu_test.sh`, wirecheck's `hsm.go` | Host unit (`test_hsm_loop`, `test_verify_p256`) and level 2 (`make integration-test-cortex-m`, the `hsm-*` cases) | #7, #161 |
| 4 | **Channel commands.** Done: `0x76`–`0x7A`, the level 1 channel table met | `include/tfw/protocol.h` (ids extend), `src/core/hsm_cmd.c`, `tools/tfwsim/hsm_port.c`, `sdk/go/hsm.go`, literal vectors in `testdata/` and the Go mirror; `docs/wire-protocol/` | Host unit (`test_hsm_cmd`) and `tfwsim` (`hsm_tfwsim_test.go`): heartbeat, report, verdict, select, load | #7 |
| 5 | **SCMI actions A1, A2, A5.** Done at host level: A1, RESET held asserted on the PHYs' reset domain and then POWER_STATE_SET off on their power domain, each where its id is set; A5, SYSTEM_POWER_STATE_SET as a forceful shutdown (DEN0056E §4.4.2.5, the encoder already in `scmi.c` and checked, so no placeholder stands for its encoding). Each is done only on the System Manager's SUCCESS; a refusal, no answer or a channel error is not done, and an unset placeholder or channel sends nothing and answers unsupported. Open: which domain holds the PHYs off (question 10) and whether the M7's agent may drive it or shut the system down (question 7), zero placeholders `TFW_IMX95_HSM_PHY_RESET`, `TFW_IMX95_HSM_PHY_POWER`, `TFW_IMX95_HSM_SHUTDOWN` in `board_profile.h`; A2, untouched by this slice, on question 7 and `docs/multi-arch/imx95-design.md` question 21; the board | `src/hal/cortex-m/imx95/` (`hsm_port.c`, `hsm_act.h`), `boards/frdm-imx95-m7/board_profile.h`; `docs/reference/README.md` (Arm SCMI, DEN0056, not redistributable) | Host against a model of the SCMI transport (`test_imx95_hsm_port`): each frame, success, each refusal status, no answer, a channel error, each placeholder unset. Then the board | #49; architecture.md questions 7, 10; imx95-design.md question 21 |
| 6 | **Enclave: S3, A3, A4.** Done at host level: S3, GET_INFO's question 17 fields (LMDA, ROM patch hash, `sha_fw`'s first 256 bits, OEM SRK hash, SRK revocation bits) compared with the set's expected values, any mismatch raising S3 through `tfw_hsm_port_measure`; A4 for keys, each deleted with the strict flag and done only on the enclave's acknowledgement, else R3's lockdown. Open: the MU (question 3); the key-group delete (question 5), not implemented; A3, which question 4 says the M7 likely cannot do; the delete's id and layout, the S3 keys, and the places in GET_INFO's response of the ROM patch hash, the OEM SRK hash and the revocation bits (RM00284 §3.13 names the fields, no source gives their offsets; S3 counts an expected value for one as a mismatch), zero placeholders in `board_profile.h`; the board | `src/hal/cortex-m/imx95/` (`ele_mu.c`, `hsm_port.c`), `src/core/hsm_engine.c`, `boards/frdm-imx95-m7/board_profile.h` | Host against a model of the messaging unit (`test_imx95_hsm_port`, `test_imx95_ele_mu`, `test_hsm_engine`): acknowledged, refused, no answer; every field, match and mismatch. The board, throwaway keys, remains | #49; questions 3–5, 17 |
| 7 | **Silicon signals S1, S2, S5.** | `src/hal/cortex-m/imx95/`, `boards/frdm-imx95-m7/` | The board | #49; questions 1, 2, 6, 11 |
| 8 | **Bench.** | The board page | Level 3, end to end | #49, trusted-imx95 |

Slices 1, 2 and 4 need no board and no Cortex-M port; 3 needs #161. Rows 5–8
are superseded by [m7-design.md](m7-design.md) §7, which lays them on what
#49 landed and splits them into slices 5–12. The model
work is not a slice here: the row starts it only after the HSM node runs.
