# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""The ESP32 CDI handoff reserve agrees with TFW_BOOT_HANDOFF_LEN (make scripts-test).

The handoff block is TFW_BOOT_HANDOFF_LEN bytes (include/tfw/boot.h:402). The
ESP32 port keeps it in the bootloader's RTC-retained custom reserve, whose size
ESP-IDF's sdkconfig sets, and src/hal/esp32/hal_handoff.c:25 holds the two
equal only by a _Static_assert that compiles in the ESP-IDF build alone. On
the host every restatement of the size is a literal of its own: the arch
sdkconfig defaults, the -D the host rules pass, and the boot HAL test's
stand-in for rtc_retain_mem_t. problems() reads them all and says which one
disagrees; a board's sdkconfig is layered after the arch defaults and wins
(mk/arch/esp32-targets.mk), so no board file may set the reserve at all.
"""

import glob
import os
import re
import shutil
import tempfile
import unittest

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

BOOT_H = "include/tfw/boot.h"
ARCH_SDKCONFIG = "mk/arch/esp32/sdkconfig.defaults"
STAND_IN = "test/unit/boot/boot_hal_esp32/bootloader_common.h"
MK_GLOBS = ("mk/*.mk", "mk/arch/*.mk")
BOARD_GLOB = "boards/esp32*/sdkconfig*"

KEY = "CONFIG_BOOTLOADER_CUSTOM_RESERVE_RTC"
SIZE_KEY = KEY + "_SIZE"
IN_CRC_KEY = KEY + "_IN_CRC"


def _read(root, rel):
    with open(os.path.join(root, rel), encoding="utf-8") as f:
        return f.read()


def _rel(root, path):
    return os.path.relpath(path, root).replace(os.sep, "/")


def problems(root):
    """Every place the reserve size disagrees with TFW_BOOT_HANDOFF_LEN."""
    out = []

    m = re.search(r"^#define\s+TFW_BOOT_HANDOFF_LEN\s+(\w+?)[uU]?\s*$",
                  _read(root, BOOT_H), re.M)
    if not m:
        return [f"{BOOT_H}: no #define TFW_BOOT_HANDOFF_LEN"]
    want = int(m.group(1), 0)

    text = _read(root, ARCH_SDKCONFIG)
    sizes = re.findall(rf"^{SIZE_KEY}=(\S+)\s*$", text, re.M)
    if len(sizes) != 1:
        out.append(f"{ARCH_SDKCONFIG}: {len(sizes)} {SIZE_KEY} lines, want 1")
    elif int(sizes[0], 0) != want:
        out.append(f"{ARCH_SDKCONFIG}: {SIZE_KEY}={sizes[0]}, "
                   f"{BOOT_H}'s TFW_BOOT_HANDOFF_LEN is {want}")
    if not re.search(rf"^{KEY}=y\s*$", text, re.M):
        out.append(f"{ARCH_SDKCONFIG}: {KEY} is not =y")
    if (not re.search(rf"^# {IN_CRC_KEY} is not set\s*$", text, re.M)
            or re.search(rf"^{IN_CRC_KEY}=", text, re.M)):
        out.append(f"{ARCH_SDKCONFIG}: {IN_CRC_KEY} must be "
                   f"'# {IN_CRC_KEY} is not set' (boot/hal/esp32/boot_l0.c "
                   "writes the handoff with the CRC off)")

    seen = 0
    mks = sorted({p for g in MK_GLOBS for p in glob.glob(os.path.join(root, g))})
    for path in mks:
        rel = _rel(root, path)
        for n, line in enumerate(_read(root, rel).splitlines(), 1):
            for v in re.findall(rf"-D{SIZE_KEY}=(\w+)", line):
                seen += 1
                if int(v, 0) != want:
                    out.append(f"{rel}:{n}: -D{SIZE_KEY}={v}, "
                               f"{BOOT_H}'s TFW_BOOT_HANDOFF_LEN is {want}")
    if seen == 0:
        out.append(f"mk: no -D{SIZE_KEY}= in {' or '.join(MK_GLOBS)}")

    m = re.search(r"\bcustom\[(\w+)\]", _read(root, STAND_IN))
    if not m:
        out.append(f"{STAND_IN}: no custom[N]")
    elif int(m.group(1), 0) != want:
        out.append(f"{STAND_IN}: custom[{m.group(1)}], "
                   f"{BOOT_H}'s TFW_BOOT_HANDOFF_LEN is {want}")

    for path in sorted(glob.glob(os.path.join(root, BOARD_GLOB))):
        rel = _rel(root, path)
        for n, line in enumerate(_read(root, rel).splitlines(), 1):
            if re.match(rf"\s*(# )?{KEY}\w*(=| is not set)", line):
                out.append(f"{rel}:{n}: a board sets {line.strip()!r}; "
                           f"the reserve is {ARCH_SDKCONFIG}'s alone")
    return out


class RtcReserveTest(unittest.TestCase):
    def test_main_agrees(self):
        self.assertEqual(problems(REPO), [])

    def plant_tree(self):
        """A copy of only the files problems() reads."""
        tmp = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, tmp)
        rels = [BOOT_H, ARCH_SDKCONFIG, STAND_IN]
        for g in MK_GLOBS + (BOARD_GLOB,):
            rels += [_rel(REPO, p) for p in glob.glob(os.path.join(REPO, g))]
        for rel in rels:
            os.makedirs(os.path.join(tmp, os.path.dirname(rel)), exist_ok=True)
            shutil.copyfile(os.path.join(REPO, rel), os.path.join(tmp, rel))
        self.assertEqual(problems(tmp), [])
        return tmp

    def mutate(self, rel, old, new):
        tmp = self.plant_tree()
        path = os.path.join(tmp, rel)
        with open(path, encoding="utf-8") as f:
            text = f.read()
        self.assertIn(old, text)
        with open(path, "w", encoding="utf-8") as f:
            f.write(text.replace(old, new, 1))
        got = problems(tmp)
        self.assertTrue(got, f"{rel}: {new!r} not caught")
        self.assertTrue(any(rel in p for p in got), got)
        return got

    def test_arch_size_mutated(self):
        self.mutate(ARCH_SDKCONFIG, f"{SIZE_KEY}=0x70", f"{SIZE_KEY}=0x60")

    def test_in_crc_set(self):
        self.mutate(ARCH_SDKCONFIG, f"# {IN_CRC_KEY} is not set",
                    f"{IN_CRC_KEY}=y")

    def test_host_mk_define_mutated(self):
        self.mutate("mk/host.mk", f"-D{SIZE_KEY}=112", f"-D{SIZE_KEY}=96")

    def test_stand_in_mutated(self):
        self.mutate(STAND_IN, "custom[112]", "custom[96]")

    def test_board_override(self):
        rel = "boards/esp32p4-poe/sdkconfig.hil0"
        tmp = self.plant_tree()
        with open(os.path.join(tmp, rel), "a", encoding="utf-8") as f:
            f.write(f"{SIZE_KEY}=0x70\n")
        got = problems(tmp)
        self.assertTrue(any(rel in p for p in got), got)

    def test_handoff_len_mutated(self):
        self.mutate(BOOT_H, "TFW_BOOT_HANDOFF_LEN     112u",
                    "TFW_BOOT_HANDOFF_LEN     128u")


if __name__ == "__main__":
    unittest.main()
