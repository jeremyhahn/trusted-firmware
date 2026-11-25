// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package main

import (
	"context"
	"errors"
	"io"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
)

// TestOTAVerbsReportALinkFailure holds the OTA verbs and attest-csr to the
// same contract as TestEveryVerbReportsALinkFailure: over a link that has gone
// away each returns the SDK's LinkError. ota-stage's error runs through
// Upload's resume, which joins and wraps errors, so a cable that was pulled
// must still read as a LinkError and not as a count mismatch.
func TestOTAVerbsReportALinkFailure(t *testing.T) {
	img := filepath.Join(t.TempDir(), "image.bin")
	require.NoError(t, os.WriteFile(img, make([]byte, 64), 0o600))
	sig := "--sig=" + strings.Repeat("ab", tfw.OTAAuthSigLen)
	dev := tfw.New(deadConn{}, tfw.WithBootWait(0))
	for _, args := range [][]string{
		{"ota-stage", img},
		{"ota-stage", "--activate", sig, img},
		{"ota-activate", sig},
		{"ota-activate", "now", sig},
		{"ota-confirm", sig},
		{"ota-info", "0"},
		{"attest-csr", "--nonce", strings.Repeat("00", tfw.AliasNonceLen)},
	} {
		err := run(context.Background(), dev, args[0], args[1:], false, io.Discard)
		var le *tfw.LinkError
		assert.True(t, errors.As(err, &le), "%v over a dead link: %v, want a LinkError", args, err)
	}
}
