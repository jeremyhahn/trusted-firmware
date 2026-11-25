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

// diceCA plays the manufacturer: a root, and the DeviceID certificate it
// issues over the service's DeviceID public key.
type diceCA struct {
	roots    *x509.CertPool
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
	pool := x509.NewCertPool()
	pool.AddCert(root)
	return diceCA{roots: pool, deviceID: devDER}
}

func diceObject(t *testing.T, v diceVectors, ca diceCA) []byte {
	t.Helper()
	obj, err := DICEAttestationObject(v.aliasCert, ca.deviceID, v.nonce, v.aliasSig)
	require.NoError(t, err)
	return obj
}

func TestDICEAttestationObjectIsTheDiceFormat(t *testing.T) {
	v := loadDICEVectors(t)
	ca := newDICECA(t, v)
	obj := diceObject(t, v, ca)
	// {"fmt": "dice", "attStmt": {"alg": -7, ...
	require.True(t, bytes.HasPrefix(obj, []byte("\xa3\x63fmt\x64dice\x67attStmt\xa3\x63alg\x26\x63sig")))
	require.True(t, bytes.HasSuffix(obj, append([]byte("\x68authData\x58\x20"), v.nonce...)))
}

// TestDICEAttestationObjectRoundTrips: the object built from the secure DICE
// service's vectors parses back to the same alias certificate, DeviceID
// certificate, nonce and signature (the signature as ES256 DER).
func TestDICEAttestationObjectRoundTrips(t *testing.T) {
	v := loadDICEVectors(t)
	ca := newDICECA(t, v)
	st, err := decodeDICEObject(diceObject(t, v, ca))
	require.NoError(t, err)
	require.Equal(t, v.aliasCert, st.x5c[0])
	require.Equal(t, ca.deviceID, st.x5c[1])
	require.Equal(t, v.nonce, st.authData)
	var rs struct{ R, S *big.Int }
	rest, err := asn1.Unmarshal(st.sig, &rs)
	require.NoError(t, err)
	require.Empty(t, rest)
	require.Equal(t, v.aliasSig, append(rs.R.FillBytes(make([]byte, 32)), rs.S.FillBytes(make([]byte, 32))...))
}

// TestDecodeDICEObjectRefusals: decodeDICEObject reads only what
// DICEAttestationObject writes.
func TestDecodeDICEObjectRefusals(t *testing.T) {
	v := loadDICEVectors(t)
	good := diceObject(t, v, newDICECA(t, v))
	for name, obj := range map[string][]byte{
		"another fmt":               bytes.Replace(good, []byte("dice"), []byte("tpm\x00"), 1),
		"alg EdDSA":                 bytes.Replace(good, []byte("\x63alg\x26"), []byte("\x63alg\x27"), 1),
		"trailing byte":             append(bytes.Clone(good), 0x00),
		"truncated":                 good[:len(good)-1],
		"long-form map head":        append([]byte{0xb8, 0x03}, good[1:]...),
		"long-form authData length": append(append(bytes.Clone(good[:len(good)-34]), 0x59, 0x00, 0x20), v.nonce...),
	} {
		_, err := decodeDICEObject(obj)
		require.Error(t, err, name)
	}
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
