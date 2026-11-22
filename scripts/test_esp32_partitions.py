# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Drift check between the ESP32 firmware's partition labels and sizes and the
boards' partitions.csv (make scripts-test). No network."""

import os
import re
import unittest

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

BOARDS = ("esp32p4-poe", "esp32-qemu")

HAL_H = os.path.join(REPO, "src", "hal", "esp32", "hal_esp32.h")
BOOT_HAL_C = os.path.join(REPO, "boot", "hal", "esp32", "boot_hal_esp32.c")

TABLE_OFFSET = 0x10000  # CONFIG_PARTITION_TABLE_OFFSET
TABLE_SIZE = 0x1000  # the table is one 4 KB sector
DATA_ALIGN = 0x1000
APP_ALIGN = 0x10000  # ESP-IDF's app partition alignment


def read_table(board):
    """boards/<board>/partitions.csv as (name, type, subtype, offset, size)."""
    rows = []
    path = os.path.join(REPO, "boards", board, "partitions.csv")
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            cols = [c.strip() for c in line.split(",")]
            rows.append((cols[0], cols[1], cols[2], int(cols[3], 0), int(cols[4], 0)))
    return rows


def _define(path, name):
    with open(path, encoding="utf-8") as f:
        for line in f:
            m = re.match(r"\s*#\s*define\s+" + re.escape(name) + r"\s+(.*)$", line)
            if m:
                return m.group(1)
    raise AssertionError("%s: no #define %s" % (os.path.relpath(path, REPO), name))


def c_define(path, name):
    """The integer value of `#define NAME value`, its u/uL suffix and comment dropped."""
    value = re.split(r"/\*|//", _define(path, name))[0].strip()
    value = re.sub(r"[uUlL]+$", "", value)
    return int(value, 0)


def c_string(path, name):
    """The string of `#define NAME "label"`."""
    m = re.match(r'"([^"]*)"', _define(path, name).strip())
    if not m:
        raise AssertionError("%s: %s is not a string" % (os.path.relpath(path, REPO), name))
    return m.group(1)


def board_profile(board):
    return os.path.join(REPO, "boards", board, "board_profile.h")


def firmware_labels():
    """(where, label) for every partition label the firmware and Layer 0 look up."""
    labels = []
    for name in ("TFW_ESP_PART_EEPROM", "TFW_ESP_PART_PLATFORM",
                 "TFW_ESP_PART_IDENTITY", "TFW_ESP_PART_AUTH"):
        labels.append((name, c_string(HAL_H, name)))
    for name in ("PLAT_LABEL", "AUTH_LABEL"):
        labels.append((name, c_string(BOOT_HAL_C, name)))
    return labels


class TestEsp32Partitions(unittest.TestCase):
    def test_every_label_the_firmware_reads_is_one_data_partition(self):
        for board in BOARDS:
            with self.subTest(board=board):
                table = read_table(board)
                for define, label in firmware_labels():
                    rows = [r for r in table if r[0] == label]
                    self.assertEqual(len(rows), 1,
                                     "%s: %s %r is in %d rows" % (board, define, label, len(rows)))
                    self.assertEqual(rows[0][1], "data",
                                     "%s: %s %r is type %s" % (board, define, label, rows[0][1]))

    def test_eeprom_partition_holds_the_whole_eeprom_size(self):
        label = c_string(HAL_H, "TFW_ESP_PART_EEPROM")
        for board in BOARDS:
            with self.subTest(board=board):
                want = c_define(board_profile(board), "TFW_EEPROM_SIZE")
                rows = [r for r in read_table(board) if r[0] == label]
                self.assertEqual(len(rows), 1, "%s: no single %s row" % (board, label))
                self.assertGreaterEqual(rows[0][4], want,
                                        "%s: %s is 0x%x, TFW_EEPROM_SIZE is 0x%x"
                                        % (board, label, rows[0][4], want))

    def test_ota_app_slots_match_tfw_ota_slots(self):
        for board in BOARDS:
            with self.subTest(board=board):
                want = c_define(board_profile(board), "TFW_OTA_SLOTS")
                slots = [r for r in read_table(board)
                         if r[1] == "app" and re.fullmatch(r"ota_\d+", r[2])]
                self.assertEqual(len(slots), want,
                                 "%s: %d ota_N app rows, TFW_OTA_SLOTS is %d"
                                 % (board, len(slots), want))

    def test_rows_are_aligned_disjoint_and_inside_the_app_region(self):
        for board in BOARDS:
            with self.subTest(board=board):
                region = c_define(board_profile(board), "TFW_APP_REGION_SIZE")
                table = sorted(read_table(board), key=lambda r: r[3])
                self.assertGreaterEqual(table[0][3], TABLE_OFFSET + TABLE_SIZE,
                                        "%s: %s overlaps the partition table" % (board, table[0][0]))
                for name, kind, _, offset, size in table:
                    align = APP_ALIGN if kind == "app" else DATA_ALIGN
                    self.assertEqual(offset % align, 0,
                                     "%s: %s at 0x%x is not 0x%x-aligned" % (board, name, offset, align))
                    self.assertLessEqual(offset + size, region,
                                         "%s: %s ends at 0x%x, past 0x%x"
                                         % (board, name, offset + size, region))
                for prev, cur in zip(table, table[1:]):
                    self.assertLessEqual(prev[3] + prev[4], cur[3],
                                         "%s: %s overlaps %s" % (board, prev[0], cur[0]))


if __name__ == "__main__":
    unittest.main()
