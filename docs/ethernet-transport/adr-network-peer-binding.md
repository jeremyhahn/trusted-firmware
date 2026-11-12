# ADR: network peer binding

> **Status: ACCEPTED** 2026-10-24, owner ruling 130 (owner-queue items 149 (a)
> and 150 (B)) · Issues: #232, #233 · Scope: trusted-firmware (device and
> `sdk/go`) · Last verified: 2026-10-24, against `src/proto/link_eth.c`,
> `src/proto/transport.c`, `src/core/commands.def`, `src/caps/storage.c`,
> `src/core/ota_stage.c`, `src/core/ota_prev.c`, `src/core/ota_activate.c`,
> `src/core/auth_session*.c`, `boot/boot_stage.c`, `include/tfw/eth.h`,
> `include/tfw/status.h` and `mk/firmware.mk`
>
> The owner chose B for both decisions; each keeps the alternatives that were
> weighed. The code lanes for #232 and #233 build to it. The sizes are estimates: the image this
> was written in has no avr-gcc, and the code lane measures them into
> [engineering.md](engineering.md#flash-and-ram).

## The question

Two gaps are allowed by the specification as it stands, so the code is right
and the specification has to change first.

- **#232.** The connected network peer can send `OTA_BEGIN`, `OTA_DATA`,
  `OTA_END` and `OTA_STATUS` (`0x68`–`0x6B`) and nothing checks who it is. The
  auth session lives on serial
  ([OTA ADR, decision 8](../ota/adr-ota-image-format-and-trust.md#8-wire-commands--accepted-status-code-rbac-ethernet-proposed-ids-and-layouts),
  and [decision 2](adr-wire-protocol-over-w5x00.md#2-allow-list) of the
  W5x00 ADR).
- **#233.** A serial `EEPROM_WRITE` to the network block changes the device's
  peer and addressing, and the next connection attempt uses it, with no reset
  ([decision 3](adr-wire-protocol-over-w5x00.md#3-addressing) of the W5x00
  ADR).

This ADR decides what, if anything, binds each of those to a party the device
has reason to trust: the stager to the network peer, and the network block to
the serial host.

No standard covers either. The SUIT manifest draft pinned in
[`docs/reference/`](../reference/draft-ietf-suit-manifest-37.txt) requires a
manifest to be authenticated before its contents are used (REQ.SEC.AUTHENTIC,
line 3894 there). The image trailer and `OTA_AUTH` meet that before anything
runs (below). Neither SUIT nor RFC 9019 says who may write the staging area
before that check, and RFC 9124 is not in `docs/reference/`. The constraints
come from CLAUDE.md: status codes and command ids extend and never renumber,
the HAL links at link time, and preprocessor conditionals stay in the four
places.

## Context

### What the code does today

- **The Ethernet check admits `0x68`–`0x6B` and stops there.**
  `tfw_link_admits` (`src/proto/link_eth.c:150-169`) returns 1 for every
  command on serial, and on Ethernet for the five enrolment ids and the four
  staging ids (`:161-164`). The transport calls it before dispatch and answers
  `TFW_ERR_UNKNOWN_CMD` (4) for anything else (`src/proto/transport.c:63-68`).
  Nothing on that path knows who the peer is.
- **`EEPROM_WRITE` to the network block needs no session.** The dispatch table
  has no auth flag: an entry is `TFW_CMD_ENTRY(id, NAME, handler, min, max)`
  (`src/core/commands.def:8`, the `EEPROM_WRITE` row at `:75`).
  `tfw_storage_cmd_write` (`src/caps/storage.c:96-122`) checks the range and
  `boot_owned` (`:58-63`), which covers the reserved region below
  `TFW_NETCFG_ADDR` and the 2560's journal. It never calls `tfw_auth_gate()`.
  The only gate in that file is on reads of the boot-owned region (`:82-87`).
  The network block is `TFW_NETCFG_ADDR` for `TFW_NETCFG_LEN` bytes
  (`include/tfw/eth.h:34-35`), the first bytes past the reserved region, so a
  write to it passes every check. On main today the block is at 0x0C0; it
  moves to 0x140 with #231's first slice, where the boot-reserved region ends
  (owner item 149 (a)).
- **The link reloads the block at every connection attempt**
  (`src/proto/link_eth.c:63`). That is why a new block takes effect with no
  reset.
- **The auth session exists on the element tier only.** The soft tier and
  `CRYPTO=none` link `src/core/auth_session_none.c`, whose gate always answers
  `TFW_OK` (`mk/firmware.mk:213`, `:224`). `auth_session_auto.c` gates only
  when the element is the tier in use (`src/core/auth_session_auto.c:22-28`).
  On the element tier a session opens only against the operator public key,
  which lives in the reserved region (`src/core/auth_session.c:61-116`).
  Owner item 48 makes the 328P's Ethernet node the soft tier
  ([test-matrix.md](test-matrix.md)).
- **`OTA_BEGIN` destroys before it checks anything.** `tfw_stage_cmd_begin`
  (`src/core/ota_stage.c:134-167`) bounds the length, probes the store, erases
  the MARK sector (`:158`) and calls `tfw_ota_prev_backup` (`:160`). In an
  image that links `src/core/ota_prev.c`, that erases and rewrites an
  application-sized region of the store (`src/core/ota_prev.c:21-35`). All of
  it runs inside one dispatch. The OTA ADR lists "begin while an image is
  pending" among the cases `TFW_ERR_BAD_STATE` (19) answers
  ([OTA ADR](../ota/adr-ota-image-format-and-trust.md), line 553), but
  `tfw_stage_cmd_begin` checks no state before it erases.
- **An Ethernet image stages by default** (`mk/firmware.mk:331-333`). The
  2560, the 328P and the 32U4 each name `OTA_STORE := spi_nor`.

### What tfw-boot still checks on a staged image

Nothing the network peer sends can run unless all of the following pass.
`take` (`boot/boot_stage.c:195-276`) needs the MARK, which only `OTA_ACTIVATE`
writes. `OTA_ACTIVATE` is serial-only. On every tier it needs a held
`OTA_AUTH` signature, and on the element tier a session as well
(`src/core/ota_activate.c:44-50`). Before a byte of the running image is
replaced, `take` checks (`:241-260`):

- the trailer parses;
- the board binding;
- the rollback floor;
- the image length;
- the measurement, against both the trailer and the MARK's hash;
- the row key's COSE activation signature over that hash and version
  (`authorized`, `:169-193`). A refused signature answers
  `TFW_ERR_AUTH_REQUIRED`.

So the network peer cannot make the device run an image. It can only affect
what sits in the store, and when.

### The threat

| Reach | Who | What they can cause today |
|---|---|---|
| Serial | Whoever holds the USB or UART: the bench host, or someone with physical access | On the soft tier, everything. On the element tier, everything not behind the session or `OTA_AUTH`, including a rewrite of the network block (#233) |
| Network | Whoever answers the device's outbound connection to the provisioned endpoint IP and port. That is the endpoint itself, or anyone on the path: the link carries no TLS, and the device's evidence does not depend on it ([W5x00 ADR](adr-wire-protocol-over-w5x00.md#context)). With the DHCP flag set, a rogue DHCP server that supplies the gateway puts itself on the path | `0x68`–`0x6B` with no further check (#232) |

What each can cause:

- **Storage wear (#232).** Each `OTA_BEGIN` erases one MARK sector and, where
  `ota_prev.c` is linked, an application-sized span of the store. `OTA_DATA`
  then erases the stage sectors it reaches. A peer that loops `OTA_BEGIN`, or
  `OTA_BEGIN` with one `OTA_DATA`, spends the store's erase endurance with
  nothing to show for it. A typical SPI NOR is rated around 10⁵ cycles per
  sector, so an hours-long loop wears the store out.
- **A denied update (#232).** An `OTA_BEGIN` after `OTA_ACTIVATE` mode 0 (next
  reset) erases the MARK, and the next reset installs nothing. An `OTA_BEGIN`
  after the control plane's `OTA_END` drops a `VERIFIED` stage, and
  `OTA_ACTIVATE` then answers `TFW_ERR_BAD_STATE`.
- **Serial starvation (#232).** The erase and the backup run inside the
  dispatch. The transport is stop-and-wait, so serial waits through each one.
- **A redirect (#233).** A new endpoint IP and port, or a new gateway, points
  the device at another server from the next attempt on. The redirect outlasts
  the serial session. The rightful endpoint loses the device, and the new one
  gets the staging channel above. It also gets an attestation oracle, though
  the serial holder could already reach that oracle directly: the evidence is
  bound to whichever nonce it is asked for.

An on-path network attacker can always deny service by dropping the
connection. What it must not be able to do is destroy hardware, or undo
something an authorised party did. On the element tier, a serial holder
without the operator key must not be able to plant an effect that persists
after the serial cable is gone.

## Decisions

### 1. #232: bound the stager — B, ACCEPTED

| Option | What it closes | Wire | Size, ATmega2560 | Size, ATmega328P |
|---|---|---|---|---|
| A. Accept and document | nothing | none | 0 | 0 |
| **B. Bound the stager (chosen)** | the denied update after `OTA_ACTIVATE`; wear, bounded per boot | none new: `TFW_ERR_BAD_STATE` (19), which already exists, from `OTA_BEGIN` in two more cases | RAM 1 B; flash ≈ 30–50 B | the Ethernet image does not build today ([the 328P budget](adr-wire-protocol-over-w5x00.md#the-328p-budget)); a serial image with `OTA_STAGE=1` pays the same ≈ 30–50 B of its ≈ 320 free |
| C. An authorised stage: `OTA_BEGIN` needs a held `OTA_AUTH` signature over a stage message | every effect, for a peer without the row key | `0x6F` added to the allow-list (reopens item 48); a new COSE context for the stage message; a device nonce against replay, as a new reply field or command id. All extensions | element: the verify is the element's, ≈ 150–250 B; soft: a P-256 verify in the application beside uECC's sign, ≈ 0.5–1 KB | the soft tier has an HMAC identity and no asymmetric verify in the application, so several KB, which does not fit |
| D. The session over Ethernet: `0x61`/`0x62` admitted, `OTA_BEGIN` gated on `tfw_auth_gate()`, the session ending with its connection (OTA ADR open question 8) | every effect, on the element tier only | two ids added to the allow-list (reopens item 48) | ≈ 20–40 B; RAM 0 | closes nothing: item 48's 328P Ethernet node is the soft tier, whose gate is the permissive stub |
| E. Rate-limit `OTA_BEGIN` by time | the wear rate, not its total | none new | RAM 4 B (a deadline); flash ≈ 30 B | as B |

The owner chose **B** (ruling 130, item 150). It makes `OTA_BEGIN` answer `TFW_ERR_BAD_STATE`, before it erases
anything:

1. **while an activation is pending**: from a successful `OTA_ACTIVATE` mode 0
   until reset. This is the OTA ADR's own "begin while an image is pending",
   which `src/core/ota_stage.c` enforces and `test/unit/test_ota_begin_budget.c`
   tests. Mode 1 resets at once, so it needs nothing.
2. **once `TFW_OTA_BEGIN_BUDGET` stages have been opened since reset with no
   `OTA_ACTIVATE` between them.** The budget is a board-profile constant, 4 by default.
   A successful `OTA_ACTIVATE` refills it.

A transfer that drops resumes from `OTA_STATUS` and `OTA_DATA` at the offset
the device reports. That spends no `OTA_BEGIN`, so a rightful control plane
needs one per image. A peer that spends the budget denies updates until the
next reset. That is no more than an on-path attacker can do already, and it
trades unbounded wear for that bounded denial. Serial is not exempt. The
serial holder can `RESET`, which refills the budget, and an exemption would
need the stager to know the link, which it does not today.

Both cases are one byte of RAM: a pending bit and a three-bit count. The
pending bit is set through one seam, a function the stager's file defines and
`src/core/ota_activate.c` calls. An image without the stager links neither.

B also removes the starvation from repeated `OTA_BEGIN`s, because the refused
ones erase nothing. It does not remove the starvation inside the one that is
admitted.

**Rejected.** A closes nothing. C closes everything, but it reopens the
owner's narrowing of the list to `0x68`–`0x6B` (item 48). It needs a new
signed message and a nonce exchange, and it does not fit the 328P's soft tier.
It is additive on top of B if the owner wants it later ([open question 3](#owner-rulings-and-open-questions)). D
reopens the same ruling and is a no-op on the soft tier. E bounds the rate but
not the total: a year at one `OTA_BEGIN` a minute is over 5 × 10⁵ erases.

### 2. #233: gate the network block on the session — B, ACCEPTED

| Option | What it closes | Wire | Size, ATmega2560 | Size, ATmega328P |
|---|---|---|---|---|
| A. Accept and document (today's text: "anyone holding the serial port already owns the device") | nothing | none | 0 | 0 |
| **B. A write overlapping `TFW_NETCFG_ADDR` for `TFW_NETCFG_LEN` bytes calls `tfw_auth_gate()` (chosen)** | on the element tier, a redirect by a serial holder without the operator key | none new: `TFW_ERR_AUTH_REQUIRED` (17), which already exists, from `EEPROM_WRITE` in one more case | RAM 0; flash ≈ 16–24 B on the element tier | element tier ≈ 16–24 B of its ≈ 320 free; soft tier ≈ 0, where the stub gate inlines to `TFW_OK` under `-flto` |
| C. Apply at reset only: the link latches the block's CRC at its first load, and a changed block keeps the link down until reset | an unnoticed live redirect | none | RAM 2 B; flash ≈ 30 B | ≈ 30 B |
| D. The block becomes tfw-boot's: `EEPROM_WRITE` refuses it, and a request record is applied at reset | as B, and on the soft tier | `EEPROM_WRITE` to `TFW_NETCFG_ADDR` starts answering `TFW_ERR_PROTECTED_REGION` (16), which breaks `sdk/go`'s existing writer; a new command id for the request | tfw-boot gains the apply path, ≈ 100 B or more in the bootloader | the same, in the 328P's bootloader |
| E. A lock bit: flags bit 1, reserved and ignored today (`include/tfw/eth.h:31-32`), marks the block locked; a write overlapping a locked block answers `TFW_ERR_PROTECTED_REGION` (16) without a session | as B, and on the soft tier once locked; a locked soft-tier block then changes only by ISP | a flag bit assigned, an extension. Older firmware ignores it, so a host learns from the image version whether the lock holds | RAM 0; flash ≈ 30–40 B (one EEPROM byte read) | ≈ 30–40 B |

The owner chose **B** (ruling 130, item 150). It mirrors the read gate the same file already has for the reserved region
(`src/caps/storage.c:82-87`), and it is the same accepted gap on the soft
tier (`:37-42`): a soft-tier image has no session to gate with. On the
element tier, a redirect then takes the operator's signature, the same as the
other things on that tier that outlast a serial session.

The consequence is an order on the bench. On the element tier the operator key
must be provisioned before the network block can be written, because a session
opens only against it. `tfwctl`'s network-configuration step gains the auth
exchange there. `sdk/go`'s block encoder does not change. A host reading
`TFW_ERR_AUTH_REQUIRED` from `EEPROM_WRITE` already knows the status.

**Rejected.** A leaves the element tier's one persistent effect that needs no
operator key. C closes nothing against the serial holder, who can send
`RESET`. D breaks an existing writer and costs bootloader flash. E is the only
option that reaches the soft tier. It is additive on top of B, which it
contains, and it trades a site renumbering on a locked soft-tier device for
an ISP visit ([ruling 5](#owner-rulings-and-open-questions): not now).

### 3. What the host sees

| Host action | Today | With B |
|---|---|---|
| `OTA_BEGIN` while an `OTA_ACTIVATE` mode 0 awaits reset | executed; the MARK is erased | `TFW_ERR_BAD_STATE` (19); nothing erased |
| `OTA_BEGIN` past the budget since reset | executed | `TFW_ERR_BAD_STATE` (19); nothing erased |
| `OTA_BEGIN` otherwise; `OTA_DATA`, `OTA_END`, `OTA_STATUS` | executed | executed |
| `EEPROM_WRITE` overlapping `TFW_NETCFG_ADDR` for `TFW_NETCFG_LEN` bytes, element tier, no session | executed | `TFW_ERR_AUTH_REQUIRED` (17) |
| the same, with a session, or on the soft tier | executed | executed |

**No wire byte changes.** No command id, status code, frame or reply layout is
added or moved. Two existing statuses answer from existing commands in more
cases, and an older host already decodes both. No existing vector moves.

## Tests a code lane adds

- **Unit, the stager.** `OTA_BEGIN` after `OTA_ACTIVATE` mode 0 answers
  `TFW_ERR_BAD_STATE`, and the store model shows the MARK intact. The fifth
  `OTA_BEGIN` since reset answers it, the store model counts no erase for it,
  and an `OTA_ACTIVATE` refills the budget. A resumed transfer spends none.
- **Unit, storage.** On the element model, `EEPROM_WRITE` at `TFW_NETCFG_ADDR`
  and at `TFW_NETCFG_ADDR + TFW_NETCFG_LEN - 1` each answer
  `TFW_ERR_AUTH_REQUIRED` without a session and write with one. A write ending
  at `TFW_NETCFG_ADDR - 1` keeps its `TFW_ERR_PROTECTED_REGION`, and one
  starting at `TFW_NETCFG_ADDR + TFW_NETCFG_LEN` is unchanged (on the 2560, the journal's refusal). On the soft-tier link the same writes succeed.
- **Vectors.** `TFWV_*` literals for the two refusal frames, in
  `testdata/vectors.h` and its `sdk/go` mirror.
- **Cross-implementation.** `tfwsim` over Ethernet: a staging loop is refused
  after the budget. Over serial, element tier: a network-block write without a
  session is `StatusErrAuthRequired`.
- **Build.** The size rows in [engineering.md](engineering.md#flash-and-ram)
  re-measured, with the no-stager images unchanged.

## Consequences

- The stager gains one byte of RAM and a seam with `ota_activate.c`; storage
  gains a range test and a call. No new file, no `#if`.
- An on-path network peer can still deny updates until a reset, and can still
  wear the store by the budget per boot. It can no longer cancel an
  authorised activation.
- On the soft tier, the serial holder still owns the network block, as the
  W5x00 ADR states today, unless the owner takes E.
- The W5x00 ADR's decision 3 sentence "anyone holding the serial port already
  owns the device" narrows to the soft tier. The OTA ADR's "begin while an
  image is pending" gains its implementation.

## Owner rulings and open questions

Ruling 130 (2026-10-24) took 1, 2, 4 and 5 on their recommendations.

1. #232: B, C, D or E? Ruled: B.
2. #232, B: the budget of `OTA_BEGIN`s per boot. Ruled: 4, as a board-profile
   constant.
3. #232: C (an authorised stage) as a later extension, reopening item 48 for
   `0x6F`? Open.
4. #233: B, C, D or E? Ruled: B.
5. #233: E (the lock bit) on top of B, for the soft tier? Ruled: not now; the
   soft tier's serial holder can do worse.
6. Both: the session's scope on TCP (OTA ADR open question 8) is not needed by
   either decision and stays open there.
