// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package main

import (
	"bytes"
	"context"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/x509"
	"encoding/hex"
	"encoding/pem"
	"errors"
	"fmt"
	"os"
	"strings"
	"time"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/serialport"
)

// --write-pubkey is its own mode: it puts a P-256 public key into one of the
// element's public-key slots through PROV_WRITE_PUBKEY, X then Y, and fails
// unless the slot reads back as the key. The file holds a public key and
// nothing else; a private key is refused unparsed.

// writePubKeyFlags are the flags --write-pubkey takes beside itself.
var writePubKeyFlags = map[string]bool{"write-pubkey": true, "slot": true, "baud": true, "timeout": true}

// runWritePubKey checks the mode's flags and its key file before it opens the
// port, so a refusal sends the element nothing. set is every flag given on
// the command line, args the positional arguments.
func runWritePubKey(set, args []string, path string, slot, baud int, timeout time.Duration) error {
	for _, name := range set {
		if !writePubKeyFlags[name] {
			return fmt.Errorf("--%s does not combine with --write-pubkey, which takes a key file, --slot and a port", name)
		}
	}
	if path == "" {
		return errors.New("--slot belongs to --write-pubkey <file>")
	}
	if slot < 0 || slot > 15 {
		return fmt.Errorf("--write-pubkey requires --slot, an element slot 0-15 (the public-key slots are %d and %d)",
			tfw.SlotImageSigning, tfw.SlotSecureBootKey)
	}
	if len(args) != 1 {
		return fmt.Errorf("--write-pubkey takes one positional argument, the port; got %d", len(args))
	}
	key, err := readPubKey(path)
	if err != nil {
		return err
	}
	link, err := serialport.Open(args[0], baud)
	if err != nil {
		return err
	}
	// By here the write has reported its own result; a Close error on the
	// way out changes nothing written to the device.
	defer func() { _ = link.Close() }()
	return writePubKey(context.Background(), tfw.New(link, tfw.WithTimeout(timeout)), uint8(slot), key)
}

// writePubKey writes key, X||Y, into slot. Device.ProvWritePubKey compares
// the slot's read-back with the key and fails with a *tfw.ReadBackError when
// they differ.
func writePubKey(ctx context.Context, dev *tfw.Device, slot uint8, key []byte) error {
	fmt.Printf("  writing the public key into slot %d\n", slot)
	if err := dev.ProvWritePubKey(ctx, slot, key); err != nil {
		return fmt.Errorf("write public key into slot %d: %w", slot, err)
	}
	fmt.Printf("  slot %d reads back %s\n", slot, hex.EncodeToString(key))
	return nil
}

// readPubKey loads the key --write-pubkey names.
func readPubKey(path string) ([]byte, error) {
	b, err := os.ReadFile(path) // #nosec G304 -- the operator names the key file
	if err != nil {
		return nil, err
	}
	xy, err := parsePubKey(b)
	if err != nil {
		return nil, fmt.Errorf("--write-pubkey %s: %w", path, err)
	}
	return xy, nil
}

// parsePubKey returns the X||Y of a P-256 public key given as the raw 64-byte
// point or as a PEM "PUBLIC KEY" block, a SubjectPublicKeyInfo (RFC 5480).
// Either way the point must lie on the curve.
func parsePubKey(b []byte) ([]byte, error) {
	block, rest := pem.Decode(b)
	if block == nil {
		if len(b) != tfw.OperatorPubkeyLen {
			return nil, fmt.Errorf("%d bytes and no PEM block: want the raw %d-byte X||Y or a PEM PUBLIC KEY",
				len(b), tfw.OperatorPubkeyLen)
		}
		if err := requireOnCurveP256(b); err != nil {
			return nil, err
		}
		return append([]byte(nil), b...), nil
	}
	if strings.Contains(block.Type, "PRIVATE") {
		return nil, fmt.Errorf("a PEM %s block: --write-pubkey takes the public key only", block.Type)
	}
	if block.Type != "PUBLIC KEY" {
		return nil, fmt.Errorf("a PEM %s block: want PUBLIC KEY, a SubjectPublicKeyInfo", block.Type)
	}
	if len(bytes.TrimSpace(rest)) != 0 {
		return nil, errors.New("more after the PEM PUBLIC KEY block: want one key")
	}
	pub, err := x509.ParsePKIXPublicKey(block.Bytes)
	if err != nil {
		return nil, fmt.Errorf("PEM PUBLIC KEY: %w", err)
	}
	ec, ok := pub.(*ecdsa.PublicKey)
	if !ok {
		return nil, fmt.Errorf("a %T: want an ECDSA P-256 key", pub)
	}
	if ec.Curve != elliptic.P256() {
		return nil, fmt.Errorf("an ECDSA %s key: want P-256", ec.Params().Name)
	}
	sec1, err := ec.Bytes()
	if err != nil {
		return nil, fmt.Errorf("PEM PUBLIC KEY: %w", err)
	}
	// The SEC1 uncompressed form, 0x04 || X || Y.
	return sec1[1:], nil
}
