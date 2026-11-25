package tfw

import (
	"encoding/asn1"
	"encoding/binary"
	"fmt"
	"math/big"
)

// FormatDICE is the fmt of a device-attest-01 attestation object whose
// statement is a TCG DICE certificate chain (draft-ietf-acme-device-attest-10,
// the name go-xkms's device-attest-01 and xkms/avr route on).
const FormatDICE = "dice"

// coseES256 is the attStmt alg: COSE ES256.
const coseES256 = -7

// DICEAttestationObject assembles the device-attest-01 "dice" attestation
// object from the secure DICE service's outputs (#590 slice c):
//
//	{"fmt": "dice",
//	 "attStmt": {"alg": -7, "sig": ES256 DER, "x5c": [aliasCert, deviceIDCert]},
//	 "authData": nonce}
//
// in deterministic CBOR (RFC 8949 section 4.2.1). sig is the alias key's r||s
// over SHA-256(nonce), tfw_dice_alias_sign's answer; aliasCert is the alias
// certificate the service built for the same nonce.
//
// sdk/go builds this object and parses it (decodeDICEObject); it does not
// verify it. Verifying the chain, the nonce, the signature and the
// measurement is go-xkms's device-attest-01 dice verifier's (docs/SPEC.md).
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

// diceStatement is a "dice" object as decodeDICEObject reads it back.
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
