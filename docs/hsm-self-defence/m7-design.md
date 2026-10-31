# HSM self-defence: the M7 enforcement on the board #49 landed

> **Status: PROPOSED** · Issue: [#50](../../../../issues/50) · SPEC row:
> `hsm-self-defence` (future) · Scope: trusted-firmware (the M7 enforcement and
> the signature check); go-xkms, go-trusted-platform, trusted-imx95 and NXP
> (seams only) · Last verified: 2026-10-26
>
> The row's design when it starts again after #49 closed (2026-10-25). The
> requirements, signals S1–S6, actions A1–A6 and rules R1–R7 are in
> [`architecture.md`](architecture.md) and are not restated; the policy format
> is [`adr-response-policy-format.md`](adr-response-policy-format.md) ("ADR").
> This page lays them on what row `imx95` put on `main`, names the standard or
> vendor interface for every part, and sets the slices that follow. Its slices
> supersede rows 5–8 of [`engineering.md`](engineering.md), Slices.

## 1. On main, and what the row lacks

**On main for the row** (host only, `make host-test`, and tfwsim under
`make sdk-test`): the SUIT reader for an integrated payload under the tenant
key (`src/core/suit_integrated.c`, slice 1); the payload reader and the
decision engine, R1–R7 (`src/core/hsm_payload.c`, `hsm_engine.c`, slice 2);
the channel commands `0x76`–`0x7A` (`src/core/hsm_cmd.c`, slice 4). The port
the engine acts through is `src/core/hsm_port.h`: `tfw_hsm_port_act` (A1–A6,
`TFW_HSM_ACT_*` in `src/core/hsm.h`) and `tfw_hsm_port_key` (ADR D3). Its only
implementations are tfwsim's (`tools/tfwsim/hsm_port.c`) and the unit tests'.

**On main from #49** (`docs/multi-arch/imx95-design.md` §9, §10), each "met
(host, register model)" against `test/model/imx95_model.c` and built in CI, not
run on the board:

| Landed | Where | What it gives this row |
|---|---|---|
| The board as data, id `0x41` (provisional) | `boards/frdm-imx95-m7/` | The profile the port's ids and class ids go in |
| The SCMI client: Base PROTOCOL_VERSION, PROTOCOL_ATTRIBUTES; Clock CLOCK_CONFIG_SET, CLOCK_RATE_GET; Power domain POWER_STATE_SET; Reset domain RESET; imx-sm's Logical Machine Management LMM_SHUTDOWN | `src/hal/cortex-m/imx95/scmi.c`, `scmi.h` | A2 (power domains off) and, if a PHY sits behind a reset or power domain, A1. Not called until imx95 question 21 names the channel and ids |
| The SCMI shared-memory transport over an MU | `src/hal/cortex-m/imx95/scmi_mu.c`, `scmi_mu.h` | The path to the System Manager for A1, A2, A5 |
| The enclave's message layer: GET_INFO, session, key store, key management, KEY_GENERATE, signature generation, PUB_KEY_RECOVERY, signature verification | `src/hal/cortex-m/imx95/ele_mu.c`, `ele_mu.h` | The session and key-store handles A3 and A4 need; GET_INFO's lifecycle field for S3. Not called until imx95 questions 3 and 4 are answered |
| LPUART3, RGPIO, WDOG5 behind `hal.h`; the tick | `src/hal/cortex-m/imx95/hal_*.c`, `src/hal/cortex-m/hal_tick.c` | The channel's UART until the MU channel exists; the time base S6 and *W* count from |
| The remoteproc resource table | `src/hal/cortex-m/imx95/rsc_table.c` | Nothing; it lets Linux load and start the M7 (question O1) |

**What the row lacks**, by part:

| Part | Lacks | Slice |
|---|---|---|
| H1, the M7 application | `hsm_port.c` for the i.MX 95; the engine and `hsm_cmd.c` in the M7's object list; the loop | 3, 5 |
| A2 | The power-domain ids the M7's agent may switch (imx95 Q21; architecture Q7) | 5 |
| A1 | Which reset or power domain holds the Ethernet PHYs (architecture Q10) | 5 |
| A5 | SCMI System power management's SYSTEM_POWER_STATE_SET, in `scmi.c` (slice 6, done) | 6 |
| A2 by logical machine | The System Manager's logical-machine stop, imx-sm's LMM_SHUTDOWN, in `scmi.c` (slice 7, done) | 7 |
| A3 | A lock call: the client has KEY_STORE_CLOSE, which ends our own handle, not go-xkms's session (architecture Q4). Until it is answered, A3 is A2 (O5), and slice 8 adds no call | 8 |
| A4 | The key-group delete and the whole-store wipe (architecture Q5). Key deletion and the strict operation that commits it are in `ele_mu.c` (slice 8, done) | 8 |
| S3 | Device attestation, absent from `ele_mu.c`; the expected values (ADR Q9) | 9 |
| S1, S2, S5 | How each reaches the M7 (architecture Q1, Q2, Q6, Q11) | 10 |
| A6, the report | A store that survives reset (architecture Q12) and a read path (§5) | 11a, 11b-1 (the read, done), 11b-2 (the engine's call sites, done), 11b-3 (the durable store) |
| H2 | The M7 started only by the ROM and the System Manager, closed to the A55 (question O1) | trusted-imx95 |

## 2. Standards and vendor interfaces

Each part is implemented against the document named; nothing is designed in
its place (`documentation.md` §9).

| Part | Standard or interface | On file |
|---|---|---|
| The M7 to the System Manager: A1, A2, A5 | Arm *System Control and Management Interface*, DEN0056E (v3.2): Power domain management (A2), Reset domain management (A1), System power management (A5), the shared-memory transport | Listed, `docs/reference/README.md` (not redistributable) |
| The System Manager's own protocols: logical machines, the M7 agent's resources | NXP i.MX System Manager documentation, <https://github.com/nxp-imx/imx-sm>: the Logical Machine Management protocol | Copied at `rel_imx_sm_2026q3`, `docs/reference/imx-sm/` |
| The M7 to the enclave: A3, A4, S3 | NXP RM00284 Rev. 4.5, *EdgeLock Enclave Hardware Security Module API*; the wire values from NXP imx-secure-enclave and U-Boot | Listed, `docs/reference/README.md` (not redistributable; the source trees listed) |
| The MU registers | IMX95RM, MU chapter (NDA); Linux `drivers/mailbox/imx-mailbox.c` v6.12 as the secondary source | Linux listed; IMX95RM **no** |
| AHAB, S2, and the container that carries the tenant key | IMX95RM, AHAB chapter; IMX95SRM (both NDA) | **No**, to file (title only) |
| The tamper pins, S1, and their event | IMX95SRM, tamper chapter, and the fuse map (NDA). No open standard defines a tamper event model; the SoC's is the one this row implements | **No**, to file (title only) |
| The board: PHYs, hotplug pins, J22 | NXP UM12527 Rev. 1.0, *FRDM-IMX95-PRO Board User Manual* (cited by `imx95-design.md`) | **No**, to file |
| The policy and script envelope | draft-ietf-suit-manifest-37; draft-ietf-suit-mti-23 (ES256, SHA-256); RFC 9052 (COSE_Sign1) | Yes: `docs/reference/draft-ietf-suit-manifest-37.txt`, `draft-ietf-suit-mti-23.txt`, `rfc9052.txt`, `rfc9053.txt` |
| The policy payload | RFC 8949 (deterministic CBOR), RFC 8610 (CDDL) | Yes: `rfc8949.txt`, `rfc8610.txt` |
| The signature check | FIPS 180-4 (SHA-256); ECDSA P-256 under RFC 9053 | Yes: `NIST.FIPS.180-4.pdf`, `rfc9053.txt` |
| The channel | This project's wire protocol (`docs/wire-protocol/`) | Ours |

## 3. Tamper sources and what the M7 observes

As architecture.md §2, with what #49 changes:

- **S1, tamper pins.** M7-observed only if the event is routed to the M7, to
  the System Manager (an SCMI notification) or to the enclave. Nothing on main
  receives an interrupt or a notification; `scmi.c` has no notification
  channel. Open until IMX95SRM is on file.
- **S2, AHAB failure.** The M7's own container failing means the M7 never runs.
  For an A55 container the M7 hears nothing unless the enclave or the System
  Manager tells it. Open (architecture Q6).
- **S3, measurement.** The M7 asks the enclave over the MU `ele_mu.c` drives;
  GET_INFO's lifecycle field is on main, the attestation call is not. M7-observed
  once the M7 has the MU (imx95 Q3).
- **S4, IMA; S5, hotplug through Linux; the verdict; the selection.**
  A55-reported, as `0x76`–`0x7A`; on the M7 their transport is LPUART3 until
  an MU channel exists (architecture Q9). A compromised A55 can forge or drop them.
- **S6, heartbeat loss.** The M7's own tick. A compromised A55 can force it
  (lockdown) but not prevent the M7 counting it.

## 4. The responses and who executes each

The engine (ours, in `src/core/`) decides; `tfw_hsm_port_act` dispatches; the
board's port, `src/hal/cortex-m/imx95/hsm_port.c` (slice 5), calls the client
below. An action whose id the profile leaves zero returns
`TFW_ERR_UNSUPPORTED` and the engine carries on (`hsm_port.h`).

| Action | Decides | Executes | Through |
|---|---|---|---|
| A1 PHYs off | The engine | The System Manager | `tfw_scmi_reset` or `tfw_scmi_power_state_set` on the PHY's domain (DEN0056E) |
| A2 A55 off | The engine | The System Manager | `tfw_scmi_power_state_set` on the A55's domains; or the logical-machine stop (slice 7) |
| A3 keys locked | The engine | The enclave, on go-xkms's key store | Open (architecture Q4). Until a call exists, A3 is A2: with the A55 off no session uses the keys |
| A4 keys wiped | The engine, under R3 only | The enclave, on go-xkms's key store | Deletion, then the strict operation; done only when it is acknowledged (RM00284 §2.4) |
| A5 shutdown | The engine | The System Manager | SYSTEM_POWER_STATE_SET (DEN0056E, slice 6) |
| A6 record | The engine | The M7 | TCM today, behind `hsm_store.h`; read back over the channel (11b-1, done); the durable store is open, option (c) (11b-3, O4) |

Lockdown is A1, A2, A3, A6 in that order, latched (R6). A wipe is A4 then
lockdown. The M7 never holds a go-xkms key and never computes with one: it asks
the enclave to delete or lock what go-xkms created (L2).

## 5. The policy, the script and the report

**Policy and script** (ADR D1–D7; slice 1, 2 code on main): SUIT envelopes
with an integrated payload, `COSE_Sign1_Tagged` ES256 under the tenant's
P-256 key, policy and script class ids per board, the floor, a script as a
bounded list of actions. On the M7 the check is `tfw_suit_read_integrated`
with micro-ecc in software, against `tfw_hsm_port_key`: the key is read-only in
the M7 image, inside its AHAB container, never from the channel. Its private
half is held off the device by go-xkms's signing service, reached through
go-trusted-platform (ADR D3, Q11). Whether the enclave's SIGNATURE_VERIFY
(`tfw_ele_sign_verify`, on main) replaces the software check is question O2.

**The report: what the device says and to whom.**

| Report | To | Path | State |
|---|---|---|---|
| The engine's state (latched, set held, selection, refusals) in every reply to `0x76`–`0x7A` | The caller on the A55: go-trusted-platform's agent and the detector | The channel | On main (slice 4) |
| The record: each signal, decision and action, with its result | go-trusted-platform, after recovery | Read from the M7 after reset, through the channel | The record over a RAM store, slice 11a; the read through the channel, `HSM_RECORD_READ`, slice 11b-1 (done, O9); the engine's call sites, slice 11b-2 (done); the durable store (architecture Q12, O4), slice 11b-3 |
| The policy-set digest and floor as attestation evidence | The verifier go-xkms names | go-xkms's evidence, not ours | Seam to file (§6) |

A compromised A55 can drop the first two in transit; only a record that
survives reset lets go-trusted-platform see what happened. The record's form is
this row's; the evidence format (EAT, RFC 9711) is go-xkms's and is not
designed here.

## 6. Seams

| Seam | Owner | Issue |
|---|---|---|
| The key store the M7 locks or wipes, and its key groups | go-xkms | to file (its seam row's name is architecture Q19) |
| The policy-set digest and floor in attestation evidence | go-xkms | to file |
| Policy and script delivery; the payload CDDL; the tenant key and its rotation; reading the record | go-trusted-platform, `hsm-device-lifecycle` | to file |
| The M7 logical machine, its SCMI agent, channel and ids; TRDC isolation of the M7's TCM; the loader | trusted-imx95 | to file (imx95 Q19, Q21) |
| The System Manager and the enclave firmware | NXP | none: we consume their published interfaces |

## 7. Slices

One PR each (m751). Each adds its host tests to `make ci`, with no skip guard;
"host" means the unit build under the mock HAL and the register model
`test/model/imx95_model.c`. Slices 1 to 4, 6 and 7 are done, and slice 5's
code and tests (engineering.md), and slice 8 for A4's keys. Slice 3, the loop on QEMU's `mps2-an505`, is proved on the host by `test_hsm_loop` (the
loop's pass: a rising line raised once, signal order, every pass keeping the
heartbeat deadline) and `test_verify_p256` (the tenant-key verify the image
links), and at level 2 by the `hsm-*` cases of `make
integration-test-cortex-m`: no set, a forged set, a replayed set, a lapsed
heartbeat with no message sent, a corroborated wipe and a signal with no
verdict, each read back from the engine's state and the board's LEDs. QEMU does
not model the board's push buttons, so there the signals arrive by HSM_REPORT
and the switch path is the host test's.

| # | Slice | Without hardware | Waits on |
|---|---|---|---|
| 5 | **The i.MX 95 port, A1 and A2.** `src/hal/cortex-m/imx95/hsm_port.c` on `tfw_scmi_power_state_set` and `tfw_scmi_reset`; the ids in `boards/frdm-imx95-m7/board_profile.h`, zero meaning unsupported; the engine in the board's object list | Host: the SCMI bytes each action puts in the model's shared memory; an id left zero returns `TFW_ERR_UNSUPPORTED` | Code and tests done: `tfw_imx95_hsm_act`, `test_imx95_hsm_port`. The ids and the channel, zero in `board_profile.h`, still wait on imx95 Q21 (the PHYs' domain also on architecture Q10). The engine in the board's object list waits on O2: `tfw_hsm_load` needs the M7's verifier |
| 6 | **A5.** SYSTEM_POWER_STATE_SET in `scmi.c`, from DEN0056E's System power management protocol | Host: its bytes against DEN0056E's table; every status | Done: `tfw_scmi_system_power_state_set`, `tfw_scmi_system_power_status` |
| 7 | **A2 by logical machine.** The System Manager's logical-machine stop in `scmi.c` | Host: its bytes against the imx-sm documentation | Done: `tfw_scmi_lmm_shutdown`, `tfw_scmi_lmm_status`, from imx-sm `rel_imx_sm_2026q3`; `test_imx95_scmi.c`: `test_the_lmm_identifiers_are_imx_sms`, `test_lmm_shutdown_is_lm_id_flags`, `test_lmm_shutdown_clears_reserved_flags`, `test_lmm_shutdown_of_lm_zero_is_unsupported`, `test_every_lmm_status_maps` |
| 8 | **A3, A4.** Key deletion and the strict operation in `ele_mu.c`, from imx-secure-enclave as #49's pieces were; A4 done only on the acknowledgement | Host: the model answers, refuses, times out; a wipe the commit refuses is not done | Done for A4's keys: `tfw_ele_key_delete` with `TFW_ELE_DEL_KEY_STRICT`, `tfw_imx95_hsm_wipe`; `test_imx95_ele_mu.c`: `test_a_good_delete_is_strict_and_acknowledged`, `test_a_refused_delete_is_a_status_error`, `test_a_delete_never_answered_times_out`; `test_imx95_hsm_port.c`: `test_a4_acknowledged_deletes_every_key_strictly_and_closes`, `test_a4_a_refused_delete_is_not_done_and_the_rest_are_tried`, `test_a4_no_answer_to_a_delete_is_a_timeout_not_done`; `test_hsm_engine.c`: `test_r3_an_unacknowledged_wipe_is_not_counted_done`, `test_r3_a_refused_or_silent_wipe_is_not_done_and_locks_down`. A3 is A2, the documented default (O5). Still waits on architecture Q4, Q5 (whether the M7's domain may touch go-xkms's store) |
| 9 | **S3.** The enclave's device attestation in `ele_mu.c`; the comparison against the policy's expected values in the engine | Host: model; a mismatch raises S3 | Code and tests done: `tfw_ele_dev_attest` on a caller-given `struct tfw_ele_attest_layout`, `tfw_imx95_hsm_attest` (session open, attest under a nonce, close, compare), `test_imx95_ele_mu`, `test_imx95_hsm_port`; the measure falls back to GET_INFO while the id is zero. The message's id and layout, the report's layout and the S3 keys stay zero placeholders in `board_profile.h` (ADR Q9 (a)). The report's signature is not verified (architecture Q17). The M7's messaging unit and domain wait on imx95 Q3 (a) |
| 10 | **S1, S2, S5 on silicon.** Their intake on the i.MX 95 port | Host: an injected interrupt or notification raises the engine's signal | Code and tests done: `hsm_intake.c`, `tfw_hsm_port_sense`, `test_imx95_hsm_intake`. S1 is the System Manager's SCMI notification (Q2 (b)), taken through `tfw_imx95_hsm_notify_isr`; the channel's notification receiver and the protocol's NOTIFY subscription, and S1's ids, stay open (IMX95SRM). S5 is an M7 GPIO pin interrupt, no fuse (Q1 (a)), the pin the System Manager assigns with the A55's report a hint (Q11 (c)); its pad and interrupt stay open (board schematic), no vector installed. S2 polls hsm_get_event (Q6 (a)), unconfirmed until the bench test; its message and event code stay open (IMX95RM, imx-secure-enclave) |
| 11a | **The record behind a store.** A6 as a fixed-size append-only log of signal, decision and action entries, each action with its result, in `src/core/hsm_record.c`; full, it keeps its first entries, refuses the rest and counts them. The store is `src/core/hsm_store.h`, substituted at link time; its one implementation, `hsm_store_ram.c`, survives nothing | Host: `test_hsm_record`: an empty record, append and read-back, overflow, the dropped count's top | Done. No command id, wire vector or status code (O9); the engine's call sites append since 11b-2, after slices 8 to 10 changed the engine |
| 11b-1 | **The read.** `HSM_RECORD_READ` (`0x7C`), one new command id that extends: `[index:1]` returns `[count:1][dropped:2][at:4][kind:1][what:1][result:1]`, an index at or past the count is `TFW_ERR_RANGE`, any other payload length `TFW_ERR_BAD_ARGC`. `tfw_hsm_cmd_record_read` in `hsm_cmd.c`; `tfw_hsm_record_erase` is reachable from no command | Host: `test_hsm_cmd`, `test_frame`, tfwsim, literal vectors and the Go mirror | Done. `test_hsm_cmd.c`: `test_an_empty_record_reads_range`, `test_one_entry_reads_back_with_every_field`, `test_the_last_index_reads_and_the_one_past_it_is_range`, `test_a_full_record_reads_with_its_dropped_count`, `test_the_read_leaves_the_engine_and_the_record_alone`, `test_the_record_read_is_one_byte_long_in_the_table`; `test_frame.c`: `test_hsm_channel_frames_match_the_vectors`; the Go SDK's `HSMRecordRead` and `HSMRecordReadAll`, `TestHSMRecordRead*` and, against tfwsim, `TestSimHSMRecordRead*`. O9 answered (item 173). The record stayed empty until 11b-2 wired the engine's call sites |
| 11b-2 | **The engine's call sites.** The engine's calls to `tfw_hsm_record_append` in `hsm_engine.c`: `tfw_hsm_signal` appends a `TFW_HSM_REC_SIGNAL` on entry; `tfw_hsm_verdict` appends a `TFW_HSM_REC_DECISION` with V on receipt, signal or no signal (R4); R3 appends a `TFW_HSM_REC_DECISION` with the V it consults (`corroborated`, nowhere else) on every wipe-enabled rule, before any wipe, so a withheld wipe is explained by a clear or expired V; every `tfw_hsm_port_act` call, in `lockdown`, `run` and R3's wipe, appends a `TFW_HSM_REC_ACTION` with its status. The engine ignores the append's status: a full record changes nothing it does | Host: the engine appends what it saw, decided and did, over the recording mock port | Done. `test_hsm_engine.c`: `test_a6_nothing_is_recorded_without_a_signal`, `test_a6_a_verdict_with_no_signal_is_recorded_and_acts_on_nothing`, `test_a6_r3_a_clear_verdict_records_the_decision_and_wipes_nothing`, `test_a6_r3_an_expired_verdict_records_the_decision_and_wipes_nothing`, `test_a6_r1_lockdown_records_the_signal_then_the_four_actions`, `test_a6_r5_heartbeat_loss_records_s6_then_lockdown`, `test_a6_r2_p_actions_are_recorded_and_the_wipe_is_not`, `test_a6_r3_an_acknowledged_wipe_records_signal_decision_wipe_lockdown`, `test_a6_r3_a_refused_wipe_records_the_ports_status`, `test_a6_a_full_record_does_not_stop_the_engine` |
| 11b-3 | **The durable store.** A store that survives reset, as another file behind `hsm_store.h` | Host: the store against its model | Open. The durable store is option (c), the monotonic counter in the EdgeLock or ATECC608C, named in the ADR before this slice ships (item 172, O4) |
| 12 | **Bench.** End to end on the FRDM-IMX95-PRO, a stand-in detector on the A55, throwaway keys | None: this is the board level | Every slice above; trusted-imx95's M7 logical machine; question O1 |

Nothing in a slice fuses the board or arms a tamper pin (engineering.md level
3). The NPU model is not a slice: the row starts it only after the HSM node
runs.

### Slice 6's values against DEN0056E

Slice 6 entered SYSTEM_POWER_STATE_SET without the specification at hand. Each
value it added or uses was then read against DEN0056E (version 3.2, issue E;
2026-10-26). Every value matched. The only change was to the citations, which
moved from §4.4 to §4.4.2.5, and to the comment's wording for each status.

| Name | Ours | DEN0056E | Section, page |
| --- | --- | --- | --- |
| Header: message_id, message_type, protocol_id, token; reserved | bits 7:0, 9:8, 17:10, 27:18; 31:28 zero | the same | §4.1.2, Table 3, p. 21 |
| message_type of a command | 0 | 0 | §4.1.2, p. 21 |
| `TFW_SCMI_PROTO_SYSTEM` | 0x12 | 0x12, System power management | §4.1.2, Table 2, p. 20; §4.4.2.5, p. 61 |
| `TFW_SCMI_SYSTEM_POWER_STATE_SET` | 0x3 | message_id 0x3 | §4.4.2.5, p. 61 |
| Parameters | flags, then system_state | flags, then system_state | §4.4.2.5, pp. 61–62 |
| `TFW_SCMI_SYSTEM_GRACEFUL` / `_FORCEFUL` | bit 0 = 1 / 0; bits 31:1 cleared | bit 0: 1 graceful, 0 forceful; 31:1 reserved, zero | §4.4.2.5, p. 61 |
| `TFW_SCMI_SYSTEM_SHUTDOWN` | 0x0 | 0x0 System shutdown | §4.4.2.5, p. 62 |
| `TFW_SCMI_SYSTEM_COLD_RESET` | 0x1 | 0x1 System cold reset | §4.4.2.5, p. 62 |
| `TFW_SCMI_SYSTEM_WARM_RESET` | 0x2 | 0x2 System warm reset | §4.4.2.5, p. 62 |
| `TFW_SCMI_SYSTEM_POWER_UP` | 0x3 | 0x3 System power-up | §4.4.2.5, p. 62 |
| `TFW_SCMI_SYSTEM_SUSPEND` | 0x4 | 0x4 System suspend | §4.4.2.5, p. 62 |
| Reserved and vendor system_state | 5 to 0x7FFFFFFF reserved, 0x80000000 up vendor | the same | §4.4.2.5, p. 62 |
| The command's statuses | INVALID_PARAMETERS, NOT_SUPPORTED, DENIED | the same ("one of, but not limited to") | §4.4.2.5, p. 62 |
| SUCCESS, NOT_SUPPORTED, INVALID_PARAMETERS, DENIED | 0, -1, -2, -3 | 0, -1, -2, -3 | §4.1.4, Table 5, p. 23 |
| NOT_FOUND to IN_USE; -12 to -127 | -4 to -11; unrecognised | -4 to -11; reserved | §4.1.4, Table 5, p. 23 |

The test (`test/unit/test_imx95_scmi.c`) already pinned each value as a
literal, so only its comments changed.

## 8. Open questions for the owner

Marked **[#14]** or **[#15]** where the answer waits on that ADR, both
PROPOSED on `main`.

1. **O1. The loader.** #49 landed a remoteproc resource table so Linux can load
   and start the M7, while the ref ADR says the M7 is started by the ROM and
   the System Manager, never by remoteproc. If the A55 can load, stop or
   rewrite the M7, H2 fails and nothing here holds. Is remoteproc
   development-only? (imx95 Q19.) **[#14] [#15]**
2. **O2. Where the policy signature is checked:** micro-ecc in privileged M7
   code (trust ADR option (c)) or the enclave's SIGNATURE_VERIFY. **[#15]**
3. **O3. The board id.** `0x41` is provisional and claimed by four rows (imx95
   Q20). **[#14]**
4. **O4. Durable state** for the latch, the floor and the record, on an M7 with
   no NVM (architecture Q12; ADR Q8). **[#14]** Answered (CA m1656, order
   (b)): "172: #50 slice 11 proceeds with the TCM behind an interface and the
   durable store as option (c), the monotonic counter in the EdgeLock or
   ATECC608C, named in the ADR before slice 11 ships." Slice 11b-3 carries it.
5. **O5. A3 as A2.** Until the enclave offers a lock on go-xkms's store, is
   powering off the A55 an acceptable "keys locked"?
6. **O6. May an A55-reported signal corroborate a wipe?** (architecture Q15,
   ADR Q10.)
7. **O7. Tamper pins and fuses.** Arming either is the owner's decision; which,
   and when?
8. **O8. Gate #7.** engineering.md gates every slice on the AVR end-to-end gate
   unless lifted for this row; is it lifted for slices 3 to 11?
9. **O9. The record's reader.** Does go-trusted-platform read A6 over the
   channel, or does it reach the verifier only as go-xkms's evidence? Answered
   (CA m1656, ruling 251 item 4): "173 trusted-firmware #50 O9, option (b) xkms
   evidence through the tf xkms adapter, the new command id is allowed". The
   command id is `HSM_RECORD_READ` (`0x7C`), slice 11b-1; the evidence through
   the adapter is not in this slice.
10. The policy-format questions, ADR Q1–Q11, stand as written there.
