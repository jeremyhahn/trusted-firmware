// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"bytes"
	"crypto/x509/pkix"
	"encoding/asn1"
	"testing"

	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/vectors"
)

// UccsEvidence ::= SEQUENCE { uccs OCTET STRING } has exactly one element:
// a second one, of any type, is refused, as go-xkms's dice.ParseUccsEvidence
// refuses it, so one certificate never reads two ways.
func TestAliasUCCSRefusesASecondElement(t *testing.T) {
	uccs := []byte(vectors.AppEvWithUccs)
	octets, err := asn1.Marshal(struct{ UCCS, Extra []byte }{uccs, []byte("x")})
	require.NoError(t, err)
	integer, err := asn1.Marshal(struct {
		UCCS []byte
		N    int
	}{uccs, 1})
	require.NoError(t, err)

	for name, val := range map[string][]byte{"octet string": octets, "integer": integer} {
		t.Run(name, func(t *testing.T) {
			_, err := AliasUCCS(aliasCert(t, nil, pkix.Extension{Id: oidUCCSEvidenceLiteral, Value: val}))
			var ee *EvidenceError
			require.ErrorAs(t, err, &ee)
			require.ErrorIs(t, err, ErrBadFormat)
			require.Equal(t, "UccsEvidence carries more than its one uccs", ee.Reason)
		})
	}
}

// A UccsEvidence is a universal, constructed SEQUENCE whose one element is
// an OCTET STRING. An empty SEQUENCE or one holding another type fails the
// element decode; a SET, a primitive SEQUENCE or a context-specific value
// fails the class, tag and IsCompound check. [16] carries the SEQUENCE's
// tag number in the wrong class, so only the class check refuses it, and
// the primitive SEQUENCE is refused only by the IsCompound check.
func TestAliasUCCSRefusesAUccsEvidenceThatIsNotOneOctetString(t *testing.T) {
	for _, tc := range []struct {
		name string
		val  []byte
	}{
		{"empty SEQUENCE", []byte{0x30, 0x00}},
		{"SEQUENCE of an INTEGER", []byte{0x30, 0x03, 0x02, 0x01, 0x01}},
		{"SET", []byte{0x31, 0x03, 0x04, 0x01, 0x00}},
		{"context-specific [0]", []byte{0xA0, 0x03, 0x04, 0x01, 0x00}},
		{"context-specific [16]", []byte{0xB0, 0x03, 0x04, 0x01, 0x00}},
		{"primitive SEQUENCE", []byte{0x10, 0x03, 0x04, 0x01, 0x00}},
	} {
		t.Run(tc.name, func(t *testing.T) {
			got, err := AliasUCCS(aliasCert(t, nil, pkix.Extension{Id: oidUCCSEvidenceLiteral, Value: tc.val}))
			var ee *EvidenceError
			require.ErrorAs(t, err, &ee)
			require.ErrorIs(t, err, ErrBadFormat)
			require.Equal(t, "tcg-dice-UCCS-evidence is not a UccsEvidence", ee.Reason)
			require.Nil(t, got)
		})
	}
}

// The raw decode keeps a one-element UccsEvidence whole: the uccs comes back
// byte for byte.
func TestAliasUCCSKeepsAOneElementUccsEvidenceWhole(t *testing.T) {
	uccs := []byte(vectors.AppEvWithUccs)
	val, err := asn1.Marshal(struct{ UCCS []byte }{uccs})
	require.NoError(t, err)
	got, err := AliasUCCS(aliasCert(t, nil, pkix.Extension{Id: oidUCCSEvidenceLiteral, Value: val}))
	require.NoError(t, err)
	require.True(t, bytes.Equal(uccs, got))
}
