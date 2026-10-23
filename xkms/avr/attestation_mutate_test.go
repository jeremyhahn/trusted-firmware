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
	"bytes"
	"context"
	"testing"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"

	"github.com/jeremyhahn/go-xkms/pkg/types"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// The attestation keeps its own copy of the challenge. A caller that reuses
// its nonce buffer for the next order must not rewrite the nonce an earlier
// attestation reports it answered.
func TestAttestKeyKeepsItsOwnCopyOfTheNonce(t *testing.T) {
	device := newAliasDevice(t)
	nonce := bytes.Repeat([]byte{0x5A}, tfw.AliasNonceLen)
	want := bytes.Clone(nonce)

	b := newTestBackend(t, secureElementLink().answerAttestCert(device.mint(t, nonce)))
	result, err := b.AttestKey(context.Background(), &types.KeyAttributes{CN: DefaultKeyCN}, nonce)
	require.NoError(t, err)

	for i := range nonce {
		nonce[i] = 0
	}
	assert.Equal(t, want, result.(*Attestation).Nonce)
}
