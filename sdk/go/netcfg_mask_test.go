// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"errors"
	"net/netip"
	"testing"
)

// A static block's mask must be contiguous, the same test tfwprov
// --bake-netblock makes, so both writers refuse the same blocks. 0.0.0.0 is
// contiguous (prefix length 0) and is accepted, as tfwprov accepts it.
func TestNetConfigMarshalRefusesANonContiguousMask(t *testing.T) {
	var c NetConfig
	if err := c.UnmarshalBinary(netcfgVector); err != nil {
		t.Fatalf("UnmarshalBinary: %v", err)
	}

	bad := c
	bad.Mask = netip.MustParseAddr("255.0.255.0")
	b, err := bad.MarshalBinary()
	var nerr *NetConfigError
	if !errors.As(err, &nerr) {
		t.Fatalf("mask 255.0.255.0: err = %v, want a *NetConfigError", err)
	}
	if nerr.Field != "Mask" {
		t.Fatalf("mask 255.0.255.0: field = %q, want Mask", nerr.Field)
	}
	if b != nil {
		t.Fatalf("mask 255.0.255.0: block = % x, want none", b)
	}

	for _, mask := range []string{"255.255.255.0", "0.0.0.0"} {
		ok := c
		ok.Mask = netip.MustParseAddr(mask)
		if _, err := ok.MarshalBinary(); err != nil {
			t.Fatalf("mask %s: MarshalBinary: %v", mask, err)
		}
	}

	dhcp := c
	dhcp.DHCP = true
	dhcp.Mask = netip.Addr{}
	if _, err := dhcp.MarshalBinary(); err != nil {
		t.Fatalf("DHCP with the mask unset: MarshalBinary: %v", err)
	}
}
