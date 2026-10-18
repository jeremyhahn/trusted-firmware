// Command wirecheck is one wire exchange with an ESP32 image over TCP, run by
// test/integration/esp32/qemu_test.sh against QEMU's forwarded port.
//
// It is the Go SDK, unmodified, on a net.Conn: the framing on TCP is the
// framing on a UART (docs/multi-arch/adr-esp-idf-integration.md), so nothing
// here knows it is not a serial port beyond WithBootWait(0) -- a TCP accept
// does not reset the device, so there is no BOOT event to wait for.
//
// Exit status 0 means every check passed; each failure prints one line
// naming the check and exits 1.
package main

import (
	"bytes"
	"context"
	"flag"
	"fmt"
	"math"
	"net"
	"os"
	"time"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
)

// markAddr is past the platform-reserved block, [0x000, 0x0C0) on every board
// (include/tfw/boot.h), which the wire refuses to write.
const markAddr = 0x100

func main() {
	addr := flag.String("addr", "127.0.0.1:21574", "the device's wire address")
	wait := flag.Duration("wait", 60*time.Second, "how long to keep dialling while the device boots")
	board := flag.Uint("board", 0x81, "the board id CAPS must report")
	tier := flag.Int("tier", -1, "the crypto tier CAPS must report, or -1 for any")
	mark := flag.String("mark", "", "write these bytes to EEPROM at markAddr")
	expect := flag.String("expect-mark", "", "require these bytes at EEPROM markAddr")
	reset := flag.Bool("reset", false, "end with a soft reset")
	flag.Parse()

	d, conn, err := dial(*addr, *wait)
	if err != nil {
		fail("dial", err)
	}
	defer func() { _ = conn.Close() }() // every check has printed or failed by here
	ctx := context.Background()

	caps, err := d.Caps(ctx)
	if err != nil {
		fail("caps", err)
	}
	if uint(caps.BoardID) != *board {
		fail("caps", fmt.Errorf("board id 0x%02x, want 0x%02x", uint8(caps.BoardID), *board))
	}
	if *tier >= 0 && int(caps.CryptoTier) != *tier {
		fail("caps", fmt.Errorf("crypto tier %d, want %d", uint8(caps.CryptoTier), *tier))
	}
	info, err := d.SysInfo(ctx)
	if err != nil {
		fail("sysinfo", err)
	}
	fmt.Printf("wirecheck: caps proto %d.%d fw %d.%d.%d board 0x%02x pins %d tier %d features 0x%08x\n",
		caps.ProtoMajor, caps.ProtoMinor, caps.FirmwareMajor, caps.FirmwareMinor, caps.FirmwarePatch,
		uint8(caps.BoardID), caps.PinCount, uint8(caps.CryptoTier), caps.Features)
	fmt.Printf("wirecheck: sysinfo uptime %v free-ram %d reset-cause %d\n",
		info.Uptime, info.FreeRAM, uint8(info.ResetCause))

	if *expect != "" {
		n := len(*expect)
		if n > math.MaxUint8 {
			fail("eeprom read", fmt.Errorf("-expect is %d bytes; one read carries at most %d", n, math.MaxUint8))
			return // fail exits; the return is for a reader who cannot see that
		}
		got, err := d.EEPROMRead(ctx, markAddr, uint8(n))
		if err != nil {
			fail("eeprom read", err)
		}
		if !bytes.Equal(got, []byte(*expect)) {
			fail("eeprom read", fmt.Errorf("%q at 0x%03x, want %q", got, markAddr, *expect))
		}
		fmt.Printf("wirecheck: eeprom holds %q across the reboot\n", got)
	}
	if *mark != "" {
		if err := d.EEPROMWrite(ctx, markAddr, []byte(*mark)); err != nil {
			fail("eeprom write", err)
		}
	}
	if *reset {
		if err := d.Reset(ctx, tfw.ResetSoft); err != nil {
			fail("reset", err)
		}
		fmt.Println("wirecheck: soft reset accepted")
	}
	fmt.Println("wirecheck: ok")
}

// dial keeps trying until a PING round-trips. QEMU's user-mode network accepts
// on the host side before the guest listens and then drops the connection, so
// a connect alone proves nothing; an echoed PING proves the firmware. The
// harness runs this once the guest has logged its link up, so the guest's
// listen is moments away; each try is bounded by its dial and PING deadlines,
// with nothing slept between them.
//
// A refused connect is not retried: QEMU's forwarded port is bound for as long
// as QEMU runs, so a refusal means there is no QEMU to wait for.
func dial(addr string, wait time.Duration) (*tfw.Device, net.Conn, error) {
	deadline := time.Now().Add(wait)
	msg := []byte("tfw-esp32-qemu")
	var last error
	for time.Now().Before(deadline) {
		conn, err := net.DialTimeout("tcp", addr, 2*time.Second)
		if err != nil {
			return nil, nil, fmt.Errorf("dial %s: %w", addr, err)
		}
		d := tfw.New(conn, tfw.WithBootWait(0), tfw.WithTimeout(2*time.Second))
		echo, err := d.Ping(context.Background(), msg)
		if err == nil && bytes.Equal(echo, msg) {
			fmt.Printf("wirecheck: ping echoed over tcp %s\n", addr)
			d = tfw.New(conn, tfw.WithBootWait(0))
			return d, conn, nil
		}
		last = fmt.Errorf("ping: %v (echo %q)", err, echo)
		_ = conn.Close() // abandoned for a fresh dial; last already says why
	}
	return nil, nil, fmt.Errorf("no answer from %s within %v: %v", addr, wait, last)
}

func fail(check string, err error) {
	fmt.Fprintf(os.Stderr, "wirecheck: %s: %v\n", check, err)
	os.Exit(1)
}
