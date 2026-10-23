package tfw

import (
	"context"
	"testing"

	"github.com/stretchr/testify/require"
)

// recordingLink answers like cannedLink and keeps the request frame, so a
// test can pin the exact bytes a command put on the wire.
type recordingLink struct {
	cannedLink
	cmd     Cmd
	payload []byte
}

func (l *recordingLink) Write(p []byte) (int, error) {
	f, _, err := Decode(p)
	if err != nil {
		return 0, err
	}
	l.cmd = f.Cmd()
	l.payload = append([]byte(nil), f.Payload...)
	return l.cannedLink.Write(p)
}

// TestThinCommandsEncodeTheirPayloads covers the one-line commands the tfwsim
// suite does not reach. The expected payloads are literals.
func TestThinCommandsEncodeTheirPayloads(t *testing.T) {
	ctx := context.Background()
	for _, tc := range []struct {
		name string
		cmd  Cmd
		want []byte
		call func(d *Device) error
	}{
		{"Reset", CmdReset, []byte{0x02}, func(d *Device) error { return d.Reset(ctx, ResetMode(2)) }},
		{"SetPinMode", CmdPinMode, []byte{13, 0x01}, func(d *Device) error { return d.SetPinMode(ctx, 13, PinOutput) }},
		{"TimerCancel final", CmdTimerCancel, []byte{7, 0x01}, func(d *Device) error { return d.TimerCancel(ctx, 7, true) }},
		{"TimerCancel hold", CmdTimerCancel, []byte{7, 0x00}, func(d *Device) error { return d.TimerCancel(ctx, 7, false) }},
		{"SetADCReference", CmdADCRef, []byte{0x03}, func(d *Device) error { return d.SetADCReference(ctx, ADCReference(3)) }},
		{"Tone", CmdTone, []byte{9, 0xB8, 0x01}, func(d *Device) error { return d.Tone(ctx, 9, 440) }},
		{"NoTone", CmdNoTone, []byte{9}, func(d *Device) error { return d.NoTone(ctx, 9) }},
		{"OneWireWrite", CmdOneWireWrite, []byte{4, 0xCC, 0x44}, func(d *Device) error { return d.OneWireWrite(ctx, 4, []byte{0xCC, 0x44}) }},
	} {
		t.Run(tc.name, func(t *testing.T) {
			link := &recordingLink{}
			require.NoError(t, tc.call(New(link)))
			require.Equal(t, tc.cmd, link.cmd)
			require.Equal(t, tc.want, link.payload)

			// An unknown status comes back as that status, never a known one.
			bad := &recordingLink{cannedLink: cannedLink{status: 0xEE}}
			require.Equal(t, Status(0xEE), asStatus(t, tc.call(New(bad))))
		})
	}
}

func TestReadingTransfersReturnThePayload(t *testing.T) {
	ctx := context.Background()
	for _, tc := range []struct {
		name string
		cmd  Cmd
		want []byte
		call func(d *Device) ([]byte, error)
	}{
		{"SPITransfer", CmdSPIXfer, []byte{10, 0x9F, 0x00}, func(d *Device) ([]byte, error) { return d.SPITransfer(ctx, 10, []byte{0x9F, 0x00}) }},
		{"OneWireRead", CmdOneWireRead, []byte{4, 2}, func(d *Device) ([]byte, error) { return d.OneWireRead(ctx, 4, 2) }},
	} {
		t.Run(tc.name, func(t *testing.T) {
			link := &recordingLink{cannedLink: cannedLink{payload: []byte{0xEF, 0x40}}}
			got, err := tc.call(New(link))
			require.NoError(t, err)
			require.Equal(t, []byte{0xEF, 0x40}, got)
			require.Equal(t, tc.cmd, link.cmd)
			require.Equal(t, tc.want, link.payload)

			bad := &recordingLink{cannedLink: cannedLink{status: 0x7F}}
			got, err = tc.call(New(bad))
			require.Nil(t, got)
			require.Equal(t, Status(0x7F), asStatus(t, err))
		})
	}
}
