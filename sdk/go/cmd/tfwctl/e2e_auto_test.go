// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build tfwsim

package main

import (
	"bytes"
	"context"
	"encoding/hex"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/simproc"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/tfwsim"
)

// The CRYPTO=auto image through tfwctl: one firmware that probes for the
// element at boot and runs the element tier when it answers, the soft tier
// when it does not. The platform block carries the test serial and no
// operator key; the handoff carries the test FWHash. Simulator-only, so
// `make hw-test` does not run these: whether the element answers is the
// simulator option they vary, and a board's is wired one way.

// heldSig is OTA_AUTH's signature for the scenarios here. The application
// only holds it; tfw-boot verifies it at reset, so any 64 bytes pass.
var heldSig = "--sig=" + strings.Repeat("a5", tfw.OTAAuthSigLen)

func startAuto(t *testing.T, args ...string) *tfw.Device {
	t.Helper()
	block := tfw.BuildPlatformBlock(tfwsim.TestSerial, [tfw.PubkeyHashLen]byte{}, [tfw.OperatorPubkeyLen]byte{})
	eep := filepath.Join(t.TempDir(), "platform.bin")
	require.NoError(t, os.WriteFile(eep, block[:], 0o600))
	args = append([]string{"--eeprom-bin", eep, "--image-hash", hex.EncodeToString(tfwsim.TestFWHash[:])}, args...)
	return tfw.New(simproc.Start(t, simproc.Auto, args...), tfw.WithTimeout(5*time.Second))
}

// With no element the probe finds nothing and the image runs the soft root.
// The soft gate is open (src/core/auth_session_auto.c), so ota-activate is
// not refused for want of a session: it reaches the stager, which has nothing
// staged to take.
func TestE2EAutoWithNoElementRunsTheSoftTier(t *testing.T) {
	d := startAuto(t, "--no-element")
	assert.Equal(t, tfw.CryptoSoftware, cliCaps(t, d).CryptoTier)
	assert.Equal(t, tfw.StatusErrBadState, cliStatus(t, d, "ota-activate", heldSig), "ota-activate on the soft tier")
}

// caps reports the commands compiled into the image, not the ones the running
// tier can serve (CA m507, option B). The auto image compiles the element's
// auth session, so the soft tier lists it, and the dispatcher refuses it there.
func TestE2EAutoWithNoElementReportsTheCompiledBitmapAndRefusesAuth(t *testing.T) {
	d := startAuto(t, "--no-element")
	want := tfw.FeatureGPIO | tfw.FeatureSched | tfw.FeatureAnalog | tfw.FeaturePWM |
		tfw.FeatureTone | tfw.FeaturePulse | tfw.FeatureEEPROM | tfw.FeatureI2C |
		tfw.FeatureSPI | tfw.FeatureOneWire | tfw.FeatureAttest | tfw.FeatureAliasCert |
		tfw.FeatureAuthSession | tfw.FeatureAttestCSR
	assert.Equal(t, want, cliCaps(t, d).Features, "caps features, want the compiled bitmap")

	// auth's first frame is AUTH_CHALLENGE; the soft tier refuses it.
	_, key := writeOperatorKey(t)
	assert.Equal(t, tfw.StatusErrCrypto, cliStatus(t, d, "auth", testSerialHex, key), "auth on the soft tier")
}

// With the element on the bus the image runs the element tier, and its gate
// refuses ota-activate without an operator session. ota-activate sends
// OTA_AUTH first, so the refusal is the session's, not OTA_AUTH's.
func TestE2EAutoWithTheElementRunsTheElementTier(t *testing.T) {
	d := startAuto(t, "--provision-element")
	assert.Equal(t, tfw.CryptoSecureElement, cliCaps(t, d).CryptoTier)
	assert.Equal(t, tfw.StatusErrAuthRequired, cliStatus(t, d, "ota-activate", heldSig),
		"ota-activate without a session")
}

// The rollback floor follows the boot's tier, never the image's probe. The
// handoff says tfw-boot booted on the element, whose counter cannot be read
// with the part gone, so ota-info names no floor rather than the EEPROM copy.
func TestE2EAutoWithNoElementReportsNoFloorForAnElementBoot(t *testing.T) {
	d := startAuto(t, "--no-element")
	assert.Equal(t, tfw.StatusErrCrypto, cliStatus(t, d, "ota-info", "0"),
		"ota-info after an element boot with no element")
}

// softBoot is the handoff a soft tfw-boot (BOOT_TIER=soft) leaves: its CDI
// derived from the UDS in boot-section flash, the tier named soft.
var softBoot = []string{"--no-element", "--boot-tier", "soft"}

// With no element and a soft tfw-boot, the default image attests on the soft
// root (owner ruling 2026-10-17, #130): ES256 under the P-256 DeviceID, never
// the element's key nor an HMAC under the CDI.
func TestE2EAutoWithNoElementAttestsOnTheSoftRoot(t *testing.T) {
	// The soft root's P-256 DeviceID comes from the seed tfw-boot derives from
	// the UDS and hands on (SOFT_ID=p256, #48, #130, #173); the application
	// never holds the UDS. The auto simulator has no UDS, as a part tfw-boot
	// has not yet provisioned has none, so its handoff carries no seed: it
	// reports ES256 and refuses to attest until it is provisioned.
	d := startAuto(t, softBoot...)
	c := cliCaps(t, d)
	assert.Equal(t, tfw.CryptoSoftware, c.CryptoTier)
	assert.Equal(t, tfw.AlgECDSAP256SHA256, c.Algorithms, "algorithms = %s, want ecdsa-p256-sha256 alone", c.Algorithms)

	assert.Equal(t, tfw.StatusErrNotProvisioned,
		cliStatus(t, d, "attest", hex.EncodeToString(attestNonce(0x5a))), "attest on the unprovisioned soft root")
	assert.Equal(t, tfw.StatusErrNotProvisioned,
		cliStatus(t, d, "attest-cert", hex.EncodeToString(aliasNonce(0x77))), "attest-cert on the unprovisioned soft root")
}

// With an element answering, the same image attests on the element: ES256
// under the DeviceID key.
func TestE2EAutoWithTheElementAttestsOnTheElementRoot(t *testing.T) {
	ev := cliAttest(t, startAuto(t, "--provision-element"), attestNonce(0x5a))
	require.Len(t, ev, tfw.EvidenceLen)
	assert.EqualValues(t, algECDSA, ev[offAlg], "algorithm is ECDSA P-256, the element")
	assert.True(t, verifyFrame(tfwsim.TestDeviceIDPublicKey(), ev),
		"the evidence signature does not verify under the DeviceID key")
}

// An element tfw-boot's CDI on a board with no element names a root this
// image cannot use: it refuses to attest rather than sign under another.
func TestE2EAutoWithNoElementRefusesAnElementBootsCDI(t *testing.T) {
	d := startAuto(t, "--no-element")
	assert.Equal(t, tfw.StatusErrNotProvisioned,
		cliStatus(t, d, "attest", hex.EncodeToString(attestNonce(0x5a))), "attest after an element boot")
}

// The soft root leaves the device fully functional: every capability the
// image reports answers. Those that need nothing on a bus succeed; a bus
// transfer with no part on it answers with the device's status, never an
// unknown command.
func TestE2EAutoWithNoElementEveryCapabilityAnswers(t *testing.T) {
	d := startAuto(t, softBoot...)
	ctx := context.Background()
	for _, tc := range []struct {
		verb string
		args []string
		ok   bool
	}{
		{"ping", []string{"hello"}, true},
		{"sys", nil, true},
		{"mode", []string{"13", "out"}, true},
		{"write", []string{"13", "1"}, true},
		{"read", []string{"12"}, true},
		{"timer", []string{"13", "1", "5s"}, true},
		{"timer-cancel", []string{"13"}, true},
		{"analog", []string{"14"}, true},
		{"pwm", []string{"3", "128"}, true},
		{"tone", []string{"11", "1000"}, true},
		{"notone", []string{"11"}, true},
		{"eeprom-write", []string{"0x200", "deadbeef"}, true},
		{"eeprom-read", []string{"0x200", "4"}, true},
		{"i2c-config", []string{"100000"}, true},
		{"spi-config", []string{"0", "1", "msb"}, true},
		{"spi", []string{"10", "0102"}, true},
		// The soft root has no UDS here: a status, not an unknown command.
		{"attest", []string{hex.EncodeToString(attestNonce(0x01))}, false},
		{"pulse-in", []string{"2", "1", "10ms"}, false},
		{"i2c", []string{"0x20", "01", "2"}, false},
		{"1wire-reset", []string{"4"}, false},
	} {
		var out bytes.Buffer
		err := run(ctx, d, tc.verb, tc.args, false, &out)
		if tc.ok {
			assert.NoError(t, err, "%s %v", tc.verb, tc.args)
			continue
		}
		var st tfw.Status
		if err != nil {
			require.True(t, errorsAsStatus(err, &st), "%s %v: %v, want a device status", tc.verb, tc.args, err)
			assert.NotEqual(t, tfw.StatusErrUnknownCmd, st, "%s %v", tc.verb, tc.args)
		}
	}
}
