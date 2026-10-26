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
// [StartProvisioningWith] runs the element tier as a provisioning jig image
// (tfwsim-prov): the same binary plus the PROV_* commands, its element blank
// unless told otherwise, so a test drives the real provisioning sequence over
// the wire and then attests with the key the element generated.
//
// # Finding the binaries
//
// A missing binary FAILS the test and says how to build it; it never skips.
// They are looked up in $TFWSIM_DIR if set, otherwise in build/host of the
// checkout this package was compiled from -- which is the right answer for a
// module that depends on this one through a `replace` to a sibling checkout.
// A module that resolves it from the module cache has no build directory and
// must set TFWSIM_DIR. Build with `make tfwsim tfwsim-atecc608a tfwsim-prov`.
package tfwsim

import (
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/sha256"
	"encoding/hex"
	"io"
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
	pub, err := ecdsa.ParseUncompressedPublicKey(elliptic.P256(), append([]byte{0x04}, raw...))
	if err != nil {
		panic("tfwsim: testDeviceIDPublic is not a P-256 point: " + err.Error())
	}
	return pub
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
	return startTestIdentity(tb, simproc.Element, [tfw.OperatorPubkeyLen]byte{}, nil, opts)
}

// StartElementApp runs the element-tier simulator with test/app/example
// linked, provisioned with the test identity as [StartElement] is. The
// example supplies evidence, so the alias certificate's measurements claim
// carries the example's 32 bytes rather than [TestFWHash].
func StartElementApp(tb testing.TB, opts ...tfw.Option) *tfw.Device {
	tb.Helper()
	return startTestIdentity(tb, simproc.ElementApp, [tfw.OperatorPubkeyLen]byte{}, nil, opts)
}

// StartElementVia is [StartElement] with the connection to the simulator passed
// through wrap before the Device sees it. It is how a test stands between a host
// and a real device: rewriting a frame in flight, to show the host refuses a
// reply the device did not give. wrap is handed the simulator's connection and
// returns the one the Device will use.
func StartElementVia(tb testing.TB, wrap func(io.ReadWriter) io.ReadWriter, opts ...tfw.Option) *tfw.Device {
	tb.Helper()
	return startTestIdentity(tb, simproc.Element, [tfw.OperatorPubkeyLen]byte{}, wrap, opts)
}

// StartElementWithOperator is [StartElement] with operator baked into the
// platform block as the trust anchor of the AUTH_CHALLENGE/AUTH_RESPONSE
// session -- the key tfwprov provisions on a real device. The test holds the
// matching private key and passes it, as a crypto.Signer, to
// [tfw.Device.AuthResponse]. operator must be a P-256 public key.
func StartElementWithOperator(tb testing.TB, operator *ecdsa.PublicKey, opts ...tfw.Option) *tfw.Device {
	tb.Helper()

	// Bytes is the uncompressed SEC 1 point, 0x04 || X || Y. It can fail only
	// for a key built through the deprecated X and Y fields, which the curve
	// check leaves as the one refusal.
	var sec1 []byte
	if operator != nil && operator.Curve == elliptic.P256() {
		sec1, _ = operator.Bytes()
	}
	if len(sec1) != 1+tfw.OperatorPubkeyLen {
		tb.Fatal("tfwsim: the operator key must be a P-256 public key")
	}
	var point [tfw.OperatorPubkeyLen]byte
	copy(point[:], sec1[1:])
	return startTestIdentity(tb, simproc.Element, point, nil, opts)
}

func startTestIdentity(tb testing.TB, bin simproc.Binary, operator [tfw.OperatorPubkeyLen]byte, wrap func(io.ReadWriter) io.ReadWriter, opts []tfw.Option) *tfw.Device {
	tb.Helper()

	block := tfw.BuildPlatformBlock(TestSerial, [tfw.PubkeyHashLen]byte{}, operator)
	eep := filepath.Join(tb.TempDir(), "platform.bin")
	if err := os.WriteFile(eep, block[:], 0o600); err != nil {
		tb.Fatal(err)
	}
	return startElement(tb, bin, wrap, opts,
		"--eeprom-bin", eep,
		"--provision-element",
		"--image-hash", hex.EncodeToString(TestFWHash[:]))
}

// StartBlankElement runs the element-tier simulator with nothing provisioned:
// an element on the bus with no key, an erased EEPROM and no handoff. It is
// what a part straight off the reel does, and attestation must refuse.
func StartBlankElement(tb testing.TB, opts ...tfw.Option) *tfw.Device {
	tb.Helper()
	return startElement(tb, simproc.Element, nil, opts)
}

// StartElementWith runs the element-tier simulator with exactly the given
// command-line arguments, for a test that supplies its own identity -- an
// `tfwprov --bake-eeprom` image, say. See tools/tfwsim/tier_atecc608a.c for the
// options.
func StartElementWith(tb testing.TB, args []string, opts ...tfw.Option) *tfw.Device {
	tb.Helper()
	return startElement(tb, simproc.Element, nil, opts, args...)
}

func startElement(tb testing.TB, bin simproc.Binary, wrap func(io.ReadWriter) io.ReadWriter, opts []tfw.Option, args ...string) *tfw.Device {
	tb.Helper()
	return startBinary(tb, bin, wrap, opts, args...)
}

// StartProvisioningWith runs the element tier as a provisioning jig image,
// tfwsim-prov, with exactly the given command-line arguments: the options are
// tfwsim-atecc608a's (tools/tfwsim/tier_atecc608a.c). Without
// --provision-element its element is a blank part, so `tfwprov --commit`
// provisions it over the wire, and ATTEST afterwards signs with the key the
// element generated. A device that answers PROV_* is never one to trust.
func StartProvisioningWith(tb testing.TB, args []string, opts ...tfw.Option) *tfw.Device {
	tb.Helper()
	return startBinary(tb, simproc.Prov, nil, opts, args...)
}

func startBinary(tb testing.TB, b simproc.Binary, wrap func(io.ReadWriter) io.ReadWriter, opts []tfw.Option, args ...string) *tfw.Device {
	tb.Helper()
	var conn io.ReadWriter = simproc.Start(tb, b, args...)
	if wrap != nil {
		conn = wrap(conn)
	}
	return device(conn, opts)
}

func device(conn io.ReadWriter, opts []tfw.Option) *tfw.Device {
	// WithTimeout first so a caller's own option, if any, wins.
	return tfw.New(conn, append([]tfw.Option{tfw.WithTimeout(bootTimeout)}, opts...)...)
}
