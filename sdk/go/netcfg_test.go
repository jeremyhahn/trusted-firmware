// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"bytes"
	"context"
	"encoding/binary"
	"errors"
	"net"
	"net/netip"
	"testing"
)

// The same 32 bytes test/unit/test_link_eth.c pins as NETCFG, literal, CRC
// included: what this package writes is what the device's decoder accepts.
var netcfgVector = []byte{
	'T', 'F', 'N', 'C', 0x01, 0x00,
	0x02, 0x00, 0x00, 0x00, 0x00, 0x01,
	192, 0, 2, 10, 255, 255, 255, 0, 192, 0, 2, 1, 192, 0, 2, 20,
	0x1B, 0x6C,
	0x79, 0xF8,
}

func testNetConfig() NetConfig {
	return NetConfig{
		MAC:      net.HardwareAddr{0x02, 0x00, 0x00, 0x00, 0x00, 0x01},
		IP:       netip.MustParseAddr("192.0.2.10"),
		Mask:     netip.MustParseAddr("255.255.255.0"),
		Gateway:  netip.MustParseAddr("192.0.2.1"),
		Endpoint: netip.MustParseAddrPort("192.0.2.20:7020"),
	}
}

func TestNetConfigMarshalsToThePinnedBlock(t *testing.T) {
	got, err := testNetConfig().MarshalBinary()
	if err != nil {
		t.Fatalf("MarshalBinary: %v", err)
	}
	if !bytes.Equal(got, netcfgVector) {
		t.Fatalf("block = % x\nwant    % x", got, netcfgVector)
	}
}

func TestNetConfigRefusesWhatTheDeviceWouldTreatAsAbsent(t *testing.T) {
	for _, tc := range []struct {
		name  string
		field string
		edit  func(*NetConfig)
	}{
		{"short MAC", "MAC", func(c *NetConfig) { c.MAC = c.MAC[:5] }},
		{"multicast MAC", "MAC", func(c *NetConfig) {
			c.MAC = net.HardwareAddr{0x03, 0x00, 0x00, 0x00, 0x00, 0x01}
		}},
		{"IPv6 device address", "IP", func(c *NetConfig) { c.IP = netip.MustParseAddr("2001:db8::1") }},
		{"unset mask", "Mask", func(c *NetConfig) { c.Mask = netip.Addr{} }},
		{"unset gateway", "Gateway", func(c *NetConfig) { c.Gateway = netip.Addr{} }},
		{"zero endpoint address", "Endpoint", func(c *NetConfig) {
			c.Endpoint = netip.MustParseAddrPort("0.0.0.0:7020")
		}},
		{"zero endpoint port", "Endpoint", func(c *NetConfig) {
			c.Endpoint = netip.MustParseAddrPort("192.0.2.20:0")
		}},
	} {
		t.Run(tc.name, func(t *testing.T) {
			c := testNetConfig()
			tc.edit(&c)
			b, err := c.MarshalBinary()
			var nerr *NetConfigError
			if !errors.As(err, &nerr) {
				t.Fatalf("err = %v, want a *NetConfigError", err)
			}
			if nerr.Field != tc.field {
				t.Fatalf("field = %q, want %q", nerr.Field, tc.field)
			}
			if b != nil {
				t.Fatalf("block = % x, want none", b)
			}
		})
	}
}

// The derivation is pinned by a literal, computed outside this package:
// SHA-256("tfw-eth-mac-v1" || 00 01 .. 0f) begins 85 c4 10 37 0a 0a, and the
// first octet becomes 0x86 once it is made local and unicast.
func TestDerivedMACIsPinned(t *testing.T) {
	serial := make([]byte, SerialLen)
	for i := range serial {
		serial[i] = byte(i)
	}
	want := net.HardwareAddr{0x86, 0xC4, 0x10, 0x37, 0x0A, 0x0A}
	if got := DerivedMAC(serial); !bytes.Equal(got, want) {
		t.Fatalf("DerivedMAC = %s, want %s", got, want)
	}
}

// Whatever the identity, the address is locally administered and unicast,
// and the block accepts it. A sticker address, universally administered, is
// accepted the same way.
func TestDerivedAndStickerMACsAreAccepted(t *testing.T) {
	for _, id := range [][]byte{nil, {0xFF}, bytes.Repeat([]byte{0xA5}, 64)} {
		mac := DerivedMAC(id)
		if mac[0]&0x02 == 0 || mac[0]&0x01 != 0 {
			t.Fatalf("DerivedMAC(% x) = %s: not local unicast", id, mac)
		}
		c := testNetConfig()
		c.MAC = mac
		if _, err := c.MarshalBinary(); err != nil {
			t.Fatalf("MarshalBinary with %s: %v", mac, err)
		}
	}
	c := testNetConfig()
	c.MAC = net.HardwareAddr{0x90, 0xA2, 0xDA, 0x00, 0x00, 0x01}
	if _, err := c.MarshalBinary(); err != nil {
		t.Fatalf("MarshalBinary with a sticker MAC: %v", err)
	}
}

// eepromLink answers every request OK and keeps what EEPROM_WRITE asked for.
// eepromLink is a device whose EEPROM holds what was last written to it, so a
// read after a write returns the written bytes.
type eepromLink struct {
	rx     bytes.Buffer
	writes [][]byte
	mem    [1024]byte
}

func (l *eepromLink) Write(p []byte) (int, error) {
	f, _, err := Decode(p)
	if err != nil {
		return 0, err
	}
	var reply []byte
	switch f.Cmd() {
	case CmdEEPROMWrite:
		l.writes = append(l.writes, append([]byte(nil), f.Payload...))
		copy(l.mem[binary.LittleEndian.Uint16(f.Payload):], f.Payload[2:])
	case CmdEEPROMRead:
		addr := int(binary.LittleEndian.Uint16(f.Payload))
		reply = l.mem[addr : addr+int(f.Payload[2])]
	}
	resp, err := EncodeResponse(f.Seq, f.Cmd(), StatusOK, reply)
	if err != nil {
		return 0, err
	}
	l.rx.Write(resp)
	return len(p), nil
}

func (l *eepromLink) Read(p []byte) (int, error) { return l.rx.Read(p) }

func TestWriteNetConfigWritesTheBlockInEEPROMChunks(t *testing.T) {
	link := &eepromLink{}
	d := New(link, WithBootWait(0))

	if err := d.WriteNetConfig(context.Background(), testNetConfig()); err != nil {
		t.Fatalf("WriteNetConfig: %v", err)
	}
	if len(link.writes) != NetConfigLen/EEPROMChunk {
		t.Fatalf("%d writes, want %d", len(link.writes), NetConfigLen/EEPROMChunk)
	}
	for i, w := range link.writes {
		addr := uint16(NetConfigAddr + i*EEPROMChunk)
		want := append([]byte{byte(addr), byte(addr >> 8)}, netcfgVector[i*EEPROMChunk:(i+1)*EEPROMChunk]...)
		if !bytes.Equal(w, want) {
			t.Fatalf("write %d = % x, want % x", i, w, want)
		}
	}
}

func TestWriteNetConfigSendsNothingForABlockItCannotEncode(t *testing.T) {
	link := &eepromLink{}
	d := New(link, WithBootWait(0))
	c := testNetConfig()
	c.Endpoint = netip.AddrPortFrom(c.Endpoint.Addr(), 0)

	if err := d.WriteNetConfig(context.Background(), c); err == nil {
		t.Fatal("WriteNetConfig accepted a zero port")
	}
	if len(link.writes) != 0 {
		t.Fatalf("%d writes, want none", len(link.writes))
	}
}

func TestNetConfigDecodesThePinnedBlock(t *testing.T) {
	var c NetConfig
	if err := c.UnmarshalBinary(netcfgVector); err != nil {
		t.Fatalf("UnmarshalBinary: %v", err)
	}
	want := testNetConfig()
	if c.DHCP || !bytes.Equal(c.MAC, want.MAC) || c.IP != want.IP || c.Mask != want.Mask ||
		c.Gateway != want.Gateway || c.Endpoint != want.Endpoint {
		t.Fatalf("decoded %+v, want %+v", c, want)
	}
}

// The pinned block with flags bit 0 set and the three leased fields zero,
// literal, its CRC computed apart from this package: the block a host writes
// for a device that leases its address. test/unit/test_link_eth.c sets the
// same bit on its own block.
var netcfgDHCPVector = []byte{
	'T', 'F', 'N', 'C', 0x01, 0x01,
	0x02, 0x00, 0x00, 0x00, 0x00, 0x01,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 192, 0, 2, 20,
	0x1B, 0x6C,
	0xC9, 0xBE,
}

func testDHCPConfig() NetConfig {
	c := testNetConfig()
	c.DHCP = true
	c.IP, c.Mask, c.Gateway = netip.Addr{}, netip.Addr{}, netip.Addr{}
	return c
}

func TestNetConfigWithDHCPMarshalsFlagsBit0AndNoAddress(t *testing.T) {
	got, err := testDHCPConfig().MarshalBinary()
	if err != nil {
		t.Fatalf("MarshalBinary: %v", err)
	}
	if !bytes.Equal(got, netcfgDHCPVector) {
		t.Fatalf("block = % x\nwant    % x", got, netcfgDHCPVector)
	}
}

// Addresses given beside DHCP are written as given: the device ignores them,
// and a host that later clears DHCP has them already in place.
func TestNetConfigWithDHCPKeepsTheAddressesItIsGiven(t *testing.T) {
	c := testNetConfig()
	c.DHCP = true
	got, err := c.MarshalBinary()
	if err != nil {
		t.Fatalf("MarshalBinary: %v", err)
	}
	want, _ := testNetConfig().MarshalBinary()
	if got[netcfgOffFlags] != netcfgFlagDHCP ||
		!bytes.Equal(got[netcfgOffMAC:netcfgOffCRC], want[netcfgOffMAC:netcfgOffCRC]) {
		t.Fatalf("block = % x", got)
	}
}

// DHCP leases the device's own addresses, never the endpoint's, and an
// address given beside it must still be IPv4.
func TestNetConfigWithDHCPStillRefusesWhatItCannotEncode(t *testing.T) {
	for name, edit := range map[string]func(*NetConfig){
		"unset endpoint": func(c *NetConfig) { c.Endpoint = netip.AddrPort{} },
		"zero endpoint port": func(c *NetConfig) {
			c.Endpoint = netip.AddrPortFrom(c.Endpoint.Addr(), 0)
		},
		"IPv6 device address": func(c *NetConfig) { c.IP = netip.MustParseAddr("2001:db8::1") },
		"short MAC":           func(c *NetConfig) { c.MAC = c.MAC[:5] },
	} {
		c := testDHCPConfig()
		edit(&c)
		var nerr *NetConfigError
		if _, err := c.MarshalBinary(); !errors.As(err, &nerr) {
			t.Errorf("%s: err = %v, want a *NetConfigError", name, err)
		}
	}
}

func TestNetConfigDecodesFlagsBit0AsDHCP(t *testing.T) {
	var c NetConfig
	if err := c.UnmarshalBinary(netcfgDHCPVector); err != nil {
		t.Fatalf("UnmarshalBinary: %v", err)
	}
	zero := netip.AddrFrom4([4]byte{})
	if !c.DHCP || c.IP != zero || c.Mask != zero || c.Gateway != zero ||
		c.Endpoint != testNetConfig().Endpoint {
		t.Fatalf("decoded %+v", c)
	}
	// What it decodes, it writes back byte for byte.
	again, err := c.MarshalBinary()
	if err != nil || !bytes.Equal(again, netcfgDHCPVector) {
		t.Fatalf("re-marshalled % x, %v", again, err)
	}
}

// The other flag bits are reserved: the device ignores them, so the host
// reads past them too, and only bit 0 decides DHCP.
func TestNetConfigIgnoresTheReservedFlagBits(t *testing.T) {
	for _, flags := range []byte{0xFE, 0xFF} {
		b := append([]byte(nil), netcfgVector...)
		b[netcfgOffFlags] = flags
		binary.LittleEndian.PutUint16(b[netcfgOffCRC:], CRC16(b[:netcfgOffCRC]))
		var c NetConfig
		if err := c.UnmarshalBinary(b); err != nil {
			t.Fatalf("flags %#02x: %v", flags, err)
		}
		if c.DHCP != (flags&1 != 0) {
			t.Fatalf("flags %#02x: DHCP = %v", flags, c.DHCP)
		}
	}
}

// The block is re-sealed after each edit but the CRC case, so each case
// trips exactly the refusal it names -- the same set tfw_netcfg_decode
// returns TFW_ERR_NOT_PROVISIONED for.
func TestNetConfigDecodeRefusesWhatTheDeviceTreatsAsAbsent(t *testing.T) {
	seal := func(b []byte) []byte {
		binary.LittleEndian.PutUint16(b[netcfgOffCRC:], CRC16(b[:netcfgOffCRC]))
		return b
	}
	edit := func(f func(b []byte)) []byte {
		b := append([]byte(nil), netcfgVector...)
		f(b)
		return seal(b)
	}
	cases := map[string]struct {
		block []byte
		field string
	}{
		"short":     {netcfgVector[:31], "block"},
		"erased":    {bytes.Repeat([]byte{0xff}, NetConfigLen), "block"},
		"version":   {edit(func(b []byte) { b[4] = 2 }), "block"},
		"crc":       {append(append([]byte(nil), netcfgVector[:31]...), netcfgVector[31]^1), "block"},
		"zero peer": {edit(func(b []byte) { copy(b[netcfgOffPeer:], []byte{0, 0, 0, 0}) }), "Endpoint"},
		"zero port": {edit(func(b []byte) { b[netcfgOffPort], b[netcfgOffPort+1] = 0, 0 }), "Endpoint"},
	}
	for name, tc := range cases {
		var c NetConfig
		err := c.UnmarshalBinary(tc.block)
		var nerr *NetConfigError
		if !errors.As(err, &nerr) || nerr.Field != tc.field {
			t.Errorf("%s: err = %v, want a %s refusal", name, err, tc.field)
		}
	}
}

func TestReadNetConfigReturnsTheBlockWriteNetConfigWrote(t *testing.T) {
	link := &eepromLink{}
	d := New(link, WithBootWait(0))
	ctx := context.Background()

	if _, err := d.ReadNetConfig(ctx); err == nil {
		t.Fatal("ReadNetConfig decoded a blank EEPROM")
	}
	if err := d.WriteNetConfig(ctx, testNetConfig()); err != nil {
		t.Fatalf("WriteNetConfig: %v", err)
	}
	got, err := d.ReadNetConfig(ctx)
	if err != nil {
		t.Fatalf("ReadNetConfig: %v", err)
	}
	want := testNetConfig()
	if !bytes.Equal(got.MAC, want.MAC) || got.IP != want.IP || got.Mask != want.Mask ||
		got.Gateway != want.Gateway || got.Endpoint != want.Endpoint {
		t.Fatalf("read %+v, want %+v", got, want)
	}
}

type deadLink struct{}

func (deadLink) Write([]byte) (int, error) { return 0, errors.New("link down") }
func (deadLink) Read([]byte) (int, error)  { return 0, errors.New("link down") }

func TestReadNetConfigReturnsTheLinkError(t *testing.T) {
	d := New(deadLink{}, WithBootWait(0))
	_, err := d.ReadNetConfig(context.Background())
	if err == nil {
		t.Fatal("ReadNetConfig succeeded on a dead link")
	}
	var linkErr *LinkError
	if !errors.As(err, &linkErr) {
		t.Fatalf("ReadNetConfig returned %v (%T), want a *LinkError", err, err)
	}
}
