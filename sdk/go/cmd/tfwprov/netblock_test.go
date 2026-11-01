// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package main

import (
	"bytes"
	"encoding/hex"
	"errors"
	"net/netip"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"testing"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
)

// benchNetblock is the Mega re-provisioning run's network block (tf-7-isp,
// 2026-10-16): MAC 5a:ed:09:10:bf:59, 198.51.100.22/24, no gateway, endpoint
// 198.51.100.1:7020. LITERALS, worked out by hand from the layout in
// docs/ethernet-transport/api.md, not computed by the code they pin: a change
// to these bytes is a change to what a provisioned device reads.
var benchNetblock = []byte{
	0x54, 0x46, 0x4E, 0x43, // "TFNC"
	0x01, 0x00, // version, reserved
	0x5A, 0xED, 0x09, 0x10, 0xBF, 0x59, // MAC
	0xC6, 0x33, 0x64, 0x16, // IP 198.51.100.22
	0xFF, 0xFF, 0xFF, 0x00, // mask /24
	0x00, 0x00, 0x00, 0x00, // gateway 0.0.0.0
	0xC6, 0x33, 0x64, 0x01, // endpoint 198.51.100.1
	0x1B, 0x6C, // port 7020, big-endian
	0x3E, 0x07, // CRC-16/CCITT-FALSE over [0, 30), little-endian
}

// benchNetblockHex is that block as the .eep file --bake-netblock writes, at
// 0x140 since #125 (0x0C0 before it).
const benchNetblockHex = ":1001400054464E4301005AED0910BF59C633641698\n" +
	":10015000FFFFFF0000000000C63364011B6C3E0778\n" +
	":00000001FF\n"

func benchFlags() netblockFlags {
	return netblockFlags{
		mac:      "5a:ed:09:10:bf:59",
		ip:       "198.51.100.22/24",
		gateway:  "0.0.0.0",
		endpoint: "198.51.100.1:7020",
	}
}

func TestNetblockFromFlagsIsTheBenchBlock(t *testing.T) {
	got, err := netblockFromFlags(benchFlags())
	if err != nil {
		t.Fatalf("netblockFromFlags: %v", err)
	}
	if !bytes.Equal(got, benchNetblock) {
		t.Fatalf("block = % x\nwant    % x", got, benchNetblock)
	}
}

func TestNetblockMaskAndPrefixAreTheSameBlock(t *testing.T) {
	f := benchFlags()
	f.ip, f.mask = "198.51.100.22", "255.255.255.0"
	got, err := netblockFromFlags(f)
	if err != nil {
		t.Fatalf("netblockFromFlags: %v", err)
	}
	if !bytes.Equal(got, benchNetblock) {
		t.Fatalf("block = % x\nwant    % x", got, benchNetblock)
	}
}

func TestNetblockDHCPIsTheFlagAndNoAddress(t *testing.T) {
	f := netblockFlags{dhcp: true, mac: "02:00:00:00:00:16", endpoint: "198.51.100.1:7020"}
	got, err := netblockFromFlags(f)
	if err != nil {
		t.Fatalf("netblockFromFlags: %v", err)
	}
	if got[5] != 0x01 {
		t.Fatalf("flags byte = %#02x, want bit 0 (DHCP) alone", got[5])
	}
	var c tfw.NetConfig
	if err := c.UnmarshalBinary(got); err != nil {
		t.Fatalf("the baked block: %v", err)
	}
	if !c.DHCP || c.IP != netip.IPv4Unspecified() || c.Mask != netip.IPv4Unspecified() ||
		c.Gateway != netip.IPv4Unspecified() || c.Endpoint.String() != f.endpoint ||
		c.MAC.String() != f.mac {
		t.Fatalf("the baked block decodes as %+v", c)
	}
}

func TestNetblockDHCPRefusesAStaticAddressBesideIt(t *testing.T) {
	for name, edit := range map[string]func(*netblockFlags){
		// A static address beside --dhcp: TestMainBakeNetblockDHCPRefusesAStaticAddress.
		"no mac":      func(f *netblockFlags) { f.mac = "" },
		"no endpoint": func(f *netblockFlags) { f.endpoint = "" },
	} {
		t.Run(name, func(t *testing.T) {
			f := netblockFlags{dhcp: true, mac: "02:00:00:00:00:16", endpoint: "198.51.100.1:7020"}
			edit(&f)
			if b, err := netblockFromFlags(f); err == nil {
				t.Fatalf("accepted; block % x", b)
			}
		})
	}
}

func TestBakeNetblockWritesTheBenchFile(t *testing.T) {
	out := filepath.Join(t.TempDir(), "net.eep")
	if err := runBake(out, false, "", "", "", true, benchFlags(), ""); err != nil {
		t.Fatalf("runBake: %v", err)
	}
	data, err := os.ReadFile(out)
	if err != nil {
		t.Fatal(err)
	}
	if string(data) != benchNetblockHex {
		t.Fatalf("file =\n%s\nwant\n%s", data, benchNetblockHex)
	}
}

// One command line, one image: the platform block [0x000, 0x0C0) and the
// network block [0x140, 0x160), two runs. The gap is the bootloader's swap
// journal and confirm request (#125), which the file must not touch.
func TestBakeBothIsOneImageOfTwoRuns(t *testing.T) {
	out := filepath.Join(t.TempDir(), "both.eep")
	if err := runBake(out, true, "000102030405060708090a0b0c0d0e0f", "", "", true, benchFlags(), ""); err != nil {
		t.Fatalf("runBake: %v", err)
	}
	data, err := os.ReadFile(out)
	if err != nil {
		t.Fatal(err)
	}
	runs := readIHEXRuns(t, string(data))
	if len(runs) != 2 || runs[0].base != 0x000 || runs[1].base != 0x140 {
		t.Fatalf("runs = %d, want [0x000, 0x0C0) and [0x140, 0x160)", len(runs))
	}
	image, net := runs[0].data, runs[1].data
	// Placement, not encoding: the platform block's bytes are pinned where
	// the platform block is defined.
	want := tfw.BuildPlatformBlock([tfw.SerialLen]byte{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
		[tfw.PubkeyHashLen]byte{}, [tfw.OperatorPubkeyLen]byte{})
	if !bytes.Equal(image, want[:]) {
		t.Fatalf("[0x000, 0x0C0) is not the platform block")
	}
	if !bytes.Equal(net, benchNetblock) {
		t.Fatalf("[0x140, 0x160) = % x\nwant              % x", net, benchNetblock)
	}
}

func TestNetblockFlagsRefuseWhatTheyCannotEncode(t *testing.T) {
	// want is the refusal each row names; field, where set, is the
	// tfw.NetConfigError the SDK's encoder returns for a value the flags parse.
	for name, c := range map[string]struct {
		edit        func(*netblockFlags)
		want, field string
	}{
		"no mac":            {edit: func(f *netblockFlags) { f.mac = "" }, want: "requires --mac"},
		"no mask":           {edit: func(f *netblockFlags) { f.ip = "198.51.100.22" }, want: "requires the netmask"},
		"mask twice":        {edit: func(f *netblockFlags) { f.mask = "255.255.255.0" }, want: "give the netmask once"},
		"holed mask":        {edit: func(f *netblockFlags) { f.ip, f.mask = "198.51.100.22", "255.0.255.0" }, want: "not a contiguous netmask"},
		"multicast mac":     {edit: func(f *netblockFlags) { f.mac = "01:00:5e:00:00:01" }, field: "MAC"},
		"ipv6 ip":           {edit: func(f *netblockFlags) { f.ip = "2001:db8::1/64" }, want: "--ip: must be an IPv4 address"},
		"no gateway":        {edit: func(f *netblockFlags) { f.gateway = "" }, want: "requires --gateway"},
		"endpoint no port":  {edit: func(f *netblockFlags) { f.endpoint = "198.51.100.1" }, want: "--endpoint"},
		"endpoint port 0":   {edit: func(f *netblockFlags) { f.endpoint = "198.51.100.1:0" }, field: "Endpoint"},
		"endpoint 0.0.0.0":  {edit: func(f *netblockFlags) { f.endpoint = "0.0.0.0:7020" }, field: "Endpoint"},
		"endpoint hostname": {edit: func(f *netblockFlags) { f.endpoint = "bench.example:7020" }, want: "--endpoint"},
	} {
		t.Run(name, func(t *testing.T) {
			f := benchFlags()
			c.edit(&f)
			b, err := netblockFromFlags(f)
			if err == nil {
				t.Fatalf("accepted; block % x", b)
			}
			if c.field != "" {
				var ne *tfw.NetConfigError
				if !errors.As(err, &ne) || ne.Field != c.field {
					t.Fatalf("err = %v, want a tfw.NetConfigError on %s", err, c.field)
				}
				return
			}
			if !strings.Contains(err.Error(), c.want) {
				t.Fatalf("error %q does not name %q", err, c.want)
			}
		})
	}
}

// readIHEX decodes the data records of an Intel HEX file into one image,
// failing on a gap: the image must be a single run from address zero.
func readIHEX(t *testing.T, s string) []byte {
	t.Helper()
	runs := readIHEXRuns(t, s)
	if len(runs) != 1 || runs[0].base != 0 {
		t.Fatalf("the image is %d runs, want one at 0x0000", len(runs))
	}
	return runs[0].data
}

// readIHEXRuns decodes a file into its runs of contiguous records, in order.
func readIHEXRuns(t *testing.T, s string) []ihexSegment {
	t.Helper()
	var runs []ihexSegment
	for _, line := range strings.Split(strings.TrimSpace(s), "\n") {
		raw, err := hex.DecodeString(strings.TrimPrefix(line, ":"))
		if err != nil || len(raw) < 5 {
			t.Fatalf("bad record %q", line)
		}
		var sum byte
		for _, b := range raw {
			sum += b
		}
		if sum != 0 {
			t.Fatalf("record %q: checksum", line)
		}
		if raw[3] == 0x01 {
			break
		}
		addr, _ := strconv.ParseUint(hex.EncodeToString(raw[1:3]), 16, 16)
		if n := len(runs); n == 0 || int(addr) != int(runs[n-1].base)+len(runs[n-1].data) {
			if n > 0 && int(addr) < int(runs[n-1].base)+len(runs[n-1].data) {
				t.Fatalf("record at 0x%04X overlaps or precedes the run before it", addr)
			}
			runs = append(runs, ihexSegment{base: uint16(addr)})
		}
		last := &runs[len(runs)-1]
		last.data = append(last.data, raw[4:4+int(raw[0])]...)
	}
	return runs
}

func TestEveryMutatedNetBlockIsRefused(t *testing.T) {
	good, err := netblockFromFlags(benchFlags())
	if err != nil {
		t.Fatal(err)
	}
	var c tfw.NetConfig
	if err := c.UnmarshalBinary(good); err != nil {
		t.Fatalf("the good block: %v", err)
	}
	cases := 0
	for i := range good {
		for _, mask := range []byte{0x01, 0x80, 0xFF} {
			bad := bytes.Clone(good)
			bad[i] ^= mask
			if err := c.UnmarshalBinary(bad); err == nil {
				t.Fatalf("byte %d ^ %02x accepted: % x", i, mask, bad)
			}
			cases++
		}
	}
	for n := 0; n < len(good); n++ {
		if err := c.UnmarshalBinary(good[:n]); err == nil {
			t.Fatalf("truncated to %d bytes accepted", n)
		}
		cases++
	}
	for _, tail := range [][]byte{{0x00}, {0xFF}, {0x00, 0x00}, bytes.Repeat([]byte{0xA5}, 32)} {
		if err := c.UnmarshalBinary(append(bytes.Clone(good), tail...)); err == nil {
			t.Fatalf("extended by % x accepted", tail)
		}
		cases++
	}
	t.Logf("%d net-block mutations refused", cases)
}

func FuzzNetblockFlags(f *testing.F) {
	g := benchFlags()
	f.Add(g.mac, g.ip, g.mask, g.gateway, g.endpoint)
	f.Add(g.mac, "198.51.100.22", "255.255.255.0", g.gateway, g.endpoint)
	for _, m := range netblockMutations {
		b := benchFlags()
		m.edit(&b)
		f.Add(b.mac, b.ip, b.mask, b.gateway, b.endpoint)
	}
	f.Fuzz(func(t *testing.T, mac, ip, mask, gateway, endpoint string) {
		nf := netblockFlags{mac: mac, ip: ip, mask: mask, gateway: gateway, endpoint: endpoint}
		block, err := netblockFromFlags(nf)
		if err != nil {
			return
		}
		if len(block) != tfw.NetConfigLen {
			t.Fatalf("%+v: block is %d bytes", nf, len(block))
		}
		var c tfw.NetConfig
		if err := c.UnmarshalBinary(block); err != nil {
			t.Fatalf("%+v baked a block its own reader refuses: %v", nf, err)
		}
		again, err := c.MarshalBinary()
		if err != nil || !bytes.Equal(again, block) {
			t.Fatalf("%+v: re-encoded % x (%v), baked % x", nf, again, err, block)
		}
	})
}
