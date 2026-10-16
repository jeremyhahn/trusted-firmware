// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfwsim_test

import (
	"context"
	"crypto/ecdsa"
	"encoding/hex"
	"errors"
	"io"
	"math/big"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/tfwsim"
)

// These tests drive ATTEST_CSR against the real firmware at the transport level:
// the host asks for b and is handed 64 bytes. What the device signed is a
// CertificationRequestInfo, which is certificate-profile work and go-xkms's; the
// library holds none of it, and nothing here but crypto/ecdsa judges the result.

// csrDigestForTestIdentity is SHA-256 of the CertificationRequestInfo the
// element-tier test identity signs for b = 00 01 .. 1f. It is a literal, not
// computed here: the identity and b are fixed, so the digest is too, and a
// firmware change that moves it is a wire change to be read, not a value to
// regenerate. The template that produces the bytes is pinned separately by
// TFWV_CSR_CRI and TFWV_CSR_CRI_DIGEST in testdata/vectors.h, whose identity is
// the model's rather than the simulator's; this is the same template filled with
// the simulator's DeviceID, UEID and measurements.
const csrDigestForTestIdentity = "ae9c167beb85d288c4d5c792e059cce6eef82327d1c2b3dc9f4fc0a28262bc4b"

// meddler sits between the host and the simulator and lets a test rewrite one
// reply in flight. Frames are decoded and re-encoded, so the CRC stays valid.
type meddler struct {
	conn  io.ReadWriter
	reply func(f tfw.Frame) []byte // new payload for a response, or nil to leave it
	rx    []byte
}

// frameOverhead is SOF, LEN and the two CRC bytes around a frame's body, per
// docs/wire-protocol.
const frameOverhead = 4

func (m *meddler) Write(p []byte) (int, error) { return m.conn.Write(p) }

func (m *meddler) Read(p []byte) (int, error) {
	if len(m.rx) == 0 {
		var head [2]byte
		if _, err := io.ReadFull(m.conn, head[:]); err != nil {
			return 0, err
		}
		frame := make([]byte, int(head[1])+frameOverhead)
		copy(frame, head[:])
		if _, err := io.ReadFull(m.conn, frame[2:]); err != nil {
			return 0, err
		}
		if f, _, err := tfw.Decode(frame); err == nil && m.reply != nil && f.IsResponse() {
			if payload := m.reply(f); payload != nil {
				if frame, err = tfw.EncodeResponse(f.Seq, f.Cmd(), f.Status, payload); err != nil {
					return 0, err
				}
			}
		}
		m.rx = frame
	}
	n := copy(p, m.rx)
	m.rx = m.rx[n:]
	return n, nil
}

func via(m *meddler) func(io.ReadWriter) io.ReadWriter {
	return func(c io.ReadWriter) io.ReadWriter { m.conn = c; return m }
}

func csrNonce(seed byte) [tfw.AliasNonceLen]byte {
	var b [tfw.AliasNonceLen]byte
	copy(b[:], aliasNonce(seed))
	return b
}

func verifies(t *testing.T, sig [tfw.CSRSigLen]byte, digestHex string) bool {
	t.Helper()
	digest, err := hex.DecodeString(digestHex)
	require.NoError(t, err)
	r := new(big.Int).SetBytes(sig[:tfw.CSRSigLen/2])
	s := new(big.Int).SetBytes(sig[tfw.CSRSigLen/2:])
	return ecdsa.Verify(tfwsim.TestDeviceIDPublicKey(), digest, r, s)
}

func TestElementAttestCSRReturnsASignatureThatVerifiesOverTheKnownDigest(t *testing.T) {
	var b [tfw.AliasNonceLen]byte
	for i := range b {
		b[i] = byte(i)
	}

	sig, err := tfwsim.StartElement(t).AttestCSR(context.Background(), b)
	require.NoError(t, err)

	assert.NotEqual(t, [tfw.CSRSigLen]byte{}, sig)
	assert.True(t, verifies(t, sig, csrDigestForTestIdentity),
		"the signature must verify under the DeviceID key over the pinned digest")
}

func TestElementAttestCSRSignatureIsOverTheNonce(t *testing.T) {
	d := tfwsim.StartElement(t)

	a, err := d.AttestCSR(context.Background(), csrNonce(0x10))
	require.NoError(t, err)
	b, err := d.AttestCSR(context.Background(), csrNonce(0x20))
	require.NoError(t, err)

	assert.NotEqual(t, a, b)
	assert.False(t, verifies(t, a, csrDigestForTestIdentity), "a signature for another b is not over the pinned digest")
}

// The software tier builds ATTEST_CSR in beside ATTEST_CERT and refuses it the
// same way -- it has no asymmetric key to sign with -- so CAPS says yes and the
// command itself says unsupported.
func TestSoftwareTierRefusesAttestCSRAsUnsupported(t *testing.T) {
	sig, err := tfwsim.Start(t).AttestCSR(context.Background(), csrNonce(0x50))
	assert.Equal(t, [tfw.CSRSigLen]byte{}, sig)
	assert.True(t, errors.Is(err, tfw.StatusErrUnsupported), "got %v", err)
}

// A device whose CAPS does not report bit 15 is refused by the host without
// being asked: the same element, with the bit cleared on the way back.
func TestElementWithoutTheCapsBitIsRefusedAsUnsupported(t *testing.T) {
	const featuresOff, bit15 = 10, 0x80 // byte 1 of the little-endian bitmap
	m := &meddler{reply: func(f tfw.Frame) []byte {
		if f.Cmd() != tfw.CmdCaps {
			return nil
		}
		p := append([]byte(nil), f.Payload...)
		p[featuresOff+1] &^= bit15
		return p
	}}
	d := tfwsim.StartElementVia(t, via(m))

	c, err := d.Caps(context.Background())
	require.NoError(t, err)
	require.False(t, c.Has(tfw.FeatureAttestCSR), "the rewrite did not clear the bit")

	sig, err := d.AttestCSR(context.Background(), csrNonce(0x50))
	assert.Equal(t, [tfw.CSRSigLen]byte{}, sig)
	assert.True(t, errors.Is(err, tfw.StatusErrUnsupported), "got %v", err)
}

func TestElementReportsAttestCSRInCaps(t *testing.T) {
	c, err := tfwsim.StartElement(t).Caps(context.Background())
	require.NoError(t, err)
	assert.True(t, c.Has(tfw.FeatureAttestCSR))
}
