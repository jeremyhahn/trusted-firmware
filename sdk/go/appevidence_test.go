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
	"encoding/binary"
	"math/big"
	"testing"
	"time"

	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/vectors"
)

// kv and cmap give the test encoder maps in the order the device writes them,
// which is what makes a rebuilt claims set comparable to the literal vector.
type kv struct {
	k uint64
	v any
}
type cmap []kv

func encHead(major byte, n uint64) []byte {
	switch {
	case n < 24:
		return []byte{major<<5 | byte(n)}
	case n < 1<<8:
		return []byte{major<<5 | 24, byte(n)}
	case n < 1<<16:
		return binary.BigEndian.AppendUint16([]byte{major<<5 | 25}, uint16(n))
	default:
		return binary.BigEndian.AppendUint32([]byte{major<<5 | 26}, uint32(n))
	}
}

func enc(v any) []byte {
	switch x := v.(type) {
	case uint64:
		return encHead(0, x)
	case []byte:
		return append(encHead(2, uint64(len(x))), x...)
	case string:
		return append(encHead(3, uint64(len(x))), x...)
	case []any:
		out := encHead(4, uint64(len(x)))
		for _, e := range x {
			out = append(out, enc(e)...)
		}
		return out
	case cmap:
		out := encHead(5, uint64(len(x)))
		for _, e := range x {
			out = append(out, enc(e.k)...)
			out = append(out, enc(e.v)...)
		}
		return out
	}
	panic("enc: unsupported type")
}

// parts are the claims set's pieces, each replaceable by a test.
type parts struct {
	file, evidence, coswid cmap
	coswidBytes            any // the content of the [258, content] pair
	ct                     any // the content type
	pair, meas             any
	claims                 cmap
}

// build assembles the claims set from the inside out; mut, when not nil, is
// called after each layer's default is set so it can replace that layer.
func build(t *testing.T, ev []byte, mut func(layer string, p *parts)) []byte {
	t.Helper()
	w := []byte(vectors.AppEvWithUccs)
	nonce, ueid, tagID := w[4:36], w[40:57], w[70:86]
	if mut == nil {
		mut = func(string, *parts) {}
	}
	p := &parts{}
	p.file = cmap{{7, []any{uint64(1), ev}}, {24, "app"}}
	mut("file", p)
	p.evidence = cmap{{17, p.file}}
	mut("evidence", p)
	p.coswid = cmap{
		{0, tagID}, {1, "tfw"}, {2, cmap{{31, "tfw"}, {33, uint64(1)}}},
		{3, p.evidence}, {12, uint64(0)},
	}
	mut("coswid", p)
	p.coswidBytes = enc(p.coswid)
	p.ct = uint64(258)
	mut("content", p)
	p.pair = []any{p.ct, p.coswidBytes}
	mut("pair", p)
	p.meas = []any{p.pair}
	mut("meas", p)
	p.claims = cmap{{10, nonce}, {256, ueid}, {273, p.meas}}
	mut("claims", p)
	return enc(p.claims)
}

// The encoder above rebuilds both literal vectors byte for byte, so the
// refusals below are each one change away from what the device writes.
func TestApplicationEvidenceBuilderMatchesTheVectors(t *testing.T) {
	require.Equal(t, []byte(vectors.AppEvWithUccs), build(t, []byte(vectors.AppEvExample), nil))
	w := []byte(vectors.AppEvWithoutUccs)
	require.Equal(t, w, build(t, w[111:143], nil))
}

func TestApplicationEvidenceWithTheClaim(t *testing.T) {
	ev, err := ApplicationEvidence([]byte(vectors.AppEvWithUccs))
	require.NoError(t, err)
	require.Equal(t, []byte(vectors.AppEvExample), ev[:])
}

// With no application supplying evidence, 273 holds the firmware measurement:
// the claim is present and is not zeros.
func TestApplicationEvidenceWithoutTheClaim(t *testing.T) {
	ev, err := ApplicationEvidence([]byte(vectors.AppEvWithoutUccs))
	require.NoError(t, err)
	require.Equal(t, []byte(vectors.AppEvWithoutUccs)[111:143], ev[:])
	require.NotEqual(t, [ApplicationEvidenceLen]byte{}, ev)
}

// One case per refusal. Each is the with-claim vector with one change.
func TestApplicationEvidenceRefusals(t *testing.T) {
	ex := []byte(vectors.AppEvExample)
	at := func(layer string, f func(p *parts)) func(string, *parts) {
		return func(l string, p *parts) {
			if l == layer {
				f(p)
			}
		}
	}
	w := []byte(vectors.AppEvWithUccs)
	cases := []struct {
		name string
		uccs []byte
	}{
		{"truncated", w[:len(w)-1]},
		{"trailing byte", append(append([]byte{}, w...), 0x00)},
		{"not a map", enc([]any{uint64(1)})},
		{"no 273", build(t, ex, at("claims", func(p *parts) { p.claims = p.claims[:2] }))},
		{"273 not an array", build(t, ex, at("meas", func(p *parts) { p.meas = uint64(1) }))},
		{"273 two entries", build(t, ex, at("meas", func(p *parts) { p.meas = []any{p.pair, p.pair} }))},
		{"273 no entries", build(t, ex, at("meas", func(p *parts) { p.meas = []any{} }))},
		{"entry not a pair", build(t, ex, at("pair", func(p *parts) { p.pair = []any{p.ct, p.coswidBytes, p.ct} }))},
		{"content type 257", build(t, ex, at("content", func(p *parts) { p.ct = uint64(257) }))},
		{"content type 259", build(t, ex, at("content", func(p *parts) { p.ct = uint64(259) }))},
		{"content type text", build(t, ex, at("content", func(p *parts) { p.ct = "258" }))},
		{"content not bytes", build(t, ex, at("content", func(p *parts) { p.coswidBytes = "tag" }))},
		{"CoSWID not CBOR", build(t, ex, at("content", func(p *parts) { p.coswidBytes = []byte{0xff} }))},
		{"CoSWID not a map", build(t, ex, at("content", func(p *parts) { p.coswidBytes = enc(uint64(3)) }))},
		{"no evidence (3)", build(t, ex, at("coswid", func(p *parts) { p.coswid = append(p.coswid[:3], p.coswid[4]) }))},
		{"evidence two keys", build(t, ex, at("evidence", func(p *parts) { p.evidence = append(p.evidence, kv{18, uint64(0)}) }))},
		{"evidence no file", build(t, ex, at("evidence", func(p *parts) { p.evidence = cmap{{18, p.file}} }))},
		{"fs-name not app", build(t, ex, at("file", func(p *parts) { p.file[1].v = "bpp" }))},
		{"no fs-name", build(t, ex, at("file", func(p *parts) { p.file = p.file[:1] }))},
		{"file third key", build(t, ex, at("file", func(p *parts) { p.file = append(p.file, kv{25, uint64(0)}) }))},
		{"hash not an entry", build(t, ex, at("file", func(p *parts) { p.file[0].v = []any{uint64(1)} }))},
		{"hash-alg 7", build(t, ex, at("file", func(p *parts) { p.file[0].v = []any{uint64(7), ex} }))},
		{"hash-alg text", build(t, ex, at("file", func(p *parts) { p.file[0].v = []any{"sha-256", ex} }))},
		{"hash-value text", build(t, ex, at("file", func(p *parts) { p.file[0].v = []any{uint64(1), "app"} }))},
		{"width 31", build(t, ex[:31], nil)},
		{"width 33", build(t, append(append([]byte{}, ex...), 0x00), nil)},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			_, err := ApplicationEvidence(c.uccs)
			var ee *EvidenceError
			require.ErrorAs(t, err, &ee)
			require.ErrorIs(t, err, ErrBadFormat)
			require.Contains(t, err.Error(), "tfw: malformed evidence: ")
		})
	}
}

// aliasCert returns a certificate carrying extValue as its
// tcg-dice-UCCS-evidence extension, or no such extension when extValue is nil.
func aliasCert(t *testing.T, extValue []byte, before ...pkix.Extension) []byte {
	t.Helper()
	key, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	require.NoError(t, err)
	tmpl := &x509.Certificate{
		SerialNumber: big.NewInt(0x40),
		Subject:      pkix.Name{CommonName: "tfw Alias"},
		NotBefore:    time.Date(2020, 1, 1, 0, 0, 0, 0, time.UTC),
		NotAfter:     time.Date(9999, 12, 31, 23, 59, 59, 0, time.UTC),
	}
	tmpl.ExtraExtensions = append(tmpl.ExtraExtensions, before...)
	if extValue != nil {
		tmpl.ExtraExtensions = append(tmpl.ExtraExtensions, pkix.Extension{Id: oidUCCSEvidence, Critical: true, Value: extValue})
	}
	der, err := x509.CreateCertificate(rand.Reader, tmpl, tmpl, &key.PublicKey, key)
	require.NoError(t, err)
	return der
}

func TestAliasUCCSReturnsTheClaimsSet(t *testing.T) {
	val, err := asn1.Marshal(struct{ UCCS []byte }{[]byte(vectors.AppEvWithUccs)})
	require.NoError(t, err)
	uccs, err := AliasUCCS(aliasCert(t, val))
	require.NoError(t, err)
	require.Equal(t, []byte(vectors.AppEvWithUccs), uccs)

	// An extension ahead of it is passed over, not mistaken for the evidence.
	// 1.3.6.1.4.1.32473 is the documentation enterprise number (RFC 5612).
	other := pkix.Extension{Id: asn1.ObjectIdentifier{1, 3, 6, 1, 4, 1, 32473, 1}, Value: []byte{0x05, 0x00}}
	uccs, err = AliasUCCS(aliasCert(t, val, other))
	require.NoError(t, err)
	require.Equal(t, []byte(vectors.AppEvWithUccs), uccs)
}

func TestAliasUCCSRefusals(t *testing.T) {
	val, err := asn1.Marshal(struct{ UCCS []byte }{[]byte(vectors.AppEvWithUccs)})
	require.NoError(t, err)
	cases := []struct {
		name string
		der  []byte
	}{
		{"not a certificate", []byte{0x30, 0x00}},
		{"no extension", aliasCert(t, nil)},
		{"not a UccsEvidence", aliasCert(t, []byte{0x04, 0x01, 0x00})},
		{"another OID only", aliasCert(t, nil, pkix.Extension{Id: asn1.ObjectIdentifier{2, 23, 133, 5, 4, 7}, Value: val})},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			_, err := AliasUCCS(c.der)
			var ee *EvidenceError
			require.ErrorAs(t, err, &ee)
			require.ErrorIs(t, err, ErrBadFormat)
		})
	}
}

// oidUCCSEvidenceLiteral is tcg-dice-UCCS-evidence spelt out, so a test does
// not inherit a change to the package's own variable.
var oidUCCSEvidenceLiteral = asn1.ObjectIdentifier{2, 23, 133, 5, 4, 6}

func uccsEvidence(t *testing.T, uccs []byte) []byte {
	t.Helper()
	val, err := asn1.Marshal(struct{ UCCS []byte }{uccs})
	require.NoError(t, err)
	return val
}

// AliasUCCS reads the extension named 2.23.133.5.4.6 and passes over any
// other, even one shaped like a UccsEvidence that comes first.
func TestAliasUCCSSkipsOtherExtensions(t *testing.T) {
	want := []byte(vectors.AppEvWithUccs)
	der := aliasCert(t, nil,
		pkix.Extension{Id: asn1.ObjectIdentifier{2, 23, 133, 5, 4, 5}, Value: uccsEvidence(t, []byte("decoy"))},
		pkix.Extension{Id: oidUCCSEvidenceLiteral, Critical: true, Value: uccsEvidence(t, want)},
	)
	got, err := AliasUCCS(der)
	require.NoError(t, err)
	require.Equal(t, want, got)
}

// A single trailing byte after the UccsEvidence is refused, not only two.
func TestAliasUCCSRefusesOneTrailingByte(t *testing.T) {
	val := append(uccsEvidence(t, []byte(vectors.AppEvWithUccs)), 0x00)
	_, err := AliasUCCS(aliasCert(t, nil, pkix.Extension{Id: oidUCCSEvidenceLiteral, Value: val}))
	var ee *EvidenceError
	require.ErrorAs(t, err, &ee)
	require.Equal(t, "trailing bytes after the UccsEvidence", ee.Reason)
}

// The file hash is exactly [hash-alg-id, hash-value]: a third entry is refused.
func TestApplicationEvidenceRefusesAThreeEntryHash(t *testing.T) {
	ex := []byte(vectors.AppEvExample)
	uccs := build(t, ex, func(layer string, p *parts) {
		if layer == "file" {
			p.file = cmap{{7, []any{uint64(1), ex, uint64(0)}}, {24, "app"}}
		}
	})
	_, err := ApplicationEvidence(uccs)
	var ee *EvidenceError
	require.ErrorAs(t, err, &ee)
	require.Equal(t, "file hash is not a [hash-alg-id, hash-value] entry", ee.Reason)
}

// An EvidenceError is ErrBadFormat and nothing else.
func TestEvidenceErrorMatchesOnlyErrBadFormat(t *testing.T) {
	_, err := ApplicationEvidence([]byte{0x01})
	require.ErrorIs(t, err, ErrBadFormat)
	for _, other := range []error{ErrShortFrame, ErrBadCRC, ErrPayloadTooLarge, ErrTimeout, StatusErrUnsupported} {
		require.NotErrorIs(t, err, other)
	}
}

// Every definite-length argument width decodes to the same value.
func TestCBORArgumentWidths(t *testing.T) {
	for _, b := range [][]byte{
		{0x18, 0x2a},
		{0x19, 0x00, 0x2a},
		{0x1a, 0x00, 0x00, 0x00, 0x2a},
		{0x1b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2a},
	} {
		v, err := cborDecode(b)
		require.NoError(t, err)
		require.Equal(t, uint64(42), v)
	}
}
