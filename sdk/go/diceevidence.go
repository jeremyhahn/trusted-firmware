package tfw

import (
	"bytes"
	"crypto/ecdsa"
	"crypto/sha256"
	"crypto/x509"
	"encoding/asn1"
	"encoding/binary"
	"errors"
	"fmt"
	"math/big"
)

// FormatDICE is the fmt of a device-attest-01 attestation object whose
// statement is a TCG DICE certificate chain (draft-ietf-acme-device-attest-10,
// the name go-xkms's device-attest-01 and xkms/avr route on).
const FormatDICE = "dice"

// coseES256 is the attStmt alg: COSE ES256.
const coseES256 = -7

// diceUEIDLen is the length of the UEID the device writes: the RFC 9711
// 4.2.1.1 type byte 0x01 (RAND), then the 16-byte device id.
const diceUEIDLen = 1 + 16

// DICEEvidence is what VerifyDICEAttestation proved: the chain it checked and
// the alias certificate's claims.
type DICEEvidence struct {
	Alias    *x509.Certificate // x5c[0], the alias key's certificate
	DeviceID *x509.Certificate // x5c[1], under one of the roots
	UEID     []byte            // claim 256
	FWHash   []byte            // claim 273's SHA-256 file hash, the TCB measurement
}

// DICEAttestationObject assembles the device-attest-01 "dice" attestation
// object from the secure DICE service's outputs (docs/plans/590c.md):
//
//	{"fmt": "dice",
//	 "attStmt": {"alg": -7, "sig": ES256 DER, "x5c": [aliasCert, deviceIDCert]},
//	 "authData": nonce}
//
// in deterministic CBOR (RFC 8949 section 4.2.1). sig is the alias key's r||s
// over SHA-256(nonce), tfw_dice_alias_sign's answer; aliasCert is the alias
// certificate the service built for the same nonce.
func DICEAttestationObject(aliasCert, deviceIDCert, nonce, sig []byte) ([]byte, error) {
	if len(nonce) != AliasNonceLen {
		return nil, evidenceErr(fmt.Sprintf("nonce is %d bytes, want %d", len(nonce), AliasNonceLen))
	}
	if len(sig) != 64 {
		return nil, evidenceErr(fmt.Sprintf("signature is %d bytes, want r||s of 64", len(sig)))
	}
	if len(aliasCert) == 0 || len(deviceIDCert) == 0 {
		return nil, evidenceErr("empty certificate")
	}
	der, err := asn1.Marshal(struct{ R, S *big.Int }{
		new(big.Int).SetBytes(sig[:32]), new(big.Int).SetBytes(sig[32:]),
	})
	if err != nil {
		return nil, err
	}
	var b []byte
	b = cborHead(b, 5, 3)
	b = cborText(b, "fmt")
	b = cborText(b, FormatDICE)
	b = cborText(b, "attStmt")
	b = cborHead(b, 5, 3)
	b = cborText(b, "alg")
	b = cborHead(b, 1, -1-coseES256)
	b = cborText(b, "sig")
	b = cborBytes(b, der)
	b = cborText(b, "x5c")
	b = cborHead(b, 4, 2)
	b = cborBytes(b, aliasCert)
	b = cborBytes(b, deviceIDCert)
	b = cborText(b, "authData")
	b = cborBytes(b, nonce)
	return b, nil
}

// VerifyDICEAttestation checks a "dice" attestation object the way
// device-attest-01's dice verifier does, with the standard library (sdk/go
// imports nothing of the stack; docs/plans/590c.md names the go-xkms checks
// each step mirrors):
//
//   - fmt is "dice" and alg is ES256;
//   - x5c[1], the DeviceID certificate, chains to roots, which must be
//     non-nil (nil is refused, never the host's system roots);
//   - x5c[0], the alias certificate, is signed by x5c[1]'s key;
//   - the alias certificate's eat_nonce and authData are nonce;
//   - sig verifies under the alias key over SHA-256(authData);
//   - the UEID (claim 256) is present, and is the type byte 0x01 then the
//     16-byte device id;
//   - the TCB measurement (claim 273's file hash) is present, and equals
//     measurement unless that is nil.
func VerifyDICEAttestation(obj, nonce []byte, roots *x509.CertPool, measurement []byte) (*DICEEvidence, error) {
	if roots == nil {
		return nil, evidenceErr("no trusted roots for the DeviceID certificate")
	}
	st, err := decodeDICEObject(obj)
	if err != nil {
		return nil, errors.Join(evidenceErr("not a dice attestation object"), err)
	}
	if len(nonce) != AliasNonceLen || !bytes.Equal(st.authData, nonce) {
		return nil, evidenceErr("authData is not the challenge nonce")
	}
	alias, err := x509.ParseCertificate(st.x5c[0])
	if err != nil {
		return nil, errors.Join(evidenceErr("x5c[0] is not a certificate"), err)
	}
	devID, err := x509.ParseCertificate(st.x5c[1])
	if err != nil {
		return nil, errors.Join(evidenceErr("x5c[1] is not a certificate"), err)
	}
	if _, err = devID.Verify(x509.VerifyOptions{Roots: roots, KeyUsages: []x509.ExtKeyUsage{x509.ExtKeyUsageAny}}); err != nil {
		return nil, errors.Join(evidenceErr("the DeviceID certificate does not chain to a root"), err)
	}
	if err = alias.CheckSignatureFrom(devID); err != nil {
		return nil, errors.Join(evidenceErr("the alias certificate is not signed by the DeviceID key"), err)
	}
	ev := &DICEEvidence{Alias: alias, DeviceID: devID}
	var gotNonce []byte
	if gotNonce, ev.UEID, ev.FWHash, err = aliasClaims(st.x5c[0]); err != nil {
		return nil, err
	}
	if !bytes.Equal(gotNonce, nonce) {
		return nil, evidenceErr("the alias certificate's eat_nonce is not the challenge nonce")
	}
	key, ok := alias.PublicKey.(*ecdsa.PublicKey)
	if !ok {
		return nil, evidenceErr("the alias key is not ECDSA")
	}
	digest := sha256.Sum256(st.authData)
	if !ecdsa.VerifyASN1(key, digest[:], st.sig) {
		return nil, evidenceErr("sig does not verify under the alias key")
	}
	if measurement != nil && !bytes.Equal(ev.FWHash, measurement) {
		return nil, evidenceErr("the TCB measurement is not the reference value")
	}
	return ev, nil
}

// aliasClaims reads eat_nonce (10), ueid (256) and the measurement (273,
// [[258, CoSWID]] with the file hash at evidence/file/hash) from the alias
// certificate's UCCS, and refuses a claims set whose ueid is absent, not a
// byte string, or not the type byte 0x01 then the 16-byte device id.
func aliasClaims(der []byte) (nonce, ueid, fwHash []byte, err error) {
	uccs, err := AliasUCCS(der)
	if err != nil {
		return nil, nil, nil, err
	}
	v, err := cborDecode(uccs)
	if err != nil {
		return nil, nil, nil, errors.Join(evidenceErr("the UCCS is not CBOR"), err)
	}
	claims, _ := v.(map[uint64]any)
	nonce, _ = claims[10].([]byte)
	ueid, _ = claims[256].([]byte)
	if len(ueid) == 0 {
		return nil, nil, nil, evidenceErr("no UEID (claim 256)")
	}
	if len(ueid) != diceUEIDLen || ueid[0] != 0x01 {
		return nil, nil, nil, evidenceErr("the UEID is not 0x01 and a 16-byte device id")
	}
	meas, _ := claims[273].([]any)
	if len(meas) != 1 {
		return nil, nil, nil, evidenceErr("no TCB measurement (claim 273)")
	}
	pair, _ := meas[0].([]any)
	if len(pair) != 2 || pair[0] != uint64(258) {
		return nil, nil, nil, evidenceErr("the measurement is not a CoSWID")
	}
	raw, _ := pair[1].([]byte)
	if v, err = cborDecode(raw); err != nil {
		return nil, nil, nil, errors.Join(evidenceErr("the CoSWID is not CBOR"), err)
	}
	coswid, _ := v.(map[uint64]any)
	evidence, _ := coswid[3].(map[uint64]any)
	file, _ := evidence[17].(map[uint64]any)
	hash, _ := file[7].([]any)
	if len(hash) != 2 || hash[0] != uint64(1) {
		return nil, nil, nil, evidenceErr("the CoSWID carries no SHA-256 file hash")
	}
	fwHash, _ = hash[1].([]byte)
	if len(fwHash) != sha256.Size {
		return nil, nil, nil, evidenceErr("the file hash is not 32 bytes")
	}
	return nonce, ueid, fwHash, nil
}

type diceStatement struct {
	sig, authData []byte
	x5c           [2][]byte
}

// decodeDICEObject reads the object DICEAttestationObject writes, and only
// that: the shortest heads, the keys in deterministic order, x5c of exactly
// two, nothing after.
func decodeDICEObject(b []byte) (*diceStatement, error) {
	r := &cborReader{b: b}
	var st diceStatement
	if err := r.expectHead(5, 3); err != nil {
		return nil, err
	}
	if err := r.expectText("fmt"); err != nil {
		return nil, err
	}
	if err := r.expectText(FormatDICE); err != nil {
		return nil, err
	}
	if err := r.expectText("attStmt"); err != nil {
		return nil, err
	}
	if err := r.expectHead(5, 3); err != nil {
		return nil, err
	}
	if err := r.expectText("alg"); err != nil {
		return nil, err
	}
	if err := r.expectHead(1, -1-coseES256); err != nil {
		return nil, err
	}
	if err := r.expectText("sig"); err != nil {
		return nil, err
	}
	var err error
	if st.sig, err = r.bstr(); err != nil {
		return nil, err
	}
	if err = r.expectText("x5c"); err != nil {
		return nil, err
	}
	if err = r.expectHead(4, 2); err != nil {
		return nil, err
	}
	for i := range st.x5c {
		if st.x5c[i], err = r.bstr(); err != nil {
			return nil, err
		}
	}
	if err = r.expectText("authData"); err != nil {
		return nil, err
	}
	if st.authData, err = r.bstr(); err != nil {
		return nil, err
	}
	if len(r.b) != 0 {
		return nil, fmt.Errorf("%d trailing bytes after the object", len(r.b))
	}
	return &st, nil
}

func (r *cborReader) expectHead(major byte, arg uint64) error {
	m, a, err := r.shortestHead()
	if err != nil {
		return err
	}
	if m != major || a != arg {
		return fmt.Errorf("head is major %d arg %d, want major %d arg %d", m, a, major, arg)
	}
	return nil
}

// shortestHead reads a head and refuses it unless its argument has the
// shortest form, the one cborHead writes (RFC 8949 4.2.1).
func (r *cborReader) shortestHead() (byte, uint64, error) {
	left := len(r.b)
	m, a, err := r.head()
	if err != nil {
		return 0, 0, err
	}
	if left-len(r.b) != len(cborHead(nil, m, a)) {
		return 0, 0, fmt.Errorf("head of major %d arg %d is not the shortest form", m, a)
	}
	return m, a, nil
}

func (r *cborReader) str(major byte) ([]byte, error) {
	m, a, err := r.shortestHead()
	if err != nil {
		return nil, err
	}
	if m != major {
		return nil, fmt.Errorf("major type %d, want %d", m, major)
	}
	n, err := r.count(a)
	if err != nil {
		return nil, err
	}
	s := r.b[:n:n]
	r.b = r.b[n:]
	return s, nil
}

func (r *cborReader) bstr() ([]byte, error) { return r.str(2) }

func (r *cborReader) expectText(want string) error {
	s, err := r.str(3)
	if err != nil {
		return err
	}
	if string(s) != want {
		return fmt.Errorf("text %q, want %q", s, want)
	}
	return nil
}

// cborHead appends a head with the shortest argument (RFC 8949 4.2.1).
func cborHead(b []byte, major byte, arg uint64) []byte {
	m := major << 5
	var be [8]byte
	binary.BigEndian.PutUint64(be[:], arg)
	switch {
	case arg < 24:
		return append(b, m|be[7])
	case arg <= 0xff:
		return append(b, m|24, be[7])
	case arg <= 0xffff:
		return append(append(b, m|25), be[6:]...)
	case arg <= 0xffffffff:
		return append(append(b, m|26), be[4:]...)
	default:
		return append(append(b, m|27), be[:]...)
	}
}

func cborText(b []byte, s string) []byte {
	return append(cborHead(b, 3, uint64(len(s))), s...)
}

func cborBytes(b, s []byte) []byte {
	return append(cborHead(b, 2, uint64(len(s))), s...)
}
