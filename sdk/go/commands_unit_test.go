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
		{"SetADCReference", CmdADCRef, []byte{0x03}, func(d *Device) error { return d.SetADCReference(ctx, ADCReference(3)) }},
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
