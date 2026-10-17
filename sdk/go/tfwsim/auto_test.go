// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfwsim_test

import (
	"context"
	"encoding/hex"
	"errors"
	"os"
	"path/filepath"
	"testing"
	"time"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/simproc"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/tfwsim"
)

// startAuto runs tfwsim-auto, the Mega's default CRYPTO=auto tier (issue
// #130), with the test identity's platform block and a handoff carrying
// tfwsim.TestFWHash. The tier is picked by the image's own probe at startup;
// --no-element takes the part off the bus before it runs.
func startAuto(t *testing.T, args ...string) *tfw.Device {
	t.Helper()
	block := tfw.BuildPlatformBlock(tfwsim.TestSerial, [tfw.PubkeyHashLen]byte{}, [tfw.OperatorPubkeyLen]byte{})
	eep := filepath.Join(t.TempDir(), "platform.bin")
	if err := os.WriteFile(eep, block[:], 0o600); err != nil {
		t.Fatal(err)
	}
	args = append([]string{"--eeprom-bin", eep, "--image-hash", hex.EncodeToString(tfwsim.TestFWHash[:])}, args...)
	return tfw.New(simproc.Start(t, simproc.Auto, args...), tfw.WithTimeout(5*time.Second))
}

func expectTier(t *testing.T, d *tfw.Device, want tfw.CryptoTier) {
	t.Helper()
	c, err := d.Caps(context.Background())
	if err != nil {
		t.Fatalf("CAPS: %v", err)
	}
	if c.CryptoTier != want {
		t.Fatalf("CAPS crypto tier %v, want %v", c.CryptoTier, want)
	}
}

// With no element the probe finds nothing and the image runs the soft root.
// The soft gate is open (src/core/auth_session_auto.c), so OTA_ACTIVATE is
// not refused for want of a session: it reaches the stager, which has nothing
// staged to take.
func TestAgainstFirmwareAutoWithNoElementRunsTheSoftTier(t *testing.T) {
	d := startAuto(t, "--no-element")
	expectTier(t, d, tfw.CryptoSoftware)
	if err := d.OTAActivate(context.Background(), 0); !errors.Is(err, tfw.StatusErrBadState) {
		t.Fatalf("OTA_ACTIVATE on the soft tier: %v, want %v", err, tfw.StatusErrBadState)
	}
}

// With the element on the bus the image runs the element tier, and its gate
// refuses OTA_ACTIVATE without an operator session.
func TestAgainstFirmwareAutoWithTheElementRunsTheElementTier(t *testing.T) {
	d := startAuto(t, "--provision-element")
	expectTier(t, d, tfw.CryptoSecureElement)
	if err := d.OTAActivate(context.Background(), 0); !errors.Is(err, tfw.StatusErrAuthRequired) {
		t.Fatalf("OTA_ACTIVATE without a session: %v, want %v", err, tfw.StatusErrAuthRequired)
	}
}

// The rollback floor follows the boot's tier, never the image's probe. The
// handoff says tfw-boot booted on the element, whose counter cannot be read
// with the part gone, so OTA_INFO names no floor rather than the EEPROM copy.
func TestAgainstFirmwareAutoWithNoElementReportsNoFloorForAnElementBoot(t *testing.T) {
	d := startAuto(t, "--no-element")
	if _, err := d.OTAInfo(context.Background(), 0); !errors.Is(err, tfw.StatusErrCrypto) {
		t.Fatalf("OTA_INFO after an element boot with no element: %v, want %v", err, tfw.StatusErrCrypto)
	}
}
