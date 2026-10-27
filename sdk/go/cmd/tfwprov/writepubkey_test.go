// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package main

import (
	"bytes"
	"context"
	"crypto/ecdsa"
	"crypto/ed25519"
	"crypto/elliptic"
	"crypto/x509"
	"encoding/pem"
	"errors"
	"io/fs"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
)

// pubKeyPEM is the PEM PUBLIC KEY of the point sec1 on curve: a public test
// vector, never a key anyone holds the private half of.
func pubKeyPEM(t *testing.T, curve elliptic.Curve, sec1 []byte) []byte {
	t.Helper()
	k, err := ecdsa.ParseUncompressedPublicKey(curve, sec1)
	if err != nil {
		t.Fatal(err)
	}
	return marshalPEM(t, k)
}

func marshalPEM(t *testing.T, pub any) []byte {
	t.Helper()
	der, err := x509.MarshalPKIXPublicKey(pub)
	if err != nil {
		t.Fatal(err)
	}
	return pem.EncodeToMemory(&pem.Block{Type: "PUBLIC KEY", Bytes: der})
}

// p384Generator is the uncompressed P-384 generator: a point on the wrong curve.
func p384Generator() []byte {
	p := elliptic.P384().Params()
	b := make([]byte, 97)
	b[0] = 0x04
	p.Gx.FillBytes(b[1:49])
	p.Gy.FillBytes(b[49:])
	return b
}

func writeKeyFile(t *testing.T, b []byte) string {
	t.Helper()
	path := filepath.Join(t.TempDir(), "key")
	if err := os.WriteFile(path, b, 0o600); err != nil {
		t.Fatal(err)
	}
	return path
}

func TestParsePubKeyTakesTheRawPointOrAPEMPublicKey(t *testing.T) {
	g := mustHex(t, p256GeneratorHex)
	gPEM := pubKeyPEM(t, elliptic.P256(), append([]byte{0x04}, g...))
	for name, in := range map[string][]byte{
		"raw X||Y":                  g,
		"PEM PUBLIC KEY":            gPEM,
		"PEM with trailing newline": append(append([]byte(nil), gPEM...), "\n\n"...),
	} {
		got, err := parsePubKey(in)
		if err != nil {
			t.Fatalf("%s: %v", name, err)
		}
		if !bytes.Equal(got, g) {
			t.Fatalf("%s: got %x, want %x", name, got, g)
		}
	}
}

func TestParsePubKeyNamesWhyItRefuses(t *testing.T) {
	g := mustHex(t, p256GeneratorHex)
	offCurve := append([]byte(nil), g...)
	offCurve[63] ^= 1
	gPEM := pubKeyPEM(t, elliptic.P256(), append([]byte{0x04}, g...))
	for name, c := range map[string]struct {
		in   []byte
		want string
	}{
		"short raw":   {g[:63], "63 bytes and no PEM block"},
		"empty":       {nil, "0 bytes and no PEM block"},
		"off curve":   {offCurve, "not a point on P-256"},
		"private key": {pem.EncodeToMemory(&pem.Block{Type: "EC PRIVATE KEY", Bytes: []byte{1}}), "public key only"},
		"certificate": {pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: []byte{1}}), "want PUBLIC KEY"},
		"two keys":    {append(append([]byte(nil), gPEM...), gPEM...), "want one key"},
		"bad DER":     {pem.EncodeToMemory(&pem.Block{Type: "PUBLIC KEY", Bytes: []byte{1}}), "PEM PUBLIC KEY: "},
		"ed25519":     {marshalPEM(t, ed25519.PublicKey(make([]byte, ed25519.PublicKeySize))), "want an ECDSA P-256 key"},
		"P-384":       {pubKeyPEM(t, elliptic.P384(), p384Generator()), "ECDSA P-384 key: want P-256"},
	} {
		_, err := parsePubKey(c.in)
		if err == nil || !strings.Contains(err.Error(), c.want) {
			t.Fatalf("%s: err = %v, want one containing %q", name, err, c.want)
		}
	}
}

func TestReadPubKeyNamesTheFile(t *testing.T) {
	path := writeKeyFile(t, []byte("not a key"))
	if _, err := readPubKey(path); err == nil || !strings.Contains(err.Error(), path) {
		t.Fatalf("err = %v, want one naming %s", err, path)
	}
	if _, err := readPubKey(filepath.Join(t.TempDir(), "absent")); !errors.Is(err, fs.ErrNotExist) {
		t.Fatalf("an absent file: %v", err)
	}
}

func TestWritePubKeySendsTheKeyAndChecksTheReadBack(t *testing.T) {
	key := mustHex(t, p256GeneratorHex)
	e := &element{}
	if err := writePubKey(context.Background(), startElement(t, e), tfw.SlotSecureBootKey, key); err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(e.slot[:], key) {
		t.Fatalf("slot = %x, want %x", e.slot, key)
	}
	if !equalCmds(e.commands(), []tfw.Cmd{tfw.CmdProvWritePubKey, tfw.CmdProvWritePubKey}) {
		t.Fatalf("commands = %v, want X then Y", e.commands())
	}
}

func TestWritePubKeyFailsOnAReadBackThatDiffers(t *testing.T) {
	err := writePubKey(context.Background(), startElement(t, &element{misread: true}),
		tfw.SlotImageSigning, mustHex(t, p256GeneratorHex))
	var rb *tfw.ReadBackError
	if !errors.As(err, &rb) || rb.Slot != tfw.SlotImageSigning {
		t.Fatalf("err = %v, want a *tfw.ReadBackError for slot %d", err, tfw.SlotImageSigning)
	}
}

func TestWritePubKeyReportsTheElementsRefusal(t *testing.T) {
	e := &element{refuseCmd: tfw.CmdProvWritePubKey, refuseNth: 1, refuseStatus: tfw.StatusErrBadState}
	err := writePubKey(context.Background(), startElement(t, e), tfw.SlotSecureBootKey, mustHex(t, p256GeneratorHex))
	var st tfw.Status
	if !errors.As(err, &st) || st != tfw.StatusErrBadState {
		t.Fatalf("err = %v, want BAD_STATE", err)
	}
	if !strings.Contains(err.Error(), "slot 15") {
		t.Fatalf("err = %v, want it to name slot 15", err)
	}
	if n := len(e.commands()); n != 1 {
		t.Fatalf("%d requests after a refused X, want 1", n)
	}
}

func TestRunWritePubKeyRefusesBeforeOpeningThePort(t *testing.T) {
	good := writeKeyFile(t, mustHex(t, p256GeneratorHex))
	port := filepath.Join(t.TempDir(), "no-such-port")
	for name, c := range map[string]struct {
		set, args []string
		path      string
		slot      int
		want      string
	}{
		"another mode": {[]string{"write-pubkey", "slot", "commit"}, []string{port}, good, 15, "--commit does not combine"},
		"a bake flag":  {[]string{"write-pubkey", "out"}, []string{port}, good, 15, "--out does not combine"},
		"no file":      {[]string{"slot"}, []string{port}, "", 15, "--slot belongs to --write-pubkey"},
		"no slot":      {[]string{"write-pubkey"}, []string{port}, good, -1, "requires --slot"},
		"slot 16":      {[]string{"write-pubkey", "slot"}, []string{port}, good, 16, "requires --slot"},
		"no port":      {[]string{"write-pubkey", "slot"}, nil, good, 15, "got 0"},
		"two ports":    {[]string{"write-pubkey", "slot"}, []string{port, port}, good, 15, "got 2"},
		"bad key":      {[]string{"write-pubkey", "slot"}, []string{port}, writeKeyFile(t, []byte("x")), 15, "1 bytes"},
	} {
		err := runWritePubKey(c.set, c.args, c.path, c.slot, 115200, time.Second)
		if err == nil || !strings.Contains(err.Error(), c.want) {
			t.Fatalf("%s: err = %v, want one containing %q", name, err, c.want)
		}
	}
	// Everything checked, the port is opened: here it does not exist.
	if err := runWritePubKey([]string{"write-pubkey", "slot", "baud", "timeout"}, []string{port}, good, 15,
		115200, time.Second); err == nil {
		t.Fatal("a port that does not exist opened")
	}
}

func TestMainWritePubKeyRefusesAnotherModeAndExits1(t *testing.T) {
	key := writeKeyFile(t, mustHex(t, p256GeneratorHex))
	code, first := runMain(t, "--write-pubkey", key, "--slot", "15", "--verify", "port")
	if code != 1 || !strings.Contains(first, "--verify does not combine with --write-pubkey") {
		t.Fatalf("exit %d: %s", code, first)
	}
}
