# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests for scripts/api-doc-check.py (make scripts-test). No network."""

import contextlib
import importlib.util
import io
import os
import shutil
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
spec = importlib.util.spec_from_file_location("api_doc_check", os.path.join(HERE, "api-doc-check.py"))
adc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(adc)

DEVICE_GO = """// Package tfw is the fixture.
package tfw

import "fmt"

// The wire contract.
const (
	SOF        = 0xA5
	MaxPayload = 64
	seqMax     = 255
)

// Status mirrors a firmware status.
type Status uint8

// The status codes.
const (
	StatusOK Status = iota
	StatusErrCRC
)

// ErrTimeout is a category.
var ErrTimeout = fmt.Errorf("timeout")

var usage = `
func NotAFunc() {}
`

// Device is one board.
type Device struct {
	Name string
	conn int
}

// New wraps a stream.
func New() *Device {
	if true {
		return &Device{}
	}
	return nil
}

// Ping echoes.
func (d *Device) Ping(msg []byte) ([]byte, error) { return msg, nil }

func (d *Device) send() {}

type frame struct{}

// Bytes is exported on an unexported type.
func (f frame) Bytes() []byte { return nil }

// String names the status.
func (s Status) String() string { return "" }
"""

CODEC_GO = """//go:build codec_json

package tfw

// MarshalJSON is behind a build tag.
func (s Status) MarshalJSON() ([]byte, error) { return nil, nil }
"""

SIM_GO = """//go:build tfwsim

// Package tfwsim is the fixture's sub-package.
package tfwsim

// Start runs it.
func Start() {}

type helper int
"""

INTERNAL_GO = """package peer

// Hidden is internal.
func Hidden() {}
"""

MAIN_GO = """package main

// Run is a command's.
func Run() {}

func main() {}
"""

PAGE = """# Host SDK API

Open a board with `tfw.New(...)`; ask it `tfw.Device.Ping` -- not checked, a
method ref -- and see `tfw.Status`.

## Exported symbols

### `tfw`

| Symbol | Signature | Contract |
|---|---|---|
| `tfw.SOF`, `tfw.MaxPayload` | `const` | The wire contract. |
| `tfw.Status` | `type Status uint8` | Mirrors a firmware status. |
| `tfw.StatusOK`, `tfw.StatusErrCRC` | `const Status` | The status codes. |
| `tfw.Status.String` | `func (s Status) String() string` | Names the status. |
| `tfw.Status.MarshalJSON` | `func (s Status) MarshalJSON() ([]byte, error)` | Behind a build tag. |
| `tfw.ErrTimeout` | `var ErrTimeout error` | A category. |
| `tfw.Device` | `type Device struct` | One board. |
| `tfw.New` | `func New() *Device` | Wraps a stream. |
| `tfw.Device.Ping` | `func (d *Device) Ping(msg []byte) ([]byte, error)` | Echoes. |

### `tfwsim`

| Symbol | Signature | Contract |
|---|---|---|
| `tfwsim.Start` | `func Start()` | Runs it. |

## After

| `tfw.New` | a table outside the section is not a second row |
"""


class FixtureTree(unittest.TestCase):
    """A two-package SDK whose page passes; each test breaks one fact in it."""

    def setUp(self):
        self.root = tempfile.mkdtemp(prefix="api-doc-check-")
        self.addCleanup(shutil.rmtree, self.root)
        files = {
            "sdk/go/device.go": DEVICE_GO,
            "sdk/go/codec_json.go": CODEC_GO,
            "sdk/go/device_test.go": "package tfw\n\nfunc TestOnly() {}\n",
            "sdk/go/tfwsim/sim.go": SIM_GO,
            "sdk/go/internal/peer/peer.go": INTERNAL_GO,
            "sdk/go/cmd/tfwctl/main.go": MAIN_GO,
            "sdk/go/testdata/x/x.go": "package x\n\nfunc X() {}\n",
            "docs/host-sdk/api.md": PAGE,
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
        errors = adc.check(self.root)
        joined = "\n".join(errors)
        for fragment in fragments:
            self.assertIn(fragment, joined)
        return errors


class TestPasses(FixtureTree):
    def test_exports_are_exactly_the_public_api(self):
        self.assertEqual({
            "tfw.SOF", "tfw.MaxPayload", "tfw.Status", "tfw.StatusOK", "tfw.StatusErrCRC",
            "tfw.Status.String", "tfw.Status.MarshalJSON", "tfw.ErrTimeout", "tfw.Device",
            "tfw.New", "tfw.Device.Ping", "tfwsim.Start",
        }, adc.exports(self.root))

    def test_main_reports_and_exits_zero(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(0, adc.main(["api-doc-check", self.root]))
        self.assertIn("names every exported symbol of sdk/go and nothing else", out.getvalue())

    def test_main_exits_one_on_a_failure(self):
        self.edit("docs/host-sdk/api.md", "| `tfw.New` |", "| `tfw.Neu` |")
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(1, adc.main(["api-doc-check", self.root]))
        self.assertIn("api-doc-check: docs/host-sdk/api.md: tfw.New is exported and has no row", out.getvalue())


class TestMissing(FixtureTree):
    def test_new_function_without_a_row_fails(self):
        self.edit("sdk/go/device.go", "// New wraps", "// Open opens.\nfunc Open() {}\n\n// New wraps")
        self.assertFails("tfw.Open is exported and has no row")

    def test_new_method_without_a_row_fails(self):
        self.edit("sdk/go/device.go", "func (d *Device) send() {}", "func (d *Device) Send() {}")
        self.assertFails("tfw.Device.Send is exported and has no row")

    def test_one_name_of_a_grouped_row_missing_fails(self):
        self.edit("docs/host-sdk/api.md", "`tfw.StatusOK`, `tfw.StatusErrCRC`", "`tfw.StatusOK`")
        self.assertFails("tfw.StatusErrCRC is exported and has no row")

    def test_new_block_constant_without_a_row_fails(self):
        self.edit("sdk/go/device.go", "\tseqMax     = 255", "\tSeqMax     = 255")
        self.assertFails("tfw.SeqMax is exported and has no row")

    def test_build_tagged_symbol_counts(self):
        self.edit("docs/host-sdk/api.md", "| `tfw.Status.MarshalJSON` |", "| x |")
        self.assertFails("tfw.Status.MarshalJSON is exported and has no row")

    def test_sub_package_counts(self):
        self.edit("sdk/go/tfwsim/sim.go", "type helper int", "// Stop stops.\nfunc Stop() {}")
        self.assertFails("tfwsim.Stop is exported and has no row")

    def test_new_public_package_counts(self):
        self.write("sdk/go/serialport/port.go", "package serialport\n\n// List lists.\nfunc List() {}\n")
        self.assertFails("serialport.List is exported and has no row")

    def test_missing_section_fails(self):
        self.edit("docs/host-sdk/api.md", "## Exported symbols", "## Symbols")
        self.assertFails("no '## Exported symbols' section")

    def test_missing_page_fails(self):
        os.remove(self.path("docs/host-sdk/api.md"))
        self.assertFails("docs/host-sdk/api.md: No such file")


class TestStale(FixtureTree):
    def test_removed_function_fails(self):
        self.edit("sdk/go/device.go", "func New()", "func newDevice()")
        self.assertFails("tfw.New is named and not exported")

    def test_removed_method_fails(self):
        self.edit("sdk/go/device.go", "func (d *Device) Ping(", "func (d *Device) ping(")
        self.assertFails("tfw.Device.Ping is named and not exported")

    def test_stale_prose_reference_fails(self):
        self.edit("docs/host-sdk/api.md", "and see `tfw.Status`.", "and see `tfw.Gone`.")
        self.assertFails("tfw.Gone is named and not exported")

    def test_prose_reference_to_another_package_is_not_checked(self):
        self.edit("docs/host-sdk/api.md", "and see `tfw.Status`.", "and see `xkms.Register(...)`.")
        self.assertEqual([], adc.check(self.root))

    def test_duplicate_row_fails(self):
        self.edit("docs/host-sdk/api.md", "### `tfwsim`", "| `tfw.New` | dup | dup |\n\n### `tfwsim`")
        self.assertFails("tfw.New has two rows")


class TestParse(unittest.TestCase):
    def test_literals_and_comments_are_not_code(self):
        src = 'package p\n// func Commented() {}\n/* func Block() {\n} */\nvar S = "func Str() {"\nvar R = \'{\'\nfunc Real() {}\n'
        self.assertEqual(("p", [("S", None), ("R", None), ("Real", None)]), adc.parse(src))

    def test_generic_receiver_and_function(self):
        src = "package p\n\nfunc Map[T any](x T) T { return x }\n\nfunc (l *List[T]) Push(v T) {}\n"
        self.assertEqual([("Map", None), ("Push", "List")], adc.parse(src)[1])

    def test_type_block_keeps_names_only(self):
        src = "package p\n\ntype (\n\tA int\n\tB struct {\n\t\tField int\n\t}\n)\n"
        self.assertEqual([("A", None), ("B", None)], adc.parse(src)[1])


class TestRepository(unittest.TestCase):
    """The tree itself: api.md names every exported symbol of sdk/go. A
    failure here is a missing or stale row in docs/host-sdk/api.md, not a test
    to edit."""

    def test_the_page_matches_the_sdk(self):
        self.assertEqual([], adc.check(REPO))

    def test_the_sdk_was_read(self):
        keys = adc.exports(REPO)
        for key in ("tfw.New", "tfw.Device.Do", "serialport.Open", "tfwsim.StartProvisioningWith"):
            self.assertIn(key, keys)
        self.assertFalse([k for k in keys if k.split(".")[0] in ("main", "peer", "simproc", "vectors")])


if __name__ == "__main__":
    unittest.main()
