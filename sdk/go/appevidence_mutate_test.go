// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/asn1"
	"errors"
	"math/big"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/vectors"
)

// Mutation tests for appevidence.go: each test kills a fault the other tests
// let through. Faults applied (killed/total in the commit): invert or drop
// the extension OID match, drop the UccsEvidence error check, allow one
// trailing byte, return no error without the extension, accept more than one
// measurement, accept a short pair, accept a content type below 258, drop the
// one-file check, drop the "app" name check, drop the two-key file check,
// accept a hash entry of three, drop the sha-256 check, accept a long hash,
// skip the copy, read the wrong claim/CoSWID/evidence/hash key, make
// EvidenceError match every error or none, change the OID's last arc.

// oidUCCSEvidenceLiteral is tcg-dice-UCCS-evidence spelt out, so a test does
// not inherit a change to the package's own variable.
var oidUCCSEvidenceLiteral = asn1.ObjectIdentifier{2, 23, 133, 5, 4, 6}

// certWithExtensions is a self-signed certificate carrying exts in order.
func certWithExtensions(t *testing.T, exts ...pkix.Extension) []byte {
	t.Helper()
	key, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	require.NoError(t, err)
	tmpl := &x509.Certificate{
		SerialNumber:    big.NewInt(0x41),
		Subject:         pkix.Name{CommonName: "tfw Alias"},
		NotBefore:       time.Date(2020, 1, 1, 0, 0, 0, 0, time.UTC),
		NotAfter:        time.Date(9999, 12, 31, 23, 59, 59, 0, time.UTC),
		ExtraExtensions: exts,
	}
	der, err := x509.CreateCertificate(rand.Reader, tmpl, tmpl, &key.PublicKey, key)
	require.NoError(t, err)
	return der
}

func uccsEvidence(t *testing.T, uccs []byte) []byte {
	t.Helper()
	val, err := asn1.Marshal(struct{ UCCS []byte }{uccs})
	require.NoError(t, err)
	return val
}

// AliasUCCS reads the extension named 2.23.133.5.4.6 and passes over any
// other, even one shaped like a UccsEvidence that comes first.
func TestMutateAliasUCCSSkipsOtherExtensions(t *testing.T) {
	want := []byte(vectors.AppEvWithUccs)
	der := certWithExtensions(t,
		pkix.Extension{Id: asn1.ObjectIdentifier{2, 23, 133, 5, 4, 5}, Value: uccsEvidence(t, []byte("decoy"))},
		pkix.Extension{Id: oidUCCSEvidenceLiteral, Critical: true, Value: uccsEvidence(t, want)},
	)
	got, err := AliasUCCS(der)
	require.NoError(t, err)
	assert.Equal(t, want, got)
}

// A certificate whose only extension is not tcg-dice-UCCS-evidence has none.
func TestMutateAliasUCCSRefusesAnotherOID(t *testing.T) {
	der := certWithExtensions(t,
		pkix.Extension{Id: asn1.ObjectIdentifier{2, 23, 133, 5, 4, 7}, Value: uccsEvidence(t, []byte(vectors.AppEvWithUccs))},
	)
	_, err := AliasUCCS(der)
	require.ErrorIs(t, err, ErrBadFormat)
}

// A single trailing byte after the UccsEvidence is refused, not only two.
func TestMutateAliasUCCSRefusesOneTrailingByte(t *testing.T) {
	val := append(uccsEvidence(t, []byte(vectors.AppEvWithUccs)), 0x00)
	_, err := AliasUCCS(certWithExtensions(t, pkix.Extension{Id: oidUCCSEvidenceLiteral, Value: val}))
	var ee *EvidenceError
	require.ErrorAs(t, err, &ee)
	assert.Equal(t, "trailing bytes after the UccsEvidence", ee.Reason)
}

func mutateEvidence() []byte {
	ev := make([]byte, ApplicationEvidenceLen)
	for i := range ev {
		ev[i] = byte(0x30 + i)
	}
	return ev
}

// The content type is exactly 258: neither side of it is CoSWID.
func TestMutateApplicationEvidenceContentTypeIsExactly258(t *testing.T) {
	for _, ct := range []uint64{257, 259} {
		uccs := build(t, mutateEvidence(), func(layer string, p *parts) {
			if layer == "content" {
				p.ct = ct
			}
		})
		_, err := ApplicationEvidence(uccs)
		require.ErrorIs(t, err, ErrBadFormat, "content type %d was accepted", ct)
	}
}

// The file hash is exactly [hash-alg-id, hash-value]: a third entry is refused.
func TestMutateApplicationEvidenceRefusesAThreeEntryHash(t *testing.T) {
	ev := mutateEvidence()
	uccs := build(t, ev, func(layer string, p *parts) {
		if layer == "file" {
			p.file = cmap{{7, []any{uint64(1), ev, uint64(0)}}, {24, "app"}}
		}
	})
	_, err := ApplicationEvidence(uccs)
	var ee *EvidenceError
	require.ErrorAs(t, err, &ee)
	assert.Equal(t, "file hash is not a [hash-alg-id, hash-value] entry", ee.Reason)
}

// An EvidenceError is ErrBadFormat and nothing else.
func TestMutateEvidenceErrorMatchesOnlyErrBadFormat(t *testing.T) {
	_, err := ApplicationEvidence([]byte{0x01})
	require.ErrorIs(t, err, ErrBadFormat)
	for _, other := range []error{ErrShortFrame, ErrBadCRC, ErrPayloadTooLarge, ErrTimeout, StatusErrUnsupported} {
		assert.False(t, errors.Is(err, other), "EvidenceError matched %v", other)
	}
}
