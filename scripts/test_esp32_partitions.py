# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Tests for the ESP32 boards' partition tables (make scripts-test).

They read each boards/<board>/partitions.csv as plain CSV, so they need no
ESP-IDF: the rows do not overlap, start above the partition table, put the app
slots on 64 KB and inside the flash (below 16 MB on esp32p4-poe), hold the four
tfw_* labels src/hal/esp32/hal_esp32.h names, and give the offsets
docs/boards/esp32p4-provisioning.md tells the operator to write (#23)."""

import os
import re
import tempfile
import unittest

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

BOARDS = ("esp32p4-poe", "esp32-qemu")
ARCH_SDKCONFIG = "mk/arch/esp32/sdkconfig.defaults"
HAL_HEADER = "src/hal/esp32/hal_esp32.h"
PROVISIONING = "docs/boards/esp32p4-provisioning.md"

SECTOR = 0x1000
TABLE_SIZE = 0x1000           # the partition table itself
APP_ALIGN = 0x10000           # ESP-IDF's app partition alignment
P4_APP_LIMIT = 0x1000000      # 16 MB: ESP-IDF v5.5.5 maps apps only below it
HEX = re.compile(r"0x[0-9A-Fa-f]+$")


def table_path(board):
    return os.path.join(REPO, "boards", board, "partitions.csv")


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def table_lines(path):
    """(line number, fields) for each row of the table, comments skipped."""
    for n, line in enumerate(read(path).splitlines(), 1):
        line = line.strip()
        if line and not line.startswith("#"):
            yield n, [f.strip() for f in line.split(",")]


def parse_table(path):
    """The table's rows as (name, type, subtype, offset, size, flags) tuples.

    A row that does not parse is left out; check_rows reports it."""
    rows = []
    for _, fields in table_lines(path):
        if len(fields) not in (5, 6):
            continue
        if not (HEX.match(fields[3]) and HEX.match(fields[4])):
            continue
        flags = fields[5] if len(fields) == 6 else ""
        rows.append((fields[0], fields[1], fields[2],
                     int(fields[3], 16), int(fields[4], 16), flags))
    return rows


def check_rows(path):
    """Check 1: every row has 5 or 6 fields, hex offset and size, whole sectors."""
    problems = []
    for n, fields in table_lines(path):
        where = f"{path}:{n} ({fields[0]})"
        if len(fields) not in (5, 6):
            problems.append(f"{where}: {len(fields)} fields, want 5 or 6")
            continue
        for what, value in (("offset", fields[3]), ("size", fields[4])):
            if not HEX.match(value):
                problems.append(f"{where}: {what} {value!r} is not hex")
        if HEX.match(fields[4]) and int(fields[4], 16) % SECTOR:
            problems.append(f"{where}: size {fields[4]} is not whole 4 KB "
                            "sectors")
    return problems


def check_order(path):
    """Check 2: the rows ascend by offset and no two overlap."""
    problems = []
    rows = parse_table(path)
    for prev, row in zip(rows, rows[1:]):
        if row[3] < prev[3]:
            problems.append(f"{path}: {row[0]} at {row[3]:#x} is below "
                            f"{prev[0]} at {prev[3]:#x}")
        elif row[3] < prev[3] + prev[4]:
            problems.append(f"{path}: {row[0]} at {row[3]:#x} overlaps "
                            f"{prev[0]}, which ends at {prev[3] + prev[4]:#x}")
    return problems


def check_above_table(path, table_offset):
    """Check 3: no row starts on or below the partition table."""
    floor = table_offset + TABLE_SIZE
    return [f"{path}: {r[0]} at {r[3]:#x} is below {floor:#x}, the end of "
            f"the partition table at {table_offset:#x}"
            for r in parse_table(path) if r[3] < floor]


def check_app_alignment(path):
    """Check 4: every app row starts on 64 KB."""
    return [f"{path}: app {r[0]} at {r[3]:#x} is not 64 KB aligned"
            for r in parse_table(path) if r[1] == "app" and r[3] % APP_ALIGN]


def check_flash(path, flash_size, app_limit=None):
    """Check 5: every row ends inside the flash; app rows below app_limit."""
    problems = []
    for r in parse_table(path):
        end = r[3] + r[4]
        if end > flash_size:
            problems.append(f"{path}: {r[0]} ends at {end:#x}, past the "
                            f"{flash_size:#x} flash")
        if app_limit is not None and r[1] == "app" and end > app_limit:
            problems.append(f"{path}: app {r[0]} ends at {end:#x}, past "
                            f"{app_limit:#x}")
    return problems


def check_labels(path, labels):
    """Check 6: the hal_esp32.h labels, ESP-IDF's OTA rows, the encrypted flags."""
    problems = []
    rows = parse_table(path)
    names = [r[0] for r in rows]
    subtypes = {}
    for label in labels:
        found = [r for r in rows if r[0] == label]
        if len(found) != 1:
            problems.append(f"{path}: {label} appears {len(found)} times, "
                            "want once")
            continue
        r = found[0]
        if r[1] != "data":
            problems.append(f"{path}: {label} is type {r[1]}, want data")
        if not HEX.match(r[2]) or not 0x40 <= int(r[2], 16) <= 0xFE:
            problems.append(f"{path}: {label} subtype {r[2]} is not a custom "
                            "one (0x40..0xFE)")
        elif int(r[2], 16) in subtypes:
            problems.append(f"{path}: {label} subtype {r[2]} is also "
                            f"{subtypes[int(r[2], 16)]}'s")
        else:
            subtypes[int(r[2], 16)] = label
    for name in ("otadata", "ota_0", "ota_1"):
        if name not in names:
            problems.append(f"{path}: no {name} row")
    for r in rows:
        if r[0] in ("tfw_plat", "tfw_eeprom") and "encrypted" not in r[5]:
            problems.append(f"{path}: {r[0]} is not encrypted")
    return problems


def check_provisioning(doc, path):
    """Check 7: the doc's write and erase offsets are the table's."""
    problems = []
    text = read(doc)
    rows = {r[0]: r for r in parse_table(path)}

    def one(pattern, what):
        found = re.findall(pattern, text, re.MULTILINE)
        if len(found) != 1:
            problems.append(f"{doc}: {len(found)} {what} lines, want one")
            return None
        return found[0]

    for image, row in (("tfw_plat.bin", "tfw_plat"),
                       ("trusted-firmware.bin", "ota_0")):
        offset = one(r"^\s*(0x[0-9A-Fa-f]+)\s+" + re.escape(image) + r"\b",
                     f"write-flash {image}")
        if offset is None:
            continue
        if row not in rows:
            problems.append(f"{path}: no {row} row for {doc}'s {image}")
        elif int(offset, 16) != rows[row][3]:
            problems.append(f"{doc}: {image} at {offset}, but {path} puts "
                            f"{row} at {rows[row][3]:#x}")
    erase = one(r"erase-region (0x[0-9A-Fa-f]+) (0x[0-9A-Fa-f]+)",
                "erase-region")
    if erase is not None:
        if "tfw_id" not in rows or "tfw_auth" not in rows:
            problems.append(f"{path}: no tfw_id or tfw_auth row for {doc}'s "
                            "erase-region")
        else:
            start = rows["tfw_id"][3]
            end = rows["tfw_auth"][3] + rows["tfw_auth"][4]
            if (int(erase[0], 16), int(erase[1], 16)) != (start, end - start):
                problems.append(
                    f"{doc}: erase-region {erase[0]} {erase[1]}, but {path} "
                    f"has tfw_id..tfw_auth at {start:#x} for {end - start:#x}")
    return problems


def table_offset():
    m = re.search(r"^CONFIG_PARTITION_TABLE_OFFSET=(0x[0-9A-Fa-f]+)$",
                  read(os.path.join(REPO, ARCH_SDKCONFIG)), re.MULTILINE)
    if not m:
        raise AssertionError(f"{ARCH_SDKCONFIG}: no "
                             "CONFIG_PARTITION_TABLE_OFFSET")
    return int(m.group(1), 16)


def flash_size(board):
    path = os.path.join("boards", board, "sdkconfig.defaults")
    found = re.findall(r"^CONFIG_ESPTOOLPY_FLASHSIZE_(\d+)MB=y$",
                       read(os.path.join(REPO, path)), re.MULTILINE)
    if len(found) != 1:
        raise AssertionError(f"{path}: {len(found)} "
                             "CONFIG_ESPTOOLPY_FLASHSIZE_<n>MB=y lines")
    return int(found[0]) << 20


def app_limit(board):
    return P4_APP_LIMIT if board == "esp32p4-poe" else None


def hal_labels():
    labels = re.findall(r'^#define TFW_ESP_PART_\w+\s+"([^"]+)"',
                        read(os.path.join(REPO, HAL_HEADER)), re.MULTILINE)
    if len(labels) != 4:
        raise AssertionError(f"{HAL_HEADER}: {len(labels)} TFW_ESP_PART_* "
                             f"labels, want 4: {labels}")
    return labels


class PartitionTablesKeepTheirLayout(unittest.TestCase):

    def each_board(self, check):
        for board in BOARDS:
            with self.subTest(board=board):
                self.assertEqual(check(board), [])

    def test_rows_parse(self):
        self.each_board(lambda b: check_rows(table_path(b)))

    def test_rows_ascend_and_do_not_overlap(self):
        self.each_board(lambda b: check_order(table_path(b)))

    def test_rows_start_above_the_partition_table(self):
        self.each_board(lambda b: check_above_table(table_path(b),
                                                    table_offset()))

    def test_app_slots_are_64k_aligned(self):
        self.each_board(lambda b: check_app_alignment(table_path(b)))

    def test_rows_end_inside_the_flash(self):
        self.each_board(lambda b: check_flash(table_path(b), flash_size(b),
                                              app_limit(b)))

    def test_hal_labels_ota_rows_and_encryption(self):
        self.each_board(lambda b: check_labels(table_path(b), hal_labels()))

    def test_provisioning_doc_offsets_are_the_tables(self):
        self.assertEqual(check_provisioning(os.path.join(REPO, PROVISIONING),
                                            table_path("esp32p4-poe")), [])


class BadTablesAreReported(unittest.TestCase):
    """Each check reports a table edited the way a careless change would."""

    def mutated(self, board, old, new):
        text = read(table_path(board))
        self.assertEqual(text.count(old), 1, f"{old!r} in {board}")
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        path = os.path.join(tmp.name, "partitions.csv")
        with open(path, "w", encoding="utf-8") as f:
            f.write(text.replace(old, new))
        return path

    def test_unaligned_overlapping_ota_1(self):
        path = self.mutated("esp32p4-poe", "0x620000, 0x600000",
                            "0x618000, 0x600000")
        self.assertTrue(check_order(path))
        self.assertTrue(check_app_alignment(path))

    def test_tfw_plat_on_the_partition_table(self):
        path = self.mutated("esp32p4-poe", "0x11000,  0x1000",
                            "0x10000,  0x1000")
        self.assertTrue(check_above_table(path, table_offset()))
        self.assertTrue(check_provisioning(
            os.path.join(REPO, PROVISIONING), path))

    def test_qemu_ota_1_past_4mb(self):
        # 0x200000 + 0x201000 ends a sector past the 4 MB flash.
        path = self.mutated("esp32-qemu", "0x200000, 0x1E0000",
                            "0x200000, 0x201000")
        self.assertTrue(check_flash(path, flash_size("esp32-qemu")))

    def test_p4_ota_1_past_16mb(self):
        path = self.mutated("esp32p4-poe", "0x620000, 0x600000",
                            "0xA20000, 0x600000")
        self.assertEqual(check_flash(path, flash_size("esp32p4-poe")), [])
        self.assertTrue(check_flash(path, flash_size("esp32p4-poe"),
                                    P4_APP_LIMIT))

    def test_tfw_auth_removed(self):
        path = self.mutated("esp32p4-poe",
                            "tfw_auth,    data, 0x43,    0x16000,  0x1000,\n",
                            "")
        self.assertTrue(check_labels(path, hal_labels()))
        self.assertTrue(check_provisioning(
            os.path.join(REPO, PROVISIONING), path))


if __name__ == "__main__":
    unittest.main()
