// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"bytes"
	"encoding/binary"
	"errors"
	"net/netip"
	"testing"

	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/vectors"
)

// netcfgByHand decodes a network block from docs/ethernet-transport/api.md's
// offsets, written out here rather than read from netcfg.go.
func netcfgByHand(b []byte) NetConfig {
	return NetConfig{
		MAC:      bytes.Clone(b[6:12]),
		IP:       netip.AddrFrom4([4]byte(b[12:16])),
		Mask:     netip.AddrFrom4([4]byte(b[16:20])),
		Gateway:  netip.AddrFrom4([4]byte(b[20:24])),
		Endpoint: netip.AddrPortFrom(netip.AddrFrom4([4]byte(b[24:28])), binary.BigEndian.Uint16(b[28:30])),
	}
}

func netcfgEqual(a, b NetConfig) bool {
	return bytes.Equal(a.MAC, b.MAC) && a.IP == b.IP && a.Mask == b.Mask &&
		a.Gateway == b.Gateway && a.Endpoint == b.Endpoint
}

// TestNetConfigMutations: every single-byte change, flip, truncation and
// append of the pinned block is a *NetConfigError (a CRC-16 catches every
// single-byte change), never a panic; a seeded multi-byte change the CRC
// happens to pass decodes to the bytes it carries.
func TestNetConfigMutations(t *testing.T) {
	var orig NetConfig
	if err := orig.UnmarshalBinary(netcfgVector); err != nil {
		t.Fatalf("the pinned block does not decode: %v", err)
	}
	eachMutation(netcfgVector, 400, func(m mutation) {
		var c NetConfig
		var err error
		noPanic(t, m, func() { err = c.UnmarshalBinary(m.b) })
		if err != nil {
			var e *NetConfigError
			if !errors.As(err, &e) {
				t.Fatalf("%v: error %v is not a *NetConfigError", m, err)
			}
			return
		}
		if m.kind != "random" {
			t.Fatalf("%v: decoded to %+v", m, c)
		}
		if !netcfgEqual(c, netcfgByHand(m.b)) {
			t.Fatalf("%v: decoded to %+v, not the bytes it carries", m, c)
		}
	})
}

// TestNetConfigMutationsPastTheCRC: every single-byte change of the block's
// first 30 bytes, its CRC made good again, so the change reaches the field
// checks. A changed magic or version is refused; the flags byte, reserved
// (api.md, offset 5), decodes to the original; a zero endpoint address or
// port is refused; any other change decodes to exactly the bytes it carries.
func TestNetConfigMutationsPastTheCRC(t *testing.T) {
	// CRC16 is pinned by its own check vector before it is trusted here.
	if CRC16([]byte(vectors.CRCCheckInput)) != vectors.CRCCheckResult {
		t.Fatal("CRC16 does not answer its check vector")
	}
	orig := netcfgByHand(netcfgVector)
	for off := range 30 {
		for x := range 256 {
			if byte(x) == netcfgVector[off] {
				continue
			}
			b := bytes.Clone(netcfgVector)
			b[off] = byte(x)
			binary.LittleEndian.PutUint16(b[30:], CRC16(b[:30]))
			m := mutation{kind: "byte+crc", lo: off, hi: off + 1, b: b}
			var c NetConfig
			var err error
			noPanic(t, m, func() { err = c.UnmarshalBinary(b) })
			zeroPeer := bytes.Equal(b[24:28], []byte{0, 0, 0, 0})
			zeroPort := b[28] == 0 && b[29] == 0
			switch {
			case off < 5 || zeroPeer || zeroPort:
				if err == nil {
					t.Fatalf("%v: decoded to %+v", m, c)
				}
			case err != nil:
				t.Fatalf("%v: %v", m, err)
			case off == 5:
				if !netcfgEqual(c, orig) {
					t.Fatalf("%v: the reserved flags byte moved the block to %+v", m, c)
				}
			default:
				if netcfgEqual(c, orig) || !netcfgEqual(c, netcfgByHand(b)) {
					t.Fatalf("%v: decoded to %+v", m, c)
				}
			}
		}
	}
}
