package tfw

import (
	"bufio"
	"bytes"
	"crypto/ecdsa"
	"crypto/ed25519"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/sha256"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/hex"
	"math/big"
	"os"
	"strings"
	"testing"
	"time"

	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/vectors"
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

func TestDICEAttestationFromTheSecureServiceVerifies(t *testing.T) {
	v := loadDICEVectors(t)
	ca := newDICECA(t, v)
	ev, err := VerifyDICEAttestation(diceObject(t, v, ca), v.nonce, ca.roots, v.fwHash)
	require.NoError(t, err)
	// RFC 9711 section 4.2.1.1: a leading type byte, 0x01 for a random identifier.
	require.Equal(t, append([]byte{0x01}, v.ueid...), ev.UEID)
	require.Equal(t, v.fwHash, ev.FWHash)
	require.Equal(t, ca.deviceID, ev.DeviceID.Raw)
	require.Equal(t, v.aliasCert, ev.Alias.Raw)

	// No reference measurement: the measurement must still be present.
	_, err = VerifyDICEAttestation(diceObject(t, v, ca), v.nonce, ca.roots, nil)
	require.NoError(t, err)
}

func TestDICEAttestationObjectIsTheDiceFormat(t *testing.T) {
	v := loadDICEVectors(t)
	ca := newDICECA(t, v)
	obj := diceObject(t, v, ca)
	// {"fmt": "dice", "attStmt": {"alg": -7, ...
	require.True(t, bytes.HasPrefix(obj, []byte("\xa3\x63fmt\x64dice\x67attStmt\xa3\x63alg\x26\x63sig")))
	require.True(t, bytes.HasSuffix(obj, append([]byte("\x68authData\x58\x20"), v.nonce...)))
}

func TestDICEAttestationRefusals(t *testing.T) {
	v := loadDICEVectors(t)
	ca := newDICECA(t, v)
	good := diceObject(t, v, ca)

	otherKey, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	require.NoError(t, err)
	digest := sha256.Sum256(v.nonce)
	r, s, err := ecdsa.Sign(rand.Reader, otherKey, digest[:])
	require.NoError(t, err)
	otherSig := append(r.FillBytes(make([]byte, 32)), s.FillBytes(make([]byte, 32))...)

	// A byte of the alias certificate's measurement (claim 273) flipped.
	tampered := bytes.Clone(v.aliasCert)
	at := bytes.Index(tampered, v.fwHash)
	require.GreaterOrEqual(t, at, 0, "the alias certificate carries the FWHash")
	tampered[at] ^= 0x01

	wrongNonce := bytes.Clone(v.nonce)
	wrongNonce[0] ^= 0x01
	wrongMeasurement := bytes.Clone(v.fwHash)
	wrongMeasurement[31] ^= 0x01

	other := newDICECA(t, v)
	build := func(alias, devID, sig []byte) []byte {
		obj, err := DICEAttestationObject(alias, devID, v.nonce, sig)
		require.NoError(t, err)
		return obj
	}

	cases := []struct {
		name        string
		obj, nonce  []byte
		roots       *x509.CertPool
		measurement []byte
	}{
		{"wrong nonce", good, wrongNonce, ca.roots, v.fwHash},
		{"swapped chain order", build(ca.deviceID, v.aliasCert, v.aliasSig), v.nonce, ca.roots, v.fwHash},
		{"tampered measurement", build(tampered, ca.deviceID, v.aliasSig), v.nonce, ca.roots, v.fwHash},
		{"wrong reference measurement", good, v.nonce, ca.roots, wrongMeasurement},
		{"signature by another key", build(v.aliasCert, ca.deviceID, otherSig), v.nonce, ca.roots, v.fwHash},
		{"DeviceID under an unknown root", good, v.nonce, other.roots, v.fwHash},
		{"another fmt", bytes.Replace(good, []byte("dice"), []byte("tpm\x00"), 1), v.nonce, ca.roots, v.fwHash},
		{"trailing byte", append(bytes.Clone(good), 0x00), v.nonce, ca.roots, v.fwHash},
		{"truncated", good[:len(good)-1], v.nonce, ca.roots, v.fwHash},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			_, err := VerifyDICEAttestation(c.obj, c.nonce, c.roots, c.measurement)
			require.Error(t, err)
			var ee *EvidenceError
			require.ErrorAs(t, err, &ee)
		})
	}
}

// selfDICEChain plays both the manufacturer and the device: a root, a
// DeviceID certificate under it, and an alias certificate the DeviceID key
// signs over aliasPub (a fresh P-256 key when nil) carrying uccs as its
// tcg-dice-UCCS-evidence extension (none when uccs is nil). The signature
// over the nonce is by a P-256 alias key, so a chain built over a valid
// UCCS gets past the alias signature to the claims checks.
func selfDICEChain(t *testing.T, uccs []byte, aliasPub any) (obj, nonce []byte, roots *x509.CertPool) {
	t.Helper()
	now := time.Now()
	rootKey, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	require.NoError(t, err)
	rootTmpl := &x509.Certificate{
		SerialNumber: big.NewInt(1), Subject: pkix.Name{CommonName: "test manufacturer root"},
		NotBefore: now.Add(-time.Hour), NotAfter: now.Add(time.Hour),
		IsCA: true, BasicConstraintsValid: true, KeyUsage: x509.KeyUsageCertSign,
	}
	rootDER, err := x509.CreateCertificate(rand.Reader, rootTmpl, rootTmpl, &rootKey.PublicKey, rootKey)
	require.NoError(t, err)
	root, err := x509.ParseCertificate(rootDER)
	require.NoError(t, err)

	devKey, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	require.NoError(t, err)
	devTmpl := &x509.Certificate{
		SerialNumber: big.NewInt(2), Subject: pkix.Name{CommonName: "tfw DeviceID"},
		NotBefore: now.Add(-time.Hour), NotAfter: now.Add(time.Hour),
		IsCA: true, BasicConstraintsValid: true, KeyUsage: x509.KeyUsageCertSign,
	}
	devDER, err := x509.CreateCertificate(rand.Reader, devTmpl, root, &devKey.PublicKey, rootKey)
	require.NoError(t, err)
	devID, err := x509.ParseCertificate(devDER)
	require.NoError(t, err)

	aliasKey, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	require.NoError(t, err)
	if aliasPub == nil {
		aliasPub = &aliasKey.PublicKey
	}
	aliasTmpl := &x509.Certificate{
		SerialNumber: big.NewInt(3), Subject: pkix.Name{CommonName: "tfw Alias"},
		NotBefore: now.Add(-time.Hour), NotAfter: now.Add(time.Hour),
	}
	if uccs != nil {
		aliasTmpl.ExtraExtensions = []pkix.Extension{{Id: oidUCCSEvidenceLiteral, Critical: true, Value: uccsEvidence(t, uccs)}}
	}
	aliasDER, err := x509.CreateCertificate(rand.Reader, aliasTmpl, devID, aliasPub, devKey)
	require.NoError(t, err)

	nonce = []byte(vectors.AppEvWithUccs)[4:36]
	digest := sha256.Sum256(nonce)
	r, s, err := ecdsa.Sign(rand.Reader, aliasKey, digest[:])
	require.NoError(t, err)
	sig := append(r.FillBytes(make([]byte, 32)), s.FillBytes(make([]byte, 32))...)
	obj, err = DICEAttestationObject(aliasDER, devDER, nonce, sig)
	require.NoError(t, err)
	pool := x509.NewCertPool()
	pool.AddCert(root)
	return obj, nonce, pool
}

// TestDICEAttestationClaimsRefusals reaches the checks after the alias
// signature: each case is one change from a chain that verifies.
func TestDICEAttestationClaimsRefusals(t *testing.T) {
	ex := []byte(vectors.AppEvExample)

	obj, nonce, roots := selfDICEChain(t, build(t, ex, nil), nil)
	ev, err := VerifyDICEAttestation(obj, nonce, roots, ex)
	require.NoError(t, err)
	require.Equal(t, ex, ev.FWHash)

	edPub, _, err := ed25519.GenerateKey(rand.Reader)
	require.NoError(t, err)
	otherNonce := bytes.Clone([]byte(vectors.AppEvWithUccs)[4:36])
	otherNonce[0] ^= 0x01

	cases := []struct {
		name     string
		uccs     []byte
		aliasPub any
		reason   string
	}{
		{"eat_nonce of another challenge", build(t, ex, func(layer string, p *parts) {
			if layer == "claims" {
				p.claims[0].v = otherNonce
			}
		}), nil, "the alias certificate's eat_nonce is not the challenge nonce"},
		{"Ed25519 alias key", build(t, ex, nil), edPub, "the alias key is not ECDSA"},
		{"UCCS not CBOR", []byte{0xff}, nil, "the UCCS is not CBOR"},
		{"no claim 273", build(t, ex, func(layer string, p *parts) {
			if layer == "claims" {
				p.claims = p.claims[:2]
			}
		}), nil, "no TCB measurement (claim 273)"},
		{"content type 257", build(t, ex, func(layer string, p *parts) {
			if layer == "content" {
				p.ct = uint64(257)
			}
		}), nil, "the measurement is not a CoSWID"},
		{"CoSWID not CBOR", build(t, ex, func(layer string, p *parts) {
			if layer == "content" {
				p.coswidBytes = []byte{0xff}
			}
		}), nil, "the CoSWID is not CBOR"},
		{"hash algorithm 7", build(t, ex, func(layer string, p *parts) {
			if layer == "file" {
				p.file[0].v = []any{uint64(7), ex}
			}
		}), nil, "the CoSWID carries no SHA-256 file hash"},
		{"31-byte hash", build(t, ex[:31], nil), nil, "the file hash is not 32 bytes"},
		{"no UCCS extension", nil, nil, "no tcg-dice-UCCS-evidence extension"},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			obj, nonce, roots := selfDICEChain(t, c.uccs, c.aliasPub)
			_, err := VerifyDICEAttestation(obj, nonce, roots, ex)
			var ee *EvidenceError
			require.ErrorAs(t, err, &ee)
			require.Equal(t, c.reason, ee.Reason)
		})
	}
}

func TestDICEAttestationObjectRefusesBadInputs(t *testing.T) {
	v := loadDICEVectors(t)
	ca := newDICECA(t, v)
	for name, args := range map[string][4][]byte{
		"short nonce":     {v.aliasCert, ca.deviceID, v.nonce[:31], v.aliasSig},
		"short signature": {v.aliasCert, ca.deviceID, v.nonce, v.aliasSig[:63]},
		"no alias":        {nil, ca.deviceID, v.nonce, v.aliasSig},
		"no DeviceID":     {v.aliasCert, nil, v.nonce, v.aliasSig},
	} {
		_, err := DICEAttestationObject(args[0], args[1], args[2], args[3])
		var ee *EvidenceError
		require.ErrorAs(t, err, &ee, name)
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
