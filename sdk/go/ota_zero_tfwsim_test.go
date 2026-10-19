// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build tfwsim

package tfw

import (
	"context"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/simproc"
)

// The firmware's stager, not a board: tfwsim links it (mk/host.mk), a Nano
// image need not, so this starts the simulator itself and is not one of the
// TestAgainstFirmware* tests hw-test runs. A missing binary fails the test and
// names `make tfwsim`.
func TestSimStagerRefusesAZeroLength(t *testing.T) {
	d := New(simproc.Start(t, simproc.Software), WithTimeout(5*time.Second))
	ctx := context.Background()

	// The device's own answer to a zero total: OTA_BEGIN must exceed the
	// trailer (docs/ota/api.md), and the session is left as it was.
	require.Equal(t, StatusErrRange, asStatus(t, d.OTABegin(ctx, 0)))
	state, received, err := d.OTAStatus(ctx)
	require.NoError(t, err)
	assert.Equal(t, OTAIdle, state)
	assert.Zero(t, received)

	// StageImage gives the same status for an empty image.
	require.Equal(t, StatusErrRange, asStatus(t, d.StageImage(ctx, nil)))
	state, received, err = d.OTAStatus(ctx)
	require.NoError(t, err)
	assert.Equal(t, OTAIdle, state)
	assert.Zero(t, received)
}
