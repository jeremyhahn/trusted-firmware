// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build tfwsim

package tfw

import (
	"context"
	"errors"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/simproc"
)

// The wire-protocol row's claims end to end (docs/SPEC.md row wire-protocol,
// docs/wire-protocol/): raw frames written to the real firmware compiled for
// the host, over the same stdio byte stream every tfwsim test uses, and the
// replies decoded by this SDK's codec. Device.Do cannot ask these questions: it
// encodes only well-formed frames and drops a reply whose SEQ it did not
// allocate, which is exactly what a CRC refusal of a hand-made frame carries.

// rawPeer writes bytes to tfwsim unmodified and reads back whole frames.
type rawPeer struct {
	t   *testing.T
	c   *simproc.Conn
	buf []byte
}

func (p *rawPeer) send(b []byte) {
	p.t.Helper()
	_, err := p.c.Write(b)
	require.NoError(p.t, err)
}

// reply returns the next frame that is not an event. An event is OP 0xFF with
// status TFW_OK (api.md "Events"); the 0x7F refusal shares the OP but not the
// status, so it is returned.
func (p *rawPeer) reply() Frame {
	p.t.Helper()
	deadline := time.Now().Add(5 * time.Second)
	tmp := make([]byte, 256)
	for {
		for len(p.buf) > 0 {
			f, n, err := Decode(p.buf)
			if errors.Is(err, ErrShortFrame) {
				break
			}
			require.NoError(p.t, err, "the device sent a frame this codec refuses: % x", p.buf)
			p.buf = p.buf[n:]
			if f.IsEvent() && f.Status == StatusOK {
				continue
			}
			return f
		}
		require.NoError(p.t, p.c.SetReadDeadline(deadline))
		n, err := p.c.Read(tmp)
		require.NoError(p.t, err, "no reply before the deadline")
		p.buf = append(p.buf, tmp[:n]...)
	}
}

// startRaw starts b, lets a Device take the boot event and CAPS, and hands back
// the same stream for raw frames.
func startRaw(t *testing.T, b simproc.Binary) (*Device, *rawPeer, *Caps) {
	t.Helper()
	c := simproc.Start(t, b)
	d := New(c, WithTimeout(5*time.Second))
	caps, err := d.Caps(context.Background())
	require.NoError(t, err)
	return d, &rawPeer{t: t, c: c}, caps
}

func rawRequest(t *testing.T, seq Seq, cmd Cmd, payload []byte) []byte {
	t.Helper()
	b, err := EncodeRequest(seq, cmd, payload)
	require.NoError(t, err)
	return b
}

// featureOf is the CAPS feature bit (api.md "Discovery") that says whether each
// gated command id is in the image's table.
var featureOf = map[Cmd]uint32{
	CmdPinMode: FeatureGPIO, CmdDigitalRead: FeatureGPIO, CmdDigitalWrite: FeatureGPIO,
	CmdTimerSet: FeatureSched, CmdTimerCancel: FeatureSched, CmdTimerQuery: FeatureSched,
	CmdAnalogRead: FeatureAnalog, CmdADCRef: FeatureAnalog,
	CmdPWMWrite: FeaturePWM,
	CmdTone:     FeatureTone, CmdNoTone: FeatureTone,
	CmdPulseIn: FeaturePulse, CmdPulseCapture: FeaturePulse,
	CmdEEPROMRead: FeatureEEPROM, CmdEEPROMWrite: FeatureEEPROM,
	CmdI2CXfer: FeatureI2C, CmdI2CConfig: FeatureI2C,
	CmdSPIXfer: FeatureSPI, CmdSPIConfig: FeatureSPI,
	CmdOneWireReset: FeatureOneWire, CmdOneWireWrite: FeatureOneWire, CmdOneWireRead: FeatureOneWire,
	CmdAttest:        FeatureAttest,
	CmdAuthChallenge: FeatureAuthSession, CmdAuthResponse: FeatureAuthSession,
	CmdAttestCert: FeatureAliasCert,
	CmdAttestCSR:  FeatureAttestCSR,
	CmdProvInfo:   FeatureProvision, CmdProvWriteConfig: FeatureProvision, CmdProvLock: FeatureProvision,
	CmdProvGenKey: FeatureProvision, CmdProvPubKey: FeatureProvision, CmdProvSeedUDS: FeatureProvision,
}

// noBit are the assigned ids with no CAPS bit, where the command answering is
// the capability: APP_INFO (asserted per image below), the OTA family and the
// HSM channel.
var noBit = map[Cmd]bool{
	CmdOTABegin: true, CmdOTAData: true, CmdOTAEnd: true, CmdOTAStatus: true,
	CmdOTAInfo: true, CmdOTAActivate: true, CmdOTAConfirm: true, CmdOTAAuth: true,
	CmdHSMHeartbeat: true, CmdHSMReport: true, CmdHSMVerdict: true, CmdHSMSelect: true, CmdHSMLoad: true,
}

// Every request id 0x00..0x7F, each with an empty payload, gets the reply
// api.md's command table documents on the serial link: OP = id|0x80 and SEQ
// echoed; TFW_ERR_UNKNOWN_CMD for an id with no table entry (the reserved
// WDT_SET, SLEEP and PULSE_COUNT_*, every unassigned id, and every gated
// command the image's CAPS does not report), and anything but that for a
// command in the table. 0x7F is the documented exception: refused, in a frame
// whose OP is 0xFF.
func TestSimEveryCommandIDGetsItsDocumentedReply(t *testing.T) {
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
			_, p, caps := startRaw(t, tc.bin)
			for id := 0x00; id <= 0x7F; id++ {
				cmd := Cmd(id)
				seq := Seq(id + 1)
				p.send(rawRequest(t, seq, cmd, nil))
				f := p.reply()

				require.Equal(t, seq, f.Seq, "id 0x%02x: SEQ not echoed", id)
				require.Equal(t, byte(id)|0x80, f.Op, "id 0x%02x: OP", id)
				if f.Status != StatusOK {
					assert.Empty(t, f.Payload, "id 0x%02x: a refusal carries a payload", id)
				}

				known := true
				switch bit, gated := featureOf[cmd]; {
				case cmd <= CmdSysInfo:
				case gated:
					known = caps.Features&bit != 0
				case cmd == CmdAppInfo:
					known = tc.app
				case noBit[cmd]:
					continue // the answer is the capability; the shape is checked above
				default:
					known = false
				}
				if known {
					assert.NotEqual(t, StatusErrUnknownCmd, f.Status, "id 0x%02x is in the table", id)
				} else {
					assert.Equal(t, StatusErrUnknownCmd, f.Status, "id 0x%02x has no table entry", id)
				}
			}
		})
	}
}
