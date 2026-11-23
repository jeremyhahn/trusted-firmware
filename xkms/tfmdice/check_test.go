// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfmdice

import (
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/x509"
	"crypto/x509/pkix"
	"math/big"
	"strings"
	"testing"

	"github.com/jeremyhahn/go-xkms/pkg/attestation/dice"
)

// The fixed values the QEMU gate's non-secure check uses.
func seq(start byte) []byte {
	b := make([]byte, 32)
	for i := range b {
		b[i] = start + byte(i)
	}
	return b
}

func pub64(t *testing.T, k *ecdsa.PublicKey) []byte {
	t.Helper()
	b, err := k.Bytes()
	if err != nil {
		t.Fatal(err)
	}
	return b[1:]
}

func genKey(t *testing.T) *ecdsa.PrivateKey {
	t.Helper()
	k, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	return k
}

// aliasCert is what the partition answers: the alias key certified by the
// signer (the DeviceID key), the nonce in the UCCS evidence's eat_nonce.
func aliasCert(t *testing.T, signer *ecdsa.PrivateKey, alias *ecdsa.PublicKey, nonce []byte) []byte {
	t.Helper()
	ext, err := dice.CreateUccsEvidenceExtension(&dice.ClaimsSet{Nonce: nonce})
	if err != nil {
		t.Fatal(err)
	}
	tmpl := &x509.Certificate{
		SerialNumber:    big.NewInt(1),
		Subject:         pkix.Name{CommonName: "alias"},
		ExtraExtensions: []pkix.Extension{ext},
	}
	der, err := x509.CreateCertificate(rand.Reader, tmpl, tmpl, alias, signer)
	if err != nil {
		t.Fatal(err)
	}
	return der
}

func rs(t *testing.T, k *ecdsa.PrivateKey, digest []byte) []byte {
	t.Helper()
	r, s, err := ecdsa.Sign(rand.Reader, k, digest)
	if err != nil {
		t.Fatal(err)
	}
	out := make([]byte, 64)
	r.FillBytes(out[:32])
	s.FillBytes(out[32:])
	return out
}

type fixture struct {
	dev, alias *ecdsa.PrivateKey
	in         Input
}

func newFixture(t *testing.T) *fixture {
	dev, alias := genKey(t), genKey(t)
	nonce, digest := seq(0x00), seq(0x20)
	return &fixture{dev: dev, alias: alias, in: Input{
		DeviceIDPub: pub64(t, &dev.PublicKey),
		AliasPub:    pub64(t, &alias.PublicKey),
		CertDER:     aliasCert(t, dev, &alias.PublicKey, nonce),
		Nonce:       nonce,
		Digest:      digest,
		Sig:         rs(t, alias, digest),
	}}
}

func TestCheckAcceptsTheDeviceEvidence(t *testing.T) {
	f := newFixture(t)
	if err := Check(f.in); err != nil {
		t.Fatalf("Check: %v", err)
	}
}

func TestCheckRefuses(t *testing.T) {
	cases := []struct {
		name string
		edit func(t *testing.T, f *fixture)
		want string
	}{
		{"cert signed by another key", func(t *testing.T, f *fixture) {
			f.in.CertDER = aliasCert(t, genKey(t), &f.alias.PublicKey, f.in.Nonce)
		}, "certificate signature"},
		{"wrong nonce", func(t *testing.T, f *fixture) {
			f.in.CertDER = aliasCert(t, f.dev, &f.alias.PublicKey, seq(0x40))
		}, "nonce"},
		{"alias-pub mismatch", func(t *testing.T, f *fixture) {
			f.in.AliasPub = pub64(t, &genKey(t).PublicKey)
		}, "alias public key"},
		{"bad signature", func(t *testing.T, f *fixture) {
			f.in.Sig = rs(t, f.alias, seq(0x60))
		}, "alias signature"},
		{"signature by the DeviceID key", func(t *testing.T, f *fixture) {
			f.in.Sig = rs(t, f.dev, f.in.Digest)
		}, "alias signature"},
		{"truncated DER", func(t *testing.T, f *fixture) {
			f.in.CertDER = f.in.CertDER[:len(f.in.CertDER)-1]
		}, "certificate"},
		{"short DeviceID key", func(t *testing.T, f *fixture) {
			f.in.DeviceIDPub = f.in.DeviceIDPub[:63]
		}, "deviceid public key"},
		{"DeviceID key off the curve", func(t *testing.T, f *fixture) {
			f.in.DeviceIDPub = make([]byte, 64)
		}, "deviceid public key"},
		{"short signature", func(t *testing.T, f *fixture) {
			f.in.Sig = f.in.Sig[:63]
		}, "alias signature"},
		{"short nonce", func(t *testing.T, f *fixture) {
			f.in.Nonce = f.in.Nonce[:31]
		}, "nonce"},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			f := newFixture(t)
			c.edit(t, f)
			err := Check(f.in)
			if err == nil {
				t.Fatal("Check accepted it")
			}
			if !strings.Contains(err.Error(), c.want) {
				t.Fatalf("error %q does not name %q", err, c.want)
			}
		})
	}
}
