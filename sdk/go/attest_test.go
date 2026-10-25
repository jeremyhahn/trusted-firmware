// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"bytes"
	"context"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// pagedLink answers a paged command the way the firmware does: the first
// request carries a nonce and returns page 0, and each later request carries a
// page index. It speaks the real codec in both directions, so what is under
// test is the paging rather than a mock of it.
type pagedLink struct {
	doc      []byte // the whole document the device would return
	pageSize int
	header   bool // prepend [page][total lo][total hi], as ATTEST_CERT does
	attest   bool // prepend [page][total], as ATTEST does (src/caps/attest.c)
	rx       bytes.Buffer
	requests [][]byte
}

func (l *pagedLink) Write(p []byte) (int, error) {
	f, _, err := Decode(p)
	if err != nil {
		return 0, err
	}
	l.requests = append(l.requests, append([]byte(nil), f.Payload...))

	page := 0
	if len(f.Payload) == 1 {
		page = int(f.Payload[0])
	}
	start := page * l.pageSize
	if start >= len(l.doc) {
		// Past the end is a refusal, never an empty success -- a device that
		// answered OK with nothing would loop a host forever.
		resp, _ := EncodeResponse(f.Seq, f.Cmd(), StatusErrRange, nil)
		l.rx.Write(resp)
		return len(p), nil
	}
	end := start + l.pageSize
	if end > len(l.doc) {
		end = len(l.doc)
	}
	body := l.doc[start:end]
	if l.header {
		h := []byte{byte(page), byte(len(l.doc)), byte(len(l.doc) >> 8)}
		body = append(h, body...)
	}
	if l.attest {
		body = append([]byte{byte(page), byte(len(l.doc))}, body...)
	}
	resp, err := EncodeResponse(f.Seq, f.Cmd(), StatusOK, body)
	if err != nil {
		return 0, err
	}
	l.rx.Write(resp)
	return len(p), nil
}

func (l *pagedLink) Read(p []byte) (int, error) { return l.rx.Read(p) }

func evidence() []byte {
	d := make([]byte, EvidenceLen)
	for i := range d {
		d[i] = byte(i)
	}
	return d
}

func TestAttestPagesTheWholeEvidenceFrame(t *testing.T) {
	want := evidence()
	link := &pagedLink{doc: want, pageSize: attestPage, attest: true}

	got, err := New(link).Attest(context.Background(), make([]byte, AttestNonceLen))
	require.NoError(t, err)
	assert.Equal(t, want, got)

	// 164 bytes in 48-byte pages is four requests: the nonce, then pages 1..3.
	require.Len(t, link.requests, 4)
	assert.Len(t, link.requests[0], AttestNonceLen, "the first request carries the nonce")
	assert.Equal(t, []byte{1}, link.requests[1])
	assert.Equal(t, []byte{3}, link.requests[3])
}

// The firmware puts [page][total] ahead of every page of evidence. A host that
// kept those bytes would return a frame of the right length with every field
// shifted, so the fake above emits them and this pins the header being stripped.
func TestAttestStripsThePageHeaderFromEveryPage(t *testing.T) {
	want := evidence()
	link := &pagedLink{doc: want, pageSize: attestPage, attest: true}

	got, err := New(link).Attest(context.Background(), make([]byte, AttestNonceLen))
	require.NoError(t, err)
	assert.Equal(t, want[:attestPage], got[:attestPage], "page 0 must not start with its header")
	assert.Equal(t, want[attestPage:2*attestPage], got[attestPage:2*attestPage], "no header spliced in at the page boundary")
}

func TestAttestRefusesAPageWithNoEvidenceBehindItsHeader(t *testing.T) {
	link := &cannedLink{payload: []byte{0, 164}}
	_, err := New(link).Attest(context.Background(), make([]byte, AttestNonceLen))
	require.Error(t, err)
	var short *ShortReplyError
	assert.ErrorAs(t, err, &short)
}

// The two nonce widths are not interchangeable, and a caller that swaps them is
// refused HERE rather than sent to the device. A 16-byte enrolment challenge is
// a truncated one, and the device would have no way to tell.
func TestAttestRefusesTheWrongNonceWidth(t *testing.T) {
	link := &pagedLink{doc: evidence(), pageSize: attestPage, attest: true}
	d := New(link)

	for _, n := range []int{0, 15, 17, AliasNonceLen} {
		_, err := d.Attest(context.Background(), make([]byte, n))
		require.Error(t, err, "a %d-byte nonce was accepted", n)
		var rangeErr *ArgRangeError
		assert.ErrorAs(t, err, &rangeErr)
	}
	assert.Empty(t, link.requests, "a refused nonce must not reach the device")
}

func TestAttestCertPagesTheCertificate(t *testing.T) {
	// 489 bytes is what the firmware reports for a real alias certificate.
	want := make([]byte, 489)
	for i := range want {
		want[i] = byte(i * 3)
	}
	link := &pagedLink{doc: want, pageSize: aliasPage, header: true}

	got, err := New(link).AttestCert(context.Background(), make([]byte, AliasNonceLen))
	require.NoError(t, err)
	assert.Equal(t, want, got)
	// ceil(489/60) = 9 pages.
	assert.Len(t, link.requests, 9)
}

func TestAttestCertRefusesTheWrongNonceWidth(t *testing.T) {
	link := &pagedLink{doc: make([]byte, 100), pageSize: aliasPage, header: true}
	d := New(link)

	for _, n := range []int{0, AttestNonceLen, 31, 33} {
		_, err := d.AttestCert(context.Background(), make([]byte, n))
		require.Error(t, err, "a %d-byte nonce was accepted", n)
		var rangeErr *ArgRangeError
		assert.ErrorAs(t, err, &rangeErr, "a %d-byte nonce", n)
	}
	assert.Empty(t, link.requests)
}

// A total of zero would make the loop below it read nothing forever. The device
// cannot legally send it, which is exactly why a host must not trust that.
func TestAttestCertRefusesAZeroTotal(t *testing.T) {
	link := &cannedLink{payload: []byte{0, 0, 0}}
	_, err := New(link).AttestCert(context.Background(), make([]byte, AliasNonceLen))
	require.Error(t, err)
	var short *ShortReplyError
	assert.ErrorAs(t, err, &short)
}

func TestAttestCertRefusesAReplyWithNoHeader(t *testing.T) {
	link := &cannedLink{payload: []byte{0, 1}}
	_, err := New(link).AttestCert(context.Background(), make([]byte, AliasNonceLen))
	require.Error(t, err)
	var short *ShortReplyError
	assert.ErrorAs(t, err, &short)
}

// A software-tier device knows what a certificate is and has no public half to
// put in one. It says TFW_ERR_UNSUPPORTED, which is a different fact from "this
// firmware has never heard of certificates".
func TestAttestCertSurfacesAnUnsupportedDevice(t *testing.T) {
	link := &cannedLink{status: StatusErrUnsupported}
	_, err := New(link).AttestCert(context.Background(), make([]byte, AliasNonceLen))
	require.Error(t, err)
	assert.ErrorIs(t, err, StatusErrUnsupported)
}
