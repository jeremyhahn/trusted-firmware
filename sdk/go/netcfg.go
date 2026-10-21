// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"context"
	"crypto/sha256"
	"encoding/binary"
	"fmt"
	"net"
	"net/netip"
)

// The network block, [0x0C0, 0x0E0) on the device -- include/tfw/eth.h's
// TFW_NETCFG_* is the wire contract this mirrors, and src/net/netcfg.c's
// tfw_netcfg_decode is the same layout on the firmware's side of it. It sits
// at the first host-writable address, so it is written with EEPROM_WRITE over
// serial like any other host data; docs/ethernet-transport/api.md has the
// layout.
//
// The Ethernet link reads it each time it opens a connection, so a block
// written while the device runs is used at the next attempt, no reset needed.
const (
	NetConfigAddr = 0x0C0
	NetConfigLen  = 32

	netcfgVersion = 1

	// netcfgFlagDHCP is flags bit 0, TFW_NETCFG_DHCP: the device takes its
	// IP, mask and gateway from a DHCP lease instead of bytes 12..23.
	netcfgFlagDHCP = 0x01

	netcfgOffFlags   = 5
	netcfgOffMAC     = 6
	netcfgOffIP      = 12
	netcfgOffMask    = 16
	netcfgOffGateway = 20
	netcfgOffPeer    = 24
	netcfgOffPort    = 28
	netcfgOffCRC     = 30
)

// NetConfig is the device's network block: its own address, and the endpoint
// it connects to. On a network with no router, Gateway is 0.0.0.0. MAC is the
// shield's own address when it is known, and DerivedMAC otherwise.
//
// DHCP sets flags bit 0: the device leases its IP, mask and gateway (RFC 2131)
// and ignores the three fields here, which may be left unset and are then
// written as 0.0.0.0. Only an image built with DHCP=1 carries the client; any
// other image keeps its link down for such a block. docs/ethernet-transport/
// has the client.
type NetConfig struct {
	DHCP     bool
	MAC      net.HardwareAddr
	IP       netip.Addr
	Mask     netip.Addr
	Gateway  netip.Addr
	Endpoint netip.AddrPort
}

// NetConfigError names the NetConfig field MarshalBinary cannot encode.
type NetConfigError struct {
	Field  string
	Reason string
}

func (e *NetConfigError) Error() string {
	return fmt.Sprintf("tfw: network block: %s %s", e.Field, e.Reason)
}

// macDomain separates the MAC derivation from every other use of the same
// identity bytes.
const macDomain = "tfw-eth-mac-v1"

// DerivedMAC is the MAC address a device uses until its shield's own is
// provisioned. A W5100 or W5500 has no MAC ROM -- the address is whatever the
// network block says -- and an authentic shield carries its address on a
// sticker. Without the sticker's value, write this one.
//
// It is the first six bytes of SHA-256("tfw-eth-mac-v1" || id), with bit 1 of
// the first octet set (locally administered) and bit 0 cleared (unicast). On
// the element tier id is the DeviceID public key, the 64-byte X||Y of its
// P-256 point. The software tier has no public key, its identity being a
// symmetric secret, so there id is the 16-byte serial: the DeviceID field of
// its ATTEST evidence.
//
// The device does not derive anything. The block holds the one MAC it uses,
// derived or provisioned, so a sticker value replaces this one by writing it.
func DerivedMAC(id []byte) net.HardwareAddr {
	h := sha256.Sum256(append([]byte(macDomain), id...))
	mac := make(net.HardwareAddr, 6)
	copy(mac, h[:6])
	mac[0] = (mac[0] | 0x02) &^ 0x01
	return mac
}

// MarshalBinary serialises the block: magic, version, the fields, and the
// frame CRC over everything before it. It refuses what the device would
// treat as absent -- a zero endpoint address or port -- and a multicast MAC,
// rather than write a block the link will never use.
func (c NetConfig) MarshalBinary() ([]byte, error) {
	if len(c.MAC) != 6 {
		return nil, &NetConfigError{Field: "MAC", Reason: "must be 6 bytes"}
	}
	if c.MAC[0]&0x01 != 0 {
		return nil, &NetConfigError{Field: "MAC", Reason: "must be a unicast address"}
	}
	addrs := []struct {
		name string
		a    netip.Addr
		off  int
	}{
		{"IP", c.IP, netcfgOffIP},
		{"Mask", c.Mask, netcfgOffMask},
		{"Gateway", c.Gateway, netcfgOffGateway},
		{"Endpoint", c.Endpoint.Addr(), netcfgOffPeer},
	}
	b := make([]byte, NetConfigLen)
	copy(b, "TFNC")
	b[4] = netcfgVersion
	if c.DHCP {
		b[netcfgOffFlags] = netcfgFlagDHCP
	}
	copy(b[netcfgOffMAC:], c.MAC)
	for _, a := range addrs {
		if c.DHCP && a.off != netcfgOffPeer && !a.a.IsValid() {
			continue // leased: left 0.0.0.0
		}
		if !a.a.Is4() {
			return nil, &NetConfigError{Field: a.name, Reason: "must be an IPv4 address"}
		}
		v := a.a.As4()
		copy(b[a.off:], v[:])
	}
	if c.Endpoint.Addr().IsUnspecified() {
		return nil, &NetConfigError{Field: "Endpoint", Reason: "address must not be 0.0.0.0"}
	}
	if c.Endpoint.Port() == 0 {
		return nil, &NetConfigError{Field: "Endpoint", Reason: "port must not be 0"}
	}
	binary.BigEndian.PutUint16(b[netcfgOffPort:], c.Endpoint.Port())
	binary.LittleEndian.PutUint16(b[netcfgOffCRC:], CRC16(b[:netcfgOffCRC]))
	return b, nil
}

// WriteNetConfig writes the block in EEPROMChunk-sized EEPROM_WRITEs. Over
// serial: EEPROM_WRITE is not on the Ethernet allow-list, so a device can
// never be re-pointed from the network it is pointed at.
//
// A write interrupted part-way leaves a block whose CRC does not match, which
// the device treats as absent; writing it again completes it.
func (d *Device) WriteNetConfig(ctx context.Context, c NetConfig) error {
	b, err := c.MarshalBinary()
	if err != nil {
		return err
	}
	for off := 0; off < len(b); off += EEPROMChunk {
		if err := d.EEPROMWrite(ctx, uint16(NetConfigAddr+off), b[off:off+EEPROMChunk]); err != nil {
			return err
		}
	}
	return nil
}

// UnmarshalBinary decodes a 32-byte network block. It refuses what the
// firmware's tfw_netcfg_decode treats as absent -- a missing magic, another
// version, a CRC mismatch, a zero endpoint address or port -- so a block it
// returns is one the device would bring its link up with.
func (c *NetConfig) UnmarshalBinary(b []byte) error {
	if len(b) != NetConfigLen {
		return &NetConfigError{Field: "block", Reason: fmt.Sprintf("is %d bytes, want %d", len(b), NetConfigLen)}
	}
	if string(b[:4]) != "TFNC" {
		return &NetConfigError{Field: "block", Reason: "has no TFNC magic: not provisioned"}
	}
	if b[4] != netcfgVersion {
		return &NetConfigError{Field: "block", Reason: fmt.Sprintf("version %d is not %d", b[4], netcfgVersion)}
	}
	if CRC16(b[:netcfgOffCRC]) != binary.LittleEndian.Uint16(b[netcfgOffCRC:]) {
		return &NetConfigError{Field: "block", Reason: "CRC does not match"}
	}
	peer := netip.AddrFrom4([4]byte(b[netcfgOffPeer : netcfgOffPeer+4]))
	if peer.IsUnspecified() {
		return &NetConfigError{Field: "Endpoint", Reason: "address must not be 0.0.0.0"}
	}
	port := binary.BigEndian.Uint16(b[netcfgOffPort:])
	if port == 0 {
		return &NetConfigError{Field: "Endpoint", Reason: "port must not be 0"}
	}
	*c = NetConfig{
		DHCP:     b[netcfgOffFlags]&netcfgFlagDHCP != 0,
		MAC:      append(net.HardwareAddr(nil), b[netcfgOffMAC:netcfgOffMAC+6]...),
		IP:       netip.AddrFrom4([4]byte(b[netcfgOffIP : netcfgOffIP+4])),
		Mask:     netip.AddrFrom4([4]byte(b[netcfgOffMask : netcfgOffMask+4])),
		Gateway:  netip.AddrFrom4([4]byte(b[netcfgOffGateway : netcfgOffGateway+4])),
		Endpoint: netip.AddrPortFrom(peer, port),
	}
	return nil
}

// ReadNetConfig reads the network block back with EEPROM_READ, in
// EEPROMChunk pieces, and decodes it. The block sits at
// TFW_EEPROM_RESERVED_END, above the reads an element-tier build gates, so it
// needs no operator session.
func (d *Device) ReadNetConfig(ctx context.Context) (NetConfig, error) {
	b := make([]byte, 0, NetConfigLen)
	for off := 0; off < NetConfigLen; off += EEPROMChunk {
		p, err := d.EEPROMRead(ctx, uint16(NetConfigAddr+off), EEPROMChunk)
		if err != nil {
			return NetConfig{}, err
		}
		b = append(b, p...)
	}
	var c NetConfig
	if err := c.UnmarshalBinary(b); err != nil {
		return NetConfig{}, err
	}
	return c, nil
}
