// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

// Command tfm-dice-check runs tfmdice.Check on the DICE evidence the QEMU TF-M
// gate captured from the non-secure check (#608). Exit 0 when it holds, 1 when
// a check fails, 2 on bad arguments.
package main

import (
	"encoding/hex"
	"flag"
	"fmt"
	"io"
	"os"

	"github.com/jeremyhahn/trusted-firmware/xkms/tfmdice"
)

func main() { os.Exit(run(os.Args[1:], os.Stdout, os.Stderr)) }

// run is the command: it parses args, reports to stdout and stderr, and
// returns the exit code (0 the evidence holds, 1 a check fails, 2 bad
// arguments).
func run(args []string, stdout, stderr io.Writer) int {
	fs := flag.NewFlagSet("tfm-dice-check", flag.ContinueOnError)
	fs.SetOutput(stderr)
	devPub := fs.String("deviceid-pub", "", "DeviceID public key X||Y, hex")
	aliasPub := fs.String("alias-pub", "", "alias public key X||Y, hex")
	certPath := fs.String("cert", "", "alias certificate DER file")
	nonce := fs.String("nonce", "", "the nonce the certificate was asked over, hex")
	digest := fs.String("digest", "", "the digest the alias key signed, hex")
	sig := fs.String("sig", "", "the alias signature r||s, hex")
	if err := fs.Parse(args); err != nil {
		return 2
	}

	var in tfmdice.Input
	for _, f := range []struct {
		name string
		val  string
		dst  *[]byte
	}{
		{"deviceid-pub", *devPub, &in.DeviceIDPub},
		{"alias-pub", *aliasPub, &in.AliasPub},
		{"nonce", *nonce, &in.Nonce},
		{"digest", *digest, &in.Digest},
		{"sig", *sig, &in.Sig},
	} {
		b, err := hex.DecodeString(f.val)
		if err != nil || len(b) == 0 {
			_, _ = fmt.Fprintf(stderr, "tfm-dice-check: -%s: want non-empty hex\n", f.name)
			return 2
		}
		*f.dst = b
	}
	der, err := os.ReadFile(*certPath)
	if err != nil {
		_, _ = fmt.Fprintf(stderr, "tfm-dice-check: -cert: %v\n", err)
		return 2
	}
	in.CertDER = der

	if err := tfmdice.Check(in); err != nil {
		_, _ = fmt.Fprintln(stderr, "tfm-dice-check:", err)
		return 1
	}
	_, _ = fmt.Fprintln(stdout, "tfm-dice-check: alias evidence ok")
	return 0
}
