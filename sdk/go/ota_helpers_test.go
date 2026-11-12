package tfw

import (
	"context"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// The ADR section 13 helpers send the same frames as the commands they name.
func TestOTAHelpersSendTheirCommand(t *testing.T) {
	ctx := context.Background()
	for _, tc := range []struct {
		name    string
		call    func(d *Device) error
		cmd     Cmd
		payload []byte
	}{
		{"Activate(false)", func(d *Device) error { return d.Activate(ctx, false) }, CmdOTAActivate, []byte{OTAActivateAtReset}},
		{"Activate(true)", func(d *Device) error { return d.Activate(ctx, true) }, CmdOTAActivate, []byte{OTAActivateResetNow}},
		{"Confirm", func(d *Device) error { return d.Confirm(ctx) }, CmdOTAConfirm, nil},
	} {
		t.Run(tc.name, func(t *testing.T) {
			link := &captureLink{}
			require.NoError(t, tc.call(New(link)))
			require.Len(t, link.sent, 1)
			assert.Equal(t, tc.cmd, link.sent[0].Cmd())
			if tc.payload == nil {
				assert.Empty(t, link.sent[0].Payload)
			} else {
				assert.Equal(t, tc.payload, link.sent[0].Payload)
			}
		})
	}
}

// A refusal comes back from the helpers unchanged.
func TestOTAHelpersCarryTheRefusal(t *testing.T) {
	ctx := context.Background()
	for _, tc := range []struct {
		name string
		call func(d *Device) error
	}{
		{"Activate(false)", func(d *Device) error { return d.Activate(ctx, false) }},
		{"Activate(true)", func(d *Device) error { return d.Activate(ctx, true) }},
		{"Confirm", func(d *Device) error { return d.Confirm(ctx) }},
	} {
		t.Run(tc.name, func(t *testing.T) {
			link := &captureLink{cannedLink: cannedLink{status: StatusErrAuthRequired}}
			assert.ErrorIs(t, tc.call(New(link)), StatusErrAuthRequired)
		})
	}
}
