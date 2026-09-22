// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

// Command agctl is the reference client for the arduino-generic protocol.
//
// One host implementation, not two. An earlier design had a C agctl beside
// this, which would have meant two host-side parsers to keep in agreement for
// no gain -- the C codec is already proven by the firmware itself and by
// agsim. The codec necessarily exists twice, once per side of the link,
// because Go does not run on an ATmega328P; anything beyond that would be
// duplication.
package main

import (
	"context"
	"encoding/hex"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"os"
	"strings"
	"time"

	ag "github.com/jeremyhahn/arduino-generic/sdk/go"
	"github.com/jeremyhahn/arduino-generic/sdk/go/serialport"
)

func usage() {
	fmt.Fprint(os.Stderr, `agctl -- talk to an arduino-generic device

  agctl [flags] <port> <command> [args]

commands
  ping [text]        echo text through the device
  caps               what the device is and what it can do
  sys                uptime, free RAM, why it last reset
  reset [soft|boot]  restart; boot enters the bootloader
  ports              list the serial ports this host can see

flags
`)
	flag.PrintDefaults()
}

func main() {
	var (
		baud    = flag.Int("baud", serialport.DefaultBaud, "serial baud rate")
		asJSON  = flag.Bool("json", false, "emit JSON instead of text")
		timeout = flag.Duration("timeout", 10*time.Second, "how long to wait for a reply")
	)
	flag.Usage = usage
	flag.Parse()

	if flag.NArg() >= 1 && flag.Arg(0) == "ports" {
		ports, err := serialport.List()
		if err != nil {
			fail(err)
		}
		for _, p := range ports {
			fmt.Println(p)
		}
		return
	}
	if flag.NArg() < 2 {
		usage()
		os.Exit(2)
	}

	port, cmd, args := flag.Arg(0), flag.Arg(1), flag.Args()[2:]

	conn, err := serialport.Open(port, *baud)
	if err != nil {
		fail(err)
	}
	defer func() { _ = conn.Close() }()

	dev := ag.New(conn, ag.WithTimeout(*timeout))
	ctx := context.Background()

	if err := run(ctx, dev, cmd, args, *asJSON, os.Stdout); err != nil {
		fail(err)
	}
	// An unexpected boot event means the device reset underneath us and
	// anything the caller believed about its state is stale. Worth saying out
	// loud rather than swallowing.
	for _, e := range dev.Events() {
		if len(e.Payload) > 0 && e.Payload[0] == ag.EventBoot {
			fmt.Fprintln(os.Stderr, "agctl: note -- the device reported a boot event during this exchange")
		}
	}
}

func run(ctx context.Context, dev *ag.Device, cmd string, args []string, asJSON bool, out io.Writer) error {
	switch cmd {
	case "ping":
		msg := []byte(strings.Join(args, " "))
		got, err := dev.Ping(ctx, msg)
		if err != nil {
			return err
		}
		return emit(out, asJSON, map[string]any{"echo": string(got), "hex": hex.EncodeToString(got)},
			func() string { return fmt.Sprintf("pong: %q", got) })

	case "caps":
		c, err := dev.Caps(ctx)
		if err != nil {
			return err
		}
		return emit(out, asJSON, c, func() string {
			var b strings.Builder
			fmt.Fprintf(&b, "protocol    %d.%d\n", c.ProtoMajor, c.ProtoMinor)
			fmt.Fprintf(&b, "firmware    %d.%d.%d\n", c.FirmwareMajor, c.FirmwareMinor, c.FirmwarePatch)
			fmt.Fprintf(&b, "board id    0x%02x\n", c.BoardID)
			fmt.Fprintf(&b, "pins        %d\n", c.PinCount)
			fmt.Fprintf(&b, "sched slots %d\n", c.SchedSlots)
			fmt.Fprintf(&b, "max payload %d\n", c.MaxPayload)
			fmt.Fprintf(&b, "root of trust %s\n", c.CryptoTier)
			fmt.Fprintf(&b, "features    %s\n", featureList(c))
			return b.String()
		})

	case "sys":
		si, err := dev.SysInfo(ctx)
		if err != nil {
			return err
		}
		return emit(out, asJSON, si, func() string {
			return fmt.Sprintf("uptime %s\nfree RAM %d bytes\nlast reset %s\n",
				si.Uptime, si.FreeRAM, si.ResetCause)
		})

	case "reset":
		mode := ag.ResetSoft
		if len(args) > 0 && args[0] == "boot" {
			mode = ag.ResetBootloader
		}
		if err := dev.Reset(ctx, mode); err != nil {
			return err
		}
		return emit(out, asJSON, map[string]any{"reset": true, "mode": args},
			func() string { return "reset accepted; the device acknowledged before restarting\n" })

	default:
		return fmt.Errorf("unknown command %q", cmd)
	}
}

func featureList(c *ag.Caps) string {
	var on []string
	for _, f := range []struct {
		bit  uint32
		name string
	}{
		{ag.FeatureGPIO, "gpio"}, {ag.FeatureSched, "sched"}, {ag.FeatureAnalog, "analog"},
		{ag.FeaturePWM, "pwm"}, {ag.FeatureTone, "tone"}, {ag.FeaturePulse, "pulse"},
		{ag.FeatureEEPROM, "eeprom"}, {ag.FeatureI2C, "i2c"}, {ag.FeatureSPI, "spi"},
		{ag.FeatureOneWire, "onewire"}, {ag.FeatureAttest, "attest"}, {ag.FeatureDeadman, "deadman"},
	} {
		if c.Has(f.bit) {
			on = append(on, f.name)
		}
	}
	if len(on) == 0 {
		return "(none)"
	}
	return strings.Join(on, " ")
}

func emit(out io.Writer, asJSON bool, v any, text func() string) error {
	if asJSON {
		e := json.NewEncoder(out)
		e.SetIndent("", "  ")
		return e.Encode(v)
	}
	_, err := io.WriteString(out, text())
	return err
}

func fail(err error) {
	fmt.Fprintln(os.Stderr, "agctl:", err)
	os.Exit(1)
}
