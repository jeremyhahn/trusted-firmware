// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build tfwsim

package main

import (
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
