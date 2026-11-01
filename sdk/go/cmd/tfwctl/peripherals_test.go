// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package main

import (
	"context"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/x509"
	"encoding/pem"
	"io"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
)

// A throwaway operator key, PEM-encoded to a temp file -- exactly the shape
// loadECPrivateKey reads, and the only kind of "key material" this test (or
// tfwctl itself) ever holds: one generated for the duration of one test run,
// never a real device's or fleet's.
func writeTestECKey(t *testing.T) string {
	t.Helper()
	key, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		t.Fatalf("generating test key: %v", err)
	}
	der, err := x509.MarshalECPrivateKey(key)
	if err != nil {
		t.Fatalf("marshalling test key: %v", err)
	}
	path := filepath.Join(t.TempDir(), "operator.pem")
	f, err := os.Create(path)
	if err != nil {
		t.Fatalf("writing test key: %v", err)
	}
	if err := pem.Encode(f, &pem.Block{Type: "EC PRIVATE KEY", Bytes: der}); err != nil {
		_ = f.Close()
		t.Fatalf("encoding test key: %v", err)
	}
	if err := f.Close(); err != nil {
		t.Fatalf("writing test key: %v", err)
	}
	return path
}

// Bad input is refused by tfwctl, before a frame is put on the wire. The device
// is deliberately absent: any of these reaching it would hang or panic here
// rather than passing quietly.
func TestMalformedArgumentsAreRefusedBeforeTheWire(t *testing.T) {
	for _, tc := range []struct {
		name string
		verb string
		args []string
	}{
		{"analog with a non-numeric pin", "analog", []string{"gpio3"}},
		{"analog with a pin past a byte", "analog", []string{"300"}},
		{"adc-ref with an unknown reference", "adc-ref", []string{"bandgap"}},
		{"pulse-in with an unparseable timeout", "pulse-in", []string{"2", "1", "soon"}},
		{"pulse-in with too few arguments", "pulse-in", []string{"2", "1"}},
		{"pulse-capture with too few arguments", "pulse-capture", []string{"2", "1", "8"}},
		{"pulse-capture with an unparseable timeout", "pulse-capture", []string{"2", "1", "8", "ages"}},
		{"eeprom-read without a count", "eeprom-read", []string{"0x80"}},
		{"eeprom-write with odd-length hex", "eeprom-write", []string{"0x80", "abc"}},
		{"eeprom-write with empty hex", "eeprom-write", []string{"0x80", ""}},
		{"i2c with too few arguments", "i2c", []string{"0x20", "01"}},
		{"i2c with non-hex payload", "i2c", []string{"0x20", "zz", "1"}},
		{"1wire-write with non-hex payload", "1wire-write", []string{"4", "gg"}},
		{"attest with a short nonce", "attest", []string{"0011"}},
		{"attest-cert with a runtime-length nonce", "attest-cert", []string{strings.Repeat("00", tfw.AttestNonceLen)}},
		{"auth with too few arguments", "auth", []string{strings.Repeat("00", tfw.SerialLen)}},
		{"auth with a short serial", "auth", []string{"0011", "/dev/null"}},
		{"a verb that does not exist", "morse", []string{"3"}},
	} {
		t.Run(tc.name, func(t *testing.T) {
			err := runPeripheral(context.Background(), nil, tc.verb, tc.args, false, io.Discard)
			if err == nil {
				t.Fatalf("%s %v was accepted", tc.verb, tc.args)
			}
		})
	}
}

// errorsAsStatus reports whether err is the device's own answer rather than a
// fault in tfwctl. A protocol status means the frame went out and came back.
func errorsAsStatus(err error, out *tfw.Status) bool {
	for e := err; e != nil; {
		if s, ok := e.(tfw.Status); ok {
			*out = s
			return true
		}
		u, ok := e.(interface{ Unwrap() error })
		if !ok {
			return false
		}
		e = u.Unwrap()
	}
	return false
}
