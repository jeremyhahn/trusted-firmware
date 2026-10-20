// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"bytes"
	"context"
	"errors"
	"math"
	"testing"
	"time"

	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/vectors"
)

// recLink answers every request as cannedLink does and records each request
// written.
type recLink struct {
	cannedLink
	sent [][]byte
}

func (l *recLink) Write(p []byte) (int, error) {
	l.sent = append(l.sent, bytes.Clone(p))
	return l.cannedLink.Write(p)
}

// sentBy runs one builder on a fresh Device whose next seq is seq and
// returns the request frames it wrote and its error.
func sentBy(seq Seq, payload []byte, call func(d *Device) error) ([][]byte, error) {
	l := &recLink{cannedLink: cannedLink{payload: payload}}
	d := New(l, WithBootWait(0))
	d.seq = seq - 1
	err := call(d)
	return l.sent, err
}

// TestRequestBuildersMatchTheWireVectors: each builder a pinned request
// frame names writes exactly that frame, byte for byte.
func TestRequestBuildersMatchTheWireVectors(t *testing.T) {
	ctx := context.Background()
	otaInfo := framePayload(t, vectors.FrameOTAInfoResp)
	for _, c := range []struct {
		name    string
		want    string
		payload []byte
		call    func(d *Device) error
	}{
		{"SetI2CClock(100000)", vectors.FrameI2CConfigReq, nil, func(d *Device) error { return d.SetI2CClock(ctx, 100_000) }},
		{"ConfigureSPI(3, 2, lsb)", vectors.FrameSPIConfigReq, nil, func(d *Device) error {
			return d.ConfigureSPI(ctx, SPIMode3, SPIClockDiv64, true)
		}},
		{"OTAInfo(1)", vectors.FrameOTAInfoReq, otaInfo, func(d *Device) error { _, err := d.OTAInfo(ctx, 1); return err }},
		{"OTAActivate(1)", vectors.FrameOTAActivateReq, nil, func(d *Device) error { return d.OTAActivate(ctx, 1) }},
		{"OTAConfirm", vectors.FrameOTAConfirmReq, nil, func(d *Device) error { return d.OTAConfirm(ctx) }},
	} {
		want := []byte(c.want)
		sent, err := sentBy(Seq(want[2]), c.payload, c.call)
		if err != nil || len(sent) != 1 || !bytes.Equal(sent[0], want) {
			t.Errorf("%s wrote % x, %v; want % x", c.name, sent, err, want)
		}
	}
}

// TestRequestBuilderArgumentsMoveTheFrame: every other in-range SPI
// configuration, and every in-range I2C clock one bit away from 100 kHz,
// writes a frame other than the pinned one; a bit that leaves the range is
// refused.
func TestRequestBuilderArgumentsMoveTheFrame(t *testing.T) {
	ctx := context.Background()
	spi := []byte(vectors.FrameSPIConfigReq)
	for mode := SPIMode0; mode <= SPIMode3; mode++ {
		for div := SPIClockDiv4; div <= SPIClockDiv128; div++ {
			for _, lsb := range []bool{false, true} {
				sent, err := sentBy(Seq(spi[2]), nil, func(d *Device) error { return d.ConfigureSPI(ctx, mode, div, lsb) })
				same := err == nil && len(sent) == 1 && bytes.Equal(sent[0], spi)
				if same != (mode == SPIMode3 && div == SPIClockDiv64 && lsb) {
					t.Errorf("ConfigureSPI(%d, %d, %v): wrote % x, %v", mode, div, lsb, sent, err)
				}
			}
		}
	}
	i2c := []byte(vectors.FrameI2CConfigReq)
	for bit := range 32 {
		hz := uint32(100_000) ^ 1<<bit
		sent, err := sentBy(Seq(i2c[2]), nil, func(d *Device) error { return d.SetI2CClock(ctx, hz) })
		if hz < I2CClockMin || hz > I2CClockMax {
			requireRefused(t, "SetI2CClock", hz, sent, err, CmdI2CConfig, "clock")
			continue
		}
		if err != nil || len(sent) != 1 || bytes.Equal(sent[0], i2c) {
			t.Errorf("SetI2CClock(%d): wrote % x, %v", hz, sent, err)
		}
	}
}

// requireRefused fails unless err is an *ArgRangeError for cmd's arg and
// nothing was written.
func requireRefused(t *testing.T, name string, v any, sent [][]byte, err error, cmd Cmd, arg string) {
	t.Helper()
	var e *ArgRangeError
	if !errors.As(err, &e) || e.Cmd != cmd || e.Arg != arg || len(sent) != 0 {
		t.Errorf("%s(%v): %v, wrote % x; want an argument range error for %s, nothing sent", name, v, err, sent, arg)
	}
}

// requireSent fails unless exactly one request for cmd carrying payload was
// written and the builder succeeded.
func requireSent(t *testing.T, name string, v any, sent [][]byte, err error, cmd Cmd, payload []byte) {
	t.Helper()
	if err != nil || len(sent) != 1 {
		t.Errorf("%s(%v): %v, wrote % x", name, v, err, sent)
		return
	}
	f, _, derr := Decode(sent[0])
	if derr != nil || f.Cmd() != cmd || !bytes.Equal(f.Payload, payload) {
		t.Errorf("%s(%v): wrote % x, want cmd %#02x payload % x", name, v, sent[0], byte(cmd), payload)
	}
}

// TestRequestBuildersRefuseOutOfRange: every range-checked argument at its
// bounds is sent, laid out by hand here, and one past them -- and at the
// extremes of its type -- is refused with nothing written.
func TestRequestBuildersRefuseOutOfRange(t *testing.T) {
	ctx := context.Background()

	for _, hz := range []uint32{0, I2CClockMin - 1, I2CClockMin, I2CClockMax, I2CClockMax + 1, math.MaxUint32} {
		sent, err := sentBy(1, nil, func(d *Device) error { return d.SetI2CClock(ctx, hz) })
		if hz < I2CClockMin || hz > I2CClockMax {
			requireRefused(t, "SetI2CClock", hz, sent, err, CmdI2CConfig, "clock")
			continue
		}
		requireSent(t, "SetI2CClock", hz, sent, err, CmdI2CConfig, []byte{byte(hz), byte(hz >> 8), byte(hz >> 16), byte(hz >> 24)})
	}

	for v := range 256 {
		sent, err := sentBy(1, nil, func(d *Device) error { return d.ConfigureSPI(ctx, SPIMode(v), SPIClockDiv4, false) })
		if v > 3 {
			requireRefused(t, "ConfigureSPI mode", v, sent, err, CmdSPIConfig, "mode")
		} else {
			requireSent(t, "ConfigureSPI mode", v, sent, err, CmdSPIConfig, []byte{byte(v), 0, 0})
		}
		sent, err = sentBy(1, nil, func(d *Device) error { return d.ConfigureSPI(ctx, SPIMode0, SPIClockDiv(v), false) })
		if v > 3 {
			requireRefused(t, "ConfigureSPI clkdiv", v, sent, err, CmdSPIConfig, "clkdiv")
		} else {
			requireSent(t, "ConfigureSPI clkdiv", v, sent, err, CmdSPIConfig, []byte{0, byte(v), 0})
		}
		sent, err = sentBy(1, nil, func(d *Device) error { return d.OTAActivate(ctx, byte(v)) })
		if v > int(OTAActivateResetNow) {
			requireRefused(t, "OTAActivate", v, sent, err, CmdOTAActivate, "mode")
		} else {
			requireSent(t, "OTAActivate", v, sent, err, CmdOTAActivate, []byte{byte(v)})
		}
		sent, err = sentBy(1, nil, func(d *Device) error { _, err := d.EEPROMRead(ctx, 0x0123, uint8(v)); return err })
		if v == 0 || v > EEPROMChunk {
			requireRefused(t, "EEPROMRead", v, sent, err, CmdEEPROMRead, "length")
		} else {
			requireSent(t, "EEPROMRead", v, sent, err, CmdEEPROMRead, []byte{0x23, 0x01, byte(v)})
		}
	}

	for n := range EEPROMChunk + 2 {
		data := bytes.Repeat([]byte{0x5a}, n)
		sent, err := sentBy(1, nil, func(d *Device) error { return d.EEPROMWrite(ctx, 0x0123, data) })
		if n == 0 || n > EEPROMChunk {
			requireRefused(t, "EEPROMWrite", n, sent, err, CmdEEPROMWrite, "length")
			continue
		}
		requireSent(t, "EEPROMWrite", n, sent, err, CmdEEPROMWrite, append([]byte{0x23, 0x01}, data...))
	}

	const maxMs = 0x7fffffff
	for _, dur := range []time.Duration{
		math.MinInt64, -time.Millisecond, 0, time.Millisecond - 1, time.Millisecond,
		maxMs * time.Millisecond, (maxMs + 1) * time.Millisecond, math.MaxInt64,
	} {
		sent, err := sentBy(1, []byte{0}, func(d *Device) error { _, err := d.TimerSet(ctx, 13, true, dur); return err })
		ms := dur.Milliseconds()
		if ms < 1 || ms > maxMs {
			requireRefused(t, "TimerSet", dur, sent, err, CmdTimerSet, "duration")
			continue
		}
		requireSent(t, "TimerSet", dur, sent, err, CmdTimerSet, []byte{13, 1, byte(ms), byte(ms >> 8), byte(ms >> 16), byte(ms >> 24)})
	}

	for _, dur := range []time.Duration{
		math.MinInt64, -time.Microsecond, -1, 0, time.Microsecond - 1,
		math.MaxUint32 * time.Microsecond, (math.MaxUint32 + 1) * time.Microsecond, math.MaxInt64,
	} {
		us := dur.Microseconds()
		ok := dur >= 0 && us <= math.MaxUint32
		le := []byte{byte(us), byte(us >> 8), byte(us >> 16), byte(us >> 24)}
		sent, err := sentBy(1, []byte{0, 0, 0, 0}, func(d *Device) error { _, err := d.PulseIn(ctx, 7, true, dur); return err })
		if ok {
			requireSent(t, "PulseIn", dur, sent, err, CmdPulseIn, append([]byte{7, 1}, le...))
		} else {
			requireRefused(t, "PulseIn", dur, sent, err, CmdPulseIn, "timeout")
		}
		sent, err = sentBy(1, []byte{0}, func(d *Device) error { _, err := d.PulseCapture(ctx, 7, false, 9, dur); return err })
		if ok {
			requireSent(t, "PulseCapture", dur, sent, err, CmdPulseCapture, append([]byte{7, 0, 9}, le...))
		} else {
			requireRefused(t, "PulseCapture", dur, sent, err, CmdPulseCapture, "timeout")
		}
	}
}

// TestRequestBuildersRefuseAnOversizePayload: a transfer whose request would
// pass MaxPayload is refused with a *PayloadSizeError and nothing written;
// one at MaxPayload is sent whole.
func TestRequestBuildersRefuseAnOversizePayload(t *testing.T) {
	ctx := context.Background()
	for _, c := range []struct {
		name   string
		header int
		cmd    Cmd
		call   func(d *Device, w []byte) error
	}{
		{"I2CTransfer", 2, CmdI2CXfer, func(d *Device, w []byte) error { _, err := d.I2CTransfer(ctx, 0x50, w, 4); return err }},
		{"SPITransfer", 1, CmdSPIXfer, func(d *Device, w []byte) error { _, err := d.SPITransfer(ctx, 10, w); return err }},
	} {
		for _, n := range []int{MaxPayload - c.header, MaxPayload - c.header + 1, 255} {
			w := bytes.Repeat([]byte{0xc3}, n)
			sent, err := sentBy(1, nil, func(d *Device) error { return c.call(d, w) })
			if c.header+n <= MaxPayload {
				if err != nil || len(sent) != 1 || len(sent[0]) != MaxPayload+frameOverhead+2 {
					t.Errorf("%s of %d bytes: %v, wrote % x", c.name, n, err, sent)
				}
				continue
			}
			var e *PayloadSizeError
			if !errors.As(err, &e) || len(sent) != 0 {
				t.Errorf("%s of %d bytes: %v, wrote % x", c.name, n, err, sent)
			}
		}
	}
}
