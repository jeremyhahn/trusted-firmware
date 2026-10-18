// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build tfwsim

package main

import (
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
func TestE2EAppSimulatorReportsItsApplication(t *testing.T) {
	_, d := startSimproc(t, simproc.App)
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
func TestE2EAppIsAnUnknownCommandWithoutAnApplication(t *testing.T) {
	_, d := startSimproc(t, simproc.Software)
	assert.Equal(t, tfw.StatusErrUnknownCmd, cliStatus(t, d, "app"))
}

// simproc.Start runs the simulator, the device answers on it, and Close ends
// the process without an error.
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
