package tfw

import (
	"bufio"
	"bytes"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/asn1"
	"encoding/hex"
	"math/big"
	"os"
	"strings"
	"testing"
	"time"

	"github.com/stretchr/testify/require"
)

// diceVectors are the secure DICE service's answers for one fixed input,
// written by the host build of src/tz/dice_service.c
// (test/unit/test_dice_service.c, which fails when they go stale).
type diceVectors struct {
	nonce, ueid, fwHash, deviceIDPub, aliasCert, aliasSig []byte
}

func loadDICEVectors(t *testing.T) diceVectors {
	t.Helper()
	f, err := os.Open("testdata/dice_service_vectors.txt")
	require.NoError(t, err)
	defer func() { _ = f.Close() }()
	m := map[string][]byte{}
	s := bufio.NewScanner(f)
	for s.Scan() {
		line := s.Text()
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		name, val, ok := strings.Cut(line, " ")
		require.True(t, ok, "vector line %q", line)
		b, err := hex.DecodeString(val)
		require.NoError(t, err, name)
		m[name] = b
	}
	require.NoError(t, s.Err())
	return diceVectors{m["nonce"], m["ueid"], m["fw_hash"], m["deviceid_pub"], m["alias_cert"], m["alias_sig"]}
}

// diceCA plays the manufacturer: the DeviceID certificate a test root issues
// over the service's DeviceID public key.
type diceCA struct {
	deviceID []byte
}

func newDICECA(t *testing.T, v diceVectors) diceCA {
	t.Helper()
	rootKey, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	require.NoError(t, err)
	now := time.Now()
	rootTmpl := &x509.Certificate{
		SerialNumber: big.NewInt(1), Subject: pkix.Name{CommonName: "test manufacturer root"},
		NotBefore: now.Add(-time.Hour), NotAfter: now.Add(time.Hour),
		IsCA: true, BasicConstraintsValid: true, KeyUsage: x509.KeyUsageCertSign,
	}
	rootDER, err := x509.CreateCertificate(rand.Reader, rootTmpl, rootTmpl, &rootKey.PublicKey, rootKey)
	require.NoError(t, err)
	root, err := x509.ParseCertificate(rootDER)
	require.NoError(t, err)

	devPub, err := ecdsa.ParseUncompressedPublicKey(elliptic.P256(), append([]byte{0x04}, v.deviceIDPub...))
	require.NoError(t, err)
	alias, err := x509.ParseCertificate(v.aliasCert)
	require.NoError(t, err)
	devTmpl := &x509.Certificate{
		SerialNumber: big.NewInt(2), RawSubject: alias.RawIssuer,
		NotBefore: now.Add(-time.Hour), NotAfter: now.Add(time.Hour),
		IsCA: true, BasicConstraintsValid: true, KeyUsage: x509.KeyUsageCertSign,
	}
	devDER, err := x509.CreateCertificate(rand.Reader, devTmpl, root, devPub, rootKey)
	require.NoError(t, err)
	return diceCA{deviceID: devDER}
}

func diceObject(t *testing.T, v diceVectors, ca diceCA) []byte {
	t.Helper()
	obj, err := DICEAttestationObject(v.aliasCert, ca.deviceID, v.nonce, v.aliasSig)
	require.NoError(t, err)
	return obj
}

// TestDICEAttestationFromTheSecureServiceParses: the object built from the
// secure service's vectors parses back to exactly those vectors.
func TestDICEAttestationFromTheSecureServiceParses(t *testing.T) {
	v := loadDICEVectors(t)
	ca := newDICECA(t, v)
	got, err := ParseDICEAttestationObject(diceObject(t, v, ca))
	require.NoError(t, err)
	require.Equal(t, &DICEAttestation{AliasCert: v.aliasCert, DeviceIDCert: ca.deviceID, Nonce: v.nonce, Sig: v.aliasSig}, got)
}

func TestDICEAttestationObjectIsTheDiceFormat(t *testing.T) {
	v := loadDICEVectors(t)
	ca := newDICECA(t, v)
	obj := diceObject(t, v, ca)
	// {"fmt": "dice", "attStmt": {"alg": -7, ...
	require.True(t, bytes.HasPrefix(obj, []byte("\xa3\x63fmt\x64dice\x67attStmt\xa3\x63alg\x26\x63sig")))
	require.True(t, bytes.HasSuffix(obj, append([]byte("\x68authData\x58\x20"), v.nonce...)))
}

// TestParseDICEAttestationObjectRefusesOtherShapes: anything but the object
// DICEAttestationObject writes is refused, each with its reason.
func TestParseDICEAttestationObjectRefusesOtherShapes(t *testing.T) {
	v := loadDICEVectors(t)
	ca := newDICECA(t, v)
	obj := diceObject(t, v, ca)
	sigDER, err := asn1.Marshal(struct{ R, S *big.Int }{
		new(big.Int).SetBytes(v.aliasSig[:32]), new(big.Int).SetBytes(v.aliasSig[32:]),
	})
	require.NoError(t, err)
	// build writes the object by hand so a field can break one rule.
	build := func(fmtName string, sig, nonce []byte) []byte {
		var b []byte
		b = cborHead(b, 5, 3)
		b = cborText(b, "fmt")
		b = cborText(b, fmtName)
		b = cborText(b, "attStmt")
		b = cborHead(b, 5, 3)
		b = cborText(b, "alg")
		b = cborHead(b, 1, -1-coseES256)
		b = cborText(b, "sig")
		b = cborBytes(b, sig)
		b = cborText(b, "x5c")
		b = cborHead(b, 4, 2)
		b = cborBytes(b, v.aliasCert)
		b = cborBytes(b, ca.deviceID)
		b = cborText(b, "authData")
		return cborBytes(b, nonce)
	}
	require.Equal(t, obj, build(FormatDICE, sigDER, v.nonce))
	longHead := append([]byte{0xb8, 0x03}, obj[1:]...) // map of 3 with a one-byte argument
	for name, c := range map[string]struct {
		obj    []byte
		reason string
	}{
		"empty":            {nil, "not a dice attestation object"},
		"truncated":        {obj[:len(obj)-1], "not a dice attestation object"},
		"trailing byte":    {append(bytes.Clone(obj), 0x00), "not a dice attestation object"},
		"not shortest":     {longHead, "not a dice attestation object"},
		"other fmt":        {build("tpm", sigDER, v.nonce), "not a dice attestation object"},
		"short authData":   {build(FormatDICE, sigDER, v.nonce[:31]), "authData is 31 bytes, want 32"},
		"sig not DER":      {build(FormatDICE, v.aliasSig, v.nonce), "sig is not an ES256 DER signature"},
		"sig with a tail":  {build(FormatDICE, append(bytes.Clone(sigDER), 0x00), v.nonce), "sig is not an ES256 DER signature"},
		"sig over 256 bit": {build(FormatDICE, mustDER(t, new(big.Int).Lsh(big.NewInt(1), 256), big.NewInt(1)), v.nonce), "sig is not an ES256 DER signature"},
		"sig of zero":      {build(FormatDICE, mustDER(t, big.NewInt(0), big.NewInt(1)), v.nonce), "sig is not an ES256 DER signature"},
	} {
		_, err := ParseDICEAttestationObject(c.obj)
		var ee *EvidenceError
		require.ErrorAs(t, err, &ee, name)
		require.Equal(t, c.reason, ee.Reason, name)
	}
}

func mustDER(t *testing.T, r, s *big.Int) []byte {
	t.Helper()
	der, err := asn1.Marshal(struct{ R, S *big.Int }{r, s})
	require.NoError(t, err)
	return der
}

func TestDICEAttestationObjectRefusesBadInputs(t *testing.T) {
	v := loadDICEVectors(t)
	ca := newDICECA(t, v)
	for name, c := range map[string]struct {
		alias, devID, nonce, sig []byte
		reason                   string
	}{
		"short nonce":     {v.aliasCert, ca.deviceID, v.nonce[:31], v.aliasSig, "nonce is 31 bytes, want 32"},
		"long nonce":      {v.aliasCert, ca.deviceID, append(bytes.Clone(v.nonce), 0x00), v.aliasSig, "nonce is 33 bytes, want 32"},
		"short signature": {v.aliasCert, ca.deviceID, v.nonce, v.aliasSig[:63], "signature is 63 bytes, want r||s of 64"},
		"long signature":  {v.aliasCert, ca.deviceID, v.nonce, append(bytes.Clone(v.aliasSig), 0x00), "signature is 65 bytes, want r||s of 64"},
		"no alias":        {nil, ca.deviceID, v.nonce, v.aliasSig, "empty certificate"},
		"no DeviceID":     {v.aliasCert, nil, v.nonce, v.aliasSig, "empty certificate"},
	} {
		_, err := DICEAttestationObject(c.alias, c.devID, c.nonce, c.sig)
		var ee *EvidenceError
		require.ErrorAs(t, err, &ee, name)
		require.Equal(t, c.reason, ee.Reason, name)
	}
}

func TestCBORHeadIsTheShortestForm(t *testing.T) {
	for arg, want := range map[uint64]string{
		23: "17", 24: "1818", 0xff: "18ff", 0x100: "190100",
		0x10000: "1a00010000", 0x100000000: "1b0000000100000000",
	} {
		require.Equal(t, want, hex.EncodeToString(cborHead(nil, 0, arg)), "arg %d", arg)
	}
}
