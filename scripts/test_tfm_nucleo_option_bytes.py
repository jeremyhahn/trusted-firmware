# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""The NUCLEO-H563ZI TF-M port never writes an option byte (make scripts-test).

#608 slice 4, Codex round 5: option bytes are the owner's to set (CA 583).
The STM32 BL2 programs them itself when TFM_ENABLE_SET_OB is defined, so no
file of the out-of-tree port src/tz/tfm/platform/nucleo_h563zi may define it
(a #define, or a -D / add_compile_definitions in a cmake file), and no file
there may call the HAL's option-byte programming functions.
"""

import os
import re
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
PORT = os.environ.get(
    "TFM_NUCLEO_PORT",
    os.path.join(REPO, "src", "tz", "tfm", "platform", "nucleo_h563zi"),
)

DEFINE = re.compile(r"#\s*define\s+TFM_ENABLE_SET_OB\b")
CMAKE_DEFINE = re.compile(
    r"(-DTFM_ENABLE_SET_OB\b|add_compile_definitions\([^)]*\bTFM_ENABLE_SET_OB\b"
    r"|target_compile_definitions\([^)]*\bTFM_ENABLE_SET_OB\b)"
)
OB_WRITE = re.compile(r"\b(HAL_FLASHEx_OBProgram|HAL_FLASH_OB_Launch)\s*\(")


def _port_files():
    for root, _dirs, files in os.walk(PORT):
        for name in sorted(files):
            path = os.path.join(root, name)
            with open(path, errors="replace") as f:
                yield os.path.relpath(path, REPO), f.read()


class OptionBytesTest(unittest.TestCase):
    def test_the_port_exists(self):
        self.assertTrue(os.path.isdir(PORT), PORT)

    def test_no_file_enables_option_byte_programming(self):
        hits = []
        for rel, text in _port_files():
            if DEFINE.search(text):
                hits.append(rel + ": #define TFM_ENABLE_SET_OB")
            if (rel.endswith(".cmake") or rel.endswith("CMakeLists.txt")) and CMAKE_DEFINE.search(text):
                hits.append(rel + ": TFM_ENABLE_SET_OB compile definition")
        self.assertEqual(hits, [])

    def test_no_file_calls_the_option_byte_programming_hal(self):
        hits = [rel for rel, text in _port_files() if OB_WRITE.search(text)]
        self.assertEqual(hits, [])


if __name__ == "__main__":
    unittest.main()
