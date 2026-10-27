// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build tfwsim

package main

import (
	"bytes"
	"context"
	"crypto/ecdsa"
	"crypto/sha256"
	"crypto/x509"
	"encoding/asn1"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"math/big"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/tfwsim"
)

// The helpers the e2e_*_test.go scenarios share. Each drives one tfwctl verb
// through run(), the same entry point main() calls, against a simulator the
// scenario started; nothing here talks to the device except through the CLI.
// The decoders below are written from include/tfw/protocol.h and the RFCs, not
// from this repository's C, so a verifier independent of the firmware reaches
// the verdict.

var testSerialHex = hex.EncodeToString(tfwsim.TestSerial[:])

// cliCaps runs `caps --json` and decodes it back into the SDK's structure.
func cliCaps(t testing.TB, dev *tfw.Device) tfw.Caps {
	t.Helper()
	var out bytes.Buffer
	require.NoError(t, run(context.Background(), dev, "caps", nil, true, &out))
	var c tfw.Caps
	require.NoError(t, json.Unmarshal(out.Bytes(), &c), "caps --json printed %q", out.String())
	return c
}

// cliStatus runs one verb and returns the device status it was refused with.
func cliStatus(t *testing.T, dev *tfw.Device, verb string, args ...string) tfw.Status {
	t.Helper()
	err := run(context.Background(), dev, verb, args, false, io.Discard)
	var st tfw.Status
	require.True(t, errorsAsStatus(err, &st), "%s %v: %v, want a device status", verb, args, err)
	return st
}

// cliJSON runs one verb with --json and decodes what it printed into v.
func cliJSON(t *testing.T, dev *tfw.Device, v any, verb string, args ...string) {
	t.Helper()
	var out bytes.Buffer
	require.NoError(t, run(context.Background(), dev, verb, args, true, &out), "%s %v", verb, args)
	require.NoError(t, json.Unmarshal(out.Bytes(), v), "%s --json printed %q", verb, out.String())
}

// cliHex runs one tfwctl verb with --json and returns the hex field it names,
// decoded.
func cliHex(t *testing.T, dev *tfw.Device, verb, field string, nonce []byte) ([]byte, error) {
	t.Helper()
	var out bytes.Buffer
	if err := run(context.Background(), dev, verb, []string{hex.EncodeToString(nonce)}, true, &out); err != nil {
		return nil, err
	}
	var got map[string]any
	require.NoError(t, json.Unmarshal(out.Bytes(), &got), "%s --json printed %q", verb, out.String())
	s, ok := got[field].(string)
	require.True(t, ok, "%s --json has no %q: %q", verb, field, out.String())
	b, err := hex.DecodeString(s)
	require.NoError(t, err)
	assert.EqualValues(t, len(b), got["len"], "%s --json len", verb)
	return b, nil
}

func cliAttest(t *testing.T, dev *tfw.Device, nonce []byte) []byte {
	t.Helper()
	ev, err := cliHex(t, dev, "attest", "evidence", nonce)
	require.NoError(t, err)
	return ev
}

func cliAttestCert(t *testing.T, dev *tfw.Device, nonce []byte) []byte {
	t.Helper()
	der, err := cliHex(t, dev, "attest-cert", "certificate", nonce)
	require.NoError(t, err)
	return der
}

// Offsets into the 164-byte evidence frame, per include/tfw/protocol.h.
const (
	offVersion = 0
	offNonce   = 1
	offDevice  = 17
	offFWHash  = 33
	offAlg     = 73
	offSig     = 74
	algECDSA   = 0x01 // TFW_ALG_ECDSA_P256_SHA256
	algECDSASW = 0x11 // TFW_ALG_ECDSA_P256_SHA256_SOFT
)

// evidenceAlg is the ATTEST AlgID include/tfw/protocol.h gives an ES256 frame
// from a root of the tier: 0x11 from the software root, 0x01 from the element.
func evidenceAlg(tier tfw.CryptoTier) byte {
	if tier == tfw.CryptoSoftware {
		return algECDSASW
	}
	return algECDSA
}

// tcg-dice-UCCS-evidence, 2.23.133.5.4.6.
var oidUCCS = asn1.ObjectIdentifier{2, 23, 133, 5, 4, 6}

func aliasNonce(seed byte) []byte {
	n := make([]byte, tfw.AliasNonceLen)
	for i := range n {
		n[i] = seed + byte(i)
	}
	return n
}

func attestNonce(seed byte) []byte {
	n := make([]byte, tfw.AttestNonceLen)
	for i := range n {
		n[i] = seed ^ byte(i*7)
	}
	return n
}

// parseCert parses the paged-out alias certificate with crypto/x509. That the
// standard library accepts it -- a real P-256 SubjectPublicKeyInfo, a valid
// ecdsa-with-SHA256 signature algorithm, extensions it does not know -- is itself
// part of what is being tested.
func parseCert(t *testing.T, der []byte) *x509.Certificate {
	t.Helper()
	cert, err := x509.ParseCertificate(der)
	require.NoError(t, err, "x509.ParseCertificate")
	return cert
}

// uccsOf returns the claims set carried in the certificate's
// tcg-dice-UCCS-evidence extension.
func uccsOf(t *testing.T, cert *x509.Certificate) []byte {
	t.Helper()
	for _, ext := range cert.Extensions {
		if !ext.Id.Equal(oidUCCS) {
			continue
		}
		// extnValue holds UccsEvidence ::= SEQUENCE { uccs OCTET STRING }.
		var ev struct{ UCCS []byte }
		_, err := asn1.Unmarshal(ext.Value, &ev)
		require.NoError(t, err, "UccsEvidence")
		return ev.UCCS
	}
	t.Fatal("certificate carries no tcg-dice-UCCS-evidence extension")
	return nil
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
	require.Empty(t, c.b, "trailing bytes after the claims set")
	return v
}

// claimsOf decodes the UCCS and returns its nonce, ueid and the SHA-256 in the
// evidence CoSWID's file entry.
func claimsOf(t *testing.T, uccs []byte) (nonce, ueid, fwHash []byte) {
	t.Helper()
	claims, ok := decodeCBOR(t, uccs).(map[uint64]any)
	require.True(t, ok, "claims set is not a CBOR map")
	nonce, _ = claims[10].([]byte) // eat_nonce, RFC 9711
	ueid, _ = claims[256].([]byte) // ueid
	meas, _ := claims[273].([]any) // measurements: [[content-type, content]]
	require.Len(t, meas, 1, "measurements")
	pair, _ := meas[0].([]any)
	require.True(t, len(pair) == 2 && pair[0] == uint64(258), // 258 = application/swid+cbor
		"measurement is %v, want [258, coswid]", pair)
	coswid, _ := decodeCBOR(t, pair[1].([]byte)).(map[uint64]any)
	evidence, _ := coswid[3].(map[uint64]any)               // CoSWID evidence
	file, _ := evidence[17].(map[uint64]any)                // file
	hash, _ := file[7].([]any)                              // hash-entry: [alg-id, bytes]
	require.True(t, len(hash) == 2 && hash[0] == uint64(1), // 1 = sha-256
		"file hash-entry is %v, want [1, sha-256]", hash)
	return nonce, ueid, hash[1].([]byte)
}

// verifyFrame checks an ATTEST frame the way a verifier does, from the layout in
// include/tfw/protocol.h and nothing else: the signature is raw R||S over the
// SHA-256 of everything before the algorithm byte.
func verifyFrame(pub *ecdsa.PublicKey, ev []byte) bool {
	if len(ev) != tfw.EvidenceLen {
		return false
	}
	digest := sha256.Sum256(ev[:offAlg])
	r := new(big.Int).SetBytes(ev[offSig : offSig+32])
	s := new(big.Int).SetBytes(ev[offSig+32 : offSig+64])
	return ecdsa.Verify(pub, digest[:], r, s)
}
