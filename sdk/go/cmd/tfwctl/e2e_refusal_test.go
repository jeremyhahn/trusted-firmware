// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build tfwsim

package main

import (
	"context"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/simproc"
)

// The refusal rows (#7): what an image without a family answers for it. Each
// runs on an image whose CAPS lacks the family and fails, naming the family
// and the peer, on one that has it; hw-test runs it only on such an image
// (firmwareRowNeeds). e2e_refusal_board_test.go has the families every
// simulator has.

// requireLacks fails the row, naming the feature and the peer, when the peer's
// CAPS reports any bit of feature.
func requireLacks(t testing.TB, dev *tfw.Device, feature uint32) {
	t.Helper()
	if cliCaps(t, dev).Has(feature) {
		t.Fatalf("this row needs a peer without CAPS feature %s (%#x), which %s reports",
			featureList(&tfw.Caps{Features: feature}), feature, peerName(t))
	}
}

// refusesAbsentFamily asserts that a peer whose CAPS lacks feature answers
// cmd, a command of that family, TFW_ERR_UNKNOWN_CMD: commands.def compiles
// the family's rows out with its feature, and dispatch refuses a command it
// has no row for before its handler or its length check (src/core/dispatch.c),
// so the command carries no payload.
func refusesAbsentFamily(t testing.TB, dev *tfw.Device, feature uint32, cmd tfw.Cmd) {
	t.Helper()
	requireLacks(t, dev, feature)
	_, err := dev.Do(context.Background(), cmd, nil)
	var st tfw.Status
	require.True(t, errorsAsStatus(err, &st), "command 0x%02x: %v, want a device status", byte(cmd), err)
	assert.Equal(t, tfw.StatusErrUnknownCmd, st, "command 0x%02x of a family CAPS lacks", byte(cmd))
}

// The software tier has no operator session (bit 14 clear:
// src/core/auth_session_none.c), so AUTH_CHALLENGE is not a command it has.
func TestFirmwareRefusesTheAuthSessionItsCapsLacks(t *testing.T) {
	refusesAbsentFamily(t, startFirmware(t), tfw.FeatureAuthSession, tfw.CmdAuthChallenge)
}

// An image without ATTEST_CSR (bit 15 clear) does not know the command; the
// host's refusal of such a device is TestFirmwareAttestCSRReportsADeviceThatCannotIssueOne.
func TestFirmwareRefusesAttestCSRItsCapsLacks(t *testing.T) {
	refusesAbsentFamily(t, startFirmware(t), tfw.FeatureAttestCSR, tfw.CmdAttestCSR)
}

// The soft tier's auth gate is open (src/core/auth_session_none.c), and still
// OTA_ACTIVATE with no OTA_AUTH before it and nothing staged is
// TFW_ERR_AUTH_REQUIRED (src/core/ota_activate.c): the row key's signature is
// asked for on every tier (#137). A short OTA_AUTH first drops any signature
// held from before, as src/core/ota_auth.c does for a payload that is not one,
// and answers TFW_ERR_BAD_ARGC.
func TestFirmwareSoftTierOTAActivateWithoutOTAAuthIsAuthRequired(t *testing.T) {
	p := startPeer(t, simproc.Software)
	requireLacks(t, p.serial, tfw.FeatureAuthSession)
	requireStager(t, p)
	ctx := context.Background()

	var st tfw.Status
	_, err := p.serial.Do(ctx, tfw.CmdOTAAuth, nil)
	require.True(t, errorsAsStatus(err, &st), "a short OTA_AUTH: %v, want a device status", err)
	require.Equal(t, tfw.StatusErrBadArgc, st, "a short OTA_AUTH")

	err = p.serial.OTAActivate(ctx, tfw.OTAActivateAtReset)
	require.True(t, errorsAsStatus(err, &st), "OTA_ACTIVATE with no OTA_AUTH: %v, want a device status", err)
	assert.Equal(t, tfw.StatusErrAuthRequired, st, "OTA_ACTIVATE with no OTA_AUTH on the soft tier")
}
