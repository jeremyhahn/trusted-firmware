// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"bytes"
	"context"
	"testing"
)

// mutPageLink answers a paged command with pages[i] for page i -- the request
// carrying the nonce is page 0, a one-byte request names its page -- and
// StatusErrRange past the last page, as src/caps/attest.c does.
type mutPageLink struct {
	pages [][]byte
	rx    bytes.Buffer
}

func (l *mutPageLink) Write(p []byte) (int, error) {
	f, _, err := Decode(p)
	if err != nil {
		return 0, err
	}
	idx := 0
	if len(f.Payload) == 1 {
		idx = int(f.Payload[0])
	}
	st, payload := StatusOK, []byte(nil)
	if idx < len(l.pages) {
		payload = l.pages[idx]
	} else {
		st = StatusErrRange
	}
	resp, err := EncodeResponse(f.Seq, f.Cmd(), st, payload)
	if err != nil {
		return 0, err
	}
	l.rx.Write(resp)
	return len(p), nil
}

func (l *mutPageLink) Read(p []byte) (int, error) { return l.rx.Read(p) }

// pagedDoc splits doc into the replies the device sends: each page is hdr
// followed by up to size bytes, with hdr's first byte set to the page number.
func pagedDoc(doc []byte, size int, hdr ...byte) [][]byte {
	var pages [][]byte
	for p := 0; p*size < len(doc); p++ {
		h := bytes.Clone(hdr)
		h[0] = byte(p)
		pages = append(pages, append(h, doc[p*size:min((p+1)*size, len(doc))]...))
	}
	return pages
}

// mutatePages mutates each page of a paged reply in turn, the others left
// valid, and hands fn the replies and the mutation. A mutated page longer
// than MaxPayload cannot be framed, so no device can send it; it is passed
// over. The pages between the second and the last have the second's shape
// -- a header and a full page of the document -- so the first, the second
// and the last are the ones mutated: every page shape, at a third of the
// round trips.
func mutatePages(pages [][]byte, stream uint64, fn func(p int, replies [][]byte, m mutation)) {
	for _, p := range []int{0, 1, len(pages) - 1} {
		eachMutation(pages[p], stream+uint64(p), func(m mutation) {
			if len(m.b) > MaxPayload {
				return
			}
			replies := make([][]byte, len(pages))
			copy(replies, pages)
			replies[p] = m.b
			fn(p, replies, m)
		})
	}
}

// TestAttestMutations: each ATTEST page mutated in turn answers an error or
// 164 bytes, never a panic. A truncated page leaves the document short, so
// the next page asked for is past the end and refused. The [page][total]
// header is stripped (attestPageBody), so a change confined to it answers
// the original evidence; a change in the evidence answers the changed bytes.
func TestAttestMutations(t *testing.T) {
	// The evidence frame as attest_test.go's evidence() writes it, paged as
	// the device pages it: [page][164] then 48 bytes, 20 on the last page.
	doc := make([]byte, 164)
	for i := range doc {
		doc[i] = byte(i)
	}
	pages := pagedDoc(doc, 48, 0, 0xa4)
	ctx := context.Background()
	nonce := make([]byte, AttestNonceLen)

	got, err := New(&mutPageLink{pages: pages}).Attest(ctx, nonce)
	if err != nil || !bytes.Equal(got, doc) {
		t.Fatalf("the valid pages answer % x, %v", got, err)
	}

	mutatePages(pages, 100, func(p int, replies [][]byte, m mutation) {
		var ev []byte
		var err error
		noPanic(t, m, func() { ev, err = New(&mutPageLink{pages: replies}).Attest(ctx, nonce) })
		if err != nil {
			return
		}
		switch m.kind {
		case "truncate", "append":
			// A page shorter or longer than its share of the 164 bytes
			// would shift every byte after it.
			t.Fatalf("page %d %v: answered % x", p, m, ev)
		}
		if !bytes.Equal(m.b[:2], pages[p][:2]) {
			// Another page's number, or a total other than 164.
			t.Fatalf("page %d %v: answered % x", p, m, ev)
		}
		want := bytes.Clone(doc)
		copy(want[p*48:], m.b[2:])
		if !bytes.Equal(ev, want) {
			t.Fatalf("page %d %v: answered % x, want % x", p, m, ev, want)
		}
	})
}

// TestAttestCertMutations: each ATTEST_CERT page mutated in turn answers an
// error or the certificate, never a panic. A truncated page, or a total
// raised, leaves the document short and the next page is refused; a change
// in the certificate's bytes answers the changed bytes.
func TestAttestCertMutations(t *testing.T) {
	// TestAttestCertPagesTheCertificate's document cut to 280 bytes -- a
	// total wider than a byte, four full pages and a part one -- paged as the
	// device pages it: [page][total lo][total hi] then 60 bytes.
	doc := make([]byte, 280)
	for i := range doc {
		doc[i] = byte(i * 3)
	}
	pages := pagedDoc(doc, 60, 0, 0x18, 0x01)
	ctx := context.Background()
	nonce := make([]byte, AliasNonceLen)

	got, err := New(&mutPageLink{pages: pages}).AttestCert(ctx, nonce)
	if err != nil || !bytes.Equal(got, doc) {
		t.Fatalf("the valid pages answer %d bytes, %v", len(got), err)
	}

	mutatePages(pages, 200, func(p int, replies [][]byte, m mutation) {
		var cert []byte
		var err error
		noPanic(t, m, func() { cert, err = New(&mutPageLink{pages: replies}).AttestCert(ctx, nonce) })
		if err != nil {
			return
		}
		switch {
		case m.kind == "truncate", m.kind == "append":
			// A page shorter or longer than its share of the total would
			// shift every byte after it.
			t.Fatalf("page %d %v: answered %d bytes", p, m, len(cert))
		case len(m.b) < 3 || !bytes.Equal(m.b[:3], pages[p][:3]):
			// Another page's number is another window's bytes; a total
			// other than page 0's, or page 0's lowered below the bytes the
			// pages carry, another certificate's.
			t.Fatalf("page %d %v: answered %d bytes", p, m, len(cert))
		}
		want := bytes.Clone(doc)
		copy(want[p*60:], m.b[3:])
		if !bytes.Equal(cert, want) {
			t.Fatalf("page %d %v: answered %d bytes, not the changed certificate", p, m, len(cert))
		}
	})
}

// TestAttestCSRMutations: the 64-byte ATTEST_CSR signature answers its own
// bytes for every change, and an error for every truncation. An append makes
// the reply longer than MaxPayload, which no device can frame.
func TestAttestCSRMutations(t *testing.T) {
	sig := make([]byte, CSRSigLen)
	for i := range sig {
		sig[i] = byte(0x80 + i)
	}
	ctx := context.Background()
	eachMutation(sig, 300, func(m mutation) {
		if len(m.b) > MaxPayload {
			return
		}
		link := newCSRDevice()
		link.sig = m.b
		var got [CSRSigLen]byte
		var err error
		noPanic(t, m, func() { got, err = New(link).AttestCSR(ctx, nonce32()) })
		if err != nil {
			if m.kind != "truncate" {
				t.Fatalf("%v: %v", m, err)
			}
			return
		}
		if m.kind == "truncate" || !bytes.Equal(got[:], m.b) {
			t.Fatalf("%v: answered % x", m, got)
		}
	})
}
