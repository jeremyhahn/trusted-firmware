// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"context"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// Every HSM_* state reply is [flags:1][selected:1][refused:4], refused
// little-endian over all four bytes (src/core/hsm_cmd.c). The reply here sets
// every flag bit and a distinct value in each byte of refused, so a decoder
// that dropped the upper half, read the wrong offset or masked flags fails.
func TestHSMStateDecodesEveryByteOfTheReply(t *testing.T) {
	want := &HSMState{Flags: HSMSetHeld | HSMLocked | HSMWiped, Selected: 0xFF, Refused: 0x04030201}
	verbs := []struct {
		name string
		call func(d *Device) (*HSMState, error)
	}{
		{"HSMHeartbeat", func(d *Device) (*HSMState, error) { return d.HSMHeartbeat(context.Background()) }},
		{"HSMReport", func(d *Device) (*HSMState, error) { return d.HSMReport(context.Background(), HSMSignalHotplug) }},
		{"HSMVerdict", func(d *Device) (*HSMState, error) { return d.HSMVerdict(context.Background(), HSMVerdictClear) }},
		{"HSMSelect", func(d *Device) (*HSMState, error) { return d.HSMSelect(context.Background(), 0) }},
		{"HSMLoad", func(d *Device) (*HSMState, error) { return d.HSMLoad(context.Background(), []byte{0xD8}) }},
	}
	for _, v := range verbs {
		link := &captureLink{cannedLink: cannedLink{payload: []byte{0x07, 0xFF, 0x01, 0x02, 0x03, 0x04}}}
		st, err := v.call(New(link))
		require.NoError(t, err, v.name)
		assert.Equal(t, want, st, v.name)
	}
}
