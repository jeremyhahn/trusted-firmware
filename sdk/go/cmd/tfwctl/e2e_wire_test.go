// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build tfwsim

package main

import (
	"bytes"
	"context"
	"encoding/hex"
	"errors"
	"io"
	"strings"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/simproc"
)

// The wire-protocol row's claims that a tfwctl verb can ask (docs/SPEC.md row
// wire-protocol, docs/wire-protocol/). What only a hand-made frame can ask
// (over-length and short LEN, a bad CRC, garbage between frames, ids no verb
// sends) is pinned by the C suite: test_frame.c, test_transport.c and
// test_dispatch.c.

// wireStart starts b and puts tfwctl's device on its serial link.
func wireStart(t *testing.T, b simproc.Binary) *tfw.Device {
	t.Helper()
	return tfw.New(simproc.Start(t, b), tfw.WithTimeout(5*time.Second))
}

// wireSys is SYS_INFO as `tfwctl sys --json` prints it.
func wireSys(t *testing.T, dev *tfw.Device) tfw.SysInfo {
	t.Helper()
	var si tfw.SysInfo
	cliJSON(t, dev, &si, "sys")
	return si
}

// The host SDK and the firmware agree on every limit the row's docs state:
// CAPS's max payload and protocol version are this SDK's MaxPayload and
// ProtoVersion, on serial and over TCP; a PING of exactly MaxPayload bytes,
// the largest frame either side may send (LEN = TFW_MAX_LEN), echoes whole;
// one byte more is refused by the encoder before the wire, so the firmware's
// framer counts nothing. The same frame built by hand, refused by the framer,
// is test_transport.c's test_an_over_length_frame_is_dropped_and_the_next_answered.
func TestWireHostAndFirmwareAgreeOnEveryLimit(t *testing.T) {
	for _, b := range []simproc.Binary{simproc.Software, simproc.Element} {
		t.Run(b.Name, func(t *testing.T) {
			dev := wireStart(t, b)

			c := cliCaps(t, dev)
			assert.Equal(t, uint8(tfw.MaxPayload), c.MaxPayload)
			assert.Equal(t, tfw.ProtoVersionMajor, c.ProtoMajor)
			assert.Equal(t, tfw.ProtoVersionMinor, c.ProtoMinor)

			full := strings.Repeat("Z", tfw.MaxPayload)
			var echo struct{ Echo string }
			cliJSON(t, dev, &echo, "ping", full)
			assert.Equal(t, full, echo.Echo)

			err := run(context.Background(), dev, "ping", []string{full + "Z"}, true, io.Discard)
			var pse *tfw.PayloadSizeError
			require.ErrorAs(t, err, &pse)

			si := wireSys(t, dev)
			assert.Zero(t, si.ErrFormat, "the refused PING reached the wire")
			assert.Zero(t, si.ErrCRC)
		})
	}
	t.Run("tcp", func(t *testing.T) {
		c := cliCaps(t, startEthernet(t).eth)
		assert.Equal(t, uint8(tfw.MaxPayload), c.MaxPayload)
		assert.Equal(t, tfw.ProtoVersionMajor, c.ProtoMajor)
		assert.Equal(t, tfw.ProtoVersionMinor, c.ProtoMinor)
	})
}

// wireNoisyLink is a serial line that damages the next request tfwctl writes
// in the way the row names, and passes everything else through.
type wireNoisyLink struct {
	io.ReadWriter
	damage func([]byte) []byte
}

func (l *wireNoisyLink) Write(b []byte) (int, error) {
	if l.damage != nil {
		out := l.damage(append([]byte(nil), b...))
		l.damage = nil
		if _, err := l.ReadWriter.Write(out); err != nil {
			return 0, err
		}
		return len(b), nil
	}
	return l.ReadWriter.Write(b)
}

// On a noisy line, tfwctl's request with a bit flipped is answered
// TFW_ERR_CRC under its own SEQ, so the verb reports that status; a stray
// start byte, a LEN below the minimum and garbage ahead of a request cost
// nothing but the noise, and `tfwctl sys` reports what the framer refused
// (docs/wire-protocol/architecture.md "Resync"). The same bytes through the
// transport alone are test_transport.c's
// test_the_link_resynchronises_and_counts_what_it_refused.
func TestWireNoisyLinkIsRefusedResynchronisedAndCounted(t *testing.T) {
	link := &wireNoisyLink{ReadWriter: simproc.Start(t, simproc.Software)}
	dev := tfw.New(link, tfw.WithTimeout(5*time.Second))
	cliCaps(t, dev)

	link.damage = func(b []byte) []byte { b[5] ^= 0x01; return b } // a payload byte
	assert.Equal(t, tfw.StatusErrCRC, cliStatus(t, dev, "ping", "abc"))

	for _, noise := range []struct {
		name  string
		bytes []byte
	}{
		{"over", []byte{tfw.SOF}},        // the real SOF is read as an over-length LEN
		{"under", []byte{tfw.SOF, 0x01}}, // LEN 1, below the minimum
		{"garbage", bytes.Repeat([]byte{0x00, 0xFF, 0x5A, 0x81, 0x13}, 40)},
	} {
		link.damage = func(b []byte) []byte { return append(noise.bytes, b...) }
		var echo struct{ Echo string }
		cliJSON(t, dev, &echo, "ping", noise.name)
		assert.Equal(t, noise.name, echo.Echo, "the request after the %s", noise.name)
	}

	si := wireSys(t, dev)
	assert.Equal(t, uint16(1), si.ErrCRC)
	assert.Equal(t, uint16(2), si.ErrFormat)
}

// wireGated is one gated command a verb sends, and the CAPS bit (api.md
// "Discovery") that says whether the image's table has it.
type wireGated struct {
	feature uint32
	verb    string
	args    []string
}

// wireAnswer is the status a verb's request was answered with: TFW_OK when
// the verb succeeded, the device's status when it refused.
func wireAnswer(t *testing.T, dev *tfw.Device, verb string, args ...string) tfw.Status {
	t.Helper()
	err := run(context.Background(), dev, verb, args, true, io.Discard)
	if err == nil {
		return tfw.StatusOK
	}
	var st tfw.Status
	require.True(t, errors.As(err, &st), "%s %v: %v, want a device status", verb, args, err)
	return st
}

// Each gated command a verb sends is answered TFW_ERR_UNKNOWN_CMD exactly when
// the image's CAPS does not report its feature, and APP_INFO exactly when the
// image has no application, in every image. The reserved and unassigned ids,
// which no verb sends, are test_transport.c's
// test_every_request_id_is_answered_under_its_seq_and_op, test_dispatch.c's
// test_the_five_reserved_ids_have_no_handler and
// test_no_command_id_reaches_the_event_marker, and test_dispatch_mutate.c's
// test_mutated_payloads_never_overrun_or_misreport.
func TestWireGatedCommandsAnswerAsCapsReports(t *testing.T) {
	attest := hex.EncodeToString(bytes.Repeat([]byte{0x01}, tfw.AttestNonceLen))
	alias := hex.EncodeToString(bytes.Repeat([]byte{0x02}, tfw.AliasNonceLen))
	gated := []wireGated{
		{tfw.FeatureGPIO, "read", []string{"13"}},
		{tfw.FeatureSched, "timer-query", []string{"13"}},
		{tfw.FeatureEEPROM, "eeprom-read", []string{"0", "4"}},
		{tfw.FeatureAttest, "attest", []string{attest}},
		{tfw.FeatureAliasCert, "attest-cert", []string{alias}},
	}
	for _, tc := range []struct {
		bin simproc.Binary
		app bool
	}{
		{simproc.Software, false},
		{simproc.Element, false},
		{simproc.Prov, false},
		{simproc.App, true},
	} {
		t.Run(tc.bin.Name, func(t *testing.T) {
			dev := wireStart(t, tc.bin)
			c := cliCaps(t, dev)
			for _, g := range gated {
				st := wireAnswer(t, dev, g.verb, g.args...)
				if c.Has(g.feature) {
					assert.NotEqual(t, tfw.StatusErrUnknownCmd, st, "%s: CAPS reports it, the table has no entry", g.verb)
				} else {
					assert.Equal(t, tfw.StatusErrUnknownCmd, st, "%s: CAPS does not report it, the table answers", g.verb)
				}
			}
			st := wireAnswer(t, dev, "app")
			if tc.app {
				assert.Equal(t, tfw.StatusOK, st, "app")
			} else {
				assert.Equal(t, tfw.StatusErrUnknownCmd, st, "app")
			}
		})
	}
}
