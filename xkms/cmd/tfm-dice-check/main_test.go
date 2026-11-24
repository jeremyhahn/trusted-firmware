// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package main

import (
	"bytes"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/hex"
	"math/big"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/jeremyhahn/go-xkms/pkg/attestation/dice"
)

func seq(start byte) []byte {
	b := make([]byte, 32)
	for i := range b {
		b[i] = start + byte(i)
	}
	return b
}

func genKey(t *testing.T) *ecdsa.PrivateKey {
	t.Helper()
	k, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	return k
}

func pubHex(t *testing.T, k *ecdsa.PublicKey) string {
	t.Helper()
	b, err := k.Bytes()
	if err != nil {
		t.Fatal(err)
	}
	return hex.EncodeToString(b[1:])
}

// evidence is a valid set of flags: P-256 DeviceID and alias keys, an alias
// certificate the DeviceID key signed carrying the nonce in its UCCS evidence
// extension, and an r||s alias signature over the digest.
type evidence struct {
	devPub, aliasPub, cert, nonce, digest, sig string
}

func (e evidence) args() []string {
	return []string{
		"-deviceid-pub", e.devPub,
		"-alias-pub", e.aliasPub,
		"-cert", e.cert,
		"-nonce", e.nonce,
		"-digest", e.digest,
		"-sig", e.sig,
	}
}

func newEvidence(t *testing.T) evidence {
	t.Helper()
	dev, alias := genKey(t), genKey(t)
	nonce, digest := seq(0x00), seq(0x20)
	ext, err := dice.CreateUccsEvidenceExtension(&dice.ClaimsSet{Nonce: nonce})
	if err != nil {
		t.Fatal(err)
	}
	tmpl := &x509.Certificate{
		SerialNumber:    big.NewInt(1),
		Subject:         pkix.Name{CommonName: "alias"},
		ExtraExtensions: []pkix.Extension{ext},
	}
	der, err := x509.CreateCertificate(rand.Reader, tmpl, tmpl, &alias.PublicKey, dev)
	if err != nil {
		t.Fatal(err)
	}
	cert := filepath.Join(t.TempDir(), "alias.der")
	if err := os.WriteFile(cert, der, 0o600); err != nil {
		t.Fatal(err)
	}
	r, s, err := ecdsa.Sign(rand.Reader, alias, digest)
	if err != nil {
		t.Fatal(err)
	}
	sig := make([]byte, 64)
	r.FillBytes(sig[:32])
	s.FillBytes(sig[32:])
	return evidence{
		devPub:   pubHex(t, &dev.PublicKey),
		aliasPub: pubHex(t, &alias.PublicKey),
		cert:     cert,
		nonce:    hex.EncodeToString(nonce),
		digest:   hex.EncodeToString(digest),
		sig:      hex.EncodeToString(sig),
	}
}

func TestRunAcceptsTheEvidence(t *testing.T) {
	var stdout, stderr bytes.Buffer
	if code := run(newEvidence(t).args(), &stdout, &stderr); code != 0 {
		t.Fatalf("exit %d, stderr %q", code, stderr.String())
	}
	if got := stdout.String(); got != "tfm-dice-check: alias evidence ok\n" {
		t.Fatalf("stdout %q", got)
	}
	if stderr.Len() != 0 {
		t.Fatalf("stderr %q", stderr.String())
	}
}

func TestRunRefuses(t *testing.T) {
	cases := []struct {
		name string
		edit func(e *evidence) []string
		code int
		want string
	}{
		{"empty deviceid-pub", func(e *evidence) []string { e.devPub = ""; return nil }, 2, "-deviceid-pub: want non-empty hex"},
		{"empty alias-pub", func(e *evidence) []string { e.aliasPub = ""; return nil }, 2, "-alias-pub: want non-empty hex"},
		{"empty nonce", func(e *evidence) []string { e.nonce = ""; return nil }, 2, "-nonce: want non-empty hex"},
		{"empty digest", func(e *evidence) []string { e.digest = ""; return nil }, 2, "-digest: want non-empty hex"},
		{"empty sig", func(e *evidence) []string { e.sig = ""; return nil }, 2, "-sig: want non-empty hex"},
		{"non-hex digest", func(e *evidence) []string { e.digest = "zz"; return nil }, 2, "-digest: want non-empty hex"},
		{"missing cert file", func(e *evidence) []string {
			e.cert = filepath.Join(filepath.Dir(e.cert), "absent.der")
			return nil
		}, 2, "tfm-dice-check: -cert:"},
		{"unknown flag", func(e *evidence) []string { return append(e.args(), "-bogus") }, 2, "-bogus"},
		{"another nonce", func(e *evidence) []string { e.nonce = hex.EncodeToString(seq(0x40)); return nil }, 1, "tfm-dice-check: tfmdice: nonce"},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			e := newEvidence(t)
			args := c.edit(&e)
			if args == nil {
				args = e.args()
			}
			var stdout, stderr bytes.Buffer
			if code := run(args, &stdout, &stderr); code != c.code {
				t.Fatalf("exit %d, want %d; stderr %q", code, c.code, stderr.String())
			}
			if !strings.Contains(stderr.String(), c.want) {
				t.Fatalf("stderr %q does not name %q", stderr.String(), c.want)
			}
			if stdout.Len() != 0 {
				t.Fatalf("stdout %q", stdout.String())
			}
		})
	}
}
