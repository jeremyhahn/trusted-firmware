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
	"os"

	"github.com/jeremyhahn/trusted-firmware/xkms/tfmdice"
)

func main() {
	devPub := flag.String("deviceid-pub", "", "DeviceID public key X||Y, hex")
	aliasPub := flag.String("alias-pub", "", "alias public key X||Y, hex")
	certPath := flag.String("cert", "", "alias certificate DER file")
	nonce := flag.String("nonce", "", "the nonce the certificate was asked over, hex")
	digest := flag.String("digest", "", "the digest the alias key signed, hex")
	sig := flag.String("sig", "", "the alias signature r||s, hex")
	flag.Parse()

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
			fmt.Fprintf(os.Stderr, "tfm-dice-check: -%s: want non-empty hex\n", f.name)
			os.Exit(2)
		}
		*f.dst = b
	}
	der, err := os.ReadFile(*certPath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "tfm-dice-check: -cert: %v\n", err)
		os.Exit(2)
	}
	in.CertDER = der

	if err := tfmdice.Check(in); err != nil {
		fmt.Fprintln(os.Stderr, "tfm-dice-check:", err)
		os.Exit(1)
	}
	fmt.Println("tfm-dice-check: alias evidence ok")
}
