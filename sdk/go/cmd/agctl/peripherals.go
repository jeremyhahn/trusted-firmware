// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

// The verbs for everything that is not GPIO or the scheduler: the converter,
// PWM and tone, pulse measurement, EEPROM, the three buses, and attestation.
//
// They live beside run() rather than inside it because the switch in main.go
// was already the length at which a reader stops finding things. The split is
// by what the command touches, not by when it was written.
//
// The rule this file exists to satisfy is the project's own: a command that
// exists on the wire and is not reachable from agctl is a gap in agctl. Every
// method on Device that speaks a peripheral command has a verb here.

package main

import (
	"context"
	"encoding/hex"
	"fmt"
	"io"
	"strconv"
	"strings"
	"time"

	ag "github.com/jeremyhahn/arduino-generic/sdk/go"
)

// runPeripheral handles the verbs run() does not. It returns an error naming
// the command when it does not recognise it, so run()'s default arm can hand
// over without having to know what lives here.
func runPeripheral(ctx context.Context, dev *ag.Device, cmd string, args []string, asJSON bool, out io.Writer) error {
	switch cmd {
	case "analog":
		if len(args) < 1 {
			return fmt.Errorf("usage: analog <pin>")
		}
		pin, err := parsePin(args[0])
		if err != nil {
			return err
		}
		raw, err := dev.AnalogRead(ctx, pin)
		if err != nil {
			return err
		}
		return emit(out, asJSON, map[string]any{"pin": pin, "raw": raw},
			func() string { return fmt.Sprintf("%d\n", raw) })

	case "adc-ref":
		if len(args) < 1 {
			return fmt.Errorf("usage: adc-ref <vcc|external|internal>")
		}
		var ref ag.ADCReference
		switch args[0] {
		case "vcc":
			ref = ag.ADCRefVCC
		case "external":
			ref = ag.ADCRefExternal
		case "internal":
			ref = ag.ADCRefInternal
		default:
			return fmt.Errorf("unknown reference %q, want vcc, external or internal", args[0])
		}
		if err := dev.SetADCReference(ctx, ref); err != nil {
			return err
		}
		return emit(out, asJSON, map[string]any{"reference": args[0]},
			func() string { return fmt.Sprintf("adc reference -> %s\n", args[0]) })

	case "pwm":
		if len(args) < 2 {
			return fmt.Errorf("usage: pwm <pin> <duty 0-255>")
		}
		pin, err := parsePin(args[0])
		if err != nil {
			return err
		}
		duty, err := parseU8(args[1], "duty")
		if err != nil {
			return err
		}
		if err := dev.PWMWrite(ctx, pin, duty); err != nil {
			return err
		}
		return emit(out, asJSON, map[string]any{"pin": pin, "duty": duty},
			func() string { return fmt.Sprintf("pin %d <- duty %d\n", pin, duty) })

	case "tone":
		if len(args) < 2 {
			return fmt.Errorf("usage: tone <pin> <hz>")
		}
		pin, err := parsePin(args[0])
		if err != nil {
			return err
		}
		hz, err := parseU16(args[1], "frequency")
		if err != nil {
			return err
		}
		if err := dev.Tone(ctx, pin, hz); err != nil {
			return err
		}
		return emit(out, asJSON, map[string]any{"pin": pin, "hz": hz},
			func() string { return fmt.Sprintf("pin %d <- %d Hz\n", pin, hz) })

	case "notone":
		if len(args) < 1 {
			return fmt.Errorf("usage: notone <pin>")
		}
		pin, err := parsePin(args[0])
		if err != nil {
			return err
		}
		if err := dev.NoTone(ctx, pin); err != nil {
			return err
		}
		return emit(out, asJSON, map[string]any{"pin": pin},
			func() string { return fmt.Sprintf("pin %d tone off\n", pin) })

	case "pulse-in":
		if len(args) < 3 {
			return fmt.Errorf("usage: pulse-in <pin> <0|1> <timeout, e.g. 50ms>")
		}
		pin, err := parsePin(args[0])
		if err != nil {
			return err
		}
		timeout, err := time.ParseDuration(args[2])
		if err != nil {
			return fmt.Errorf("timeout %q: %w", args[2], err)
		}
		width, err := dev.PulseIn(ctx, pin, args[1] != "0", timeout)
		if err != nil {
			return err
		}
		return emit(out, asJSON, map[string]any{"pin": pin, "us": width.Microseconds()},
			func() string { return fmt.Sprintf("%d us\n", width.Microseconds()) })

	case "pulse-capture":
		if len(args) < 4 {
			return fmt.Errorf("usage: pulse-capture <pin> <0|1> <max-edges> <timeout, e.g. 50ms>")
		}
		pin, err := parsePin(args[0])
		if err != nil {
			return err
		}
		maxEdges, err := parseU8(args[2], "max-edges")
		if err != nil {
			return err
		}
		timeout, err := time.ParseDuration(args[3])
		if err != nil {
			return fmt.Errorf("timeout %q: %w", args[3], err)
		}
		edges, err := dev.PulseCapture(ctx, pin, args[1] != "0", maxEdges, timeout)
		if err != nil {
			return err
		}
		return emit(out, asJSON, map[string]any{"pin": pin, "edges_us": edges}, func() string {
			parts := make([]string, len(edges))
			for i, e := range edges {
				parts[i] = strconv.FormatUint(uint64(e), 10)
			}
			// One line, space separated: the caller is decoding a waveform and
			// will pipe this somewhere, not read it.
			return strings.Join(parts, " ") + "\n"
		})

	case "eeprom-read":
		if len(args) < 2 {
			return fmt.Errorf("usage: eeprom-read <addr> <count>")
		}
		addr, err := parseU16(args[0], "address")
		if err != nil {
			return err
		}
		n, err := parseU8(args[1], "count")
		if err != nil {
			return err
		}
		data, err := dev.EEPROMRead(ctx, addr, n)
		if err != nil {
			return err
		}
		return emit(out, asJSON, map[string]any{"addr": addr, "hex": hex.EncodeToString(data)},
			func() string { return hex.EncodeToString(data) + "\n" })

	case "eeprom-write":
		if len(args) < 2 {
			return fmt.Errorf("usage: eeprom-write <addr> <hex bytes>")
		}
		addr, err := parseU16(args[0], "address")
		if err != nil {
			return err
		}
		data, err := parseHex(args[1])
		if err != nil {
			return err
		}
		if err := dev.EEPROMWrite(ctx, addr, data); err != nil {
			return err
		}
		return emit(out, asJSON, map[string]any{"addr": addr, "bytes": len(data)},
			func() string { return fmt.Sprintf("wrote %d bytes at 0x%03x\n", len(data), addr) })

	case "i2c":
		if len(args) < 3 {
			return fmt.Errorf("usage: i2c <addr7> <hex to write, or -> <bytes to read>")
		}
		addr, err := parseU8(args[0], "address")
		if err != nil {
			return err
		}
		write, err := parseHexOrEmpty(args[1])
		if err != nil {
			return err
		}
		readLen, err := parseU8(args[2], "read length")
		if err != nil {
			return err
		}
		got, err := dev.I2CTransfer(ctx, addr, write, readLen)
		if err != nil {
			return err
		}
		return emit(out, asJSON, map[string]any{"addr": addr, "hex": hex.EncodeToString(got)},
			func() string { return hex.EncodeToString(got) + "\n" })

	case "spi":
		if len(args) < 2 {
			return fmt.Errorf("usage: spi <cs pin> <hex to write>")
		}
		cs, err := parsePin(args[0])
		if err != nil {
			return err
		}
		write, err := parseHex(args[1])
		if err != nil {
			return err
		}
		got, err := dev.SPITransfer(ctx, cs, write)
		if err != nil {
			return err
		}
		return emit(out, asJSON, map[string]any{"cs": cs, "hex": hex.EncodeToString(got)},
			func() string { return hex.EncodeToString(got) + "\n" })

	case "1wire-reset":
		if len(args) < 1 {
			return fmt.Errorf("usage: 1wire-reset <pin>")
		}
		pin, err := parsePin(args[0])
		if err != nil {
			return err
		}
		present, err := dev.OneWireReset(ctx, pin)
		if err != nil {
			return err
		}
		return emit(out, asJSON, map[string]any{"pin": pin, "present": present}, func() string {
			if present {
				return "presence\n"
			}
			return "no device\n"
		})

	case "1wire-write":
		if len(args) < 2 {
			return fmt.Errorf("usage: 1wire-write <pin> <hex bytes>")
		}
		pin, err := parsePin(args[0])
		if err != nil {
			return err
		}
		data, err := parseHex(args[1])
		if err != nil {
			return err
		}
		if err := dev.OneWireWrite(ctx, pin, data); err != nil {
			return err
		}
		return emit(out, asJSON, map[string]any{"pin": pin, "bytes": len(data)},
			func() string { return fmt.Sprintf("wrote %d bytes\n", len(data)) })

	case "1wire-read":
		if len(args) < 2 {
			return fmt.Errorf("usage: 1wire-read <pin> <count>")
		}
		pin, err := parsePin(args[0])
		if err != nil {
			return err
		}
		n, err := parseU8(args[1], "count")
		if err != nil {
			return err
		}
		data, err := dev.OneWireRead(ctx, pin, n)
		if err != nil {
			return err
		}
		return emit(out, asJSON, map[string]any{"pin": pin, "hex": hex.EncodeToString(data)},
			func() string { return hex.EncodeToString(data) + "\n" })

	case "attest":
		// The nonce is the caller's, never ours. A client that generates its
		// own challenge is a client attesting to itself: freshness has to come
		// from whoever is deciding whether to trust the answer.
		if len(args) < 1 {
			return fmt.Errorf("usage: attest <nonce, %d hex bytes>", ag.AttestNonceLen)
		}
		nonce, err := parseNonce(args[0], ag.AttestNonceLen)
		if err != nil {
			return err
		}
		ev, err := dev.Attest(ctx, nonce)
		if err != nil {
			return err
		}
		return emit(out, asJSON, map[string]any{"evidence": hex.EncodeToString(ev), "len": len(ev)},
			func() string { return hex.EncodeToString(ev) + "\n" })

	case "attest-cert":
		if len(args) < 1 {
			return fmt.Errorf("usage: attest-cert <nonce, %d hex bytes>", ag.AliasNonceLen)
		}
		nonce, err := parseNonce(args[0], ag.AliasNonceLen)
		if err != nil {
			return err
		}
		der, err := dev.AttestCert(ctx, nonce)
		if err != nil {
			return err
		}
		return emit(out, asJSON, map[string]any{"certificate": hex.EncodeToString(der), "len": len(der)},
			func() string { return hex.EncodeToString(der) + "\n" })
	}
	return fmt.Errorf("unknown command %q", cmd)
}

func parseU8(s, what string) (uint8, error) {
	n, err := strconv.ParseUint(s, 0, 8)
	if err != nil {
		return 0, fmt.Errorf("%s %q: %w", what, s, err)
	}
	return uint8(n), nil
}

func parseU16(s, what string) (uint16, error) {
	n, err := strconv.ParseUint(s, 0, 16)
	if err != nil {
		return 0, fmt.Errorf("%s %q: %w", what, s, err)
	}
	return uint16(n), nil
}

func parseHex(s string) ([]byte, error) {
	b, err := hex.DecodeString(strings.TrimPrefix(s, "0x"))
	if err != nil {
		return nil, fmt.Errorf("hex %q: %w", s, err)
	}
	if len(b) == 0 {
		return nil, fmt.Errorf("hex %q: no bytes", s)
	}
	return b, nil
}

// parseHexOrEmpty accepts "-" for "write nothing", which an I2C read-only
// transfer needs and which an empty shell argument expresses too easily by
// accident.
func parseHexOrEmpty(s string) ([]byte, error) {
	if s == "-" {
		return nil, nil
	}
	return parseHex(s)
}

func parseNonce(s string, want int) ([]byte, error) {
	b, err := hex.DecodeString(strings.TrimPrefix(s, "0x"))
	if err != nil {
		return nil, fmt.Errorf("nonce %q: %w", s, err)
	}
	if len(b) != want {
		return nil, fmt.Errorf("nonce is %d bytes, want exactly %d", len(b), want)
	}
	return b, nil
}
