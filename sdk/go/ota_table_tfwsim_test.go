// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build tfwsim

package tfw

import (
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/binary"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/simproc"
)

// The stager's status table (docs/ota/api.md), row by row, as tfwsim answers
// it. Like ota_zero_tfwsim_test.go these start the simulator themselves and
// are not TestAgainstFirmware* tests: a missing binary fails and names
// `make tfwsim`. OTA_BEGIN's TFW_ERR_NO_STORE needs a store fault, so it is
// pinned by the host tests only (test/unit/test_ota_stage.c).

func startStager(t *testing.T) (*Device, context.Context) {
	t.Helper()
	return New(simproc.Start(t, simproc.Software), WithTimeout(5*time.Second)), context.Background()
}

// simImage is a total-byte stage image whose trailer v1 names a body-byte
// image for the 328P, as tfwsim's board.
func simImage(total, body int) []byte {
	img := bytes.Repeat([]byte{0xFF}, total)
	for i := 0; i < body; i++ {
		img[i] = byte(i*7 + 3)
	}
	tr := img[total-128:]
	clear(tr)
	copy(tr, "TFIM")
	binary.LittleEndian.PutUint16(tr[4:], 1)
	tr[6] = 0x01
	binary.LittleEndian.PutUint32(tr[8:], 3)
	binary.LittleEndian.PutUint32(tr[12:], uint32(body))
	sum := sha256.Sum256(img[:body])
	copy(tr[16:], sum[:])
	return img
}

func sendRange(t *testing.T, d *Device, img []byte, from, to int) {
	t.Helper()
	for off := from; off < to; off += OTAChunk {
		require.NoError(t, d.OTAData(context.Background(), uint32(off), img[off:min(off+OTAChunk, to)]), "offset %d", off)
	}
}

func expectOTA(t *testing.T, d *Device, state OTAState, received uint32) {
	t.Helper()
	s, n, err := d.OTAStatus(context.Background())
	require.NoError(t, err)
	assert.Equal(t, state, s)
	assert.Equal(t, received, n)
}

func TestSimStagerRefusesDataAndEndWithNoTransfer(t *testing.T) {
	d, ctx := startStager(t)

	assert.Equal(t, StatusErrBusy, asStatus(t, d.OTAData(ctx, 0, []byte{1})))
	assert.Equal(t, StatusErrBusy, asStatus(t, d.OTAEnd(ctx)))
	expectOTA(t, d, OTAIdle, 0)
}

func TestSimStagerRefusesDataOffTheReceivedCount(t *testing.T) {
	d, ctx := startStager(t)
	img := simImage(512, 200)

	require.NoError(t, d.OTABegin(ctx, 512))
	sendRange(t, d, img, 0, 120)
	assert.Equal(t, StatusErrRange, asStatus(t, d.OTAData(ctx, 0, img[:OTAChunk])), "behind")
	assert.Equal(t, StatusErrRange, asStatus(t, d.OTAData(ctx, 180, img[180:240])), "ahead")
	sendRange(t, d, img, 120, 480)
	assert.Equal(t, StatusErrRange, asStatus(t, d.OTAData(ctx, 480, make([]byte, OTAChunk))), "past total_len")
	// A refused chunk is not a failure: the upload goes on.
	expectOTA(t, d, OTAReceiving, 480)
	sendRange(t, d, img, 480, 512)
	require.NoError(t, d.OTAEnd(ctx))
	expectOTA(t, d, OTAComplete, 512)
}

func TestSimStagerEndRefusesAShortStream(t *testing.T) {
	d, ctx := startStager(t)
	img := simImage(512, 200)

	require.NoError(t, d.OTABegin(ctx, 512))
	sendRange(t, d, img, 0, 120)
	assert.Equal(t, StatusErrFraming, asStatus(t, d.OTAEnd(ctx)))
	expectOTA(t, d, OTAFailed, 120)
}

func TestSimStagerEndRefusesABadTrailerOrBody(t *testing.T) {
	for _, tc := range []struct {
		name  string
		spoil func(img []byte)
		want  Status
	}{
		{"magic", func(img []byte) { img[512-128] ^= 0x80 }, StatusErrUnsupported},
		{"version", func(img []byte) { img[512-128+4] ^= 0x80 }, StatusErrUnsupported},
		{"board id", func(img []byte) { img[512-128+6] ^= 0x80 }, StatusErrUnsupported},
		{"zero image length", func(img []byte) { binary.LittleEndian.PutUint32(img[512-128+12:], 0) }, StatusErrRange},
		{"image reaches its trailer", func(img []byte) { binary.LittleEndian.PutUint32(img[512-128+12:], 512-128+1) }, StatusErrRange},
		{"measurement", func(img []byte) { img[17] ^= 0x40 }, StatusErrCrypto},
	} {
		t.Run(tc.name, func(t *testing.T) {
			// One simulator per row: tfwsim stops answering after about
			// 4 KiB from the host on one connection.
			d, ctx := startStager(t)
			img := simImage(512, 200)
			tc.spoil(img)
			assert.Equal(t, tc.want, asStatus(t, d.StageImage(ctx, img)))
			expectOTA(t, d, OTAFailed, 512)
			// The failure leaves the stager ready for a new OTA_BEGIN.
			require.NoError(t, d.StageImage(ctx, simImage(512, 200)))
			expectOTA(t, d, OTAComplete, 512)
		})
	}
}

func TestSimStagerRefusesAWrongPayloadLength(t *testing.T) {
	d, ctx := startStager(t)
	img := simImage(512, 200)

	require.NoError(t, d.OTABegin(ctx, 512))
	sendRange(t, d, img, 0, 120)
	for _, tc := range []struct {
		cmd Cmd
		n   int
	}{
		{CmdOTABegin, 3}, {CmdOTABegin, 5}, {CmdOTAData, 4}, {CmdOTAEnd, 1}, {CmdOTAStatus, 1},
	} {
		_, err := d.Do(ctx, tc.cmd, make([]byte, tc.n))
		assert.Equal(t, StatusErrBadArgc, asStatus(t, err), "%v with %d bytes", tc.cmd, tc.n)
	}
	expectOTA(t, d, OTAReceiving, 120)
}

func TestSimStagerBeginWhileReceiving(t *testing.T) {
	d, ctx := startStager(t)
	img := simImage(512, 200)

	// A refused length does not touch the running upload.
	require.NoError(t, d.OTABegin(ctx, 512))
	sendRange(t, d, img, 0, 120)
	for _, total := range []uint32{0, 500, 1 << 30} {
		assert.Equal(t, StatusErrRange, asStatus(t, d.OTABegin(ctx, total)), "total %d", total)
	}
	expectOTA(t, d, OTAReceiving, 120)
	require.NoError(t, d.OTAData(ctx, 120, img[120:180]))
	expectOTA(t, d, OTAReceiving, 180)

	// A valid one starts over from zero.
	require.NoError(t, d.OTABegin(ctx, 512))
	expectOTA(t, d, OTAReceiving, 0)
	assert.Equal(t, StatusErrRange, asStatus(t, d.OTAData(ctx, 180, img[180:240])))
	sendRange(t, d, img, 0, 512)
	require.NoError(t, d.OTAEnd(ctx))
	expectOTA(t, d, OTAComplete, 512)
}
