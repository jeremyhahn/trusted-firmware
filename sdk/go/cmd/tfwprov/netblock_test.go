// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package main

import (
	"bytes"
	"encoding/hex"
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

// benchNetblockHex is that block as the .eep file --bake-netblock writes.
const benchNetblockHex = ":1000C00054464E4301005AED0910BF59C633641619\n" +
	":1000D000FFFFFF0000000000C63364011B6C3E07F9\n" +
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
		"ip":          func(f *netblockFlags) { f.ip = "198.51.100.22/24" },
		"mask":        func(f *netblockFlags) { f.mask = "255.255.255.0" },
		"gateway":     func(f *netblockFlags) { f.gateway = "198.51.100.1" },
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

// One command line, one image: the platform block and the network block as
// the single contiguous 224-byte run [0x000, 0x0E0) the bench wrote.
func TestBakeBothIsOneContiguousImage(t *testing.T) {
	out := filepath.Join(t.TempDir(), "both.eep")
	if err := runBake(out, true, "000102030405060708090a0b0c0d0e0f", "", "", true, benchFlags(), ""); err != nil {
		t.Fatalf("runBake: %v", err)
	}
	data, err := os.ReadFile(out)
	if err != nil {
		t.Fatal(err)
	}
	image := readIHEX(t, string(data))
	if len(image) != 224 {
		t.Fatalf("image is %d bytes, want 224", len(image))
	}
	// Placement, not encoding: the platform block's bytes are pinned where
	// the platform block is defined.
	want := tfw.BuildPlatformBlock([tfw.SerialLen]byte{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
		[tfw.PubkeyHashLen]byte{}, [tfw.OperatorPubkeyLen]byte{})
	if !bytes.Equal(image[:0xC0], want[:]) {
		t.Fatalf("[0x000, 0x0C0) is not the platform block")
	}
	if !bytes.Equal(image[0xC0:], benchNetblock) {
		t.Fatalf("[0x0C0, 0x0E0) = % x\nwant              % x", image[0xC0:], benchNetblock)
	}
}

func TestNetblockFlagsRefuseWhatTheyCannotEncode(t *testing.T) {
	for name, edit := range map[string]func(*netblockFlags){
		"no mac":            func(f *netblockFlags) { f.mac = "" },
		"no mask":           func(f *netblockFlags) { f.ip = "198.51.100.22" },
		"mask twice":        func(f *netblockFlags) { f.mask = "255.255.255.0" },
		"holed mask":        func(f *netblockFlags) { f.ip, f.mask = "198.51.100.22", "255.0.255.0" },
		"multicast mac":     func(f *netblockFlags) { f.mac = "01:00:5e:00:00:01" },
		"ipv6 ip":           func(f *netblockFlags) { f.ip = "2001:db8::1/64" },
		"no gateway":        func(f *netblockFlags) { f.gateway = "" },
		"endpoint no port":  func(f *netblockFlags) { f.endpoint = "198.51.100.1" },
		"endpoint port 0":   func(f *netblockFlags) { f.endpoint = "198.51.100.1:0" },
		"endpoint 0.0.0.0":  func(f *netblockFlags) { f.endpoint = "0.0.0.0:7020" },
		"endpoint hostname": func(f *netblockFlags) { f.endpoint = "bench.example:7020" },
	} {
		t.Run(name, func(t *testing.T) {
			f := benchFlags()
			edit(&f)
			if b, err := netblockFromFlags(f); err == nil {
				t.Fatalf("accepted; block % x", b)
			}
		})
	}
}

// readIHEX decodes the data records of an Intel HEX file into one image,
// failing on a gap: the image must be a single run from address zero.
func readIHEX(t *testing.T, s string) []byte {
	t.Helper()
	var image []byte
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
		if int(addr) != len(image) {
			t.Fatalf("record at 0x%04X, want 0x%04X: the image is not one run", addr, len(image))
		}
		image = append(image, raw[4:4+int(raw[0])]...)
	}
	return image
}
