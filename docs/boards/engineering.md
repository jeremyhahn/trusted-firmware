# Boards — engineering

> **Status: DRAFT** · Scope: trusted-firmware · Last verified: 2026-10-20

How the `boards` row of [SPEC.md](../SPEC.md) is held true on `main`. The rules
for writing a board are in [adding-a-board.md](adding-a-board.md); the ids are in
[board-id-registry.md](board-id-registry.md).

## Files

| File | Purpose |
|---|---|
| `boards/<board>/board_profile.h` | the board's constants: name, id, pin, port, PWM and ADC counts, flash and EEPROM sizes, the boot region |
| `boards/<board>/board.c` | the three tables: `tfw_board_ports`, `tfw_board_pins`, `tfw_board_pwms` |
| `boards/<board>/profile.mk` | `ARCH`, the part (`MCU`, or `QEMU_MACHINE` for an emulated board), the budgets and totals, the boot geometry, `BOARD_TIERS` |
| `test/unit/test_board_<short>.c` | the board's descriptor test; `<short>` is the directory name less an `atmega` prefix |
| `scripts/board-check.py` | the host check below, run by `make scripts-test` |
| `scripts/test_board_check.py` | its tests, and the run on this tree |

## Invariants

`scripts/board-check.py` reads every `boards/*/` directory as data, with no
compiler, and fails when any of these is false:

- The directory has the three board files and a descriptor test.
- `ARCH` is stated and `mk/arch/<ARCH>.mk` exists.
- `TFW_BOARD_NAME` is the directory name.
- `TFW_BOARD_ID` lies in the registry's range for the board's family (avr
  0x01–0x3F, cortex-m 0x40–0x7F, esp32 0x80–0xBF), and no two boards share one.
- The registry's Allocated ids table and `sdk/go/boardid.go` (constants and
  `boardNames`) each give every board its own id, allocate no id twice, and name
  no directory that does not exist.
- A board with a budget names its part, and the part is one the check knows.
  `PARTS` in the script holds each part's flash, SRAM, SRAM start, EEPROM and
  boot section, each row citing its data sheet.
- Every budget is at most its total. `FLASH_TOTAL`, `RAM_TOTAL`,
  `TFW_FLASH_TOTAL` and `TFW_EEPROM_SIZE` are the part's.
- On a part with a boot section, `RAM_END` is the last SRAM address,
  `BOOT_BASE + BOOT_SECTION` is the top of flash, and `BOOT_BASE` is
  `TFW_BOOT_REGION_BASE`.
- On a part with no boot section, `BOARD_TIERS` is exactly `none`, and
  `profile.mk` states no boot geometry. No `BOOT_SUPPORTED` row in
  `mk/boot.mk` names a board without `BOOT_SECTION`.
- Each table has the row count its `TFW_*_COUNT` declares.
- In every pin row:
  - the port is below `TFW_PORT_COUNT`;
  - the bit is within the architecture's port width (8 on avr, 32 on cortex-m
    and esp32);
  - the ADC channel is below `TFW_ADC_COUNT`;
  - the PWM unit is below `TFW_PWM_COUNT`.
- No two pins share a port bit, an ADC channel or a PWM unit.
- `TFW_PIN_ADC` comes with an ADC channel and `TFW_PIN_PWM` with a PWM unit,
  never with `TFW_PWM_RSVD`.
- A pin with no port is not `TFW_PIN_DIGITAL`, and every pin is digital or ADC.
- Every `TFW_*_PIN` define in the profile is below `TFW_PIN_COUNT`.

A failure names the board and the value. A value that fails is wrong in the
board or missing from the registry; it is never fixed by editing the check.

## Row status

The `boards` row of [SPEC.md](../SPEC.md), requirement by requirement, on
`main`. **Shipped** means the requirement is in the tree and a test pins it.
**Unpinned** means it holds today but no test would catch a break.

| Requirement | Status | Implemented at | Pinned by |
|---|---|---|---|
| A board is data: a profile, pin tables and build budgets | shipped | `boards/<board>/{board_profile.h,board.c,profile.mk}`; rules in `adding-a-board.md` | `make scripts-test` (`test_board_check.py`, `TestRepository`); `make board-sweep`, each board's descriptor test against its own profile |
| Adding a board touches nothing under `src/` unless the part lacks a peripheral | unpinned | `src/core/board_lookup.c` reads the tables and names no board | nothing: this is a fact about a change, not about the tree, so no check over the tree can see it |
| ATmega328P, ATmega2560, ATmega32U4 and ATtiny85 are registered | shipped | `board-id-registry.md` ids 0x01–0x04; `sdk/go/boardid.go` | `test_board_check.py` `TestRepository`; `sdk/go/boardid_test.go` `TestBoardIDsAreThePublishedValues` |
| Each board names the crypto tiers it can carry | shipped | `BOARD_TIERS` in `profile.mk`; atmega328p and atmega32u4 state none and take `mk/firmware.mk`'s default, `soft atecc608a` | `make firmware-matrix`, every board at every tier (CI only: it needs avr-gcc) |
| A part with no boot section reports the none tier | shipped | `boards/attiny85/profile.mk` (`BOARD_TIERS := none`, `CRYPTO ?= none`); `src/crypto/none/provider.c` answers `TFW_CRYPTO_NONE` | `test_crypto_none.c` `test_reports_the_unattested_tier`; `test_board_check.py` `test_no_boot_part_with_a_soft_tier_fails`, `test_no_boot_part_takes_no_default_tiers` |
| Board ids come from the board-id registry | shipped | `TFW_BOARD_ID` in each `board_profile.h`, checked by `scripts/board-check.py` | `test_board_check.py` `TestIds` and `TestRepository`; `test_duplicate_id_fails` |

## Tests

- `make scripts-test` runs `scripts/test_board_check.py`. Its fixture tree has
  two boards, one with a boot section and one without. Each test breaks one fact
  and asserts the failure that names it. `TestRepository` runs the check on this
  tree, so a board that breaks an invariant fails the gate.
- `make board-sweep` compiles every board's descriptor test against that
  board's profile and runs it, with the host compiler.
- `make firmware-matrix` builds every board at every tier its profile claims,
  and fails on a budget. It needs the target compilers, so only CI runs it.

## Open

- A board added on a branch must add its registry row, its `sdk/go/boardid.go`
  constant and name, and its part's `PARTS` row in `scripts/board-check.py` in
  the same change, or `make scripts-test` fails. The mps2-an505 board of #161
  takes the provisional id 0x40 and has none of the three yet.
