// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"bytes"
	"context"
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
type eepromLink struct {
	rx     bytes.Buffer
	writes [][]byte
}

func (l *eepromLink) Write(p []byte) (int, error) {
	f, _, err := Decode(p)
	if err != nil {
		return 0, err
	}
	if f.Cmd() == CmdEEPROMWrite {
		l.writes = append(l.writes, append([]byte(nil), f.Payload...))
	}
	resp, err := EncodeResponse(f.Seq, f.Cmd(), StatusOK, nil)
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
