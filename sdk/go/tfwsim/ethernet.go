// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfwsim

import (
	"bytes"
	"encoding/hex"
	"errors"
	"net"
	"net/netip"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"testing"
	"time"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/simproc"
)

// StartElementEthernet runs the element-tier simulator with the test identity
// as an Ethernet device -- issue #55. It listens on a loopback port, starts
// the simulator with --eth and a network block whose endpoint is that port,
// and accepts the connection the device opens, exactly as a host accepts one
// from a W5100 or W5500 on the bench.
//
// It returns both of the device's links: serial, which carries everything,
// and the accepted TCP connection, which carries only the Ethernet allow-list.
// Both are closed when the test ends.
func StartElementEthernet(tb testing.TB, opts ...tfw.Option) (serial *tfw.Device, eth *tfw.TCPDevice) {
	tb.Helper()

	raw, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		tb.Fatal(err)
	}
	ln := tfw.NewListener(raw, append([]tfw.Option{tfw.WithTimeout(bootTimeout)}, opts...)...)
	tb.Cleanup(func() { logClose(tb, "listener", ln.Close()) })

	port := raw.Addr().(*net.TCPAddr).AddrPort().Port()
	cfg := tfw.NetConfig{
		MAC:      net.HardwareAddr{0x02, 0x00, 0x00, 0x00, 0x00, 0x01},
		IP:       netip.MustParseAddr("192.0.2.10"),
		Mask:     netip.MustParseAddr("255.255.255.0"),
		Gateway:  netip.MustParseAddr("192.0.2.1"),
		Endpoint: netip.AddrPortFrom(netip.MustParseAddr("127.0.0.1"), port),
	}
	block, err := cfg.MarshalBinary()
	if err != nil {
		tb.Fatal(err)
	}
	// The block goes where this build reads it, erased bytes between it and
	// the platform block: the firmware owns that address, not this harness.
	at := netcfgAddr(tb, simproc.Element)
	platform := tfw.BuildPlatformBlock(TestSerial, [tfw.PubkeyHashLen]byte{}, [tfw.OperatorPubkeyLen]byte{})
	image := bytes.Repeat([]byte{0xFF}, at+len(block))
	copy(image, platform[:])
	copy(image[at:], block)
	eep := filepath.Join(tb.TempDir(), "eeprom.bin")
	if err := os.WriteFile(eep, image, 0o600); err != nil {
		tb.Fatal(err)
	}

	serial = startElement(tb, simproc.Element, nil, opts,
		"--eth",
		"--eeprom-bin", eep,
		"--provision-element",
		"--image-hash", hex.EncodeToString(TestFWHash[:]))

	// The device dials at boot. A simulator that never does fails the test
	// here instead of hanging it.
	if err := raw.(*net.TCPListener).SetDeadline(time.Now().Add(bootTimeout)); err != nil {
		tb.Fatal(err)
	}
	eth, err = ln.Accept()
	if err != nil {
		tb.Fatalf("tfwsim: the device never connected: %v", err)
	}
	tb.Cleanup(func() { logClose(tb, "ethernet link", eth.Close()) })
	return serial, eth
}

// eepromSize bounds the address a simulator may answer: the largest EEPROM
// of the parts this firmware runs on (the ATmega2560's 4 KiB).
const eepromSize = 4096

// netcfgAddr asks the simulator binary where it reads its network settings
// (`tfwsim --netcfg-addr`, one line such as "0x00C0"). A binary built before
// it could answer, or an answer inside the platform block or past the
// EEPROM, fails the test: a block written anywhere else is never read.
func netcfgAddr(tb testing.TB, b simproc.Binary) int {
	tb.Helper()
	path, err := simproc.Locate(b)
	if err != nil {
		tb.Fatal(err)
	}
	out, err := exec.Command(path, "--netcfg-addr").Output() // #nosec G204 -- path is Locate's answer, the tfwsim binary this repo's own make target built; the argument is a literal; no shell
	if err == nil {
		var addr uint64
		addr, err = strconv.ParseUint(strings.TrimSpace(string(out)), 0, 16)
		if err == nil && addr >= tfw.PlatformBlockLen && addr+tfw.NetConfigLen <= eepromSize {
			return int(addr)
		}
	}
	tb.Fatalf("tfwsim: %s --netcfg-addr answered %q (%v), not an address in [%#x, %#x); run make tfwsim",
		path, out, err, tfw.PlatformBlockLen, eepromSize-tfw.NetConfigLen+1)
	return 0
}

// logClose reports a teardown Close that failed. Logged, not failed: the test
// has finished by then, and a test that closed the link itself has done
// nothing wrong.
func logClose(tb testing.TB, what string, err error) {
	if err != nil && !errors.Is(err, net.ErrClosed) {
		tb.Logf("tfwsim: closing the %s: %v", what, err)
	}
}
