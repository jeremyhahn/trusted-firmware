// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package main

import (
	"context"
	"crypto"
	"crypto/ecdsa"
	"crypto/rand"
	"crypto/sha256"
	"crypto/x509"
	"encoding/pem"
	"errors"
	"io"
	"os"
	"path/filepath"
	"strings"
	"testing"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
)

// deadConn is a link that has gone away: every write fails, so every verb
// that reaches the wire must come back with the SDK's LinkError rather than
// a usage error, a panic or a success.
type deadConn struct{}

var errLinkGone = errors.New("link gone")

func (deadConn) Read([]byte) (int, error)  { return 0, io.EOF }
func (deadConn) Write([]byte) (int, error) { return 0, errLinkGone }

func TestEveryVerbReportsALinkFailure(t *testing.T) {
	dev := tfw.New(deadConn{}, tfw.WithBootWait(0))
	key := writeTestECKey(t)
	for _, tc := range []struct {
		verb string
		args []string
	}{
		{"ping", []string{"hello"}},
		{"caps", nil},
		{"pin", []string{"3"}},
		{"sys", nil},
		{"app", nil},
		{"reset", []string{"boot"}},
		{"mode", []string{"3", "out"}},
		{"read", []string{"3"}},
		{"write", []string{"3", "1"}},
		{"timer", []string{"3", "1", "5s"}},
		{"timer-cancel", []string{"3", "apply"}},
		{"timer-query", []string{"3"}},
		{"ota-status", nil},
		{"analog", []string{"14"}},
		{"adc-ref", []string{"vcc"}},
		{"pwm", []string{"3", "128"}},
		{"tone", []string{"11", "1000"}},
		{"notone", []string{"11"}},
		{"pulse-in", []string{"2", "1", "10ms"}},
		{"pulse-capture", []string{"2", "1", "8", "10ms"}},
		{"eeprom-read", []string{"0x080", "8"}},
		{"eeprom-write", []string{"0x080", "deadbeef"}},
		{"i2c", []string{"0x20", "01", "2"}},
		{"spi", []string{"10", "0102"}},
		{"i2c-config", []string{"100000"}},
		{"spi-config", []string{"0", "1", "lsb"}},
		{"1wire-reset", []string{"4"}},
		{"1wire-write", []string{"4", "cc"}},
		{"1wire-read", []string{"4", "9"}},
		{"attest", []string{strings.Repeat("00", tfw.AttestNonceLen)}},
		{"attest-cert", []string{strings.Repeat("00", tfw.AliasNonceLen)}},
		{"auth", []string{strings.Repeat("00", tfw.SerialLen), key}},
		{"netcfg", netcfgSetArgs[1:]},
		{"netcfg", []string{"get"}},
	} {
		t.Run(tc.verb, func(t *testing.T) {
			err := run(context.Background(), dev, tc.verb, tc.args, false, io.Discard)
			var le *tfw.LinkError
			if !errors.As(err, &le) {
				t.Fatalf("%s %v over a dead link: %v, want a LinkError", tc.verb, tc.args, err)
			}
		})
	}
}

func TestMalformedArgumentsAreRefusedBeforeTheWireToo(t *testing.T) {
	for _, tc := range []struct {
		verb string
		args []string
	}{
		{"pin", nil},
		{"pin", []string{"x"}},
		{"mode", []string{"3"}},
		{"mode", []string{"x", "out"}},
		{"mode", []string{"3", "sideways"}},
		{"read", nil},
		{"read", []string{"x"}},
		{"write", []string{"3"}},
		{"write", []string{"x", "1"}},
		{"timer", []string{"3", "1"}},
		{"timer", []string{"x", "1", "5s"}},
		{"timer", []string{"3", "1", "later"}},
		{"timer-cancel", nil},
		{"timer-cancel", []string{"x"}},
		{"timer-query", nil},
		{"timer-query", []string{"x"}},
		{"ota-status", []string{"now"}},
		{"pwm", []string{"x", "1"}},
		{"tone", []string{"x", "1000"}},
		{"notone", []string{"x"}},
		{"pulse-in", []string{"x", "1", "10ms"}},
		{"pulse-capture", []string{"x", "1", "8", "10ms"}},
		{"pulse-capture", []string{"2", "1", "999", "10ms"}},
		{"eeprom-read", []string{"x", "1"}},
		{"eeprom-read", []string{"0x80", "x"}},
		{"eeprom-write", []string{"0x80"}},
		{"eeprom-write", []string{"x", "00"}},
		{"i2c", []string{"x", "01", "1"}},
		{"i2c", []string{"0x20", "01", "x"}},
		{"spi", []string{"x", "01"}},
		{"spi", []string{"10", "zz"}},
		{"i2c-config", nil},
		{"i2c-config", []string{"fast"}},
		{"spi-config", []string{"0", "1"}},
		{"spi-config", []string{"x", "1", "msb"}},
		{"spi-config", []string{"0", "x", "msb"}},
		{"spi-config", []string{"0", "1", "middle"}},
		{"1wire-reset", nil},
		{"1wire-reset", []string{"x"}},
		{"1wire-write", []string{"4"}},
		{"1wire-write", []string{"x", "cc"}},
		{"1wire-read", []string{"x", "1"}},
		{"1wire-read", []string{"4", "x"}},
		{"attest-cert", nil},
		{"auth", []string{strings.Repeat("00", tfw.SerialLen), "/nonexistent/key.pem"}},
		{"auth", []string{strings.Repeat("00", tfw.SerialLen), "$KEY", "eeprom-read"}},
	} {
		t.Run(tc.verb+" "+strings.Join(tc.args, "_"), func(t *testing.T) {
			args := tc.args
			for i, a := range args {
				if a == "$KEY" {
					args = append([]string(nil), args...)
					args[i] = writeTestECKey(t)
				}
			}
			if err := run(context.Background(), nil, tc.verb, args, false, io.Discard); err == nil {
				t.Fatalf("%s %v was accepted", tc.verb, tc.args)
			}
		})
	}
}

func TestAuthRefusesAFileOpensslCannotLoad(t *testing.T) {
	path := filepath.Join(t.TempDir(), "junk.pem")
	if err := os.WriteFile(path, []byte("not a key\n"), 0o600); err != nil {
		t.Fatal(err)
	}
	if _, err := externalECKeySigner(path); err == nil {
		t.Fatal("a file that is not a key was accepted as one")
	}
}

func TestExternalKeySignerSignsWithTheOperatorKey(t *testing.T) {
	path := writeTestECKey(t)
	signer, err := externalECKeySigner(path)
	if err != nil {
		t.Fatal(err)
	}
	digest := sha256.Sum256([]byte("auth challenge"))
	sig, err := signer.Sign(rand.Reader, digest[:], crypto.SHA256)
	if err != nil {
		t.Fatalf("signing through openssl: %v", err)
	}
	// The signer's Public() is nil (AuthResponse never calls it), so the
	// operator's public key is taken from the key file itself.
	pemBytes, err := os.ReadFile(path) // #nosec G304 -- the test's own temp file
	if err != nil {
		t.Fatal(err)
	}
	block, _ := pem.Decode(pemBytes)
	if block == nil {
		t.Fatal("the test key file holds no PEM block")
	}
	key, err := x509.ParseECPrivateKey(block.Bytes)
	if err != nil {
		t.Fatal(err)
	}
	if !ecdsa.VerifyASN1(&key.PublicKey, digest[:], sig) {
		t.Fatal("the signature does not verify under the operator key's public half")
	}
}
