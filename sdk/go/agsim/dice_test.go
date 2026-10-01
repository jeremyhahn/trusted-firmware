// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package agsim_test

import (
	"bytes"
	"context"
	"encoding/asn1"
	"errors"
	"fmt"
	"testing"

	ag "github.com/jeremyhahn/arduino-generic/sdk/go"
	"github.com/jeremyhahn/arduino-generic/sdk/go/agsim"
)

// These tests drive the element-tier simulator: the real firmware, the real
// ATECC608A driver and provider, and the software model of the part as the I2C
// bus. They check the DICE evidence the firmware produces -- its structure, its
// identity claims and its nonce binding. They deliberately do NOT check any
// signature: the model does no curve arithmetic, so none would verify, and a
// test that pretended otherwise would be testing the model.

// Offsets into the 164-byte evidence frame, per include/ag/protocol.h.
const (
	offVersion = 0
	offNonce   = 1
	offDevice  = 17
	offFWHash  = 33
	offAlg     = 73
	offSig     = 74
	algECDSA   = 0x01 // AG_ALG_ECDSA_P256_SHA256
)

// tcg-dice-UCCS-evidence, 2.23.133.5.4.6.
var oidUCCS = asn1.ObjectIdentifier{2, 23, 133, 5, 4, 6}

func aliasNonce(seed byte) []byte {
	n := make([]byte, ag.AliasNonceLen)
	for i := range n {
		n[i] = seed + byte(i)
	}
	return n
}

func attestNonce(seed byte) []byte {
	n := make([]byte, ag.AttestNonceLen)
	for i := range n {
		n[i] = seed ^ byte(i*7)
	}
	return n
}

// ------------------------------------------------------------ DER and CBOR

// uccsOf returns the claims set carried in the certificate's
// tcg-dice-UCCS-evidence extension, and the subject public key bit string.
//
// encoding/asn1 rather than crypto/x509: the model's "public key" is not a
// point on P-256 and x509.ParseCertificate rightly refuses it, which would
// make every one of these tests fail on the one thing they cannot check.
func uccsOf(t *testing.T, der []byte) (claims []byte, spki []byte) {
	t.Helper()

	var cert struct {
		TBS asn1.RawValue
		Alg asn1.RawValue
		Sig asn1.BitString
	}
	rest, err := asn1.Unmarshal(der, &cert)
	if err != nil || len(rest) != 0 {
		t.Fatalf("certificate is not exactly one DER Certificate (rest=%d): %v", len(rest), err)
	}

	var extensions []byte
	body := cert.TBS.Bytes
	for len(body) > 0 {
		var field asn1.RawValue
		if body, err = asn1.Unmarshal(body, &field); err != nil {
			t.Fatalf("TBSCertificate: %v", err)
		}
		switch {
		case field.Class == asn1.ClassContextSpecific && field.Tag == 3:
			extensions = field.Bytes
		case field.Tag == asn1.TagSequence && field.IsCompound && bytes.Contains(field.Bytes, []byte{0x03, 0x42, 0x00, 0x04}):
			spki = field.Bytes
		}
	}
	if extensions == nil {
		t.Fatal("certificate has no extensions")
	}

	var seq asn1.RawValue
	if _, err := asn1.Unmarshal(extensions, &seq); err != nil {
		t.Fatalf("extensions: %v", err)
	}
	for list := seq.Bytes; len(list) > 0; {
		var raw asn1.RawValue
		if list, err = asn1.Unmarshal(list, &raw); err != nil {
			t.Fatalf("extension: %v", err)
		}
		var ext struct {
			ID       asn1.ObjectIdentifier
			Critical bool `asn1:"optional"`
			Value    []byte
		}
		if _, err := asn1.Unmarshal(raw.FullBytes, &ext); err != nil {
			t.Fatalf("extension: %v", err)
		}
		if ext.ID.Equal(oidUCCS) {
			// extnValue holds UccsEvidence ::= SEQUENCE { uccs OCTET STRING }.
			var ev struct{ UCCS []byte }
			if _, err := asn1.Unmarshal(ext.Value, &ev); err != nil {
				t.Fatalf("UccsEvidence: %v", err)
			}
			return ev.UCCS, spki
		}
	}
	t.Fatal("certificate carries no tcg-dice-UCCS-evidence extension")
	return nil, nil
}

// cbor decodes the small subset the claims set uses: unsigned integers, byte
// and text strings, arrays and maps. Definite lengths only, which is also a
// property the encoder is required to have (core-deterministic CBOR).
type cbor struct{ b []byte }

func (c *cbor) next() any {
	head := c.b[0]
	major, info := head>>5, uint64(head&0x1f)
	c.b = c.b[1:]
	switch {
	case info == 24:
		info, c.b = uint64(c.b[0]), c.b[1:]
	case info == 25:
		info, c.b = uint64(c.b[0])<<8|uint64(c.b[1]), c.b[2:]
	case info > 25:
		panic(fmt.Sprintf("cbor: unsupported head %#x", head))
	}
	switch major {
	case 0:
		return info
	case 2:
		v := append([]byte(nil), c.b[:info]...)
		c.b = c.b[info:]
		return v
	case 3:
		v := string(c.b[:info])
		c.b = c.b[info:]
		return v
	case 4:
		out := make([]any, info)
		for i := range out {
			out[i] = c.next()
		}
		return out
	case 5:
		out := make(map[uint64]any, info)
		for i := uint64(0); i < info; i++ {
			k := c.next().(uint64)
			out[k] = c.next()
		}
		return out
	}
	panic(fmt.Sprintf("cbor: unsupported major type %d", major))
}

func decodeCBOR(t *testing.T, b []byte) (v any) {
	t.Helper()
	defer func() {
		if r := recover(); r != nil {
			t.Fatalf("claims set is not the CBOR subset the firmware emits: %v", r)
		}
	}()
	c := &cbor{b: b}
	v = c.next()
	if len(c.b) != 0 {
		t.Fatalf("%d trailing bytes after the claims set", len(c.b))
	}
	return v
}

// claimsOf decodes the UCCS and returns its nonce, ueid and the SHA-256 in the
// evidence CoSWID's file entry.
func claimsOf(t *testing.T, uccs []byte) (nonce, ueid, fwHash []byte) {
	t.Helper()

	claims, ok := decodeCBOR(t, uccs).(map[uint64]any)
	if !ok {
		t.Fatal("claims set is not a CBOR map")
	}
	nonce, _ = claims[10].([]byte) // eat_nonce, RFC 9711
	ueid, _ = claims[256].([]byte) // ueid
	meas, _ := claims[273].([]any) // measurements: [[content-type, content]]
	if len(meas) != 1 {
		t.Fatalf("measurements has %d entries, want 1", len(meas))
	}
	pair, _ := meas[0].([]any)
	if len(pair) != 2 || pair[0] != uint64(258) { // 258 = application/swid+cbor
		t.Fatalf("measurement is %v, want [258, coswid]", pair)
	}
	coswid, _ := decodeCBOR(t, pair[1].([]byte)).(map[uint64]any)
	evidence, _ := coswid[3].(map[uint64]any)   // CoSWID evidence
	file, _ := evidence[17].(map[uint64]any)    // file
	hash, _ := file[7].([]any)                  // hash-entry: [alg-id, bytes]
	if len(hash) != 2 || hash[0] != uint64(1) { // 1 = sha-256
		t.Fatalf("file hash-entry is %v, want [1, sha-256]", hash)
	}
	return nonce, ueid, hash[1].([]byte)
}

// ----------------------------------------------------------------- tests

func TestElementAttestCertIsDICEEvidenceBoundToTheCallersNonce(t *testing.T) {
	d := agsim.StartElement(t)
	nonce := aliasNonce(0x40)

	der, err := d.AttestCert(context.Background(), nonce)
	if err != nil {
		t.Fatal(err)
	}
	uccs, spki := uccsOf(t, der)
	gotNonce, ueid, fwHash := claimsOf(t, uccs)

	if !bytes.Equal(gotNonce, nonce) {
		t.Errorf("eat_nonce = %x, want the caller's %x", gotNonce, nonce)
	}
	// RFC 9711 section 4.2.1.1: a leading type byte, 0x01 for a random identifier.
	if want := append([]byte{0x01}, agsim.TestSerial[:]...); !bytes.Equal(ueid, want) {
		t.Errorf("ueid = %x, want %x", ueid, want)
	}
	if !bytes.Equal(fwHash, agsim.TestFWHash[:]) {
		t.Errorf("FWHash = %x, want %x", fwHash, agsim.TestFWHash)
	}
	if len(spki) == 0 {
		t.Error("certificate has no uncompressed subject public key")
	}
}

func TestElementAttestCertDiffersWithTheNonce(t *testing.T) {
	d := agsim.StartElement(t)
	a, err := d.AttestCert(context.Background(), aliasNonce(0x10))
	if err != nil {
		t.Fatal(err)
	}
	b, err := d.AttestCert(context.Background(), aliasNonce(0x11))
	if err != nil {
		t.Fatal(err)
	}
	if bytes.Equal(a, b) {
		t.Fatal("two different nonces produced the same certificate")
	}
	// The signature is over a digest that covers the nonce, so it must move too.
	tail := len(a) - 64
	if bytes.Equal(a[tail:], b[tail:]) {
		t.Error("the certificate signature did not change with the nonce")
	}

	ua, _ := uccsOf(t, a)
	ub, _ := uccsOf(t, b)
	na, _, _ := claimsOf(t, ua)
	nb, _, _ := claimsOf(t, ub)
	if bytes.Equal(na, nb) {
		t.Error("eat_nonce did not change with the nonce")
	}
}

func TestElementAttestCertSameNonceIsReproducible(t *testing.T) {
	// A simulator is a test oracle only if the same input gives the same
	// output; the certificate carries no clock.
	nonce := aliasNonce(0x77)
	var first []byte
	for i := 0; i < 2; i++ {
		der, err := agsim.StartElement(t).AttestCert(context.Background(), nonce)
		if err != nil {
			t.Fatal(err)
		}
		if first == nil {
			first = der
		} else if !bytes.Equal(first, der) {
			t.Fatal("the same nonce produced different certificates on a fresh simulator")
		}
	}
}

func TestElementAttestEvidenceFrame(t *testing.T) {
	d := agsim.StartElement(t)
	nonce := attestNonce(0x5a)

	ev, err := d.Attest(context.Background(), nonce)
	if err != nil {
		t.Fatal(err)
	}
	if len(ev) != ag.EvidenceLen {
		t.Fatalf("evidence is %d bytes, want %d", len(ev), ag.EvidenceLen)
	}
	if !bytes.Equal(ev[offNonce:offNonce+ag.AttestNonceLen], nonce) {
		t.Errorf("frame nonce = %x, want %x", ev[offNonce:offNonce+ag.AttestNonceLen], nonce)
	}
	if !bytes.Equal(ev[offDevice:offDevice+ag.SerialLen], agsim.TestSerial[:]) {
		t.Errorf("DeviceID = %x, want %x", ev[offDevice:offDevice+ag.SerialLen], agsim.TestSerial)
	}
	if !bytes.Equal(ev[offFWHash:offFWHash+32], agsim.TestFWHash[:]) {
		t.Errorf("FWHash = %x, want %x", ev[offFWHash:offFWHash+32], agsim.TestFWHash)
	}
	if ev[offAlg] != algECDSA {
		t.Errorf("algorithm = %#x, want ECDSA P-256 (%#x)", ev[offAlg], algECDSA)
	}
	if bytes.Equal(ev[offSig:offSig+64], make([]byte, 64)) {
		t.Error("signature field is empty")
	}

	other, err := d.Attest(context.Background(), attestNonce(0x5b))
	if err != nil {
		t.Fatal(err)
	}
	if bytes.Equal(ev, other) {
		t.Error("two different nonces produced identical evidence")
	}
	if bytes.Equal(ev[offSig:offSig+64], other[offSig:offSig+64]) {
		t.Error("the evidence signature did not change with the nonce")
	}
}

func TestElementReportsItsRootOfTrust(t *testing.T) {
	c, err := agsim.StartElement(t).Caps(context.Background())
	if err != nil {
		t.Fatal(err)
	}
	if c.CryptoTier != ag.CryptoSecureElement {
		t.Errorf("tier = %v, want %v", c.CryptoTier, ag.CryptoSecureElement)
	}
	if !c.Algorithms.Can(ag.AlgECDSAP256SHA256) {
		t.Errorf("algorithms = %v, want ECDSA P-256", c.Algorithms)
	}
	if !c.Has(ag.FeatureAttest) || !c.Has(ag.FeatureAliasCert) {
		t.Errorf("features %#x lack attest/alias cert", c.Features)
	}
}

func TestBlankElementRefusesToAttest(t *testing.T) {
	d := agsim.StartBlankElement(t)

	// The element is present, so the tier is honest about it...
	c, err := d.Caps(context.Background())
	if err != nil {
		t.Fatal(err)
	}
	if c.CryptoTier != ag.CryptoSecureElement {
		t.Errorf("tier = %v, want %v", c.CryptoTier, ag.CryptoSecureElement)
	}
	// ...and with no identity in it, evidence must be refused, not faked.
	if _, err := d.Attest(context.Background(), attestNonce(1)); !errors.Is(err, ag.StatusErrNotProvisioned) {
		t.Errorf("Attest on a blank element: %v, want %v", err, ag.StatusErrNotProvisioned)
	}
	if _, err := d.AttestCert(context.Background(), aliasNonce(1)); !errors.Is(err, ag.StatusErrNotProvisioned) {
		t.Errorf("AttestCert on a blank element: %v, want %v", err, ag.StatusErrNotProvisioned)
	}
}

func TestSoftwareTierStillCannotIssueACertificate(t *testing.T) {
	// The reason the element tier exists: the default agsim has no asymmetric key.
	_, err := agsim.Start(t).AttestCert(context.Background(), aliasNonce(1))
	if !errors.Is(err, ag.StatusErrUnsupported) {
		t.Fatalf("AttestCert on the software tier: %v, want %v", err, ag.StatusErrUnsupported)
	}
}

func TestAttestRejectsAWrongLengthNonceBeforeTheWire(t *testing.T) {
	d := agsim.StartElement(t)
	var argErr *ag.ArgRangeError
	if _, err := d.AttestCert(context.Background(), attestNonce(1)); !errors.As(err, &argErr) {
		t.Errorf("a 16-byte nonce for ATTEST_CERT: %v, want *ArgRangeError", err)
	}
	if _, err := d.Attest(context.Background(), aliasNonce(1)); !errors.As(err, &argErr) {
		t.Errorf("a 32-byte nonce for ATTEST: %v, want *ArgRangeError", err)
	}
}
