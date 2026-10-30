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

// The HSM channel is in the element-tier simulator alone (mk/host.mk). Nothing
// in it appends to the A6 record yet (the engine's call sites are a later
// slice), so the record the firmware answers from is empty, and what is pinned
// here is the command's refusals: an index past the end, and a payload that is
// not one byte. A missing binary fails the test and names `make
// tfwsim-atecc608a`.
func TestSimHSMRecordReadOfAnEmptyRecordIsRange(t *testing.T) {
	d := New(simproc.Start(t, simproc.Element), WithTimeout(5*time.Second))
	ctx := context.Background()

	for _, index := range []byte{0, 1, 31, 32, 255} {
		_, err := d.HSMRecordRead(ctx, index)
		require.ErrorIs(t, err, StatusErrRange, "index %d", index)
	}

	got, err := d.HSMRecordReadAll(ctx)
	require.NoError(t, err)
	assert.Empty(t, got)
}

func TestSimHSMRecordReadRefusesAPayloadThatIsNotOneByte(t *testing.T) {
	d := New(simproc.Start(t, simproc.Element), WithTimeout(5*time.Second))
	ctx := context.Background()

	for _, payload := range [][]byte{nil, {0, 0}, {0, 0, 0, 0}} {
		_, err := d.Do(ctx, CmdHSMRecordRead, payload)
		require.ErrorIs(t, err, StatusErrBadArgc, "%d payload bytes", len(payload))
	}

	// The refusals left the channel as it was.
	_, err := d.HSMHeartbeat(ctx)
	require.NoError(t, err)
}
