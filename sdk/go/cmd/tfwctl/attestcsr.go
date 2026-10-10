// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package main

import (
	"context"
	"encoding/hex"
	"flag"
	"fmt"
	"io"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
)

// attestCSR is `attest-csr --nonce <hex32>`: the 64-byte r||s signature the
// device's DeviceID key made for ACME finalize, as hex on stdout.
//
// The signature is all the device returns. The CertificationRequest around it is
// go-xkms's to build, so this prints the signature and nothing more. The nonce
// stays a flag rather than the positional argument attest and attest-cert take,
// as it was when the verb printed a CertificationRequest.
//
// The nonce is the caller's, as for attest-cert: it is the ACME challenge b, and
// a tool that chose its own would be proving possession to itself.
func attestCSR(ctx context.Context, dev *tfw.Device, args []string, asJSON bool, out io.Writer) error {
	usage := fmt.Errorf("usage: attest-csr --nonce <%d hex bytes>", tfw.AliasNonceLen)

	fs := flag.NewFlagSet("attest-csr", flag.ContinueOnError)
	fs.SetOutput(io.Discard)
	nonceHex := fs.String("nonce", "", "the challenge b, hex")
	if err := fs.Parse(args); err != nil {
		return fmt.Errorf("%w: %v", usage, err)
	}
	if fs.NArg() != 0 || *nonceHex == "" {
		return usage
	}

	nonce, err := parseNonce(*nonceHex, tfw.AliasNonceLen)
	if err != nil {
		return err
	}
	var b [tfw.AliasNonceLen]byte
	copy(b[:], nonce)

	sig, err := dev.AttestCSR(ctx, b)
	if err != nil {
		return err
	}

	h := hex.EncodeToString(sig[:])
	if asJSON {
		return emit(out, true, map[string]any{"signature": h, "len": len(sig)}, nil)
	}
	_, err = fmt.Fprintln(out, h)
	return err
}
