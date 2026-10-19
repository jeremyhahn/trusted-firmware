// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"context"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// frameCounter is a cannedLink that counts the frames written to it.
type frameCounter struct {
	cannedLink
	frames int
}

func (l *frameCounter) Write(p []byte) (int, error) {
	l.frames++
	return l.cannedLink.Write(p)
}

// The device refuses a zero total at OTA_BEGIN with StatusErrRange
// (src/core/ota_stage.c, tfw_stage_cmd_begin); StageImage gives the same answer
// without a round trip.
func TestStageImageRefusesAnEmptyImageBeforeAnyFrame(t *testing.T) {
	for _, image := range [][]byte{nil, {}} {
		link := &frameCounter{}
		err := New(link).StageImage(context.Background(), image)
		require.Equal(t, StatusErrRange, asStatus(t, err))
		assert.Zero(t, link.frames, "a frame went out for an empty image")
	}
}
