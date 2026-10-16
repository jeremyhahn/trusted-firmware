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
// exists on the wire and is not reachable from tfwctl is a gap in tfwctl. Every
// method on Device that speaks a peripheral command has a verb here.

package main

import (
	"bytes"
	"context"
	"crypto"
	"encoding/hex"
	"fmt"
	"io"
	"os"
	"os/exec"
	"strconv"
	"strings"
	"time"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
)

// runPeripheral handles the verbs run() does not. It returns an error naming
// the command when it does not recognise it, so run()'s default arm can hand
// over without having to know what lives here.
func runPeripheral(ctx context.Context, dev *tfw.Device, cmd string, args []string, asJSON bool, out io.Writer) error {
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
		var ref tfw.ADCReference
		switch args[0] {
		case "vcc":
			ref = tfw.ADCRefVCC
		case "external":
			ref = tfw.ADCRefExternal
		case "internal":
			ref = tfw.ADCRefInternal
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

	case "i2c-config":
		if len(args) < 1 {
			return fmt.Errorf("usage: i2c-config <clock hz>")
		}
		hz, err := strconv.ParseUint(args[0], 0, 32)
		if err != nil {
			return fmt.Errorf("clock %q: %w", args[0], err)
		}
		if err := dev.SetI2CClock(ctx, uint32(hz)); err != nil {
			return err
		}
		return emit(out, asJSON, map[string]any{"hz": hz},
			func() string { return fmt.Sprintf("i2c clock -> %d Hz\n", hz) })

	case "spi-config":
		if len(args) < 3 {
			return fmt.Errorf("usage: spi-config <mode 0-3> <clkdiv 0-3> <msb|lsb>")
		}
		mode, err := parseU8(args[0], "mode")
		if err != nil {
			return err
		}
		clkdiv, err := parseU8(args[1], "clkdiv")
		if err != nil {
			return err
		}
		var lsbFirst bool
		switch args[2] {
		case "msb":
			lsbFirst = false
		case "lsb":
			lsbFirst = true
		default:
			return fmt.Errorf("bit order %q, want msb or lsb", args[2])
		}
		if err := dev.ConfigureSPI(ctx, tfw.SPIMode(mode), tfw.SPIClockDiv(clkdiv), lsbFirst); err != nil {
			return err
		}
		return emit(out, asJSON, map[string]any{"mode": mode, "clkdiv": clkdiv, "lsb_first": lsbFirst},
			func() string { return fmt.Sprintf("spi -> mode %d, clkdiv %d, %s first\n", mode, clkdiv, args[2]) })

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
			return fmt.Errorf("usage: attest <nonce, %d hex bytes>", tfw.AttestNonceLen)
		}
		nonce, err := parseNonce(args[0], tfw.AttestNonceLen)
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
			return fmt.Errorf("usage: attest-cert <nonce, %d hex bytes>", tfw.AliasNonceLen)
		}
		nonce, err := parseNonce(args[0], tfw.AliasNonceLen)
		if err != nil {
			return err
		}
		der, err := dev.AttestCert(ctx, nonce)
		if err != nil {
			return err
		}
		return emit(out, asJSON, map[string]any{"certificate": hex.EncodeToString(der), "len": len(der)},
			func() string { return hex.EncodeToString(der) + "\n" })

	case "attest-csr":
		return attestCSR(ctx, dev, args, asJSON, out)

	case "auth":
		// Element tier only. serial is the device's OWN platform identity,
		// never generated here -- it comes from wherever the fleet already
		// knows it (tfwprov's --bake-eeprom output, or a fresh Attest call),
		// because the signed message is bound to it: tfw.AuthResponse.
		//
		// A GATED COMMAND, IF ANY, RUNS HERE IN THE SAME CONNECTION -- given
		// after `--`. The session lives in the device's RAM, not in this
		// process, but several supported boards (the Mega 2560's 16U2/DTR
		// link) reset the MCU when the serial port is reopened, which would
		// silently discard a session established by a PRIOR tfwctl
		// invocation before a second one ever reached the device. Chaining
		// here is what makes `tfwctl auth ... -- eeprom-read ...` a
		// trustworthy demonstration rather than one that only works if the
		// operator has separately disabled DTR-on-close (see issue #17).
		if len(args) < 2 {
			return fmt.Errorf("usage: auth <device serial, %d hex bytes> <operator key, PEM EC private key file> [-- <verb> <args...>]", tfw.SerialLen)
		}
		serial, err := parseNonce(args[0], tfw.SerialLen)
		if err != nil {
			return err
		}
		signer, err := externalECKeySigner(args[1])
		if err != nil {
			return err
		}
		var then []string
		if len(args) > 2 {
			if args[2] != "--" {
				return fmt.Errorf("usage: auth <serial> <key.pem> [-- <verb> <args...>]")
			}
			then = args[3:]
		}
		nonce, err := dev.AuthChallenge(ctx)
		if err != nil {
			return err
		}
		if err := dev.AuthResponse(ctx, signer, nonce, serial); err != nil {
			return err
		}
		if len(then) == 0 {
			return emit(out, asJSON, map[string]any{"authenticated": true},
				func() string { return "authenticated\n" })
		}
		// run(), not runPeripheral(): the chained verb may be one of run()'s
		// own top-level cases (reset, eeprom-read, ...) rather than one of
		// this file's, and run() falls through to runPeripheral() for those
		// on its own.
		return run(ctx, dev, then[0], then[1:], asJSON, out)
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

// loadECPrivateKey reads a PEM-encoded EC private key -- the operator key
// AuthResponse signs a challenge with. tfwctl never generates or stores one:
// it is a bench convenience for reading whatever key file the operator
// already has, never a place this project's key material would live.
// externalKeySigner signs by shelling out to `openssl pkeyutl`, so the
// operator private key file is read only by openssl's OWN process and never
// enters tfwctl's (this repo's Layer 0 module carries no private key
// material -- see CLAUDE.md's SDK boundary, and sdk/go must stay importable
// by go-xkms and go-trusted-platform without inheriting a key-custody
// requirement it never asked for). This is a bench convenience wired to
// whatever key an operator already has on disk, not a place this project's
// own keys would live.
type externalKeySigner struct {
	path string
}

func (s *externalKeySigner) Public() crypto.PublicKey { return nil } // unused: AuthResponse never calls it

func (s *externalKeySigner) Sign(_ io.Reader, digest []byte, _ crypto.SignerOpts) ([]byte, error) {
	cmd := exec.Command("openssl", "pkeyutl", "-sign", "-inkey", s.path, "-pkeyopt", "digest:sha256") // #nosec G204 -- the program is the fixed "openssl" and no shell is involved; s.path, the operator's own key file, is one argv element and cannot become a command
	cmd.Stdin = bytes.NewReader(digest)
	var out, stderr bytes.Buffer
	cmd.Stdout = &out
	cmd.Stderr = &stderr
	if err := cmd.Run(); err != nil {
		return nil, fmt.Errorf("openssl pkeyutl -sign -inkey %s: %w: %s", s.path, err, strings.TrimSpace(stderr.String()))
	}
	return out.Bytes(), nil
}

// externalECKeySigner confirms the file is a key openssl can load, WITHOUT
// this process ever reading its bytes: `-noout` tells openssl to parse and
// then print nothing, so a successful exit code is the only thing that
// crosses back into tfwctl. The alternative -- opening the file here to check
// for a PEM header -- would itself put key material in this process's memory
// for no real gain, which is the exact thing this function exists to avoid.
func externalECKeySigner(path string) (crypto.Signer, error) {
	if _, err := os.Stat(path); err != nil {
		return nil, fmt.Errorf("%s: %w", path, err)
	}
	cmd := exec.Command("openssl", "pkey", "-in", path, "-noout") // #nosec G204 -- the program is the fixed "openssl" and no shell is involved; path, the operator's own key file, is one argv element and cannot become a command
	var stderr bytes.Buffer
	cmd.Stderr = &stderr
	if err := cmd.Run(); err != nil {
		return nil, fmt.Errorf("%s: not a key openssl can load: %w: %s", path, err, strings.TrimSpace(stderr.String()))
	}
	return &externalKeySigner{path: path}, nil
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
