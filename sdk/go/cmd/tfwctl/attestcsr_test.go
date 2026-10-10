// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package main

import (
	"bytes"
	"context"
	"encoding/hex"
	"encoding/json"
	"strings"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/tfwsim"
)

// attest-csr needs a device with a DeviceID key, so these run against the
// element-tier simulator; the soft tier is the error case.

var csrNonceHex = strings.Repeat("a5", tfw.AliasNonceLen)

func TestAttestCSRPrintsTheSignatureAsHex(t *testing.T) {
	var out bytes.Buffer
	err := run(context.Background(), tfwsim.StartElement(t), "attest-csr", []string{"--nonce", csrNonceHex}, false, &out)
	require.NoError(t, err)

	line := strings.TrimSuffix(out.String(), "\n")
	assert.Len(t, line, 2*tfw.CSRSigLen, "stdout must be the hex signature and one newline")
	sig, err := hex.DecodeString(line)
	require.NoError(t, err)
	assert.Len(t, sig, tfw.CSRSigLen)
}

func TestAttestCSRJSONCarriesTheSignatureAsHex(t *testing.T) {
	var out bytes.Buffer
	err := run(context.Background(), tfwsim.StartElement(t), "attest-csr", []string{"--nonce", csrNonceHex}, true, &out)
	require.NoError(t, err)

	var got struct {
		Signature string `json:"signature"`
		Len       int    `json:"len"`
	}
	require.NoError(t, json.Unmarshal(out.Bytes(), &got))
	sig, err := hex.DecodeString(got.Signature)
	require.NoError(t, err)
	assert.Equal(t, len(sig), got.Len)
	assert.Equal(t, tfw.CSRSigLen, got.Len)
}

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

func TestAttestCSRReportsADeviceThatCannotIssueOne(t *testing.T) {
	var out bytes.Buffer
	err := run(context.Background(), tfwsim.Start(t), "attest-csr", []string{"--nonce", csrNonceHex}, false, &out)

	var st tfw.Status
	require.True(t, errorsAsStatus(err, &st), "got %v", err)
	assert.Equal(t, tfw.StatusErrUnsupported, st)
	assert.Zero(t, out.Len())
}

// `caps` names the feature, so a person can see before asking whether the device
// will answer.
func TestCapsNamesAttestCSROnADeviceThatOffersIt(t *testing.T) {
	var out bytes.Buffer
	require.NoError(t, run(context.Background(), tfwsim.StartElement(t), "caps", nil, false, &out))
	assert.Contains(t, out.String(), "attest-csr")
}
