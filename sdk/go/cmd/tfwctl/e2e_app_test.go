// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build tfwsim

package main

import (
	"bytes"
	"context"
	"io"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/simproc"
)

// startSimproc starts one of simproc's simulators, the SDK's own launcher,
// and the device on it.
func startSimproc(t *testing.T, b simproc.Binary) (*simproc.Conn, *tfw.Device) {
	t.Helper()
	c := simproc.Start(t, b)
	return c, tfw.New(c, tfw.WithTimeout(5*time.Second))
}

// tfwsim-app-heartbeat is the one simulator with an application linked, so
// the one that answers APP_INFO: the heartbeat example, as its app.mk names it.
// Through the peer seam (e2e_peer_test.go), so on a board this row needs the
// heartbeat image flashed. CAPS has no bit for an application, so on a board
// whose image links none (the MPS2 boards' wire run, docs/cortex-m/README.md)
// the row checks APP_INFO's refusal as unknown, the answer of the row below.
func TestFirmwareAppReportsItsApplication(t *testing.T) {
	p := startPeer(t, simproc.App)
	d := p.serial
	// An image without an application does not know APP_INFO, and fails the
	// row: CAPS has no bit for it.
	requireKnown(t, d, tfw.CmdAppInfo, nil, "an application (APP_INFO)")
	var ai tfw.AppInfo
	cliJSON(t, d, &ai, "app")
	// How many polls ran past their budget is the host's scheduling, not the
	// image's.
	ai.Overruns = 0
	assert.Equal(t, tfw.AppInfo{
		State:        tfw.AppRunning,
		ABI:          1,
		VersionMajor: 1,
		RequiredCaps: 1 << 0,
		Name:         "heartbeat",
	}, ai)
}

// An image with no application answers APP_INFO as an unknown command, and
// tfwctl hands that status back rather than printing an empty application.
func TestFirmwareAppIsAnUnknownCommandWithoutAnApplication(t *testing.T) {
	d := startPeer(t, simproc.Software).serial
	var out bytes.Buffer
	err := run(context.Background(), d, "app", nil, false, &out)
	var st tfw.Status
	require.True(t, errorsAsStatus(err, &st), "app: %v, want a device status", err)
	assert.Equal(t, tfw.StatusErrUnknownCmd, st)
	assert.Zero(t, out.Len(), "app printed %q for an image with no application", out.String())
}

// simproc.Start runs the simulator, the device answers on it, and Close ends
// the process without an error. Simulator-only: the process is what it tests.
func TestE2ESimprocStartRunsTheSimulatorAndCloseEndsIt(t *testing.T) {
	c, d := startSimproc(t, simproc.Software)
	var echo struct{ Echo string }
	cliJSON(t, d, &echo, "ping", "simproc")
	assert.Equal(t, "simproc", echo.Echo)
	require.NoError(t, c.Close())
	// The process is gone: a request now fails on the link, not by timeout.
	err := run(context.Background(), d, "ping", nil, false, io.Discard)
	var le *tfw.LinkError
	assert.ErrorAs(t, err, &le)
}
