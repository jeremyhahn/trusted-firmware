// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"crypto/x509/pkix"
	"encoding/asn1"
	"errors"
	"testing"

	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/vectors"
)

// reasonAt returns a build mutator that applies f at the named layer only.
func reasonAt(layer string, f func(*parts)) func(string, *parts) {
	return func(l string, p *parts) {
		if l == layer {
			f(p)
		}
	}
}

// requireReason asserts err is an *EvidenceError carrying exactly reason and
// that it matches ErrBadFormat.
func requireReason(t *testing.T, err error, reason string) {
	t.Helper()
	var ee *EvidenceError
	require.True(t, errors.As(err, &ee), "got %v, want an *EvidenceError", err)
	require.Equal(t, reason, ee.Reason)
	require.True(t, errors.Is(err, ErrBadFormat))
}

// One case per refusal, each asserting the reason that refusal gives, so a
// swapped reason or a dropped check that lets a later one fire fails here.
func TestApplicationEvidenceRefusalReasons(t *testing.T) {
	ex := []byte(vectors.AppEvExample)
	w := []byte(vectors.AppEvWithUccs)
	const fsName = `file is not exactly fs-name (24) "app" and a hash (7)`
	cases := []struct {
		name   string
		uccs   []byte
		reason string
	}{
		{"truncated", w[:len(w)-1], "claims set: length 84 exceeds the 83 bytes left"},
		{"trailing byte", append(append([]byte{}, w...), 0x00), "claims set: 1 trailing bytes after the item"},
		{"not a map", enc([]any{uint64(1)}), "claims set is not a map"},
		{"no 273", build(t, ex, reasonAt("claims", func(p *parts) { p.claims = p.claims[:2] })), "no measurements claim (273)"},
		{"273 not an array", build(t, ex, reasonAt("meas", func(p *parts) { p.meas = uint64(1) })), "measurements claim is not exactly one entry"},
		{"273 two entries", build(t, ex, reasonAt("meas", func(p *parts) { p.meas = []any{p.pair, p.pair} })), "measurements claim is not exactly one entry"},
		{"entry not a pair", build(t, ex, reasonAt("pair", func(p *parts) { p.pair = []any{p.ct, p.coswidBytes, p.ct} })), "measurement is not a [content-type, content] pair"},
		{"content type 257", build(t, ex, reasonAt("content", func(p *parts) { p.ct = uint64(257) })), "measurement content type is not 258, application/swid+cbor"},
		{"content not bytes", build(t, ex, reasonAt("content", func(p *parts) { p.coswidBytes = "tag" })), "measurement content is not a byte string"},
		{"CoSWID not CBOR", build(t, ex, reasonAt("content", func(p *parts) { p.coswidBytes = []byte{0xff} })), "CoSWID: additional information 31 is not a definite length"},
		{"CoSWID not a map", build(t, ex, reasonAt("content", func(p *parts) { p.coswidBytes = enc(uint64(3)) })), "CoSWID is not a map"},
		{"no evidence (3)", build(t, ex, reasonAt("coswid", func(p *parts) { p.coswid = append(p.coswid[:3], p.coswid[4]) })), "CoSWID has no evidence map (3)"},
		{"evidence two keys", build(t, ex, reasonAt("evidence", func(p *parts) { p.evidence = append(p.evidence, kv{18, uint64(0)}) })), "evidence is not exactly one file (17)"},
		{"evidence no file", build(t, ex, reasonAt("evidence", func(p *parts) { p.evidence = cmap{{18, p.file}} })), "evidence is not exactly one file (17)"},
		{"fs-name not app", build(t, ex, reasonAt("file", func(p *parts) { p.file[1].v = "bpp" })), fsName},
		{"file third key", build(t, ex, reasonAt("file", func(p *parts) { p.file = append(p.file, kv{25, uint64(0)}) })), fsName},
		{"hash not an entry", build(t, ex, reasonAt("file", func(p *parts) { p.file[0].v = []any{uint64(1)} })), "file hash is not a [hash-alg-id, hash-value] entry"},
		{"hash-alg 7", build(t, ex, reasonAt("file", func(p *parts) { p.file[0].v = []any{uint64(7), ex} })), "file hash-alg-id is not 1, sha-256"},
		{"hash-value text", build(t, ex, reasonAt("file", func(p *parts) { p.file[0].v = []any{uint64(1), "app"} })), "file hash-value is not 32 bytes"},
		{"width 31", build(t, ex[:31], nil), "file hash-value is not 32 bytes"},
		{"width 33", build(t, append(append([]byte{}, ex...), 0x00), nil), "file hash-value is not 32 bytes"},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			_, err := ApplicationEvidence(c.uccs)
			requireReason(t, err, c.reason)
		})
	}
}

func TestAliasUCCSRefusalReasons(t *testing.T) {
	w := []byte(vectors.AppEvWithUccs)
	val, err := asn1.Marshal(struct{ UCCS []byte }{w})
	require.NoError(t, err)
	cases := []struct {
		name   string
		der    []byte
		reason string
	}{
		{"not a certificate", []byte{0x30, 0x00}, "not a certificate"},
		{"no extension", aliasCert(t, nil), "no tcg-dice-UCCS-evidence extension"},
		{"not a UccsEvidence", aliasCert(t, []byte{0x04, 0x01, 0x00}), "tcg-dice-UCCS-evidence is not a UccsEvidence"},
		{"another OID only", aliasCert(t, nil, pkix.Extension{Id: asn1.ObjectIdentifier{2, 23, 133, 5, 4, 7}, Value: val}), "no tcg-dice-UCCS-evidence extension"},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			_, err := AliasUCCS(c.der)
			requireReason(t, err, c.reason)
		})
	}
}
