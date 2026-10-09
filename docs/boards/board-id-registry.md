# Board-id registry

**NORMATIVE.** The board id is the byte at offset 5 of the CAPS board header. It
is `TFW_BOARD_ID` in `boards/<board>/board_profile.h` and `tfw.BoardID` in
`sdk/go`. It names a board *build target*, not a silicon part: two boards on one
MCU take two ids.

## Ranges

One range per architecture family, so a host can tell the family from the id
alone, before it knows the board.

| Range | Family | Go `Family()` |
|---|---|---|
| `0x00` | unset (`TFW_BOARD_ID` default in `config.h`; never allocated) | `unallocated` |
| `0x01`-`0x3F` | AVR | `avr` |
| `0x40`-`0x7F` | Cortex-M | `cortex-m` |
| `0x80`-`0xBF` | ESP32 and other | `esp32/other` |
| `0xC0`-`0xFF` | unallocated | `unallocated` |

## Allocated ids

| Id | Board | Directory |
|---|---|---|
| `0x01` | ATmega328P | `boards/atmega328p` |
| `0x02` | ATmega2560 | `boards/atmega2560` |
| `0x03` | ATmega32U4 | `boards/atmega32u4` |
| `0x04` | ATtiny85 | `boards/attiny85` |

## Rules

- **Ids extend; they never renumber, and a retired board's id is never reused.**
  An older host must read an id it does not know as unrecognised, not as a
  different known board.
- A new board takes the next free id in its family's range and, in the same
  change, is added to this table and to `sdk/go/boardid.go`.
- A host given an id it does not know shows the family and the raw id
  (`BoardID.String()`: `unknown cortex-m board 0x41`) and does not fail. A newer
  device on an older host is the normal case.
- A family that outgrows its range is a wire change: it is decided in an issue,
  not by borrowing from the neighbouring range.
