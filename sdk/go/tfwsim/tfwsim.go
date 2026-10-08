// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

// Package tfwsim starts the firmware, compiled for the host, as a peer for
// tests.
//
// tfwsim is the REAL firmware -- the same protocol, dispatch and capability
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
// # What the evidence is, and is not
//
// THE ELEMENT MODEL DOES REAL P-256. The DeviceID key behind the test identity
// is a real private key with a real public point ([TestDeviceIDPublicKey]);
// every ATTEST frame and ATTEST_CERT certificate is signed with it, and
// verifies under it with crypto/ecdsa and crypto/x509. The alias certificate's
// subject key is that same point, so a verifier that checks the signature
// reaches a true conclusion about the bytes it was handed.
//
// WHAT IT PROVES NOTHING ABOUT IS THE DEVICE. The private key is the SHA-256 of
// a published string (see docs/dice-attestation/), so anyone can compute it. The
// evidence verifies, and it must still never be trusted, enrolled or
// allow-listed outside a test: that is the point of the word TEST in
// [TestSerial] and [TestDeviceIDPublicKey]. A verifier is expected to accept
// this key only in a test that supplied it.
//
// The element tier also has the operator-signature auth session switched on
// (AUTH_CHALLENGE and AUTH_RESPONSE), which the atmega328p profile leaves off
// for want of flash. [StartElementWithOperator] bakes the operator's public key
// into the platform block, so a test can authenticate with a key it generated.
//
// # Finding the binaries
//
// A missing binary FAILS the test and says how to build it; it never skips.
// They are looked up in $TFWSIM_DIR if set, otherwise in build/host of the
// checkout this package was compiled from -- which is the right answer for a
// module that depends on this one through a `replace` to a sibling checkout.
// A module that resolves it from the module cache has no build directory and
// must set TFWSIM_DIR. Build with `make tfwsim tfwsim-atecc608a`.
package tfwsim

import (
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/sha256"
	"encoding/hex"
	"io"
	"math/big"
	"os"
	"path/filepath"
	"testing"
	"time"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/simproc"
)

// DirEnv is the environment variable that points at the directory holding the
// built simulators.
const DirEnv = simproc.DirEnv

// TestSerial is the DeviceID of the element-tier test identity: 16 bytes,
// "tfwsim-testid-01". It is the UEID claim in the alias certificate and the
// DeviceID field of the evidence frame.
var TestSerial = [tfw.SerialLen]byte{
	't', 'f', 'w', 's', 'i', 'm', '-', 't', 'e', 's', 't', 'i', 'd', '-', '0', '1',
}

// TestImage is the stand-in for the application image tfw-boot measured.
const TestImage = "tfwsim-test-firmware-image-v1"

// TestFWHash is the measurement of [TestImage] -- its SHA-256 -- and the FWHash
// the element-tier test identity reports in the evidence frame and in the
// alias certificate's measurements claim.
var TestFWHash = sha256.Sum256([]byte(TestImage))

// testDeviceIDPublic is the uncompressed X||Y point of the element-tier test
// identity's DeviceID key: the public half of the private key whose SHA-256
// preimage is "tfwsim-atecc608a TEST DeviceID key v1". It is pinned in
// test/unit/test_model_p256.c against an independent derivation, so the model
// cannot drift from it without a C test failing.
const testDeviceIDPublic = "41035035faf782ef1cab6a669f1c3809b89fbf1cd096771d12d87e1cb3c35aca" +
	"eda3e5021ff3ee9e48329bcc44c1061e1d5949f1fbba0742a5b6ac6799efe78a"

// TestDeviceIDPublicKey returns the public key the element-tier test identity
// signs with. It is a PUBLIC key and the private half is deliberately not
// here: this module holds no private keys. The identity is a TEST identity;
// nothing outside a test may trust this key.
func TestDeviceIDPublicKey() *ecdsa.PublicKey {
	raw, err := hex.DecodeString(testDeviceIDPublic)
	if err != nil || len(raw) != 64 {
		panic("tfwsim: testDeviceIDPublic is not 64 hex-encoded bytes")
	}
	return &ecdsa.PublicKey{
		Curve: elliptic.P256(),
		X:     new(big.Int).SetBytes(raw[:32]),
		Y:     new(big.Int).SetBytes(raw[32:]),
	}
}

// bootTimeout bounds each command. The simulator answers in microseconds; this
// is only the time a wedged one costs before the test fails.
const bootTimeout = 5 * time.Second

// Start runs the software-tier simulator and returns a Device speaking to it.
// The process is stopped when the test ends.
func Start(tb testing.TB, opts ...tfw.Option) *tfw.Device {
	tb.Helper()
	return device(simproc.Start(tb, simproc.Software), opts)
}

// StartElement runs the element-tier simulator provisioned with the test
// identity: the platform block carrying [TestSerial], a provisioned element
// (DeviceID key in slot 0 and a UDS), and a handoff carrying [TestFWHash].
func StartElement(tb testing.TB, opts ...tfw.Option) *tfw.Device {
	tb.Helper()
	return startTestIdentity(tb, [tfw.OperatorPubkeyLen]byte{}, opts)
}

// StartElementWithOperator is [StartElement] with operator baked into the
// platform block as the trust anchor of the AUTH_CHALLENGE/AUTH_RESPONSE
// session -- the key tfwprov provisions on a real device. The test holds the
// matching private key and passes it, as a crypto.Signer, to
// [tfw.Device.AuthResponse]. operator must be a P-256 public key.
func StartElementWithOperator(tb testing.TB, operator *ecdsa.PublicKey, opts ...tfw.Option) *tfw.Device {
	tb.Helper()

	if operator == nil || operator.Curve != elliptic.P256() {
		tb.Fatal("tfwsim: the operator key must be a P-256 public key")
	}
	var point [tfw.OperatorPubkeyLen]byte
	operator.X.FillBytes(point[:32])
	operator.Y.FillBytes(point[32:])
	return startTestIdentity(tb, point, opts)
}

func startTestIdentity(tb testing.TB, operator [tfw.OperatorPubkeyLen]byte, opts []tfw.Option) *tfw.Device {
	tb.Helper()

	block := tfw.BuildPlatformBlock(TestSerial, [tfw.PubkeyHashLen]byte{}, operator)
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
func StartBlankElement(tb testing.TB, opts ...tfw.Option) *tfw.Device {
	tb.Helper()
	return startElement(tb, opts)
}

// StartElementWith runs the element-tier simulator with exactly the given
// command-line arguments, for a test that supplies its own identity -- an
// `tfwprov --bake-eeprom` image, say. See tools/tfwsim/tier_atecc608a.c for the
// options.
func StartElementWith(tb testing.TB, args []string, opts ...tfw.Option) *tfw.Device {
	tb.Helper()
	return startElement(tb, opts, args...)
}

func startElement(tb testing.TB, opts []tfw.Option, args ...string) *tfw.Device {
	tb.Helper()
	return device(simproc.Start(tb, simproc.Element, args...), opts)
}

func device(conn io.ReadWriter, opts []tfw.Option) *tfw.Device {
	// WithTimeout first so a caller's own option, if any, wins.
	return tfw.New(conn, append([]tfw.Option{tfw.WithTimeout(bootTimeout)}, opts...)...)
}
