package tfw

import (
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
