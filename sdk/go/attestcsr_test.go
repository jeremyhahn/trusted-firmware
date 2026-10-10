// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"bytes"
	"context"
	"encoding/binary"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// AttestCSR is transport only: these tests pin what it sends, what it refuses to
// send, and what it hands back. The CertificationRequestInfo the device signs is
// pinned by the C vectors, and checked in go-xkms.

// csrDevice answers the two commands AttestCSR sends.
type csrDevice struct {
	features uint32
	sig      []byte         // the ATTEST_CSR reply payload
	status   map[Cmd]Status // a refusal, per command

	calls   []Cmd
	payload []byte // the ATTEST_CSR request payload as received
	rx      bytes.Buffer
}

func newCSRDevice() *csrDevice {
	sig := make([]byte, CSRSigLen)
	for i := range sig {
		sig[i] = byte(0x80 + i)
	}
	return &csrDevice{features: FeatureAttest | FeatureAliasCert | FeatureAttestCSR, sig: sig}
}

func (l *csrDevice) Write(p []byte) (int, error) {
	f, _, err := Decode(p)
	if err != nil {
		return 0, err
	}
	l.calls = append(l.calls, f.Cmd())

	var payload []byte
	switch f.Cmd() {
	case CmdCaps:
		payload = capsHeader(uint16(AlgECDSAP256SHA256))
		binary.LittleEndian.PutUint32(payload[10:], l.features)
	case CmdAttestCSR:
		l.payload = append([]byte(nil), f.Payload...)
		payload = l.sig
	}
	st := l.status[f.Cmd()]
	if st != StatusOK {
		payload = nil
	}
	resp, err := EncodeResponse(f.Seq, f.Cmd(), st, payload)
	if err != nil {
		return 0, err
	}
	l.rx.Write(resp)
	return len(p), nil
}

func (l *csrDevice) Read(p []byte) (int, error) { return l.rx.Read(p) }

func nonce32() [AliasNonceLen]byte {
	var b [AliasNonceLen]byte
	for i := range b {
		b[i] = byte(0xC0 + i)
	}
	return b
}

func TestAttestCSRSendsTheNonceAndReturnsTheSignature(t *testing.T) {
	link := newCSRDevice()
	b := nonce32()

	sig, err := New(link).AttestCSR(context.Background(), b)
	require.NoError(t, err)

	assert.Equal(t, link.sig, sig[:], "the 64 bytes the device sent come back untouched")
	assert.Equal(t, b[:], link.payload, "the request carries b and nothing else")
	assert.Equal(t, []Cmd{CmdCaps, CmdAttestCSR}, link.calls, "CAPS, then the signature; no certificate is minted")
}

func TestAttestCSRRefusesADeviceWithoutTheCapsBitBeforeSendingAnything(t *testing.T) {
	link := newCSRDevice()
	link.features = FeatureAttest | FeatureAliasCert

	sig, err := New(link).AttestCSR(context.Background(), nonce32())
	assert.Equal(t, [CSRSigLen]byte{}, sig)
	require.ErrorIs(t, err, StatusErrUnsupported)
	assert.Equal(t, []Cmd{CmdCaps}, link.calls, "an unsupported device must not be sent ATTEST_CSR")
}

func TestAttestCSRRefusesASignatureOfTheWrongLength(t *testing.T) {
	for _, n := range []int{0, CSRSigLen - 1} {
		link := newCSRDevice()
		link.sig = make([]byte, n)

		sig, err := New(link).AttestCSR(context.Background(), nonce32())
		assert.Equal(t, [CSRSigLen]byte{}, sig)
		var short *ShortReplyError
		require.ErrorAs(t, err, &short, "a %d-byte reply was accepted", n)
		assert.Equal(t, CmdAttestCSR, short.Cmd)
	}
}

// A refusal at either stage reaches the caller as the status the device gave,
// with nothing returned.
func TestAttestCSRSurfacesADeviceRefusalAtEveryStage(t *testing.T) {
	for _, tc := range []struct {
		name string
		cmd  Cmd
		st   Status
	}{
		{"CAPS", CmdCaps, StatusErrInternal},
		{"ATTEST_CSR", CmdAttestCSR, StatusErrCrypto},
	} {
		t.Run(tc.name, func(t *testing.T) {
			link := newCSRDevice()
			link.status = map[Cmd]Status{tc.cmd: tc.st}

			sig, err := New(link).AttestCSR(context.Background(), nonce32())
			assert.Equal(t, [CSRSigLen]byte{}, sig)
			assert.ErrorIs(t, err, tc.st)
		})
	}
}

func TestAttestCSRConstants(t *testing.T) {
	assert.Equal(t, uint32(1<<15), FeatureAttestCSR, "ATTEST_CSR is CAPS bit 15")
	assert.Equal(t, Cmd(0x64), CmdAttestCSR)
	assert.Equal(t, 64, CSRSigLen)
}
