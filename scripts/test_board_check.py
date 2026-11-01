# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests for scripts/board-check.py (make scripts-test). No network."""

import contextlib
import importlib.util
import io
import os
import shutil
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
spec = importlib.util.spec_from_file_location("board_check", os.path.join(HERE, "board-check.py"))
bc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bc)

REGISTRY = """# Board-id registry

## Ranges

| Range | Family | Go `Family()` |
|---|---|---|
| `0x00` | unset | `unallocated` |
| `0x01`-`0x3F` | AVR | `avr` |
| `0x40`-`0x7F` | Cortex-M | `cortex-m` |
| `0x80`-`0xBF` | ESP32 and other | `esp32/other` |
| `0xC0`-`0xFF` | unallocated | `unallocated` |

## Allocated ids

| Id | Board | Directory |
|---|---|---|
| `0x01` | Alpha | `boards/alpha` |
| `0x04` | Tiny | `boards/tiny` |

## Rules

- `0x05` in a sentence is not a row.
"""

GO_MIRROR = """package tfw

const (
\tBoardAlpha       BoardID = 0x01
\tBoardTiny        BoardID = 0x04
\tboardAVRFirst    BoardID = 0x01
)

var boardNames = map[BoardID]string{
\tBoardAlpha: "alpha",
\tBoardTiny:  "tiny",
}
"""

BOOT_MK = """BOOT_SUPPORTED := \\
  alpha:soft:soft:p256-soft:0:0 \\
  alpha:atecc608a:atecc608a:atecc608a:0:0

bootloader-check:
"""

# An ATmega328P board: a boot section at the top of flash, three pins.
ALPHA_MK = """ARCH           := avr
MCU            := atmega328p
FLASH_BUDGET   := 16384
RAM_BUDGET     := 1024
FLASH_TOTAL    := 32768
RAM_TOTAL      := 2048
BOOT_SECTION   := 4096
BOOT_BASE      := 0x7000
BOOT_BUDGET    := 4096
RAM_END        := 0x08FF
"""

ALPHA_H = """#ifndef TFW_BOARD_ALPHA_H
#define TFW_BOARD_ALPHA_H
#define TFW_BOARD_NAME "alpha"
#define TFW_BOARD_ID   0x01u   /* the first AVR id */
#define TFW_PIN_COUNT  4u
#define TFW_PORT_COUNT 2u
#define TFW_PWM_COUNT  1u
#define TFW_ADC_COUNT  8u
#define TFW_EEPROM_SIZE 1024u
#define TFW_TONE_PIN   1u
#define TFW_ADDR_PINB  0x23u
#define TFW_FLASH_TOTAL       32768uL
#define TFW_BOOT_REGION_SIZE  4096uL
#define TFW_BOOT_REGION_BASE  (TFW_FLASH_TOTAL - TFW_BOOT_REGION_SIZE)
#endif
"""

ALPHA_C = """#include "board_profile.h"
const tfw_port_desc_t tfw_board_ports[TFW_PORT_COUNT] TFW_PROGMEM = {
    { TFW_ADDR_PINB },   /* 0 */
    { 0x26u },           /* 1 */
};
const tfw_pin_desc_t tfw_board_pins[TFW_PIN_COUNT] TFW_PROGMEM = {
/* 0 */ { TFW_PB(0u, 0u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_IN_USE },
/* 1 */ { TFW_PB(0u, 1u), TFW_ADC_NONE, 0u,           TFW_PIN_DIGITAL | TFW_PIN_PWM },
/* 2 */ { TFW_PB(1u, 0u), 0u,           TFW_PWM_RSVD, TFW_PIN_DIGITAL | TFW_PIN_ADC },
/* 3 */ { TFW_PB(TFW_PORT_NONE, 0u), 6u, TFW_PWM_NONE, TFW_PIN_ADC },
};
const tfw_pwm_desc_t tfw_board_pwms[TFW_PWM_COUNT] TFW_PROGMEM = {
    { 0xB0u, 0xB4u, 7u, 0u },
};
"""

# An ATtiny85 board: no boot section, so the none tier, and no PWM table.
TINY_MK = """ARCH           := avr
MCU            := attiny85
FLASH_BUDGET   := 6144
RAM_BUDGET     := 256
FLASH_TOTAL    := 8192
RAM_TOTAL      := 512
BOARD_TIERS    := none
"""

TINY_H = """#define TFW_BOARD_NAME "tiny"
#define TFW_BOARD_ID   0x04u
#define TFW_PIN_COUNT  1u
#define TFW_PORT_COUNT 1u
#define TFW_PWM_COUNT  0u
#define TFW_ADC_COUNT  4u
#define TFW_EEPROM_SIZE 512u
#define TFW_FLASH_TOTAL 8192uL
"""

TINY_C = """const tfw_port_desc_t tfw_board_ports[TFW_PORT_COUNT] TFW_PROGMEM = {
    { 0x36u },
};
const tfw_pin_desc_t tfw_board_pins[TFW_PIN_COUNT] TFW_PROGMEM = {
    { TFW_PB(0u, 7u), TFW_ADC_NONE, TFW_PWM_RSVD, TFW_PIN_DIGITAL },
};
const tfw_pwm_desc_t tfw_board_pwms[TFW_PWM_COUNT] TFW_PROGMEM;
"""


class FixtureTree(unittest.TestCase):
    """A two-board tree that passes; each test breaks one fact in it."""

    def setUp(self):
        self.root = tempfile.mkdtemp(prefix="board-check-")
        self.addCleanup(shutil.rmtree, self.root)
        files = {
            "docs/boards/board-id-registry.md": REGISTRY,
            "sdk/go/boardid.go": GO_MIRROR,
            "mk/boot.mk": BOOT_MK,
            "mk/arch/avr.mk": "",
            "mk/arch/esp32.mk": "",
            "boards/alpha/profile.mk": ALPHA_MK,
            "boards/alpha/board_profile.h": ALPHA_H,
            "boards/alpha/board.c": ALPHA_C,
            "boards/tiny/profile.mk": TINY_MK,
            "boards/tiny/board_profile.h": TINY_H,
            "boards/tiny/board.c": TINY_C,
            "test/unit/test_board_alpha.c": "",
            "test/unit/test_board_tiny.c": "",
        }
        for rel, text in files.items():
            self.write(rel, text)

    def path(self, rel):
        return os.path.join(self.root, rel)

    def write(self, rel, text):
        os.makedirs(os.path.dirname(self.path(rel)), exist_ok=True)
        with open(self.path(rel), "w", encoding="utf-8") as f:
            f.write(text)

    def edit(self, rel, old, new):
        with open(self.path(rel), encoding="utf-8") as f:
            text = f.read()
        self.assertIn(old, text, "fixture %s has no %r" % (rel, old))
        self.write(rel, text.replace(old, new, 1))

    def assertFails(self, *fragments):
        errors = bc.check(self.root)
        joined = "\n".join(errors)
        for fragment in fragments:
            self.assertIn(fragment, joined)
        return errors


class TestPasses(FixtureTree):
    def test_main_reports_and_exits_zero(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(0, bc.main(["board-check", self.root]))
        self.assertIn("every board is registered", out.getvalue())

    def test_main_exits_one_on_a_failure(self):
        self.edit("boards/tiny/board_profile.h", "0x04u", "0x01u")
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(1, bc.main(["board-check", self.root]))
        self.assertIn("board-check: boards/tiny:", out.getvalue())


class TestFiles(FixtureTree):
    def test_each_board_file_is_required(self):
        for f in bc.BOARD_FILES:
            with self.subTest(f=f):
                os.rename(self.path("boards/alpha/" + f), self.path("boards/alpha/x"))
                self.assertFails("boards/alpha: no %s" % f)
                os.rename(self.path("boards/alpha/x"), self.path("boards/alpha/" + f))

    def test_descriptor_test_is_required(self):
        os.remove(self.path("test/unit/test_board_alpha.c"))
        self.assertFails("boards/alpha: no descriptor test")

    def test_descriptor_test_drops_the_atmega_prefix(self):
        os.rename(self.path("boards/alpha"), self.path("boards/atmega9"))
        self.write("test/unit/test_board_9.c", "")
        self.assertIsNotNone(bc.descriptor_test(self.root, "atmega9"))
        self.assertIsNone(bc.descriptor_test(self.root, "atmega8"))

    def test_arch_needs_its_makefile(self):
        self.edit("boards/alpha/profile.mk", "ARCH           := avr", "ARCH := riscv")
        self.assertFails("ARCH riscv has no mk/arch/riscv.mk")

    def test_arch_is_required(self):
        self.edit("boards/alpha/profile.mk", "ARCH           := avr\n", "")
        self.assertFails("boards/alpha: profile.mk states no ARCH")

    def test_board_name_is_the_directory(self):
        self.edit("boards/alpha/board_profile.h", '"alpha"', '"beta"')
        self.assertFails('TFW_BOARD_NAME is "beta", not "alpha"')


class TestIds(FixtureTree):
    def test_duplicate_id_fails(self):
        self.edit("boards/tiny/board_profile.h", "0x04u", "0x01u")
        self.assertFails("boards/tiny: TFW_BOARD_ID 0x01 is also boards/alpha's")

    def test_a_third_board_claiming_a_taken_id_fails(self):
        # Two branches each adding a board at the next free id.
        for f in bc.BOARD_FILES:
            shutil.copy(self.path("boards/tiny/" + f), self.path("boards/tiny/../tinz." + f))
        os.makedirs(self.path("boards/tinz"))
        for f in bc.BOARD_FILES:
            os.rename(self.path("boards/tinz." + f), self.path("boards/tinz/" + f))
        self.edit("boards/tinz/board_profile.h", '"tiny"', '"tinz"')
        self.write("test/unit/test_board_tinz.c", "")
        self.assertFails("boards/tinz: TFW_BOARD_ID 0x04 is also boards/tiny's: ids never repeat")

    def test_id_outside_its_family_range_fails(self):
        self.edit("boards/alpha/board_profile.h", "0x01u", "0x41u")
        self.assertFails("TFW_BOARD_ID 0x41 is outside the avr range 0x01-0x3F")

    def test_esp32_takes_the_esp32_other_range(self):
        self.edit("boards/alpha/profile.mk", "ARCH           := avr", "ARCH := esp32")
        self.assertFails("TFW_BOARD_ID 0x01 is outside the esp32 range 0x80-0xBF")

    def test_arch_with_no_range_fails(self):
        self.write("mk/arch/riscv.mk", "")
        self.edit("boards/alpha/profile.mk", "ARCH           := avr", "ARCH := riscv")
        self.assertFails("the registry has no id range for ARCH riscv")

    def test_id_not_in_registry_fails(self):
        self.edit("docs/boards/board-id-registry.md", "| `0x04` | Tiny | `boards/tiny` |\n", "")
        self.assertFails("boards/tiny: TFW_BOARD_ID 0x04 is not its row in docs/boards/board-id-registry.md (no row)")

    def test_registry_with_another_id_fails(self):
        self.edit("docs/boards/board-id-registry.md", "| `0x04` | Tiny", "| `0x05` | Tiny")
        self.assertFails("TFW_BOARD_ID 0x04 is not its row in docs/boards/board-id-registry.md (0x05)")

    def test_registry_duplicate_row_fails(self):
        self.edit("docs/boards/board-id-registry.md", "| `0x04` | Tiny", "| `0x01` | Tiny")
        self.assertFails("docs/boards/board-id-registry.md: 0x01 is allocated twice (alpha, tiny)")

    def test_registry_row_for_no_directory_fails(self):
        self.edit("docs/boards/board-id-registry.md", "## Rules", "| `0x05` | Gone | `boards/gone` |\n\n## Rules")
        self.assertFails("docs/boards/board-id-registry.md: 0x05 names gone, which is not a directory")

    def test_go_mirror_disagreeing_fails(self):
        self.edit("sdk/go/boardid.go", "BoardTiny        BoardID = 0x04", "BoardTiny        BoardID = 0x05")
        self.assertFails("boards/tiny: TFW_BOARD_ID 0x04 is not its row in sdk/go/boardid.go (0x05)")

    def test_go_constant_without_a_name_fails(self):
        self.edit("sdk/go/boardid.go", '\tBoardTiny:  "tiny",\n', "")
        self.assertFails("sdk/go/boardid.go: BoardTiny (0x04) has no boardNames entry")

    def test_go_name_without_a_constant_fails(self):
        self.edit("sdk/go/boardid.go", "}\n", '\tBoardGhost: "ghost",\n}\n')
        self.assertFails("boardNames names BoardGhost, which is not a BoardID constant")


class TestBudgets(FixtureTree):
    def test_budget_over_total_fails(self):
        self.edit("boards/alpha/profile.mk", "RAM_BUDGET     := 1024", "RAM_BUDGET := 4096")
        self.assertFails("RAM_BUDGET 4096 exceeds RAM_TOTAL 2048")

    def test_budget_needs_its_total(self):
        self.edit("boards/alpha/profile.mk", "FLASH_TOTAL    := 32768\n", "")
        self.assertFails("FLASH_BUDGET with no FLASH_TOTAL")

    def test_budget_needs_a_part(self):
        self.edit("boards/alpha/profile.mk", "MCU            := atmega328p\n", "")
        self.assertFails("a budget names no part")

    def test_unknown_part_fails(self):
        self.edit("boards/alpha/profile.mk", "atmega328p", "atmega329p")
        self.assertFails("part atmega329p is not one board-check knows")

    def test_emulated_board_names_its_machine(self):
        self.edit("boards/alpha/profile.mk", "MCU            := atmega328p", "QEMU_MACHINE := mps2-an521")
        self.assertFails("part mps2-an521 is not one board-check knows")

    def test_total_not_the_parts_fails(self):
        self.edit("boards/alpha/profile.mk", "RAM_TOTAL      := 2048", "RAM_TOTAL := 2560")
        self.assertFails("RAM_TOTAL 2560, but the atmega328p has 2048")

    def test_profile_flash_total_not_the_parts_fails(self):
        self.edit("boards/alpha/board_profile.h", "32768uL", "65536uL")
        self.assertFails("TFW_FLASH_TOTAL 65536, but the atmega328p has 32768")

    def test_eeprom_size_not_the_parts_fails(self):
        self.edit("boards/alpha/board_profile.h", "TFW_EEPROM_SIZE 1024u", "TFW_EEPROM_SIZE 2048u")
        self.assertFails("TFW_EEPROM_SIZE 2048, but the atmega328p has 1024")

    def test_not_a_number_fails(self):
        self.edit("boards/alpha/profile.mk", "RAM_BUDGET     := 1024", "RAM_BUDGET := lots")
        self.assertFails("profile.mk RAM_BUDGET is 'lots', not a number")

    def test_ram_end_not_the_last_sram_address_fails(self):
        self.edit("boards/alpha/profile.mk", "0x08FF", "0x09FF")
        self.assertFails("RAM_END 0x09FF, but the atmega328p's last SRAM address is 0x08FF")

    def test_boot_section_not_at_the_top_fails(self):
        self.edit("boards/alpha/profile.mk", "BOOT_BASE      := 0x7000", "BOOT_BASE := 0x6000")
        self.assertFails("BOOT_BASE 0x6000 + BOOT_SECTION 4096 is not the top of flash 32768",
                         "BOOT_BASE 0x6000, but TFW_BOOT_REGION_BASE is 0x7000")

    def with_boot_end(self, end):
        saved = bc.PARTS["atmega328p"]
        bc.PARTS["atmega328p"] = dict(saved, boot_end=end)
        self.addCleanup(bc.PARTS.__setitem__, "atmega328p", saved)

    def test_boot_end_replaces_the_top_of_flash(self):
        # A boot region apart from the flash: the top-of-flash rule no longer
        # holds, the region's end does.
        self.with_boot_end(0x8000)
        self.assertEqual([], bc.check(self.root))
        self.with_boot_end(0x9000)
        errors = self.assertFails(
            "BOOT_BASE 0x7000 + BOOT_SECTION 4096 is not the end of the atmega328p's boot region 0x9000")
        self.assertNotIn("top of flash", "\n".join(errors))

    def test_no_boot_end_keeps_the_top_of_flash(self):
        self.assertNotIn("boot_end", bc.PARTS["atmega328p"])
        self.edit("boards/alpha/profile.mk", "BOOT_SECTION   := 4096", "BOOT_SECTION := 2048")
        errors = self.assertFails("BOOT_BASE 0x7000 + BOOT_SECTION 2048 is not the top of flash 32768")
        self.assertNotIn("boot region", "\n".join(errors))

    def test_no_boot_part_with_a_soft_tier_fails(self):
        self.edit("boards/tiny/profile.mk", "BOARD_TIERS    := none", "BOARD_TIERS := soft none")
        self.assertFails("the attiny85 has no boot section, so BOARD_TIERS must be none, not soft none")

    def test_no_boot_part_takes_no_default_tiers(self):
        self.edit("boards/tiny/profile.mk", "BOARD_TIERS    := none\n", "")
        self.assertFails("BOARD_TIERS must be none, not soft atecc608a")

    def test_no_boot_part_with_boot_geometry_fails(self):
        self.edit("boards/tiny/profile.mk", "BOARD_TIERS", "BOOT_BUDGET := 512\nBOARD_TIERS")
        self.assertFails("the attiny85 has no boot section, but profile.mk states BOOT_BUDGET")

    def test_boot_supported_row_for_a_no_boot_board_fails(self):
        self.edit("mk/boot.mk", "  alpha:atecc608a", "  tiny:soft:soft:p256-soft:0:0 \\\n  alpha:atecc608a")
        self.assertFails("mk/boot.mk: BOOT_SUPPORTED names tiny, which states no BOOT_SECTION")

    def test_boot_supported_row_for_no_board_fails(self):
        self.edit("mk/boot.mk", "  alpha:atecc608a", "  gone:soft:soft:p256-soft:0:0 \\\n  alpha:atecc608a")
        self.assertFails("mk/boot.mk: BOOT_SUPPORTED names gone, which is not a board")

    def test_a_board_with_no_budget_names_no_part(self):
        for line in ("FLASH_BUDGET   := 16384\n", "RAM_BUDGET     := 1024\n", "MCU            := atmega328p\n"):
            self.edit("boards/alpha/profile.mk", line, "")
        self.assertEqual([], bc.check(self.root))


class TestPins(FixtureTree):
    def pin(self, old, new):
        self.edit("boards/alpha/board.c", old, new)

    def test_port_past_the_count_fails(self):
        self.pin("TFW_PB(1u, 0u)", "TFW_PB(2u, 0u)")
        self.assertFails("pin 2: port 2, but TFW_PORT_COUNT is 2")

    def test_bit_past_the_port_width_fails(self):
        self.pin("TFW_PB(1u, 0u)", "TFW_PB(1u, 8u)")
        self.assertFails("pin 2: bit 8 of a 8-bit avr port")

    def test_a_32_bit_port_takes_bit_31(self):
        self.write("mk/arch/cortex-m.mk", "")
        self.edit("boards/alpha/profile.mk", "ARCH           := avr", "ARCH := cortex-m")
        self.edit("boards/alpha/board_profile.h", "0x01u", "0x40u")
        self.edit("docs/boards/board-id-registry.md", "| `0x01` | Alpha", "| `0x40` | Alpha")
        self.edit("sdk/go/boardid.go", "BoardAlpha       BoardID = 0x01", "BoardAlpha       BoardID = 0x40")
        self.pin("TFW_PB(1u, 0u)", "TFW_PB(1u, 31u)")
        self.assertEqual([], bc.check(self.root))
        self.pin("TFW_PB(1u, 31u)", "TFW_PB(1u, 32u)")
        self.assertFails("pin 2: bit 32 of a 32-bit cortex-m port")

    def test_two_pins_on_one_bit_fail(self):
        self.pin("TFW_PB(0u, 1u)", "TFW_PB(0u, 0u)")
        self.assertFails("pin 1: port 0 bit 0 is pin 0's")

    def test_adc_past_the_count_fails(self):
        self.pin("6u, TFW_PWM_NONE", "8u, TFW_PWM_NONE")
        self.assertFails("pin 3: ADC channel 8, but TFW_ADC_COUNT is 8")

    def test_two_pins_on_one_channel_fail(self):
        self.pin("6u, TFW_PWM_NONE", "0u, TFW_PWM_NONE")
        self.assertFails("pin 3: ADC channel 0 is pin 2's")

    def test_adc_needs_its_flag(self):
        self.pin("TFW_PIN_DIGITAL | TFW_PIN_ADC", "TFW_PIN_DIGITAL")
        self.assertFails("pin 2: an ADC channel and the TFW_PIN_ADC flag must come together")

    def test_pwm_past_the_count_fails(self):
        self.pin("TFW_ADC_NONE, 0u,", "TFW_ADC_NONE, 1u,")
        self.assertFails("pin 1: PWM unit 1, but TFW_PWM_COUNT is 1")

    def test_pwm_needs_its_flag(self):
        self.pin("TFW_PIN_DIGITAL | TFW_PIN_PWM", "TFW_PIN_DIGITAL")
        self.assertFails("pin 1: a PWM unit and the TFW_PIN_PWM flag must come together")

    def test_reserved_pwm_takes_no_flag(self):
        self.pin("TFW_PWM_RSVD, TFW_PIN_DIGITAL | TFW_PIN_ADC", "TFW_PWM_RSVD, TFW_PIN_DIGITAL | TFW_PIN_ADC | TFW_PIN_PWM")
        self.assertFails("pin 2: a PWM unit and the TFW_PIN_PWM flag must come together")

    def test_two_pins_on_one_pwm_unit_fail(self):
        self.pin("TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_IN_USE",
                 "TFW_ADC_NONE, 0u, TFW_PIN_DIGITAL | TFW_PIN_PWM")
        self.assertFails("pin 1: PWM unit 0 is pin 0's")

    def test_portless_pin_is_not_digital(self):
        self.pin("6u, TFW_PWM_NONE, TFW_PIN_ADC", "6u, TFW_PWM_NONE, TFW_PIN_ADC | TFW_PIN_DIGITAL")
        self.assertFails("pin 3: TFW_PIN_DIGITAL on a pin with no port")

    def test_pin_neither_digital_nor_adc_fails(self):
        self.pin("TFW_PIN_DIGITAL | TFW_PIN_IN_USE", "TFW_PIN_IN_USE")
        self.assertFails("pin 0: neither digital nor ADC")

    def test_row_count_not_the_declared_count_fails(self):
        self.edit("boards/alpha/board_profile.h", "TFW_PIN_COUNT  4u", "TFW_PIN_COUNT  5u")
        self.assertFails("board.c: tfw_board_pins has 4 rows, TFW_PIN_COUNT is 5")

    def test_undeclared_pwm_table_is_zero_rows(self):
        self.edit("boards/tiny/board_profile.h", "TFW_PWM_COUNT  0u", "TFW_PWM_COUNT  1u")
        self.assertFails("board.c: tfw_board_pwms has 0 rows, TFW_PWM_COUNT is 1")

    def test_missing_table_fails(self):
        self.edit("boards/alpha/board.c", "tfw_board_ports[", "tfw_board_portz[")
        self.assertFails("board.c declares no tfw_board_ports")

    def test_unreadable_row_fails(self):
        self.pin("{ TFW_PB(0u, 0u), TFW_ADC_NONE,", "{ PIN(0u, 0u), TFW_ADC_NONE,")
        self.assertFails("pin 0: not a { TFW_PB(port, bit), adc, pwm, flags } row")

    def test_row_value_not_a_number_fails(self):
        self.pin("TFW_PB(1u, 0u)", "TFW_PB(1u, TFW_NOWHERE)")
        self.assertFails("pin 2: bit 'TFW_NOWHERE' is not a number")

    def test_pin_define_past_the_last_pin_fails(self):
        self.edit("boards/alpha/board_profile.h", "TFW_TONE_PIN   1u", "TFW_TONE_PIN   4u")
        self.assertFails("TFW_TONE_PIN is pin 4, but TFW_PIN_COUNT is 4")

    def test_register_address_is_not_a_pin(self):
        self.edit("boards/alpha/board_profile.h", "TFW_ADDR_PINB  0x23u", "TFW_ADDR_PINB  0x123u")
        self.assertEqual([], bc.check(self.root))

    def test_arch_with_no_port_width_fails(self):
        self.write("mk/arch/riscv.mk", "")
        self.edit("boards/alpha/profile.mk", "ARCH           := avr", "ARCH := riscv")
        self.assertFails("board-check knows no port width for ARCH riscv")


class TestEval(unittest.TestCase):
    def test_suffixes_names_and_operators(self):
        defs = {"A": "0x1000uL", "B": "(A - 0x10u)", "C": "B / 2u << 1u | 1u"}
        self.assertEqual(0xFF1, bc.eval_c("C", defs))

    def test_unknown_name_and_bad_expression_raise(self):
        with self.assertRaises(ValueError):
            bc.eval_c("NOPE", {})
        with self.assertRaises(ValueError):
            bc.eval_c("1.5", {})
        with self.assertRaises(ValueError):
            bc.eval_c("A", {"A": "A"})

    def test_negation_and_unary_plus(self):
        self.assertEqual(-3, bc.eval_c("-(+3)", {}))

    def test_unterminated_table_raises(self):
        with self.assertRaises(ValueError):
            bc.table_rows("const t x[2] = { {1}, {2}", "x")


class TestRepository(unittest.TestCase):
    """The tree itself: every boards/ directory passes. A failure here is a
    wrong value in a board or a missing registry row, not a test to edit."""

    def test_every_board_in_the_tree_passes(self):
        self.assertEqual([], bc.check(REPO))

    def test_every_board_in_the_tree_was_read(self):
        boards = [d for d in os.listdir(os.path.join(REPO, "boards"))
                  if os.path.isdir(os.path.join(REPO, "boards", d))]
        _, registry = bc.registry_rows(REPO)
        self.assertEqual(sorted(boards), sorted(d for _, d in registry))


if __name__ == "__main__":
    unittest.main()
