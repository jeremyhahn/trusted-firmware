// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package main

import (
	"bytes"
	"context"
	"strings"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
)

// attest-csr against a device is in e2e_attestcsr_test.go; here only what
// tfwctl refuses on its own.

var csrNonceHex = strings.Repeat("a5", tfw.AliasNonceLen)

// A malformed command line is refused before the device is touched: dev is nil,
// so reaching it would panic rather than pass.
func TestAttestCSRRefusesAMalformedCommandLine(t *testing.T) {
	for _, tc := range []struct {
		name string
		args []string
		want string
	}{
		{"no arguments", nil, "usage:"},
		{"no nonce", []string{"--json"}, "usage:"},
		{"an empty nonce", []string{"--nonce", ""}, "usage:"},
		{"a nonce flag without a value", []string{"--nonce"}, "usage:"},
		{"a flag that does not exist", []string{"--nonce", csrNonceHex, "--der"}, "usage:"},
		{"a stray positional argument", []string{"--nonce", csrNonceHex, "extra"}, "usage:"},
		{"the nonce as a positional argument", []string{csrNonceHex}, "usage:"},
		{"a nonce that is not hex", []string{"--nonce", strings.Repeat("zz", tfw.AliasNonceLen)}, ""},
		{"a short nonce", []string{"--nonce", "0011"}, ""},
		{"the evidence nonce length", []string{"--nonce", strings.Repeat("00", tfw.AttestNonceLen)}, ""},
	} {
		t.Run(tc.name, func(t *testing.T) {
			var out bytes.Buffer
			err := runPeripheral(context.Background(), nil, "attest-csr", tc.args, false, &out)
			require.Error(t, err)
			assert.Contains(t, err.Error(), tc.want)
			assert.Zero(t, out.Len(), "nothing may be written on failure")
		})
	}
}
