package main

import (
	"bytes"
	"errors"
	"testing"

	"github.com/stretchr/testify/require"
)

var errWriteRefused = errors.New("write refused")

// failNthWriter accepts n-1 writes and refuses the nth.
type failNthWriter struct {
	n   int
	buf bytes.Buffer
}

func (w *failNthWriter) Write(p []byte) (int, error) {
	w.n--
	if w.n == 0 {
		return 0, errWriteRefused
	}
	return w.buf.Write(p)
}

func TestHexLengthErrorNamesTheFlag(t *testing.T) {
	err := &hexLengthError{flag: "device-id", got: 31, want: 32}
	require.Equal(t, "tfwprov: --device-id is 31 bytes, want 32", err.Error())
}

func TestWriteIHEXRecord(t *testing.T) {
	var b bytes.Buffer
	require.NoError(t, writeIHEXRecord(&b, 0x0030, 0x00, []byte{0x02, 0x33, 0x7A}))
	require.Equal(t, ":0300300002337A1E\n", b.String())

	require.Error(t, writeIHEXRecord(&b, 0, 0x00, make([]byte, 256)), "a 256-byte record must be refused")

	for n := 1; n <= 3; n++ {
		w := &failNthWriter{n: n}
		require.ErrorIs(t, writeIHEXRecord(w, 0x0030, 0x00, []byte{0x02, 0x33, 0x7A}), errWriteRefused, "write %d", n)
	}
}

func TestWriteIHEXSegmentsPropagatesWriteErrors(t *testing.T) {
	seg := ihexSegment{base: 0x0030, data: []byte{0x02, 0x33, 0x7A}}

	var b bytes.Buffer
	require.NoError(t, writeIHEXSegments(&b, seg))
	require.Equal(t, ":0300300002337A1E\n:00000001FF\n", b.String())

	require.ErrorIs(t, writeIHEXSegments(&failNthWriter{n: 1}, seg), errWriteRefused, "data record")
	require.ErrorIs(t, writeIHEXSegments(&failNthWriter{n: 4}, seg), errWriteRefused, "EOF record")
	require.ErrorIs(t, writeIHEXSegments(&failNthWriter{n: 1}), errWriteRefused, "EOF record with no segments")
}
