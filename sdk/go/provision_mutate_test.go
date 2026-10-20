// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"context"
	"testing"
)

// TestProvInfoMutations: provision_test.go's PROV_INFO reply, data zone open
// (0x55) and config zone locked (0x00). Every byte is read, but a lock byte
// is 0x55 or not (ElementInfo: anything else means locked), so a change of
// the locked byte to another value that is not 0x55 is documented to read
// the same.
func TestProvInfoMutations(t *testing.T) {
	valid := provInfoReply(0x55, 0x00)
	mutateReply(t, "PROV_INFO", valid, 600, func(d *Device) (any, error) {
		return d.ProvInfo(context.Background())
	}, func(m mutation) bool {
		return m.lo == 37 && m.b[37] != 0x55
	})
}

// TestProvPubKeyMutations: a 64-byte X||Y reply to PROV_PUBKEY and to
// PROV_GENKEY, every byte of which is returned. At MaxPayload already, so no
// append can be framed.
func TestProvPubKeyMutations(t *testing.T) {
	valid := make([]byte, 64)
	for i := range valid {
		valid[i] = byte(0x40 + i)
	}
	mutateReply(t, "PROV_PUBKEY", valid, 601, func(d *Device) (any, error) {
		return d.ProvPubKey(context.Background(), SlotDeviceID)
	}, readNothing)
	mutateReply(t, "PROV_GENKEY", valid, 602, func(d *Device) (any, error) {
		return d.ProvGenKey(context.Background(), SlotDeviceID)
	}, readNothing)
}
