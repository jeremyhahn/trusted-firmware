// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
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
