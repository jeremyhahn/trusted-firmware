// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

// Package agsim starts the firmware, compiled for the host, as a peer for
// tests.
//
// agsim is the REAL firmware -- the same protocol, dispatch and capability
// code the device runs -- linked against a mock HAL. This package is the one
// supported way for another module's tests to run it, so go-xkms
// (pkg/backend/avr) and go-trusted-platform (pkg/firmware/avr) do not each grow
// their own launcher and their own idea of where the binary lives.
//
// # Two tiers
//
// [Start] runs the software tier. It has no asymmetric key, so ATTEST answers
// "not provisioned" and ATTEST_CERT "unsupported".
//
// [StartElement] runs the element tier: the real ATECC608A driver and provider
// against the software model of the part (test/model/atecc608a.c) standing in
// as the I2C bus, started with the TEST IDENTITY below. ATTEST and ATTEST_CERT
// then produce the same evidence frame and UCCS alias certificate a device
// produces, bound to the caller's nonce. [StartBlankElement] is the same
// binary started with nothing provisioned.
//
// # What this is not
//
// THE ELEMENT MODEL DOES NO ELLIPTIC-CURVE ARITHMETIC. Its public key and its
// signatures are deterministic filler: the evidence has the right structure,
// the right nonce binding, the right identity claims and verifies under
// nothing. A verifier that checks the signature will correctly reject it, and
// that is the point of the word TEST in [TestSerial]: this identity must never
// be trusted, enrolled or allow-listed outside a test.
//
// # Finding the binaries
//
// A missing binary FAILS the test and says how to build it; it never skips.
// They are looked up in $AGSIM_DIR if set, otherwise in build/host of the
// checkout this package was compiled from -- which is the right answer for a
// module that depends on this one through a `replace` to a sibling checkout.
// A module that resolves it from the module cache has no build directory and
// must set AGSIM_DIR. Build with `make agsim agsim-atecc608a`.
package agsim

import (
	"crypto/sha256"
	"encoding/hex"
	"io"
	"os"
	"path/filepath"
	"testing"
	"time"

	ag "github.com/jeremyhahn/arduino-generic/sdk/go"
	"github.com/jeremyhahn/arduino-generic/sdk/go/internal/simproc"
)

// DirEnv is the environment variable that points at the directory holding the
// built simulators.
const DirEnv = simproc.DirEnv

// TestSerial is the DeviceID of the element-tier test identity: 16 bytes,
// "agsim-test-id-01". It is the UEID claim in the alias certificate and the
// DeviceID field of the evidence frame.
var TestSerial = [ag.SerialLen]byte{
	'a', 'g', 's', 'i', 'm', '-', 't', 'e', 's', 't', '-', 'i', 'd', '-', '0', '1',
}

// TestImage is the stand-in for the application image ag-boot measured.
const TestImage = "agsim-test-firmware-image-v1"

// TestFWHash is the measurement of [TestImage] -- its SHA-256 -- and the FWHash
// the element-tier test identity reports in the evidence frame and in the
// alias certificate's measurements claim.
var TestFWHash = sha256.Sum256([]byte(TestImage))

// bootTimeout bounds each command. The simulator answers in microseconds; this
// is only the time a wedged one costs before the test fails.
const bootTimeout = 5 * time.Second

// Start runs the software-tier simulator and returns a Device speaking to it.
// The process is stopped when the test ends.
func Start(tb testing.TB, opts ...ag.Option) *ag.Device {
	tb.Helper()
	return device(simproc.Start(tb, simproc.Software), opts)
}

// StartElement runs the element-tier simulator provisioned with the test
// identity: the platform block carrying [TestSerial], a provisioned element
// (DeviceID key in slot 0 and a UDS), and a handoff carrying [TestFWHash].
func StartElement(tb testing.TB, opts ...ag.Option) *ag.Device {
	tb.Helper()

	block := ag.BuildPlatformBlock(TestSerial, [ag.PubkeyHashLen]byte{}, [ag.OperatorPubkeyLen]byte{})
	eep := filepath.Join(tb.TempDir(), "platform.bin")
	if err := os.WriteFile(eep, block[:], 0o600); err != nil {
		tb.Fatal(err)
	}
	return startElement(tb, opts,
		"--eeprom-bin", eep,
		"--provision-element",
		"--image-hash", hex.EncodeToString(TestFWHash[:]))
}

// StartBlankElement runs the element-tier simulator with nothing provisioned:
// an element on the bus with no key, an erased EEPROM and no handoff. It is
// what a part straight off the reel does, and attestation must refuse.
func StartBlankElement(tb testing.TB, opts ...ag.Option) *ag.Device {
	tb.Helper()
	return startElement(tb, opts)
}

// StartElementWith runs the element-tier simulator with exactly the given
// command-line arguments, for a test that supplies its own identity -- an
// `agprov --bake-eeprom` image, say. See tools/agsim/tier_atecc608a.c for the
// options.
func StartElementWith(tb testing.TB, args []string, opts ...ag.Option) *ag.Device {
	tb.Helper()
	return startElement(tb, opts, args...)
}

func startElement(tb testing.TB, opts []ag.Option, args ...string) *ag.Device {
	tb.Helper()
	return device(simproc.Start(tb, simproc.Element, args...), opts)
}

func device(conn io.ReadWriter, opts []ag.Option) *ag.Device {
	// WithTimeout first so a caller's own option, if any, wins.
	return ag.New(conn, append([]ag.Option{ag.WithTimeout(bootTimeout)}, opts...)...)
}
