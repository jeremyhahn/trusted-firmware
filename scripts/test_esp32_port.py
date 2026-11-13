# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# The ESP32 port (SPEC row esp32), held on the host. CI has no ESP-IDF, so
# nothing in `make ci` compiles or links src/hal/esp32/ or boot/hal/esp32/ for
# a part: a function added to include/tfw/hal.h that the mock and the AVR HAL
# implement passes every gate and breaks only `make esp32-build`, on a bench.
# These tests read the port's two object lists as the build hands them over
# and check, as text, what the link would:
#
#   - the image: mk/firmware.mk's FW_SRCS plus ARCH_EXTRA_SRCS, which
#     esp32-build passes to ESP-IDF as TFW_SRCS, taken from `make -n`
#     for every ESP32 board, target and provisioning image;
#   - layer 0: the bootloader component's SRCS, in
#     boot/hal/esp32/bootloader_components/main/CMakeLists.txt.
#
# Each list names only files that exist, together they name every file of the
# port, and every tfw_ function a listed file calls is defined by a listed
# file or inline in a header. And ESP-IDF stays behind the HAL, the boot HAL
# and the ESP32 board profiles: no other file includes an ESP-IDF header or
# names an ESP-IDF type, which `make lint-arch-leak` enforces and these tests
# drive. Each check also runs on a plant in a throwaway tree,
# so a check that finds nothing is known to be able to find something.

import glob
import os
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

HAL_DIR = "src/hal/esp32"
BOOT_HAL_DIR = "boot/hal/esp32"
L0_CMAKE = BOOT_HAL_DIR + "/bootloader_components/main/CMakeLists.txt"
ARCH_MK = "mk/arch/esp32.mk"

# The layer-0 verify each board's ESP_L0_VERIFY selects, and the generated key
# slot, which esp32-build writes from boot/verify/p256_key_slot.c's.
L0_VERIFY = {
    "p256-rom": ["boot/hal/esp32/verify_p256_rom.c"],
    "p256-soft": ["boot/verify/verify_p256_soft.c", "boot/hal/esp32/verify_digest_soft.c"],
}
L0_KEY_SLOT = "boot/verify/p256_key_slot.c"

# ESP-IDF's headers, by the prefix of the name the port includes them by: the
# prefixes of ARCH_LEAK_ESP_INC in mk/host.mk, each planted below.
ESP_IDF_HEADERS = ("esp_", "driver/", "freertos/", "lwip/", "bootloader_", "soc/",
                   "rom/", "hal/", "sdkconfig.h")
# The C library's, which every architecture has.
LIBC_HEADERS = {"stdint.h", "stddef.h", "stdbool.h", "string.h", "errno.h", "fcntl.h",
                "sys/reent.h"}

INCLUDE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*[<"]([^>"]+)[>"]', re.M)
DEFINITION = re.compile(r"^(?![#\s])[^;{}()\n]*?\b(tfw_\w+)\s*\([^;{}]*?\)\s*\{", re.M)
CALL = re.compile(r"\b(tfw_\w+)\s*\(")
MACRO = re.compile(r"^[ \t]*#[ \t]*define[ \t]+(tfw_\w+)", re.M)
ARCH_LINE = re.compile(r"^ARCH\s*:?=\s*esp32\s*$", re.M)
TARGETS_LINE = re.compile(r"^ESP_TARGETS\s*:?=\s*(.+?)\s*$", re.M)


def read(root, path):
    with open(os.path.join(root, path), encoding="utf-8") as f:
        return f.read()


def strip_c(text):
    """Comments and string literals blanked, line numbers kept."""
    text = re.sub(r"/\*.*?\*/", lambda m: re.sub(r"[^\n]", " ", m.group(0)), text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    return re.sub(r'"(?:\\.|[^"\\\n])*"', '""', text)


def file_scope_lines(text):
    """Declaration lines: at column 0 and not a preprocessor line."""
    return [l for l in text.splitlines() if l[:1] and not l[:1].isspace() and l[0] != "#"]


def body_lines(text):
    return [l for l in text.splitlines() if not l[:1] or l[:1].isspace() or l[0] == "#"]


def c_files(root, top):
    out = []
    for path in glob.glob(os.path.join(root, top, "**", "*.[ch]"), recursive=True):
        out.append(os.path.relpath(path, root))
    return sorted(out)


def esp32_boards(root):
    """{board: [target, ...]} for every boards/*/profile.mk that says ARCH := esp32."""
    boards = {}
    for prof in sorted(glob.glob(os.path.join(root, "boards", "*", "profile.mk"))):
        with open(prof, encoding="utf-8") as f:
            text = f.read()
        if ARCH_LINE.search(text):
            m = TARGETS_LINE.search(text)
            boards[os.path.basename(os.path.dirname(prof))] = m.group(1).split() if m else []
    return boards


def make_env():
    env = dict(os.environ)
    for k in ("MAKEFLAGS", "MFLAGS", "MAKELEVEL", "MAKEOVERRIDES"):
        env.pop(k, None)
    return env


def image_srcs(board, target, provision):
    """(tier, sources) esp32-build hands ESP-IDF, from a dry run that needs no ESP-IDF."""
    args = ["make", "--no-print-directory", "-n", "esp32-build", "BOARD=" + board,
            "ESP_TARGET=" + target]
    if provision:
        args.append("PROVISION=1")
    run = subprocess.run(args, cwd=ROOT, env=make_env(), capture_output=True, text=True)
    if run.returncode != 0:
        raise AssertionError("%s: rc %d\n%s" % (" ".join(args), run.returncode, run.stderr[-2000:]))
    srcs = re.search(r'-DTFW_SRCS="([^"]*)"', run.stdout)
    out = re.search(r"/esp32/%s-%s-([a-z0-9-]+?)(-prov)?/idf\b" % (re.escape(board), re.escape(target)),
                    run.stdout)
    if not srcs or not out:
        raise AssertionError("%s printed no TFW_SRCS or build directory" % " ".join(args))
    return out.group(1), [os.path.relpath(p, ROOT) for p in srcs.group(1).split(";")]


def provisioning_tiers(root):
    return set(re.findall(r"^ARCH_PROV_SRCS_([a-z0-9-]+)\s*:?=", read(root, ARCH_MK), re.M))


def l0_listed(root):
    return re.findall(r'"\$\{TFW_ROOT\}/([^"]+\.c)"', read(root, L0_CMAKE))


def l0_srcs(root, verify):
    every_verify = {f for files in L0_VERIFY.values() for f in files}
    return [f for f in l0_listed(root) if f not in every_verify] + L0_VERIFY[verify] + [L0_KEY_SLOT]


def headers(root):
    out = c_files(root, "include") + c_files(root, "boot") + c_files(root, "src")
    return [h for h in out if h.endswith(".h") and "/third_party/" not in h]


def unresolved_calls(root, srcs):
    """tfw_ functions declared in a header and called in srcs that no file of srcs defines."""
    declared, inline, macros = set(), set(), set()
    for h in headers(root):
        raw = read(root, h)
        text = strip_c(raw)
        for line in file_scope_lines(text):
            declared.update(CALL.findall(line))
        inline.update(DEFINITION.findall(text))
        macros.update(MACRO.findall(raw))
    called, defined = set(), set()
    for f in srcs:
        text = strip_c(read(root, f))
        for line in body_lines(text):
            called.update(CALL.findall(line))
        defined.update(DEFINITION.findall(text))
    return sorted((called & declared) - macros - defined - inline)


def arch_leak_files(root):
    """The files `make lint-arch-leak` reports as ESP-IDF leaks in the tree at root."""
    run = subprocess.run(["make", "--no-print-directory", "lint-arch-leak", "ARCH_LEAK_ROOT=" + root],
                         cwd=ROOT, env=make_env(), capture_output=True, text=True)
    files = sorted({l.split(":")[0] for l in run.stdout.splitlines() if l.endswith(": arch-leak (ESP-IDF)")})
    if run.returncode != 0 and not files:
        # A failure that names no ESP-IDF file failed for another reason; say which.
        raise AssertionError("make lint-arch-leak exited %d\n--- stdout\n%s--- stderr\n%s"
                             % (run.returncode, run.stdout, run.stderr))
    return run.returncode, files


class ImageObjectList(unittest.TestCase):
    """The application image's sources, as esp32-build passes them to ESP-IDF."""

    @classmethod
    def setUpClass(cls):
        cls.images = {}
        prov = provisioning_tiers(ROOT)
        for board, targets in esp32_boards(ROOT).items():
            for target in targets:
                tier, srcs = image_srcs(board, target, False)
                cls.images[(board, target, tier)] = srcs
                if tier in prov:
                    cls.images[(board, target, tier + "-prov")] = image_srcs(board, target, True)[1]

    def test_both_isas_have_an_image(self):
        # The row's parts: Xtensa (the ESP32) and RISC-V (the C3, the P4).
        targets = {t for (_, t, _) in self.images}
        self.assertIn("esp32", targets)
        self.assertTrue(targets & {"esp32c3", "esp32p4"}, targets)

    def test_every_listed_file_exists(self):
        for image, srcs in self.images.items():
            for f in srcs:
                self.assertTrue(os.path.isfile(os.path.join(ROOT, f)), "%s lists %s" % (image, f))

    def test_every_hal_file_is_in_an_image(self):
        listed = {f for srcs in self.images.values() for f in srcs}
        for f in c_files(ROOT, HAL_DIR):
            if f.endswith(".c"):
                self.assertIn(f, listed, "no ESP32 image links " + f)

    def test_no_image_links_another_hal_or_a_test_file(self):
        for image, srcs in self.images.items():
            for f in srcs:
                self.assertFalse(f.startswith("src/hal/") and not f.startswith(HAL_DIR + "/"),
                                 "%s links %s" % (image, f))
                self.assertFalse(f.startswith(("test/", "boot/hal/")), "%s links %s" % (image, f))

    def test_every_called_function_is_defined(self):
        for image, srcs in self.images.items():
            self.assertEqual([], unresolved_calls(ROOT, srcs), "%s would not link" % (image,))

    def test_a_dropped_hal_file_is_found(self):
        srcs = next(iter(self.images.values()))
        dropped = [f for f in srcs if f != HAL_DIR + "/hal_gpio.c"]
        self.assertNotEqual(srcs, dropped)
        self.assertIn("tfw_hal_gpio_write", unresolved_calls(ROOT, dropped))

    def test_a_hal_function_the_port_lacks_is_found(self):
        # src/core/ota_prev.c reads the AVR's program flash, which this port
        # does not implement; an image that linked it would not link.
        srcs = next(iter(self.images.values())) + ["src/core/ota_prev.c"]
        self.assertIn("tfw_hal_flash_read", unresolved_calls(ROOT, srcs))


class LayerZeroObjectList(unittest.TestCase):
    """The bootloader component's sources, in its CMakeLists.txt."""

    def test_every_listed_file_exists(self):
        for f in l0_listed(ROOT) + [L0_KEY_SLOT]:
            self.assertTrue(os.path.isfile(os.path.join(ROOT, f)), L0_CMAKE + " lists " + f)

    def test_every_verify_is_listed(self):
        listed = set(l0_listed(ROOT))
        for verify, files in L0_VERIFY.items():
            for f in files:
                self.assertIn(f, listed, "%s: %s's %s" % (L0_CMAKE, verify, f))

    def test_every_boot_hal_file_is_listed(self):
        listed = set(l0_listed(ROOT))
        for f in c_files(ROOT, BOOT_HAL_DIR):
            if f.endswith(".c") and "/bootloader_components/" not in f:
                self.assertIn(f, listed, L0_CMAKE + " does not list " + f)

    def test_every_board_selects_a_known_verify(self):
        for board in esp32_boards(ROOT):
            prof = read(ROOT, "boards/%s/profile.mk" % board)
            m = re.search(r"^ESP_L0_VERIFY\s*:?=\s*(\S+)", prof, re.M)
            self.assertTrue(m, board + " sets no ESP_L0_VERIFY")
            self.assertIn(m.group(1), L0_VERIFY, board)

    def test_every_called_function_is_defined(self):
        for verify in L0_VERIFY:
            self.assertEqual([], unresolved_calls(ROOT, l0_srcs(ROOT, verify)),
                             "layer 0 with %s would not link" % verify)

    def test_a_dropped_boot_hal_file_is_found(self):
        srcs = [f for f in l0_srcs(ROOT, "p256-soft") if f != BOOT_HAL_DIR + "/boot_hal_esp32.c"]
        self.assertTrue(unresolved_calls(ROOT, srcs))


class EspIdfStaysBehindTheHal(unittest.TestCase):
    """Per-architecture code lives only behind the HAL, the boot HAL and the board profiles."""

    # `make lint-arch-leak` (mk/host.mk) is the check; these tests prove it
    # finds what it must, and passes what it must not find, in a throwaway tree.

    def plant_tree(self):
        tmp = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, tmp)
        for d in ("src", "boot", "include", "tools", "test", "boards"):
            os.makedirs(os.path.join(tmp, d), exist_ok=True)

        def plant(path, text):
            os.makedirs(os.path.join(tmp, os.path.dirname(path)), exist_ok=True)
            with open(os.path.join(tmp, path), "w", encoding="utf-8") as f:
                f.write(text)
        return tmp, plant

    def test_this_tree(self):
        self.assertEqual((0, []), arch_leak_files(ROOT))

    def test_the_header_list_covers_what_the_port_includes(self):
        # Every header the port includes is ours, the C library's or matches
        # ESP_IDF_HEADERS, so a new ESP-IDF header family cannot slip past the
        # check above by a prefix it does not know.
        ours = ["include", "src", "src/core", "src/proto", "src/crypto", "boot", "boot/verify",
                HAL_DIR, BOOT_HAL_DIR, "src/third_party/micro-ecc"]
        for path in c_files(ROOT, HAL_DIR) + c_files(ROOT, BOOT_HAL_DIR):
            for name in INCLUDE.findall(read(ROOT, path)):
                here = os.path.join(os.path.dirname(path), name)
                if name in LIBC_HEADERS or name.startswith(ESP_IDF_HEADERS):
                    continue
                if os.path.isfile(os.path.join(ROOT, here)):
                    continue
                self.assertTrue(any(os.path.isfile(os.path.join(ROOT, d, name)) for d in ours),
                                "%s includes %s: not ours, not libc, not in ESP_IDF_HEADERS" % (path, name))

    def test_a_plant_is_found(self):
        tmp, plant = self.plant_tree()
        plant("boards/espboard/profile.mk", "ARCH := esp32\nESP_TARGETS := esp32c3\n")
        plant("boards/avrboard/profile.mk", "ARCH := avr\n")
        plant("boards/espboard/board.c", '#include "esp_log.h"\n')
        plant("src/hal/esp32/hal_x.c", '#include "driver/gpio.h"\nesp_err_t x;\n')
        plant("boot/hal/esp32/boot_x.c", '#include "rom/sha.h"\n')
        plant("src/third_party/v/v.c", '#include "esp_log.h"\n')
        plant("src/core/ok.c", '/* esp_err_t, IRAM_ATTR */\n#include "tfw/hal.h"\nconst char *s = "ESP_OK";\n')
        plant("src/core/bad.c", '#include <stdint.h>\n#include "freertos/task.h"\n')
        plant("boot/bad.c", "\nstatic esp_err_t e;\n")
        plant("include/tfw/bad.h", "#  include <sdkconfig.h>\n")
        plant("boards/avrboard/board.c", "IRAM_ATTR void f(void);\n")
        plant("test/mock/esp32/esp_err.h", "typedef int esp_err_t;\n")
        self.assertEqual(arch_leak_files(tmp), (2, [
            "boards/avrboard/board.c",
            "boot/bad.c",
            "include/tfw/bad.h",
            "src/core/bad.c",
        ]))

    def test_every_header_prefix_and_name_is_found(self):
        # One plant at a time, so each prefix of ESP_IDF_HEADERS and each name
        # is shown to fail the gate on its own, naming the file.
        leaks = ['#include "%s"' % (p if p.endswith(".h") else p + "x.h") for p in ESP_IDF_HEADERS]
        leaks += ["%s x;" % n for n in ("esp_err_t", "ESP_OK", "ESP_FAIL", "ESP_ERR_NO_MEM", "IRAM_ATTR",
                                        "DRAM_ATTR", "RTC_NOINIT_ATTR", "EXT_RAM_ATTR", "CONFIG_IDF_TARGET",
                                        "portMAX_DELAY", "xTaskCreatePinnedToCore", "vTaskDelayUntil")]
        for leak in leaks:
            with self.subTest(leak=leak):
                tmp, plant = self.plant_tree()
                plant("src/core/plant.c", "int ok;\n" + leak + "\n")
                self.assertEqual(arch_leak_files(tmp), (2, ["src/core/plant.c"]))


if __name__ == "__main__":
    unittest.main()
