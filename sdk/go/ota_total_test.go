package tfw

import (
	"context"
	"errors"
	"math"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

func TestOTATotalOutsideTheWireFieldIsRefused(t *testing.T) {
	for _, n := range []int64{-1, math.MaxUint32 + 1} {
		_, err := otaTotal(n)
		var re *ArgRangeError
		require.True(t, errors.As(err, &re), "otaTotal(%d): %v", n, err)
		assert.Equal(t, CmdOTABegin, re.Cmd)
		assert.Equal(t, n, re.Value)
	}
}

func TestOTATotalAtTheWireFieldsBoundsIsCarried(t *testing.T) {
	for _, n := range []int64{0, OTAChunk, math.MaxUint32} {
		got, err := otaTotal(n)
		require.NoError(t, err)
		assert.Equal(t, uint32(n), got)
	}
}

// An empty or oversize chunk is refused before the wire: the Device is
// unconnected, so reaching it would panic rather than pass.
func TestOTADataOutsideOneChunkIsRefused(t *testing.T) {
	for _, n := range []int{0, OTAChunk + 1} {
		err := (&Device{}).OTAData(context.Background(), 0, make([]byte, n))
		var re *ArgRangeError
		require.True(t, errors.As(err, &re), "OTAData %d bytes: %v", n, err)
		assert.Equal(t, CmdOTAData, re.Cmd)
		assert.Equal(t, int64(n), re.Value)
	}
}
