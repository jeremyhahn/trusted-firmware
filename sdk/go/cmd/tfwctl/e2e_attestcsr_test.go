// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build tfwsim

package main

import (
	"bytes"
	"context"
	"crypto/ecdsa"
	"encoding/hex"
	"encoding/json"
	"io"
	"math/big"
	"strings"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/tfwsim"
)

// attest-csr needs a device with a DeviceID key, so these run against the
// element-tier simulator; the soft tier is the error case. The host asks for b
// and is handed 64 bytes. What the device signed is a
// CertificationRequestInfo, which is certificate-profile work and go-xkms's;
// tfwctl holds none of it, and nothing here but crypto/ecdsa judges the result.
// The device signs a request only for the nonce of the certificate it last
// minted, so each test runs attest-cert for b first, as an enrollment does.

// csrDigestForTestIdentity is SHA-256 of the CertificationRequestInfo the
// element-tier test identity signs for b = 00 01 .. 1f. It is a literal, not
// computed here: the identity and b are fixed, so the digest is too, and a
// firmware change that moves it is a wire change to be read, not a value to
// regenerate. The template that produces the bytes is pinned separately by
// TFWV_CSR_CRI and TFWV_CSR_CRI_DIGEST in testdata/vectors.h, whose identity is
// the model's rather than the simulator's; this is the same template filled with
// the simulator's DeviceID, UEID and measurements.
const csrDigestForTestIdentity = "ae9c167beb85d288c4d5c792e059cce6eef82327d1c2b3dc9f4fc0a28262bc4b"

// cliCSR runs `attest-csr --nonce <b>` with --json and returns the signature.
func cliCSR(t *testing.T, dev *tfw.Device, b []byte) ([]byte, error) {
	t.Helper()
	var out bytes.Buffer
	if err := run(context.Background(), dev, "attest-csr", []string{"--nonce", hex.EncodeToString(b)}, true, &out); err != nil {
		assert.Zero(t, out.Len(), "nothing may be written on failure")
		return nil, err
	}
	var got struct {
		Signature string `json:"signature"`
		Len       int    `json:"len"`
	}
	require.NoError(t, json.Unmarshal(out.Bytes(), &got), "attest-csr --json printed %q", out.String())
	sig, err := hex.DecodeString(got.Signature)
	require.NoError(t, err)
	assert.Equal(t, len(sig), got.Len)
	require.Len(t, sig, tfw.CSRSigLen)
	return sig, nil
}

func csrVerifies(t *testing.T, sig []byte, digestHex string) bool {
	t.Helper()
	digest, err := hex.DecodeString(digestHex)
	require.NoError(t, err)
	r := new(big.Int).SetBytes(sig[:tfw.CSRSigLen/2])
	s := new(big.Int).SetBytes(sig[tfw.CSRSigLen/2:])
	return ecdsa.Verify(tfwsim.TestDeviceIDPublicKey(), digest, r, s)
}

func TestE2EAttestCSRPrintsTheSignatureAsHex(t *testing.T) {
	d := tfwsim.StartElement(t)
	b, err := hex.DecodeString(csrNonceHex)
	require.NoError(t, err)
	cliAttestCert(t, d, b)
	var out bytes.Buffer
	err = run(context.Background(), d, "attest-csr", []string{"--nonce", csrNonceHex}, false, &out)
	require.NoError(t, err)

	line := strings.TrimSuffix(out.String(), "\n")
	assert.Len(t, line, 2*tfw.CSRSigLen, "stdout must be the hex signature and one newline")
	sig, err := hex.DecodeString(line)
	require.NoError(t, err)
	assert.Len(t, sig, tfw.CSRSigLen)
}

func TestE2EAttestCSRSignatureVerifiesOverTheKnownDigest(t *testing.T) {
	b := make([]byte, tfw.AliasNonceLen)
	for i := range b {
		b[i] = byte(i)
	}
	d := tfwsim.StartElement(t)
	cliAttestCert(t, d, b)
	sig, err := cliCSR(t, d, b)
	require.NoError(t, err)
	assert.NotEqual(t, make([]byte, tfw.CSRSigLen), sig)
	assert.True(t, csrVerifies(t, sig, csrDigestForTestIdentity),
		"the signature must verify under the DeviceID key over the pinned digest")
}

func TestE2EAttestCSRSignatureIsOverTheNonce(t *testing.T) {
	d := tfwsim.StartElement(t)
	cliAttestCert(t, d, aliasNonce(0x10))
	a, err := cliCSR(t, d, aliasNonce(0x10))
	require.NoError(t, err)
	cliAttestCert(t, d, aliasNonce(0x20))
	b, err := cliCSR(t, d, aliasNonce(0x20))
	require.NoError(t, err)

	assert.NotEqual(t, a, b)
	assert.False(t, csrVerifies(t, a, csrDigestForTestIdentity), "a signature for another b is not over the pinned digest")
}

// b with no certificate minted for it is refused as the wrong argument, as a
// page request with no certificate behind it is: before any attest-cert, and
// after one for another nonce. Nothing is printed.
func TestE2EAttestCSRRefusesBWithNoCertificateMintedForIt(t *testing.T) {
	d := tfwsim.StartElement(t)
	var st tfw.Status

	_, err := cliCSR(t, d, aliasNonce(0x60))
	require.True(t, errorsAsStatus(err, &st), "got %v", err)
	assert.Equal(t, tfw.StatusErrBadArgc, st)

	cliAttestCert(t, d, aliasNonce(0x61))
	_, err = cliCSR(t, d, aliasNonce(0x60))
	require.True(t, errorsAsStatus(err, &st), "got %v", err)
	assert.Equal(t, tfw.StatusErrBadArgc, st)

	_, err = cliCSR(t, d, aliasNonce(0x61))
	require.NoError(t, err)
}

// The software tier builds ATTEST_CSR in beside ATTEST_CERT and refuses it the
// same way -- it has no asymmetric key to sign with -- so caps says yes and the
// command itself says unsupported.
func TestE2EAttestCSRReportsADeviceThatCannotIssueOne(t *testing.T) {
	_, err := cliCSR(t, tfwsim.Start(t), aliasNonce(0x50))
	var st tfw.Status
	require.True(t, errorsAsStatus(err, &st), "got %v", err)
	assert.Equal(t, tfw.StatusErrUnsupported, st)
}

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

// A device whose caps does not report bit 15 is refused by the host without
// being asked: the same element, with the bit cleared on the way back.
func TestE2EElementWithoutTheCapsBitIsRefusedAsUnsupported(t *testing.T) {
	const featuresOff, bit15 = 10, 0x80 // byte 1 of the little-endian bitmap
	m := &meddler{reply: func(f tfw.Frame) []byte {
		if f.Cmd() != tfw.CmdCaps {
			return nil
		}
		p := append([]byte(nil), f.Payload...)
		p[featuresOff+1] &^= bit15
		return p
	}}
	d := tfwsim.StartElementVia(t, func(c io.ReadWriter) io.ReadWriter { m.conn = c; return m })

	require.False(t, cliCaps(t, d).Has(tfw.FeatureAttestCSR), "the rewrite did not clear the bit")
	_, err := cliCSR(t, d, aliasNonce(0x50))
	var st tfw.Status
	require.True(t, errorsAsStatus(err, &st), "got %v", err)
	assert.Equal(t, tfw.StatusErrUnsupported, st)
}

// caps names the feature, so a person can see before asking whether the
// device will answer.
func TestE2ECapsNamesAttestCSROnADeviceThatOffersIt(t *testing.T) {
	d := tfwsim.StartElement(t)
	assert.True(t, cliCaps(t, d).Has(tfw.FeatureAttestCSR))
	var out bytes.Buffer
	require.NoError(t, run(context.Background(), d, "caps", nil, false, &out))
	assert.Contains(t, out.String(), "attest-csr")
}
