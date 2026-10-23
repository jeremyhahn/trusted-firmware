// Copyright (c) 2026 Jeremy Hahn
// Copyright (c) 2026 Automate The Things, LLC
//
// This file is part of go-xkms.
//
// go-xkms is dual-licensed:
//
// 1. GNU Affero General Public License v3.0 (AGPL-3.0)
//    See LICENSE file or visit https://www.gnu.org/licenses/agpl-3.0.html
//
// 2. Commercial License
//    Contact licensing@automatethethings.com for commercial licensing options.

package avr

import (
	"crypto"
	"testing"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// A nonce longer than the alias nonce is refused, not truncated: the device
// would sign over a prefix of the challenge the caller holds.
func TestPossessionSignerRefusesALongNonce(t *testing.T) {
	key := testPossessionKey(t)
	b := newTestBackend(t, possessionLink(t, key, []byte("cri")))

	signer, err := b.PossessionSigner(possessionAttestation(t, key, make([]byte, 2*tfw.AliasNonceLen)))

	assert.Nil(t, signer)
	assert.ErrorIs(t, err, ErrInvalidAttestation)
}

// A 32-byte digest under another hash is still not SHA-256.
func TestPossessionSignerRefusesAnotherHashOfTheSameWidth(t *testing.T) {
	key := testPossessionKey(t)
	link := possessionLink(t, key, []byte("cri"))
	b := newTestBackend(t, link)
	signer, err := b.PossessionSigner(possessionAttestation(t, key, possessionNonce))
	require.NoError(t, err)

	before := len(link.requests)
	_, err = signer.Sign(nil, make([]byte, 32), crypto.SHA512_256)

	assert.ErrorIs(t, err, ErrSigningUnsupported)
	assert.Len(t, link.requests, before, "a refused digest costs no round trip")
}
