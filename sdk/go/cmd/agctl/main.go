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
	"strconv"
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
  pin <pin>          one pin's descriptor: port, adc channel, pwm
  sys                uptime, free RAM, why it last reset
  reset [soft|boot]  restart; boot enters the bootloader
  mode <pin> <in|out|pullup>
  read <pin>
  write <pin> <0|1>
  timer <pin> <0|1> <duration>   drive a pin, the DEVICE reverts it
  timer-cancel <pin> [apply]
  timer-query <pin>
  ports              list the serial ports this host can see

  analog <pin>                     one conversion, raw counts
  adc-ref <vcc|external|internal>
  pwm <pin> <duty 0-255>
  tone <pin> <hz>                  notone <pin>
  pulse-in <pin> <0|1> <timeout>   one pulse width
  pulse-capture <pin> <0|1> <max-edges> <timeout>
  eeprom-read <addr> <count>       eeprom-write <addr> <hex>
  i2c <addr7> <hex|-> <read-count>
  spi <cs pin> <hex>
  1wire-reset <pin>                1wire-write <pin> <hex>   1wire-read <pin> <count>
  attest <16 hex bytes>            runtime evidence, nonce is YOURS
  attest-cert <32 hex bytes>       the alias certificate, DER

flags
`)
	flag.PrintDefaults()
}

func main() {
	var (
		baud     = flag.Int("baud", serialport.DefaultBaud, "serial baud rate")
		asJSON   = flag.Bool("json", false, "emit JSON instead of text")
		timeout  = flag.Duration("timeout", 10*time.Second, "how long to wait for a reply")
		bootWait = flag.Duration("boot-wait", ag.DefaultBootWait,
			"how long to wait for the firmware's BOOT event after opening the port "+
				"before sending the first command; 0 disables the wait. Raise this for "+
				"an ag-boot BOOT_MEASURE=atecc608a image, which can take several seconds "+
				"to measure a large application before it starts")
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

	dev := ag.New(conn, ag.WithTimeout(*timeout), ag.WithBootWait(*bootWait))
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
			// What the provider can SIGN with, which is not implied by the
			// tier: a secure element reports ECDSA where a software provider
			// on the same board reports only HMAC.
			fmt.Fprintf(&b, "algorithms  %s\n", c.Algorithms)
			return b.String()
		})

	case "pin":
		if len(args) < 1 {
			return fmt.Errorf("usage: pin <pin>")
		}
		pin, err := parsePin(args[0])
		if err != nil {
			return err
		}
		p, err := dev.PinInfo(ctx, pin)
		if err != nil {
			return err
		}
		return emit(out, asJSON, p, func() string {
			var b strings.Builder
			fmt.Fprintf(&b, "pin         %d\n", p.Pin)
			fmt.Fprintf(&b, "flags       %s\n", p.Flags)
			if p.HasPort {
				fmt.Fprintf(&b, "port.bit    %d.%d\n", p.Port, p.Bit)
			} else {
				fmt.Fprint(&b, "port.bit    none -- analog input, readable but never driven\n")
			}
			if p.HasADC {
				fmt.Fprintf(&b, "adc channel %d\n", p.ADCChannel)
			}
			fmt.Fprintf(&b, "pwm         %s\n", p.PWM)
			return b.String()
		})

	case "sys":
		si, err := dev.SysInfo(ctx)
		if err != nil {
			return err
		}
		return emit(out, asJSON, si, func() string {
			return fmt.Sprintf("uptime %s\nfree RAM %d bytes\nlast reset %s\nframer errors  crc %d format %d\n",
				si.Uptime, si.FreeRAM, si.ResetCause, si.ErrCRC, si.ErrFormat)
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

	case "mode":
		if len(args) < 2 {
			return fmt.Errorf("usage: mode <pin> <in|out|pullup>")
		}
		pin, err := parsePin(args[0])
		if err != nil {
			return err
		}
		var m ag.PinMode
		switch args[1] {
		case "in":
			m = ag.PinInput
		case "out":
			m = ag.PinOutput
		case "pullup":
			m = ag.PinInputPullup
		default:
			return fmt.Errorf("unknown mode %q", args[1])
		}
		if err := dev.SetPinMode(ctx, pin, m); err != nil {
			return err
		}
		return emit(out, asJSON, map[string]any{"pin": pin, "mode": args[1]},
			func() string { return fmt.Sprintf("pin %d -> %s\n", pin, args[1]) })

	case "read":
		if len(args) < 1 {
			return fmt.Errorf("usage: read <pin>")
		}
		pin, err := parsePin(args[0])
		if err != nil {
			return err
		}
		high, err := dev.DigitalRead(ctx, pin)
		if err != nil {
			return err
		}
		return emit(out, asJSON, map[string]any{"pin": pin, "high": high},
			func() string { return fmt.Sprintf("%d\n", b2i(high)) })

	case "write":
		if len(args) < 2 {
			return fmt.Errorf("usage: write <pin> <0|1>")
		}
		pin, err := parsePin(args[0])
		if err != nil {
			return err
		}
		if err := dev.DigitalWrite(ctx, pin, args[1] != "0"); err != nil {
			return err
		}
		return emit(out, asJSON, map[string]any{"pin": pin, "high": args[1] != "0"},
			func() string { return fmt.Sprintf("pin %d <- %s\n", pin, args[1]) })

	case "timer":
		if len(args) < 3 {
			return fmt.Errorf("usage: timer <pin> <0|1> <duration, e.g. 5s>")
		}
		pin, err := parsePin(args[0])
		if err != nil {
			return err
		}
		dur, err := time.ParseDuration(args[2])
		if err != nil {
			return fmt.Errorf("duration %q: %w", args[2], err)
		}
		slot, err := dev.TimerSet(ctx, pin, args[1] != "0", dur)
		if err != nil {
			return err
		}
		return emit(out, asJSON, map[string]any{"pin": pin, "slot": slot, "duration": dur.String()},
			func() string {
				return fmt.Sprintf("pin %d held for %s in slot %d; the DEVICE reverts it\n", pin, dur, slot)
			})

	case "timer-cancel":
		if len(args) < 1 {
			return fmt.Errorf("usage: timer-cancel <pin> [apply]")
		}
		pin, err := parsePin(args[0])
		if err != nil {
			return err
		}
		applyFinal := len(args) > 1 && args[1] == "apply"
		if err := dev.TimerCancel(ctx, pin, applyFinal); err != nil {
			return err
		}
		return emit(out, asJSON, map[string]any{"pin": pin, "applied_final": applyFinal},
			func() string { return fmt.Sprintf("timer on pin %d cancelled\n", pin) })

	case "timer-query":
		if len(args) < 1 {
			return fmt.Errorf("usage: timer-query <pin>")
		}
		pin, err := parsePin(args[0])
		if err != nil {
			return err
		}
		st, err := dev.TimerQuery(ctx, pin)
		if err != nil {
			return err
		}
		return emit(out, asJSON, st, func() string {
			if !st.Active {
				return "no timer on that pin\n"
			}
			return fmt.Sprintf("held %d, %s remaining\n", b2i(st.HeldLevel), st.Remaining)
		})

	default:
		return runPeripheral(ctx, dev, cmd, args, asJSON, out)
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

func parsePin(s string) (uint8, error) {
	n, err := strconv.ParseUint(s, 10, 8)
	if err != nil {
		return 0, fmt.Errorf("pin %q: %w", s, err)
	}
	return uint8(n), nil
}

func b2i(b bool) int {
	if b {
		return 1
	}
	return 0
}

func fail(err error) {
	fmt.Fprintln(os.Stderr, "agctl:", err)
	os.Exit(1)
}
