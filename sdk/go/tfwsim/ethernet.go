// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfwsim

import (
	"encoding/hex"
	"errors"
	"net"
	"net/netip"
	"os"
	"path/filepath"
	"testing"
	"time"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
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
	platform := tfw.BuildPlatformBlock(TestSerial, [tfw.PubkeyHashLen]byte{}, [tfw.OperatorPubkeyLen]byte{})
	image := append(append([]byte(nil), platform[:]...), block...)
	eep := filepath.Join(tb.TempDir(), "eeprom.bin")
	if err := os.WriteFile(eep, image, 0o600); err != nil {
		tb.Fatal(err)
	}

	serial = startElement(tb, nil, opts,
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

// logClose reports a teardown Close that failed. Logged, not failed: the test
// has finished by then, and a test that closed the link itself has done
// nothing wrong.
func logClose(tb testing.TB, what string, err error) {
	if err != nil && !errors.Is(err, net.ErrClosed) {
		tb.Logf("tfwsim: closing the %s: %v", what, err)
	}
}
