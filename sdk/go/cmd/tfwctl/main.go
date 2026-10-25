// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

// Command tfwctl is the reference client for the trusted-firmware protocol.
//
// One host implementation, not two. An earlier design had a C tfwctl beside
// this, which would have meant two host-side parsers to keep in agreement for
// no gain -- the C codec is already proven by the firmware itself and by
// tfwsim. The codec necessarily exists twice, once per side of the link,
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

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/serialport"
)

func usage() {
	fmt.Fprint(os.Stderr, `tfwctl -- talk to an trusted-firmware device

  tfwctl [flags] <port> <command> [args]

<port> is a serial port, or tcp://host:port for an Ethernet device: tfwctl
listens there, the device's network-block endpoint, and runs the command on
the first device to connect within -timeout. Over Ethernet the device answers
only CAPS, APP_INFO, the attestation commands and OTA staging.

commands
  ping [text]        echo text through the device
  caps               what the device is and what it can do
  pin <pin>          one pin's descriptor: port, adc channel, pwm
  sys                uptime, free RAM, why it last reset
  app                the application: name, state, version, the caps it needs
  reset [soft|boot]  restart; boot enters the bootloader
  mode <pin> <in|out|pullup>
  read <pin>
  write <pin> <0|1>
  timer <pin> <0|1> <duration>   drive a pin, the DEVICE reverts it
  timer-cancel <pin> [apply]
  timer-query <pin>
  ota-stage [--activate[=now]] <image>
                     stage an image file; the DEVICE gates it. --activate
                     sends ota-activate [now] on the same connection: the
                     one way on a board that resets on open (the Mega 2560)
  ota-status         the OTA session's state and bytes received
  ota-info <slot>    OTA_INFO for slot 0 (running) or 1 (staged)
  ota-activate [now] take the staged, verified image at the next reset, or
                     reset now; ota-stage does not (OTA ADR decision 8).
                     Needs a transport that does not reset the board on open
  netcfg set --mac <mac> --ip <addr> --mask <addr> --gateway <addr> --endpoint <addr:port>
                     write the network block an Ethernet build dials with;
                     the SDK refuses a block the device would not use
  netcfg set --mac <mac> --dhcp --endpoint <addr:port>
                     the same, with the address, mask and gateway leased by
                     DHCP (a DHCP=1 build; any other refuses the block)
  netcfg get         read the network block back
  ports              list the serial ports this host can see

  analog <pin>                     one conversion, raw counts
  adc-ref <vcc|external|internal>
  pwm <pin> <duty 0-255>
  tone <pin> <hz>                  notone <pin>
  pulse-in <pin> <0|1> <timeout>   one pulse width
  pulse-capture <pin> <0|1> <max-edges> <timeout>
  eeprom-read <addr> <count>       eeprom-write <addr> <hex>
  i2c <addr7> <hex|-> <read-count>
  i2c-config <clock hz>             the generic bus's clock; boots at 100 kHz
  spi <cs pin> <hex>
  spi-config <mode 0-3> <clkdiv 0-3> <msb|lsb>   boots at mode 0, slowest, msb
  1wire-reset <pin>                1wire-write <pin> <hex>   1wire-read <pin> <count>
  attest <16 hex bytes>            runtime evidence, nonce is YOURS
  attest-cert <32 hex bytes>       the alias certificate, DER
  attest-csr --nonce <32 hex bytes>
                                   the possession signature, r||s as hex
                                   for that challenge: DER on stdout, or PEM
  auth <16 hex bytes> <key.pem> [-- <verb> <args...>]
                                   element tier only: authenticate with the
                                   operator key (PEM EC private key path;
                                   never read by tfwctl itself -- see
                                   externalECKeySigner) against the given
                                   device serial, opening eeprom-read below
                                   the reserved boundary, 0x140 (the Mega
                                   2560's swap journal lies below it) until
                                   the device resets (reset boot is gated
                                   too). Unauthenticated, those reads get
                                   StatusErrAuthRequired on the Mega 2560
                                   and 32U4 element tier; every other build
                                   leaves them ungated. A gated verb after --
                                   runs in the SAME connection, which matters
                                   on boards (the Mega 2560) that reset on
                                   reopen and would otherwise discard the
                                   session before a second invocation reaches
                                   the device

flags
`)
	flag.PrintDefaults()
}

func main() {
	var (
		baud     = flag.Int("baud", serialport.DefaultBaud, "serial baud rate")
		asJSON   = flag.Bool("json", false, "emit JSON instead of text")
		timeout  = flag.Duration("timeout", 10*time.Second, "how long to wait for a reply")
		bootWait = flag.Duration("boot-wait", tfw.DefaultBootWait,
			"how long to wait for the firmware's BOOT event after opening the port "+
				"before sending the first command; 0 disables the wait. Raise this for "+
				"an tfw-boot BOOT_MEASURE=atecc608a image, which can take several seconds "+
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

	var dev *tfw.Device
	addr, isTCP, err := tcpAddr(port)
	switch {
	case err != nil:
		fail(err)
	case isTCP:
		td, err := listenForDevice(addr, *timeout, tfw.WithTimeout(*timeout))
		if err != nil {
			fail(err)
		}
		defer func() { _ = td.Close() }()
		dev = td.Device
	default:
		conn, err := serialport.Open(port, *baud)
		if err != nil {
			fail(err)
		}
		defer func() { _ = conn.Close() }()
		dev = tfw.New(conn, tfw.WithTimeout(*timeout), tfw.WithBootWait(*bootWait))
	}
	ctx := context.Background()

	if err := run(ctx, dev, cmd, args, *asJSON, os.Stdout); err != nil {
		fail(err)
	}
	// An unexpected boot event means the device reset underneath us and
	// anything the caller believed about its state is stale. Worth saying out
	// loud rather than swallowing.
	for _, e := range dev.Events() {
		if len(e.Payload) > 0 && e.Payload[0] == tfw.EventBoot {
			fmt.Fprintln(os.Stderr, "tfwctl: note -- the device reported a boot event during this exchange")
		}
	}
}

func run(ctx context.Context, dev *tfw.Device, cmd string, args []string, asJSON bool, out io.Writer) error {
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
			fmt.Fprintf(&b, "board id    0x%02x (%s)\n", uint8(c.BoardID), c.BoardID)
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

	case "app":
		// APP_INFO: an image with no application answers it as an unknown
		// command, and that status is returned as every other refusal is.
		ai, err := dev.AppInfo(ctx)
		if err != nil {
			return err
		}
		return emit(out, asJSON, ai, func() string {
			return fmt.Sprintf("name %s\nprofile %s\nstate %s\nversion %d.%d.%d\nabi %d\n"+
				"required caps 0x%08x\noverruns %d\nota slots %d\nconfig %d bytes\n",
				ai.Name, ai.Profile, ai.State, ai.VersionMajor, ai.VersionMinor, ai.VersionPatch, ai.ABI,
				ai.RequiredCaps, ai.Overruns, ai.OTASlots, ai.ConfigLen)
		})

	case "reset":
		mode := tfw.ResetSoft
		if len(args) > 0 && args[0] == "boot" {
			mode = tfw.ResetBootloader
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
		var m tfw.PinMode
		switch args[1] {
		case "in":
			m = tfw.PinInput
		case "out":
			m = tfw.PinOutput
		case "pullup":
			m = tfw.PinInputPullup
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
		high, err := parseLevel(args[1])
		if err != nil {
			return err
		}
		if err := dev.DigitalWrite(ctx, pin, high); err != nil {
			return err
		}
		return emit(out, asJSON, map[string]any{"pin": pin, "high": high},
			func() string { return fmt.Sprintf("pin %d <- %s\n", pin, args[1]) })

	case "timer":
		if len(args) < 3 {
			return fmt.Errorf("usage: timer <pin> <0|1> <duration, e.g. 5s>")
		}
		pin, err := parsePin(args[0])
		if err != nil {
			return err
		}
		high, err := parseLevel(args[1])
		if err != nil {
			return err
		}
		dur, err := time.ParseDuration(args[2])
		if err != nil {
			return fmt.Errorf("duration %q: %w", args[2], err)
		}
		slot, err := dev.TimerSet(ctx, pin, high, dur)
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

	case "ota-stage":
		// --activate sends OTA_ACTIVATE on this same connection after
		// OTA_END. The session's COMPLETE state is RAM only, and a board that
		// resets on open (the Mega 2560 over USB serial) loses it before a
		// separate ota-activate run can send anything.
		activate, mode := false, tfw.OTAActivateAtReset
		sig, args, signed, err := takeOTASig(args)
		if err != nil {
			return err
		}
		if len(args) == 2 {
			switch args[0] {
			case "--activate":
				activate = true
			case "--activate=now":
				activate, mode = true, tfw.OTAActivateResetNow
			default:
				return fmt.Errorf("usage: ota-stage [--activate[=now]] <image>")
			}
			args = args[1:]
		}
		if len(args) != 1 || signed && !activate {
			return fmt.Errorf("usage: ota-stage [--activate[=now]] <image>")
		}
		if activate && !signed {
			return needOTASig("ota-stage --activate")
		}
		// No parsing here: the device gates the image, and a refusal is its
		// status, returned as every other verb returns one.
		image, err := os.ReadFile(args[0])
		if err != nil {
			return fmt.Errorf("usage: ota-stage [--activate[=now]] <image>: %w", err)
		}
		if err := dev.StageImage(ctx, image); err != nil {
			return err
		}
		if activate {
			if err := dev.OTAAuth(ctx, sig); err != nil {
				return err
			}
			if err := dev.OTAActivate(ctx, mode); err != nil {
				return err
			}
			// The board resets once the reply drains: any further request
			// races the reset, so reset-now reports the activation alone.
			if mode == tfw.OTAActivateResetNow {
				return emit(out, asJSON, map[string]any{"activated": true, "reset_now": true},
					func() string { return "activated; the device resets now\n" })
			}
		}
		return emitOTAStatus(ctx, dev, asJSON, out)

	case "ota-activate":
		// Its own verb, not the end of ota-stage: OTA_END finishes the stream,
		// and taking the image is the control plane's call (OTA ADR decision
		// 8). The device re-verifies the store before it writes the MARK.
		// It needs a transport that does not reset the board on open: on one
		// that does, the staged session is gone and the device answers
		// TFW_ERR_BAD_STATE, so stage there with ota-stage --activate.
		mode := tfw.OTAActivateAtReset
		sig, args, signed, err := takeOTASig(args)
		if err != nil {
			return err
		}
		switch {
		case len(args) == 1 && args[0] == "now":
			mode = tfw.OTAActivateResetNow
		case len(args) != 0:
			return fmt.Errorf("usage: ota-activate [now]")
		}
		if !signed {
			return needOTASig("ota-activate")
		}
		if err := dev.OTAAuth(ctx, sig); err != nil {
			return err
		}
		if err := dev.OTAActivate(ctx, mode); err != nil {
			return err
		}
		return emit(out, asJSON, map[string]any{"activated": true, "reset_now": mode == tfw.OTAActivateResetNow},
			func() string {
				if mode == tfw.OTAActivateResetNow {
					return "activated; the device resets now\n"
				}
				return "activated; taken at the next reset\n"
			})

	case "ota-confirm":
		// Confirms the running trial image; tfw-boot settles it at the next
		// reset. The signature binds the running image's measurement (#137).
		sig, args, signed, err := takeOTASig(args)
		if err != nil {
			return err
		}
		if len(args) != 0 {
			return fmt.Errorf("usage: ota-confirm %s", otaSigUsage)
		}
		if !signed {
			return needOTASig("ota-confirm")
		}
		if err := dev.OTAAuth(ctx, sig); err != nil {
			return err
		}
		if err := dev.OTAConfirm(ctx); err != nil {
			return err
		}
		return emit(out, asJSON, map[string]any{"confirmed": true},
			func() string { return "confirmed; settled at the next reset\n" })

	case "ota-status":
		if len(args) != 0 {
			return fmt.Errorf("usage: ota-status")
		}
		return emitOTAStatus(ctx, dev, asJSON, out)

	case "ota-info":
		if len(args) != 1 {
			return fmt.Errorf("usage: ota-info <slot>")
		}
		slot, err := strconv.ParseUint(args[0], 0, 8)
		if err != nil {
			return fmt.Errorf("slot %q: %w", args[0], err)
		}
		si, err := dev.OTAInfo(ctx, uint8(slot))
		if err != nil {
			return err
		}
		m := hex.EncodeToString(si.Measurement[:])
		return emit(out, asJSON, map[string]any{
			"slots": si.Slots, "active": si.Active, "flags": si.Flags, "floor": si.Floor,
			"state": si.State, "image_version": si.ImageVersion, "measurement": m,
		}, func() string {
			return fmt.Sprintf("slots %d, active %d, flags %#02x\nfloor %d\nstate %d\nimage version %d\nmeasurement %s\n",
				si.Slots, si.Active, si.Flags, si.Floor, si.State, si.ImageVersion, m)
		})

	case "netcfg":
		return runNetcfg(ctx, dev, args, asJSON, out)

	default:
		return runPeripheral(ctx, dev, cmd, args, asJSON, out)
	}
}

// emitOTAStatus prints OTA_STATUS's state and received count: the one way
// both ota- verbs print it.
func emitOTAStatus(ctx context.Context, dev *tfw.Device, asJSON bool, out io.Writer) error {
	st, received, err := dev.OTAStatus(ctx)
	if err != nil {
		return err
	}
	return emit(out, asJSON, map[string]any{"state": st.String(), "received": received},
		func() string { return fmt.Sprintf("%s, %d bytes received\n", st, received) })
}

func featureList(c *tfw.Caps) string {
	var on []string
	for _, f := range []struct {
		bit  uint32
		name string
	}{
		{tfw.FeatureGPIO, "gpio"}, {tfw.FeatureSched, "sched"}, {tfw.FeatureAnalog, "analog"},
		{tfw.FeaturePWM, "pwm"}, {tfw.FeatureTone, "tone"}, {tfw.FeaturePulse, "pulse"},
		{tfw.FeatureEEPROM, "eeprom"}, {tfw.FeatureI2C, "i2c"}, {tfw.FeatureSPI, "spi"},
		{tfw.FeatureOneWire, "onewire"}, {tfw.FeatureAttest, "attest"}, {tfw.FeatureDeadman, "deadman"},
		{tfw.FeatureProvision, "provision"}, {tfw.FeatureAliasCert, "attest-cert"},
		{tfw.FeatureAuthSession, "auth"}, {tfw.FeatureAttestCSR, "attest-csr"},
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

// parseLevel reads a level argument: exactly "0" or "1", the words the usage
// text names. Anything else is a usage error, so a typo never drives a pin.
func parseLevel(s string) (bool, error) {
	switch s {
	case "0":
		return false, nil
	case "1":
		return true, nil
	}
	return false, fmt.Errorf("level %q, want 0 or 1", s)
}

func fail(err error) {
	fmt.Fprintln(os.Stderr, "tfwctl:", err)
	os.Exit(1)
}
