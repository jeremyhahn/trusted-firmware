// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build tfwsim

package main

import (
	"bytes"
	"errors"
	"fmt"
	"io"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/simproc"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/tfwsim"
)

var errLostFrame = errors.New("frame lost on the link")

// lossyLink loses one frame the host sends: armed with lose, the lose'th
// write after that never reaches the device.
type lossyLink struct {
	io.ReadWriter
	lose int
}

func (l *lossyLink) Write(p []byte) (int, error) {
	if l.lose > 0 {
		if l.lose--; l.lose == 0 {
			return 0, errLostFrame
		}
	}
	return l.ReadWriter.Write(p)
}

// SetReadDeadline passes the Device's read bound through to the link.
func (l *lossyLink) SetReadDeadline(t time.Time) error {
	if c, ok := l.ReadWriter.(interface{ SetReadDeadline(time.Time) error }); ok {
		return c.SetReadDeadline(t)
	}
	return nil
}

// TestE2EOTAStageResumesAfterALostFrame loses the second OTA_DATA of an
// ota-stage, over serial and over TCP: ota-stage asks OTA_STATUS where the
// session stands, goes on from the device's count, reports its progress on
// stderr, and ota-status then reads the whole image.
func TestE2EOTAStageResumesAfterALostFrame(t *testing.T) {
	sl, el := &lossyLink{}, &lossyLink{}
	platform := tfw.BuildPlatformBlock(tfwsim.TestSerial, [tfw.PubkeyHashLen]byte{}, [tfw.OperatorPubkeyLen]byte{})
	serial, eth := startEthernetOn(t, platform[:], func(args []string) *tfw.Device {
		sl.ReadWriter = simproc.Start(t, simproc.Element, args...)
		return tfw.New(sl, tfw.WithTimeout(10*time.Second))
	}, func(c io.ReadWriter) io.ReadWriter {
		el.ReadWriter = c
		return el
	})
	var progress bytes.Buffer
	prev := stderr
	t.Cleanup(func() { stderr = prev })
	stderr = &progress

	for _, tc := range []struct {
		name string
		link *lossyLink
		dev  *tfw.Device
		img  []byte
	}{
		{"serial", sl, serial, stageImage(1024, 700)},
		{"tcp", el, eth, stageImage(2048, 1500)},
	} {
		progress.Reset()
		// OTA_BEGIN is the first write and the first OTA_DATA the second.
		tc.link.lose = 3
		whole := otaStatus{tfw.OTAComplete.String(), len(tc.img)}
		assert.Equal(t, whole, cliStage(t, tc.dev, tc.img), tc.name)
		assert.Zero(t, tc.link.lose, "%s: no frame was lost", tc.name)
		assert.Equal(t, whole, cliOTAStatus(t, tc.dev), tc.name)
		assert.Contains(t, progress.String(), fmt.Sprintf("%d/%d bytes", len(tc.img), len(tc.img)), tc.name)
	}
}
