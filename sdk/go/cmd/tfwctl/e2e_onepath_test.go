// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build tfwsim

package main

import (
	"context"
	"io"
	"strings"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/simproc"
)

// One update path per 2560 image (#52): each image answers the other path's
// commands with the status code a host already knows, never a new one.

var zeroSig = "--sig=" + strings.Repeat("00", 64)

// tfwsim is the stage image: the store is its update path, and its tfw-boot
// opens no session over the UART, so RESET(1) is refused and nothing resets.
func TestStoreImageRefusesResetIntoABootSession(t *testing.T) {
	_, d := startSimproc(t, simproc.Software)
	require.NoError(t, run(context.Background(), d, "ota-status", nil, false, io.Discard))
	assert.Equal(t, tfw.StatusErrUnsupported, cliStatus(t, d, "reset", "boot"))
	// Still up and still answering on the same connection.
	require.NoError(t, run(context.Background(), d, "ota-status", nil, false, io.Discard))
}

// tfwsim-otaboot is the image without the stager: its update path is the
// session in tfw-boot, so RESET(1) is taken and the store's commands are
// unknown to it.
func TestBootSessionImageDoesNotKnowTheStoreCommands(t *testing.T) {
	_, d := startSimproc(t, simproc.OTABoot)
	for _, c := range []struct {
		verb string
		args []string
	}{
		{"ota-stage", []string{writeFile(t, "page.bin", make([]byte, 256))}},
		{"ota-status", nil},
		{"ota-info", []string{"0"}},
		{"ota-activate", []string{zeroSig}},
		{"ota-confirm", []string{zeroSig}},
	} {
		assert.Equal(t, tfw.StatusErrUnknownCmd, cliStatus(t, d, c.verb, c.args...), c.verb)
	}
	require.NoError(t, run(context.Background(), d, "reset", []string{"boot"}, false, io.Discard))
}
