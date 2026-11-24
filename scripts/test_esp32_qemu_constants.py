#!/usr/bin/env python3
"""Hold the ESP32 QEMU suite's hand-copied firmware constants to their source.

test/integration/esp32/qemu_test.py restates the wire port, board id, crypto
tiers, status codes, trial timeout, OTA_INFO states and flags, app-descriptor
magic and HMAC_UP key purpose by hand. It runs only in the ESP32 QEMU job, so a
renumber in C would otherwise show up there as an "expected text not in log"
failure. This test reads the harness with ast (it never imports it, so no
ESP-IDF, esptool or cryptography is needed) and each C file with a small
regex, and compares every pair. Run with `make scripts-test`.
"""

import ast
import os
import re
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SCRIPT = os.path.join("test", "integration", "esp32", "qemu_test.py")

HAL_ESP32 = os.path.join("src", "hal", "esp32", "hal_esp32.h")
BOARD = os.path.join("boards", "esp32-qemu", "board_profile.h")
TYPES = os.path.join("include", "tfw", "types.h")
STATUS = os.path.join("include", "tfw", "status.h")
BOOT = os.path.join("include", "tfw", "boot.h")
OTA_CTL = os.path.join("src", "core", "ota_ctl.c")
OTA_OPS = os.path.join("test", "mock", "esp32", "esp_ota_ops.h")
EFUSE = os.path.join("test", "mock", "esp32", "esp_efuse.h")

# (name in qemu_test.py, C file, name in the C file)
CONSTANTS = [
    ("WIRE_PORT", HAL_ESP32, "TFW_ESP_WIRE_PORT"),
    ("BOARD_ID", BOARD, "TFW_BOARD_ID"),
    ("TRIAL_TIMEOUT", BOARD, "TFW_OTA_TRIAL_TIMEOUT_S"),
    ("TIER_NONE", TYPES, "TFW_CRYPTO_NONE"),
    ("TIER_SECURE_ELEMENT", TYPES, "TFW_CRYPTO_SECURE_ELEMENT"),
    ("ERR_UNSUPPORTED", STATUS, "TFW_ERR_UNSUPPORTED"),
    ("ERR_CRYPTO", STATUS, "TFW_ERR_CRYPTO"),
    ("ERR_PROTECTED_REGION", STATUS, "TFW_ERR_PROTECTED_REGION"),
    ("ERR_AUTH_REQUIRED", STATUS, "TFW_ERR_AUTH_REQUIRED"),
    ("STATE_GOOD", BOOT, "TFW_BOOT_STATE_GOOD"),
    ("STATE_TRIAL", BOOT, "TFW_BOOT_STATE_TRIAL"),
    ("FLAG_FELL_BACK", OTA_CTL, "INFO_FLAG_FELL_BACK"),
    ("FLAG_TRIAL", OTA_CTL, "INFO_FLAG_TRIAL"),
    ("APP_DESC_MAGIC", OTA_OPS, "ESP_APP_DESC_MAGIC_WORD"),
    ("EFUSE_KEY_PURPOSE_HMAC_UP", EFUSE, "ESP_EFUSE_KEY_PURPOSE_HMAC_UP"),
]

_C_VALUE = re.compile(
    r"^\s*(?:#\s*define\s+(\w+)\s+|(\w+)\s*=\s*)"
    r"(0[xX][0-9A-Fa-f]+|\d+)[uU]?\b", re.MULTILINE)


def read(rel):
    with open(os.path.join(REPO, rel), encoding="utf-8") as f:
        return f.read()


def python_constants(text):
    """Module-level NAME = literal and A, B = literal, literal assignments."""
    out = {}
    for node in ast.parse(text).body:
        if not isinstance(node, ast.Assign):
            continue
        try:
            value = ast.literal_eval(node.value)
        except ValueError:
            continue
        for target in node.targets:
            if isinstance(target, ast.Name):
                out[target.id] = value
            elif (isinstance(target, ast.Tuple)
                  and isinstance(value, tuple)
                  and len(target.elts) == len(value)
                  and all(isinstance(e, ast.Name) for e in target.elts)):
                for e, v in zip(target.elts, value):
                    out[e.id] = v
    return out


def c_constants(text):
    """`#define NAME <int>u?` and `NAME = <int>` enumerators, first wins."""
    out = {}
    for m in _C_VALUE.finditer(text):
        out.setdefault(m.group(1) or m.group(2), int(m.group(3), 0))
    return out


def mismatches(py, c_texts, rows):
    """One message per row whose python value differs from the C value."""
    bad = []
    for py_name, c_file, c_name in rows:
        c_val = c_constants(c_texts[c_file]).get(c_name)
        py_val = py.get(py_name)
        if py_val != c_val:
            bad.append("%s %s = %r but %s %s = %r" % (
                SCRIPT, py_name, py_val, c_file, c_name, c_val))
    return bad


class QemuConstantsTest(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.py = python_constants(read(SCRIPT))
        cls.c_texts = {f: read(f) for f in {row[1] for row in CONSTANTS}}

    def check_file(self, c_file):
        rows = [row for row in CONSTANTS if row[1] == c_file]
        self.assertTrue(rows, c_file)
        bad = mismatches(self.py, self.c_texts, rows)
        if bad:
            self.fail("\n".join(bad))

    def test_hal_esp32_h(self):
        self.check_file(HAL_ESP32)

    def test_board_profile_h(self):
        self.check_file(BOARD)

    def test_types_h(self):
        self.check_file(TYPES)

    def test_status_h(self):
        self.check_file(STATUS)

    def test_boot_h(self):
        self.check_file(BOOT)

    def test_ota_ctl_c(self):
        self.check_file(OTA_CTL)

    def test_esp_ota_ops_h(self):
        self.check_file(OTA_OPS)

    def test_esp_efuse_h(self):
        self.check_file(EFUSE)

    def test_every_name_is_found(self):
        for py_name, c_file, c_name in CONSTANTS:
            self.assertIn(py_name, self.py, "%s has no %s" % (SCRIPT, py_name))
            self.assertIn(c_name, c_constants(self.c_texts[c_file]),
                          "%s has no %s" % (c_file, c_name))

    def test_a_renumbered_copy_fails(self):
        texts = dict(self.c_texts)
        status = texts[STATUS].replace("TFW_ERR_AUTH_REQUIRED    = 17",
                                       "TFW_ERR_AUTH_REQUIRED    = 18")
        self.assertNotEqual(status, texts[STATUS])
        hal = texts[HAL_ESP32].replace("21574", "21575")
        self.assertNotEqual(hal, texts[HAL_ESP32])
        texts[STATUS], texts[HAL_ESP32] = status, hal
        bad = mismatches(self.py, texts, CONSTANTS)
        self.assertEqual(len(bad), 2, bad)
        self.assertIn("ERR_AUTH_REQUIRED = 17 but %s" % STATUS, bad[0] + bad[1])
        self.assertIn("WIRE_PORT = 21574 but %s" % HAL_ESP32, bad[0] + bad[1])


if __name__ == "__main__":
    unittest.main()
