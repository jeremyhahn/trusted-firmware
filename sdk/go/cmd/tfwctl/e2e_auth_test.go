// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build tfwsim

package main

import (
	"bytes"
	"context"
	"encoding/hex"
	"io"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/tfwsim"
)

// The operator-signature auth session through `tfwctl auth` against the real
// firmware: the operator's key pair is generated HERE and its public half is
// baked into the device's platform block exactly as tfwprov does. The element
// verifies the signature for real (ECDSA P-256), so these scenarios establish
// that a signature tfwctl makes opens the session and that nothing else does.
// The TestFirmware* rows reach it through the peer seam (e2e_peer_test.go); on
// a board, the operator key is the one TFW_HW_OPERATOR_KEY names.
//
// tfwctl takes a fresh challenge for every response, so a response over a
// superseded challenge cannot be sent through it. That refusal is the firmware's
// test/unit/test_auth_session.c
// test_a_signature_over_an_earlier_challenge_is_refused.

// platformMagic is the first four bytes of the platform block, 'T','F','P','R'.
// EEPROM_READ of the reserved region needs the session, so reading it back is
// proof the session is open and not merely that AUTH_RESPONSE returned.
const platformMagic = "54465052"

func TestFirmwareAuthSessionIsAdvertisedByTheElementTier(t *testing.T) {
	p, _ := operatorServing(t)
	c := cliCaps(t, p.serial)
	assert.True(t, c.Has(tfw.FeatureAuthSession), "features %#x lack the auth session", c.Features)
}

func TestFirmwareAnOperatorSignatureOpensTheSession(t *testing.T) {
	p, key := operatorServing(t)
	d, serial := p.serial, p.serialHex(t)

	// Closed before: the reserved region is the operator's.
	assert.Equal(t, tfw.StatusErrAuthRequired, cliStatus(t, d, "eeprom-read", "0", "4"),
		"reserved read before authenticating")

	var out bytes.Buffer
	require.NoError(t, run(context.Background(), d, "auth",
		[]string{serial, key, "--", "eeprom-read", "0", "4"}, false, &out))
	assert.Equal(t, platformMagic+"\n", out.String(), "platform block magic read inside the session")
}

func TestFirmwareASignatureFromAnotherKeyDoesNotOpenTheSession(t *testing.T) {
	p, _ := operatorServing(t)
	_, otherKey := writeOperatorKey(t)
	d := p.serial

	assert.Equal(t, tfw.StatusErrCrypto, cliStatus(t, d, "auth", p.serialHex(t), otherKey),
		"auth with a key the device does not trust")
	assert.Equal(t, tfw.StatusErrAuthRequired, cliStatus(t, d, "eeprom-read", "0", "4"),
		"reserved read after a failed authentication")
}

// The signed message binds the serial, so a signature the operator made for
// another device is refused by this one.
func TestFirmwareASignatureBoundToAnotherDevicesSerialIsRefused(t *testing.T) {
	p, key := operatorServing(t)

	other := hex.EncodeToString(bytes.Repeat([]byte{0x77}, tfw.SerialLen))
	assert.Equal(t, tfw.StatusErrCrypto, cliStatus(t, p.serial, "auth", other, key), "auth bound to another serial")
}

// Every auth takes a fresh challenge, so the same operator opens the session
// twice running; a replayed nonce would have been refused the second time.
func TestFirmwareEachAuthTakesAFreshChallenge(t *testing.T) {
	p, key := operatorServing(t)
	serial := p.serialHex(t)
	for i := 0; i < 2; i++ {
		require.NoError(t, run(context.Background(), p.serial, "auth", []string{serial, key}, false, io.Discard),
			"auth %d", i+1)
	}
	a, err := p.serial.AuthChallenge(context.Background())
	require.NoError(t, err)
	b, err := p.serial.AuthChallenge(context.Background())
	require.NoError(t, err)
	assert.NotEqual(t, a, b, "two AUTH_CHALLENGEs returned the same nonce")
}

func TestE2ETheDefaultElementHasNoOperatorToAuthenticate(t *testing.T) {
	// StartElement bakes an all-zero operator key, which is not a point on the
	// curve: no signature, however good, can be verified against it.
	// Simulator-only: it pins that default of the simulator's, and a board's
	// operator key is whatever tfwprov wrote.
	_, key := writeOperatorKey(t)
	assert.Equal(t, tfw.StatusErrCrypto, cliStatus(t, tfwsim.StartElement(t), "auth", testSerialHex, key),
		"auth on a device with no operator key")
}
