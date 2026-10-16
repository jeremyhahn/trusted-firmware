// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package main

import (
	"errors"
	"fmt"
	"net"
	"net/netip"
	"strings"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
)

// netblockFlags are the --bake-netblock flags as typed.
type netblockFlags struct {
	mac      string
	ip       string // a.b.c.d, or a.b.c.d/prefix in place of --mask
	mask     string
	gateway  string
	endpoint string // host:port, host an IPv4 literal
}

// any reports whether one of the flags was given, so a mode that does not
// take them can refuse them rather than ignore them.
func (f netblockFlags) any() bool {
	return f.mac != "" || f.ip != "" || f.mask != "" || f.gateway != "" || f.endpoint != ""
}

// netConfig parses the flags into the SDK's NetConfig. The encoding itself is
// NetConfig.MarshalBinary's, and so are the field checks it makes (unicast
// MAC, IPv4 only, a non-zero endpoint): this only turns text into addresses.
func (f netblockFlags) netConfig() (tfw.NetConfig, error) {
	var c tfw.NetConfig
	for _, r := range []struct{ name, value string }{
		{"mac", f.mac}, {"ip", f.ip}, {"gateway", f.gateway}, {"endpoint", f.endpoint},
	} {
		if r.value == "" {
			return c, fmt.Errorf("tfwprov: --bake-netblock requires --%s", r.name)
		}
	}

	mac, err := net.ParseMAC(f.mac)
	if err != nil {
		return c, fmt.Errorf("tfwprov: --mac: %w", err)
	}
	if len(mac) != 6 {
		return c, fmt.Errorf("tfwprov: --mac: %d bytes, want 6", len(mac))
	}
	c.MAC = mac

	ipText, _, hasPrefix := strings.Cut(f.ip, "/")
	if c.IP, err = netip.ParseAddr(ipText); err != nil {
		return c, fmt.Errorf("tfwprov: --ip: %w", err)
	}
	switch {
	case hasPrefix && f.mask != "":
		return c, errors.New("tfwprov: give the netmask once: --ip a.b.c.d/prefix or --mask, not both")
	case hasPrefix:
		p, err := netip.ParsePrefix(f.ip)
		if err != nil {
			return c, fmt.Errorf("tfwprov: --ip: %w", err)
		}
		if !c.IP.Is4() {
			return c, errors.New("tfwprov: --ip: must be an IPv4 address")
		}
		m := net.CIDRMask(p.Bits(), 32)
		c.Mask = netip.AddrFrom4([4]byte(m))
	case f.mask != "":
		if c.Mask, err = netip.ParseAddr(f.mask); err != nil {
			return c, fmt.Errorf("tfwprov: --mask: %w", err)
		}
		if !c.Mask.Is4() {
			return c, errors.New("tfwprov: --mask: must be an IPv4 netmask")
		}
		m := c.Mask.As4()
		if ones, bits := net.IPMask(m[:]).Size(); ones == 0 && bits == 0 {
			return c, fmt.Errorf("tfwprov: --mask %s is not a contiguous netmask", f.mask)
		}
	default:
		return c, errors.New("tfwprov: --bake-netblock requires the netmask: --ip a.b.c.d/prefix or --mask")
	}

	if c.Gateway, err = netip.ParseAddr(f.gateway); err != nil {
		return c, fmt.Errorf("tfwprov: --gateway: %w", err)
	}
	if c.Endpoint, err = netip.ParseAddrPort(f.endpoint); err != nil {
		return c, fmt.Errorf("tfwprov: --endpoint: %w", err)
	}
	return c, nil
}

// netblockFromFlags is the network block's bytes, from MarshalBinary.
func netblockFromFlags(f netblockFlags) ([]byte, error) {
	c, err := f.netConfig()
	if err != nil {
		return nil, err
	}
	b, err := c.MarshalBinary()
	if err != nil {
		return nil, fmt.Errorf("tfwprov: %w", err)
	}
	return b, nil
}

// runBake writes the .eep image for --bake-eeprom, --bake-netblock or both.
// Both is one file covering [0x000, 0x0E0): the platform block and the
// network block after it, which is the whole image a bench writes to a part
// it re-provisions.
func runBake(outPath string, platform bool, serialHex, hashHex, opKeyHex string, netblock bool, nf netblockFlags) error {
	var p *bakedPlatform
	if platform {
		p = new(bakedPlatform)
		if err := platformBlockFromFlags(p, serialHex, hashHex, opKeyHex); err != nil {
			return err
		}
	}
	var nb []byte
	if netblock {
		var err error
		if nb, err = netblockFromFlags(nf); err != nil {
			return err
		}
	}
	return writeBakedImage(outPath, p, nb)
}
