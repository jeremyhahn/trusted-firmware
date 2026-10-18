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
//
// tfwctl takes a fresh challenge for every response, so a response over a
// superseded challenge cannot be sent through it. That refusal is the firmware's
// test/unit/test_auth_session.c
// test_a_signature_over_an_earlier_challenge_is_refused.

// platformMagic is the first four bytes of the platform block, 'T','F','P','R'.
// EEPROM_READ of the reserved region needs the session, so reading it back is
// proof the session is open and not merely that AUTH_RESPONSE returned.
const platformMagic = "54465052"

func TestE2EAuthSessionIsAdvertisedByTheElementTier(t *testing.T) {
	pub, _ := writeOperatorKey(t)
	c := cliCaps(t, tfwsim.StartElementWithOperator(t, pub))
	assert.True(t, c.Has(tfw.FeatureAuthSession), "features %#x lack the auth session", c.Features)
}

func TestE2EAnOperatorSignatureOpensTheSession(t *testing.T) {
	pub, key := writeOperatorKey(t)
	d := tfwsim.StartElementWithOperator(t, pub)

	// Closed before: the reserved region is the operator's.
	assert.Equal(t, tfw.StatusErrAuthRequired, cliStatus(t, d, "eeprom-read", "0", "4"),
		"reserved read before authenticating")

	var out bytes.Buffer
	require.NoError(t, run(context.Background(), d, "auth",
		[]string{testSerialHex, key, "--", "eeprom-read", "0", "4"}, false, &out))
	assert.Equal(t, platformMagic+"\n", out.String(), "platform block magic read inside the session")
}

func TestE2EASignatureFromAnotherKeyDoesNotOpenTheSession(t *testing.T) {
	pub, _ := writeOperatorKey(t)
	_, otherKey := writeOperatorKey(t)
	d := tfwsim.StartElementWithOperator(t, pub)

	assert.Equal(t, tfw.StatusErrCrypto, cliStatus(t, d, "auth", testSerialHex, otherKey),
		"auth with a key the device does not trust")
	assert.Equal(t, tfw.StatusErrAuthRequired, cliStatus(t, d, "eeprom-read", "0", "4"),
		"reserved read after a failed authentication")
}

// The signed message binds the serial, so a signature the operator made for
// another device is refused by this one.
func TestE2EASignatureBoundToAnotherDevicesSerialIsRefused(t *testing.T) {
	pub, key := writeOperatorKey(t)
	d := tfwsim.StartElementWithOperator(t, pub)

	other := hex.EncodeToString(bytes.Repeat([]byte{0x77}, tfw.SerialLen))
	assert.Equal(t, tfw.StatusErrCrypto, cliStatus(t, d, "auth", other, key), "auth bound to another serial")
}

// Every auth takes a fresh challenge, so the same operator opens the session
// twice running; a replayed nonce would have been refused the second time.
func TestE2EEachAuthTakesAFreshChallenge(t *testing.T) {
	pub, key := writeOperatorKey(t)
	d := tfwsim.StartElementWithOperator(t, pub)
	for i := 0; i < 2; i++ {
		require.NoError(t, run(context.Background(), d, "auth", []string{testSerialHex, key}, false, io.Discard),
			"auth %d", i+1)
	}
}

func TestE2ETheDefaultElementHasNoOperatorToAuthenticate(t *testing.T) {
	// StartElement bakes an all-zero operator key, which is not a point on the
	// curve: no signature, however good, can be verified against it.
	_, key := writeOperatorKey(t)
	assert.Equal(t, tfw.StatusErrCrypto, cliStatus(t, tfwsim.StartElement(t), "auth", testSerialHex, key),
		"auth on a device with no operator key")
}
