// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"context"
	"errors"
	"io"
	"testing"

	"github.com/stretchr/testify/require"
)

// Each refusal of a paged ATTEST or ATTEST_CERT reply names what was wrong
// with the page. These pin which refusal comes back for which reply, so one
// swapped for another, or a bound moved by one, is a failure.

// pageOf is the header bytes followed by n zero bytes.
func pageOf(n int, hdr ...byte) []byte { return append(hdr, make([]byte, n)...) }

func attestWith(link io.ReadWriter) error {
	_, err := New(link).Attest(context.Background(), make([]byte, AttestNonceLen))
	return err
}

func attestCertWith(link io.ReadWriter) error {
	_, err := New(link).AttestCert(context.Background(), make([]byte, AliasNonceLen))
	return err
}

func attestCertPages(first, second []byte) error {
	return attestCertWith(&scriptedLink{replies: []cannedLink{{payload: first}, {payload: second}}})
}

func requireEvidenceReason(t *testing.T, err error, reason string) {
	t.Helper()
	var ee *EvidenceError
	require.True(t, errors.As(err, &ee), "error %v is not an EvidenceError", err)
	require.Equal(t, reason, ee.Reason)
	require.ErrorIs(t, err, ErrBadFormat)
}

func TestAttestPageRefusalWrongPage(t *testing.T) {
	err := attestWith(&cannedLink{payload: pageOf(48, 1, 164)})
	requireEvidenceReason(t, err, "ATTEST page 0 answered as page 1 of 164 bytes")
}

func TestAttestPageRefusalWrongTotal(t *testing.T) {
	err := attestWith(&cannedLink{payload: pageOf(48, 0, 163)})
	requireEvidenceReason(t, err, "ATTEST page 0 answered as page 0 of 163 bytes")
}

func TestAttestPageRefusalOverLong(t *testing.T) {
	err := attestWith(&cannedLink{payload: pageOf(49, 0, 164)})
	requireEvidenceReason(t, err, "ATTEST page 0 carries 49 bytes, its share is 48")
}

func TestAttestPageRefusalShortPage(t *testing.T) {
	err := attestWith(&cannedLink{payload: pageOf(47, 0, 164)})
	var short *ShortReplyError
	require.True(t, errors.As(err, &short), "error %v is not a ShortReplyError", err)
	require.Equal(t, CmdAttest, short.Cmd)
	require.Equal(t, 49, short.Got)
	require.Equal(t, 50, short.Need)
	var ee *EvidenceError
	require.False(t, errors.As(err, &ee), "a short page was refused as %v", err)
}

func TestAttestCertRefusalTotalPastLastPage(t *testing.T) {
	err := attestCertWith(&cannedLink{payload: []byte{0x00, 0x01, 0x3C}})
	requireEvidenceReason(t, err, "ATTEST_CERT total of 15361 is past the last page")
}

// 15360 is 256 pages of 60, the most a one-byte page number reaches: it passes
// the total check, and the refusal is the next page's, not that one.
func TestAttestCertRefusalLastPageTotalAllowed(t *testing.T) {
	err := attestCertWith(&cannedLink{payload: pageOf(60, 0x00, 0x00, 0x3C)})
	var ee *EvidenceError
	require.True(t, errors.As(err, &ee), "error %v is not an EvidenceError", err)
	require.NotContains(t, ee.Reason, "past the last page")
	requireEvidenceReason(t, err, "ATTEST_CERT page 1 answered as page 0")
}

func TestAttestCertRefusalWrongPage(t *testing.T) {
	err := attestCertPages(pageOf(60, 0, 63, 0), pageOf(3, 2, 63, 0))
	requireEvidenceReason(t, err, "ATTEST_CERT page 1 answered as page 2")
}

func TestAttestCertRefusalTotalChanges(t *testing.T) {
	err := attestCertPages(pageOf(60, 0, 63, 0), pageOf(3, 1, 64, 0))
	requireEvidenceReason(t, err, "ATTEST_CERT page 1 names a total of 64, page 0 named 63")
}

func TestAttestCertRefusalOverLong(t *testing.T) {
	err := attestCertWith(&cannedLink{payload: pageOf(61, 0, 63, 0)})
	requireEvidenceReason(t, err, "ATTEST_CERT page 0 carries 61 bytes, its share is 60")
}
