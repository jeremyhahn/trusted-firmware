# ADR: the A6 durable store is option (c), the ATECC608C counter

> **Status: PROPOSED** · SPEC row: `hsm-self-defence` (future) · Scope:
> trusted-firmware (`src/core/hsm_store.h` and the file behind it) · Last
> verified: 2026-10-30
>
> Docs only. It adds no code, no command id and no status code. What it does
> not settle is under [Open questions](#open-questions), worded as questions;
> the store file waits on them.

## The question

The A6 record keeps what the engine saw, decided and did
([architecture.md](architecture.md), A6). Today it sits in a RAM store
(`src/core/hsm_store_ram.c`) that a reset empties. Slice 11b-3 of
[m7-design.md](m7-design.md) is a store that survives reset, as another file
behind `src/core/hsm_store.h`. This ADR names that store and says what the
repository settles about it and what it does not.

## Decided by the owner

Item 172 (CA m1656, order (b)), verbatim:

> 172: #50 slice 11 proceeds with the TCM behind an interface and the durable
> store as option (c), the monotonic counter in the EdgeLock or ATECC608C,
> named in the ADR before slice 11 ships.

So the durable store is option (c): a monotonic counter in a secure element.
This ADR takes the ATECC608C. The EdgeLock side is not written here.

## Context: what the repository holds

- **The interface.** `src/core/hsm_store.h` has a head
  (`tfw_hsm_store_head_t`: `n`, `spare`, `dropped`; four bytes), 32 slots
  (`TFW_HSM_RECORD_MAX`) of `tfw_hsm_entry_t` (`at`, `kind`, `what`, `result`,
  `spare`; eight bytes), `tfw_hsm_store_erase`, and a `tfw_hsm_store_head_get`
  that returns `void`. The record puts an entry before the head that counts it
  (`hsm_store.h`, header comment), so a store that loses power between the two
  loses that entry and keeps a whole record. The whole store is 4 + 32 × 8 =
  260 bytes.
- **The counters.** The slot-map ADR
  ([`../device-provisioning/adr-trustflex-608c-slot-map.md`](../device-provisioning/adr-trustflex-608c-slot-map.md))
  says, under "Other shipped facts" (line 88): "**Counters 0 and 1.** They
  start at zero and are not attached to any key. They are free for the
  system (§3.1, §3.1.1)." Counter 0 is taken: line 24 gives it the role
  "The monotonic anti-rollback floor" (`TFW_ATECC_COUNTER_BOOT`,
  `src/crypto/atecc608a/atecc.h:149`), and `src/core/ota_floor_atecc.c` reads
  it as the OTA floor. Counter 1 is named by no role. `tfw_crypto_counter`
  (`src/crypto/atecc608a/provider.c:312`) reads counter 0 or 1 and refuses a
  higher index with `TFW_ERR_RANGE`.
- **The counter's life.** The OTA ADR
  ([`../ota/adr-ota-image-format-and-trust.md`](../ota/adr-ota-image-format-and-trust.md),
  line 284) gives "about 2 000 000 increments for the part's life".
- **A counter holds no bytes.** It is a 32-bit value the element reads back and
  increments (`TFW_ATECC_OP_COUNTER`, modes `TFW_ATECC_COUNTER_READ` and
  `_INCR`, `src/crypto/atecc608a/atecc.h:130-131`). Entries and the head have
  to live somewhere else.
- **Where bytes live on the element.** The slot-map ADR lists slot 8 as
  "General data, 416 bytes | Data, clear read | Clear `Write` | Option 1: until
  SlotLock. Option 2: never" (line 73). Which option the soldered parts have is
  recorded as "Not in any document" (lines 125-130). The slot-map ADR assigns
  slot 8 to no role, and its status is PROPOSED, not accepted.
- **The driver.** `src/crypto/atecc608a/atecc.h` has `tfw_atecc_exec` with
  `TFW_ATECC_OP_READ`, `TFW_ATECC_OP_WRITE`, `TFW_ATECC_ZONE_DATA` and
  `TFW_ATECC_ZONE_32`. The only data-zone read and write that use them are the
  provisioning ones (`src/caps/provision_atecc.c`); no helper for runtime slot
  bytes exists.
- **The host model.** `test/model/atecc608a.c` models one counter: its
  `TFW_ATECC_OP_COUNTER` case ignores the counter index and reads or increments
  `counter0`. Its clear slots (`clear_slots[16][72]`, `CLEAR_READ_SLOTS`) are 72
  bytes each, slot 8 included, and a write needs the config zone locked and the
  slot's SlotLocked bit set (config bytes 88-89). A 260-byte store fits neither.
- **Where an image keeps durable bytes today.** The AVR EEPROM, through
  `src/caps/storage.c` (`[0x000, TFW_BOOT_EE_LEN)` is the bootloader's) and the
  cortex-m `hal_eeprom.c` (window end `0x140`). `ota_floor_eeprom.c` keeps no
  bytes of its own: it returns `p->rollback_floor` from the platform block.
- **The M7.** It "has no non-volatile store of its own" (architecture.md
  question 12; the reference-part ADR, open question 4). Nothing in the
  repository says the M7 has an I2C path to the element.

## Decisions

### D1. The durable store is option (c) — DECIDED by item 172

The store is behind `hsm_store.h`, substituted at link time (no vtable, no
`#if`), as `src/core/hsm_store_atecc.c`. The record's behaviour does not change.

### D2. The counter is counter 1 — PROPOSED

Counter 0 is the anti-rollback floor (slot-map ADR line 24) and is not shared:
a record that incremented it would spend the OTA floor's life on log entries.
Counter 1 is "free for the system" (slot-map ADR line 88) and no role names it.
`tfw_atecc_exec` reaches it with `TFW_ATECC_OP_COUNTER` and p2 = 1.

### D3. Power loss between an entry and its head — DECIDED by `hsm_store.h`

The record puts the entry, then the head. A reset between the two leaves the
head not counting the new entry, so the entry is lost and the record is whole.
The durable store keeps that order and adds nothing that reorders it. How the
counter's increment sits against the head write is
[open question 5](#open-questions).

### D4. A stale copy is one whose counter is behind the element's — PROPOSED

A monotonic counter only rises. If the head carries the counter value it was
written under, a copy of the store restored from an older state carries a value
below the element's, and the store reads as stale. That is all the counter
gives: it does not stop someone who can write the bytes from writing the
current value into a forged head, because no key in the repository is assigned
to this use ([open question 3](#open-questions)).

## Open questions

Each is for the owner. None is decided here, and the store file depends on the
first four.

1. **Where do the 260 bytes live?** A counter holds none. Slot 8 (416 bytes,
   clear read and write) is the only runtime data the slot-map ADR lists that
   would hold them, and the slot-map ADR does not assign it. Is slot 8 the
   store's, or does the record live elsewhere?
2. **Is slot 8 writable on the soldered parts?** The slot-map ADR says which
   option (1 or 2) each slot has "is not in any document", and that config
   bytes 88-89 answer it. Until a bench read of those bytes, is the store
   allowed to rely on a write that option 2 forbids?
3. **What makes a forged head detectable?** The counter detects a restored old
   copy (D4), not a rewritten one. Is a rewritten head in the threat model for
   A6? If so, which key slot signs it (the slot-map ADR assigns none), or is
   the counter alone accepted?
4. **Can the M7 reach the element?** The driver talks I2C
   (`TFW_ELEMENT_I2C_ADDR`, `tfw_hal_i2c_xfer`), and nothing in the repository
   gives the i.MX 95's M7 a bus to it. Which part is the A6 store on that
   board: this element on a bus the M7 owns, or the EdgeLock side of item 172?
5. **How does the counter increment sit against the head write?** Incrementing
   before the head write makes a reset between them look like a rolled-back
   store; writing the head first, carrying the value the counter will reach,
   leaves the head one ahead of the element. Which of the two does the store
   treat as normal, and what does it do with the other?
6. **How often does the counter increment?** About 2 000 000 increments serve
   the part for life, and the record calls `tfw_hsm_store_head_put` for every
   append and for every refused entry. Is it one increment per head write, per
   boot, or per erase? An attacker who can raise signals can raise the append
   count.
7. **What does `tfw_hsm_store_head_get` return on an element error?** It
   returns `void`, so a counter or storage failure cannot reach the caller.
   Does the header change to return a status, or does a failed read present as
   an erased store?
8. **Is a stale store erased or refused?** When D4 finds a stale copy, does the
   store read as empty, keep its bytes and return an error, or both?

## What waits

`src/core/hsm_store_atecc.c` and its host test wait on questions 1 to 4. The
host model also needs two changes the store cannot make for itself: a counter
per index (it has one) and a slot 8 of at least 260 bytes (it has 72).
