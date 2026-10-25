// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build tfwsim

package main

import (
	"bytes"
	"context"
	"crypto/ecdsa"
	"encoding/hex"
	"encoding/json"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/simproc"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/tfwsim"
)

// Issue #163: every tfwctl command end to end over both links the element
// simulator serves, serial and the Ethernet link tfwctl accepts as
// tcp://host:port. One table of cases per command family runs on each link;
// a link that does not carry a verb answers it StatusErrUnknownCmd, and that
// refusal is the case's answer there.

// ethAdmits is the Ethernet allow-list, src/proto/link_eth.c tfw_link_admits,
// named by the tfwctl verbs that send its ids. A literal, so a widening of
// the list is a change here too.
var ethAdmits = map[string]bool{
	"caps": true, "pin": true, // pin is CAPS with a pin page
	"app": true, "attest": true, "attest-cert": true, "attest-csr": true,
	"ota-stage": true, "ota-status": true,
}

// link is one of the two transports a device answers on.
type link struct {
	name   string
	pick   func(serial, eth *tfw.Device) *tfw.Device
	admits func(verb string) bool
}

var links = []link{
	{"serial", func(s, _ *tfw.Device) *tfw.Device { return s }, func(string) bool { return true }},
	{"tcp", func(_, e *tfw.Device) *tfw.Device { return e }, func(v string) bool { return ethAdmits[v] }},
}

// linkCase is one tfwctl run. With check set it must succeed on a link that
// carries the verb, and check reads its --json reply; without, the device
// must refuse it with refused. usage, when set, is a refusal tfwctl makes
// before the wire, the same on every link.
type linkCase struct {
	name    string
	verb    string
	args    []string
	check   func(t *testing.T, out []byte)
	refused tfw.Status
	usage   string
}

func (c linkCase) runOn(t *testing.T, l link, dev *tfw.Device) {
	t.Helper()
	if c.usage != "" {
		err := run(context.Background(), dev, c.verb, c.args, true, &bytes.Buffer{})
		require.Error(t, err, "%s %v", c.verb, c.args)
		assert.Contains(t, err.Error(), c.usage, "%s %v", c.verb, c.args)
		return
	}
	if !l.admits(c.verb) {
		assert.Equal(t, tfw.StatusErrUnknownCmd, cliStatus(t, dev, c.verb, c.args...),
			"%s %v is off the %s allow-list", c.verb, c.args, l.name)
		return
	}
	if c.check == nil {
		assert.Equal(t, c.refused, cliStatus(t, dev, c.verb, c.args...), "%s %v over %s", c.verb, c.args, l.name)
		return
	}
	var out bytes.Buffer
	require.NoError(t, run(context.Background(), dev, c.verb, c.args, true, &out), "%s %v over %s", c.verb, c.args, l.name)
	c.check(t, out.Bytes())
}

// linkSuite is one family's cases, run in order on a fresh device per link.
// settled, when set, runs after them with the device's serial link, which
// carries everything: it checks what the cases left behind, or did not.
type linkSuite struct {
	operator *ecdsa.PublicKey
	app      bool // the element image with the example application linked
	cases    []linkCase
	settled  func(t *testing.T, l link, serial *tfw.Device)
}

func (s linkSuite) run(t *testing.T) {
	var point [tfw.OperatorPubkeyLen]byte
	if s.operator != nil {
		sec1, err := s.operator.Bytes()
		require.NoError(t, err)
		copy(point[:], sec1[1:])
	}
	for _, l := range links {
		t.Run(l.name, func(t *testing.T) {
			var start func(args []string) *tfw.Device
			if s.app {
				start = func(args []string) *tfw.Device {
					return tfw.New(simproc.Start(t, simproc.ElementApp, args...), tfw.WithTimeout(10*time.Second))
				}
			}
			serial, eth := startEthernetOperator(t, point, start)
			dev := l.pick(serial, eth)
			for _, c := range s.cases {
				t.Run(c.name, func(t *testing.T) { c.runOn(t, l, dev) })
			}
			if s.settled != nil {
				s.settled(t, l, serial)
			}
		})
	}
}

// jsonField decodes out and returns its field as a string.
func jsonField(t *testing.T, out []byte, field string) string {
	t.Helper()
	var m map[string]any
	require.NoError(t, json.Unmarshal(out, &m), "--json printed %q", out)
	s, ok := m[field].(string)
	require.True(t, ok, "--json has no string %q: %q", field, out)
	return s
}

func jsonHex(t *testing.T, out []byte, field string) []byte {
	t.Helper()
	b, err := hex.DecodeString(jsonField(t, out, field))
	require.NoError(t, err)
	return b
}

func TestE2ELinksAuth(t *testing.T) {
	pub, key := writeOperatorKey(t)
	_, otherKey := writeOperatorKey(t)
	otherSerial := hex.EncodeToString(bytes.Repeat([]byte{0x77}, tfw.SerialLen))
	linkSuite{
		operator: pub,
		cases: []linkCase{
			{name: "reserved read before auth", verb: "eeprom-read", args: []string{"0", "4"},
				refused: tfw.StatusErrAuthRequired},
			{name: "another key", verb: "auth", args: []string{testSerialHex, otherKey},
				refused: tfw.StatusErrCrypto},
			{name: "another serial", verb: "auth", args: []string{otherSerial, key},
				refused: tfw.StatusErrCrypto},
			{name: "short serial", verb: "auth", args: []string{"0011", key}, usage: "want exactly"},
			{name: "no key", verb: "auth", args: []string{testSerialHex}, usage: "usage: auth"},
			{name: "chain without --", verb: "auth", args: []string{testSerialHex, key, "eeprom-read"},
				usage: "usage: auth"},
			{name: "operator key", verb: "auth", args: []string{testSerialHex, key}, check: func(t *testing.T, out []byte) {
				assert.JSONEq(t, `{"authenticated": true}`, string(out))
			}},
			{name: "chained reserved read", verb: "auth", args: []string{testSerialHex, key, "--", "eeprom-read", "0", "4"},
				check: func(t *testing.T, out []byte) {
					assert.Equal(t, platformMagic, jsonField(t, out, "hex"), "platform magic read inside the session")
				}},
		},
		settled: func(t *testing.T, l link, serial *tfw.Device) {
			if l.name == "serial" {
				return
			}
			// Nothing over TCP opened a session the serial host can use.
			assert.Equal(t, tfw.StatusErrAuthRequired, cliStatus(t, serial, "eeprom-read", "0", "4"))
		},
	}.run(t)
}

// The attestation commands are on both links; each answer is checked under
// the test identity's DeviceID key, and each refusal is the device's.
func TestE2ELinksAttestation(t *testing.T) {
	// The nonce csrDigestForTestIdentity was pinned over: bytes 0..31.
	b := make([]byte, tfw.AliasNonceLen)
	for i := range b {
		b[i] = byte(i)
	}
	linkSuite{
		cases: []linkCase{
			{name: "attest", verb: "attest", args: []string{hex.EncodeToString(attestNonce(0x5a))},
				check: func(t *testing.T, out []byte) {
					ev := jsonHex(t, out, "evidence")
					assert.Equal(t, attestNonce(0x5a), ev[offNonce:offNonce+tfw.AttestNonceLen], "frame nonce")
					assert.Equal(t, tfwsim.TestSerial[:], ev[offDevice:offDevice+tfw.SerialLen], "DeviceID")
					assert.True(t, verifyFrame(tfwsim.TestDeviceIDPublicKey(), ev), "evidence signature")
				}},
			{name: "attest short nonce", verb: "attest", args: []string{hex.EncodeToString(aliasNonce(1))[:2]},
				usage: "want exactly"},
			{name: "attest-cert short nonce", verb: "attest-cert", args: []string{hex.EncodeToString(attestNonce(1))},
				usage: "want exactly"},
			{name: "attest-csr before its certificate", verb: "attest-csr", args: []string{"--nonce", hex.EncodeToString(b)},
				refused: tfw.StatusErrBadArgc},
			{name: "attest-cert", verb: "attest-cert", args: []string{hex.EncodeToString(b)},
				check: func(t *testing.T, out []byte) {
					cert := parseCert(t, jsonHex(t, out, "certificate"))
					require.NoError(t, cert.CheckSignature(cert.SignatureAlgorithm, cert.RawTBSCertificate, cert.Signature))
					nonce, _, _ := claimsOf(t, uccsOf(t, cert))
					assert.Equal(t, b, nonce, "eat_nonce")
				}},
			{name: "attest-csr for another nonce", verb: "attest-csr",
				args: []string{"--nonce", hex.EncodeToString(aliasNonce(0x60))}, refused: tfw.StatusErrBadArgc},
			{name: "attest-csr", verb: "attest-csr", args: []string{"--nonce", hex.EncodeToString(b)},
				check: func(t *testing.T, out []byte) {
					sig := jsonHex(t, out, "signature")
					require.Len(t, sig, tfw.CSRSigLen)
					assert.True(t, csrVerifiesUnder(t, tfwsim.TestDeviceIDPublicKey(), sig, csrDigestForTestIdentity), "CSR signature over the pinned digest")
				}},
		},
	}.run(t)
}

func jsonInto(t *testing.T, out []byte, v any) {
	t.Helper()
	require.NoError(t, json.Unmarshal(out, v), "--json printed %q", out)
}

// The device's own description: ping, caps, sys, pin, app and reset. Only
// caps, pin and app are on the Ethernet allow-list.
func TestE2ELinksCore(t *testing.T) {
	linkSuite{
		cases: []linkCase{
			{name: "ping", verb: "ping", args: []string{"hello", "there"}, check: func(t *testing.T, out []byte) {
				assert.Equal(t, "hello there", jsonField(t, out, "echo"))
				assert.Equal(t, hex.EncodeToString([]byte("hello there")), jsonField(t, out, "hex"))
			}},
			{name: "empty ping", verb: "ping", check: func(t *testing.T, out []byte) {
				assert.Empty(t, jsonField(t, out, "echo"))
			}},
			{name: "caps", verb: "caps", check: func(t *testing.T, out []byte) {
				var c tfw.Caps
				jsonInto(t, out, &c)
				assert.Equal(t, tfw.CryptoSecureElement, c.CryptoTier)
				assert.True(t, c.Has(tfw.FeatureAttest) && c.Has(tfw.FeatureAliasCert) && c.Has(tfw.FeatureAttestCSR),
					"features %#x lack the attestation commands", c.Features)
				assert.NotZero(t, c.PinCount)
				assert.NotZero(t, c.SchedSlots)
			}},
			{name: "sys", verb: "sys", check: func(t *testing.T, out []byte) {
				var si tfw.SysInfo
				jsonInto(t, out, &si)
				assert.NotZero(t, si.FreeRAM)
				assert.Zero(t, si.ErrCRC, "framer CRC errors")
			}},
			{name: "pin", verb: "pin", args: []string{"13"}, check: func(t *testing.T, out []byte) {
				var p tfw.PinInfo
				jsonInto(t, out, &p)
				assert.EqualValues(t, 13, p.Pin)
				assert.True(t, p.HasPort)
				assert.False(t, p.HasADC)
			}},
			{name: "analog pin", verb: "pin", args: []string{"14"}, check: func(t *testing.T, out []byte) {
				var p tfw.PinInfo
				jsonInto(t, out, &p)
				assert.True(t, p.HasADC, "A0 has an ADC channel")
			}},
			{name: "pin out of range", verb: "pin", args: []string{"200"}, refused: tfw.StatusErrBadPin},
			{name: "pin not a number", verb: "pin", args: []string{"x"}, usage: "x"},
			{name: "app without an application", verb: "app", refused: tfw.StatusErrUnknownCmd},
			// Last: the device acknowledges, then restarts.
			{name: "reset", verb: "reset", check: func(t *testing.T, out []byte) {
				var r struct {
					Reset bool `json:"reset"`
				}
				jsonInto(t, out, &r)
				assert.True(t, r.Reset)
			}},
		},
	}.run(t)
}

// APP_INFO is on the Ethernet allow-list: the image with an application
// answers it on both links.
func TestE2ELinksApp(t *testing.T) {
	linkSuite{
		app: true,
		cases: []linkCase{
			{name: "app", verb: "app", check: func(t *testing.T, out []byte) {
				var ai tfw.AppInfo
				jsonInto(t, out, &ai)
				assert.Equal(t, "heartbeat", ai.Name)
				assert.Equal(t, tfw.AppRunning, ai.State)
				assert.EqualValues(t, 1, ai.ABI)
			}},
		},
	}.run(t)
}

// pinLevel is the --json reply of read and write.
func pinLevel(t *testing.T, out []byte) bool {
	t.Helper()
	var r struct {
		High bool `json:"high"`
	}
	jsonInto(t, out, &r)
	return r.High
}

// GPIO: mode, write and read. None is on the Ethernet allow-list, and a write
// refused there does not reach the pin.
func TestE2ELinksGPIO(t *testing.T) {
	level := func(want bool) func(t *testing.T, out []byte) {
		return func(t *testing.T, out []byte) { assert.Equal(t, want, pinLevel(t, out)) }
	}
	linkSuite{
		cases: []linkCase{
			{name: "mode out", verb: "mode", args: []string{"13", "out"}, check: func(t *testing.T, out []byte) {
				assert.Equal(t, "out", jsonField(t, out, "mode"))
			}},
			{name: "write high", verb: "write", args: []string{"13", "1"}, check: level(true)},
			{name: "read high", verb: "read", args: []string{"13"}, check: level(true)},
			{name: "write low", verb: "write", args: []string{"13", "0"}, check: level(false)},
			{name: "read low", verb: "read", args: []string{"13"}, check: level(false)},
			{name: "mode in", verb: "mode", args: []string{"12", "in"}, check: func(t *testing.T, out []byte) {
				assert.Equal(t, "in", jsonField(t, out, "mode"))
			}},
			{name: "mode pullup", verb: "mode", args: []string{"12", "pullup"}, check: func(t *testing.T, out []byte) {
				assert.Equal(t, "pullup", jsonField(t, out, "mode"))
			}},
			{name: "read input", verb: "read", args: []string{"12"}, check: func(t *testing.T, out []byte) { pinLevel(t, out) }},
			{name: "mode out of range", verb: "mode", args: []string{"200", "out"}, refused: tfw.StatusErrBadPin},
			{name: "write out of range", verb: "write", args: []string{"200", "1"}, refused: tfw.StatusErrBadPin},
			{name: "read out of range", verb: "read", args: []string{"200"}, refused: tfw.StatusErrBadPin},
			{name: "unknown mode", verb: "mode", args: []string{"13", "bogus"}, usage: `unknown mode "bogus"`},
			{name: "write without a level", verb: "write", args: []string{"13"}, usage: "usage: write"},
		},
		settled: func(t *testing.T, l link, serial *tfw.Device) {
			if l.name == "serial" {
				return
			}
			var r struct {
				High bool `json:"high"`
			}
			cliJSON(t, serial, &r, "read", "13")
			assert.False(t, r.High, "a write refused over TCP drove the pin")
		},
	}.run(t)
}

// eepromHex checks an eeprom-read reply.
func eepromHex(want string) func(t *testing.T, out []byte) {
	return func(t *testing.T, out []byte) { assert.Equal(t, want, jsonField(t, out, "hex")) }
}

// EEPROM: the application region reads, writes and reads back; the platform
// block is closed without a session and never written; the end of the part
// and the chunk size are refused. Off the Ethernet allow-list, and a write
// refused there does not reach the part.
func TestE2ELinksEEPROM(t *testing.T) {
	linkSuite{
		cases: []linkCase{
			{name: "erased", verb: "eeprom-read", args: []string{"0x100", "4"}, check: eepromHex("ffffffff")},
			{name: "write", verb: "eeprom-write", args: []string{"0x100", "a1b2"}, check: func(t *testing.T, out []byte) {
				var r struct {
					Addr  int `json:"addr"`
					Bytes int `json:"bytes"`
				}
				jsonInto(t, out, &r)
				assert.Equal(t, 0x100, r.Addr)
				assert.Equal(t, 2, r.Bytes)
			}},
			{name: "read back", verb: "eeprom-read", args: []string{"0x100", "2"}, check: eepromHex("a1b2")},
			{name: "platform block without a session", verb: "eeprom-read", args: []string{"0", "4"},
				refused: tfw.StatusErrAuthRequired},
			{name: "platform block write", verb: "eeprom-write", args: []string{"0", "00"},
				refused: tfw.StatusErrProtectedRegion},
			{name: "read past the end", verb: "eeprom-read", args: []string{"0xffff", "4"}, refused: tfw.StatusErrRange},
			{name: "write past the end", verb: "eeprom-write", args: []string{"0xfffe", "0102"}, refused: tfw.StatusErrRange},
			{name: "read longer than a chunk", verb: "eeprom-read", args: []string{"0x100", "200"}, usage: "outside 1..8"},
			{name: "write not hex", verb: "eeprom-write", args: []string{"0x100", "zz"}, usage: `hex "zz"`},
		},
		settled: func(t *testing.T, l link, serial *tfw.Device) {
			if l.name == "serial" {
				return
			}
			var r struct {
				Hex string `json:"hex"`
			}
			cliJSON(t, serial, &r, "eeprom-read", "0x100", "2")
			assert.Equal(t, "ffff", r.Hex, "a write refused over TCP reached the EEPROM")
		},
	}.run(t)
}

// I2C: the simulator's bus has nothing on it, so a transaction is refused
// NACK; the element owns the bus clock, so i2c-config is refused as the
// platform's. Off the Ethernet allow-list.
func TestE2ELinksI2C(t *testing.T) {
	linkSuite{
		cases: []linkCase{
			{name: "write then read", verb: "i2c", args: []string{"0x20", "01", "2"}, refused: tfw.StatusErrNACK},
			{name: "read only", verb: "i2c", args: []string{"0x20", "-", "2"}, refused: tfw.StatusErrNACK},
			{name: "address past 7 bits", verb: "i2c", args: []string{"0x90", "01", "1"}, refused: tfw.StatusErrRange},
			{name: "config on the element's bus", verb: "i2c-config", args: []string{"100000"},
				refused: tfw.StatusErrProtectedRegion},
			{name: "config below the floor", verb: "i2c-config", args: []string{"1"}, usage: "outside 1000..400000"},
		},
	}.run(t)
}

// SPI: the simulator's MISO reads zero, one byte per byte clocked out.
// Off the Ethernet allow-list.
func TestE2ELinksSPI(t *testing.T) {
	spiConfig := func(lsbFirst bool) func(t *testing.T, out []byte) {
		return func(t *testing.T, out []byte) {
			var r struct {
				Mode     int  `json:"mode"`
				ClkDiv   int  `json:"clkdiv"`
				LSBFirst bool `json:"lsb_first"`
			}
			jsonInto(t, out, &r)
			assert.Equal(t, 0, r.Mode)
			assert.Equal(t, 1, r.ClkDiv)
			assert.Equal(t, lsbFirst, r.LSBFirst)
		}
	}
	linkSuite{
		cases: []linkCase{
			{name: "config msb first", verb: "spi-config", args: []string{"0", "1", "msb"}, check: spiConfig(false)},
			{name: "config lsb first", verb: "spi-config", args: []string{"0", "1", "lsb"}, check: spiConfig(true)},
			{name: "transfer", verb: "spi", args: []string{"10", "0102"}, check: func(t *testing.T, out []byte) {
				assert.Equal(t, "0000", jsonField(t, out, "hex"))
			}},
			{name: "chip select out of range", verb: "spi", args: []string{"200", "01"}, refused: tfw.StatusErrBadPin},
			{name: "config mode past 3", verb: "spi-config", args: []string{"9", "1", "msb"}, usage: "outside 0..3"},
		},
	}.run(t)
}

// 1-Wire: the simulator's bus has no device on it. Off the Ethernet
// allow-list.
func TestE2ELinksOneWire(t *testing.T) {
	linkSuite{
		cases: []linkCase{
			{name: "reset", verb: "1wire-reset", args: []string{"4"}, check: func(t *testing.T, out []byte) {
				var r struct {
					Present bool `json:"present"`
				}
				jsonInto(t, out, &r)
				assert.False(t, r.Present, "a presence pulse on an empty bus")
			}},
			{name: "write", verb: "1wire-write", args: []string{"4", "cc"}, check: func(t *testing.T, out []byte) {
				var r struct {
					Bytes int `json:"bytes"`
				}
				jsonInto(t, out, &r)
				assert.Equal(t, 1, r.Bytes)
			}},
			{name: "read", verb: "1wire-read", args: []string{"4", "2"}, check: func(t *testing.T, out []byte) {
				assert.Equal(t, "0000", jsonField(t, out, "hex"))
			}},
			{name: "reset out of range", verb: "1wire-reset", args: []string{"200"}, refused: tfw.StatusErrBadPin},
			{name: "write out of range", verb: "1wire-write", args: []string{"200", "cc"}, refused: tfw.StatusErrBadPin},
			{name: "read out of range", verb: "1wire-read", args: []string{"200", "1"}, refused: tfw.StatusErrBadPin},
			{name: "write not hex", verb: "1wire-write", args: []string{"4", "zz"}, usage: `"zz"`},
		},
	}.run(t)
}

// pinReply checks that a reply names pin.
func pinReply(pin int) func(t *testing.T, out []byte) {
	return func(t *testing.T, out []byte) {
		var r struct {
			Pin int `json:"pin"`
		}
		jsonInto(t, out, &r)
		assert.Equal(t, pin, r.Pin)
	}
}

// Analog in, PWM, tone and pulse timing: each reaches only the pins the
// board wires to it. Nothing drives the simulator's inputs, so a pulse
// measurement times out. Off the Ethernet allow-list.
func TestE2ELinksAnalogPWMTonePulse(t *testing.T) {
	linkSuite{
		cases: []linkCase{
			{name: "analog", verb: "analog", args: []string{"14"}, check: pinReply(14)},
			{name: "analog on a digital pin", verb: "analog", args: []string{"2"}, refused: tfw.StatusErrUnsupported},
			{name: "adc-ref", verb: "adc-ref", args: []string{"internal"}, check: func(t *testing.T, out []byte) {
				assert.Equal(t, "internal", jsonField(t, out, "reference"))
			}},
			{name: "adc-ref unknown", verb: "adc-ref", args: []string{"bogus"}, usage: `unknown reference "bogus"`},
			{name: "pwm", verb: "pwm", args: []string{"3", "128"}, check: pinReply(3)},
			{name: "pwm on a pin without a timer", verb: "pwm", args: []string{"2", "128"}, refused: tfw.StatusErrUnsupported},
			{name: "pwm duty past 255", verb: "pwm", args: []string{"3", "300"}, usage: `duty "300"`},
			{name: "tone", verb: "tone", args: []string{"11", "1000"}, check: pinReply(11)},
			{name: "notone", verb: "notone", args: []string{"11"}, check: pinReply(11)},
			{name: "tone on another pin", verb: "tone", args: []string{"3", "1000"}, refused: tfw.StatusErrUnsupported},
			{name: "tone below the floor", verb: "tone", args: []string{"11", "440"}, refused: tfw.StatusErrRange},
			{name: "pulse-in with no edge", verb: "pulse-in", args: []string{"2", "1", "10ms"}, refused: tfw.StatusErrTimeout},
			{name: "pulse-in out of range", verb: "pulse-in", args: []string{"200", "1", "10ms"}, refused: tfw.StatusErrBadPin},
			{name: "pulse-capture with no edge", verb: "pulse-capture", args: []string{"2", "1", "8", "10ms"},
				refused: tfw.StatusErrTimeout},
			{name: "pulse-capture of no edges", verb: "pulse-capture", args: []string{"2", "1", "0", "10ms"},
				refused: tfw.StatusErrRange},
		},
	}.run(t)
}

// timerState is the --json reply of timer-query.
func timerState(active, held bool) func(t *testing.T, out []byte) {
	return func(t *testing.T, out []byte) {
		var st tfw.TimerState
		jsonInto(t, out, &st)
		assert.Equal(t, active, st.Active, "active")
		assert.Equal(t, held, st.HeldLevel, "held level")
	}
}

// The scheduler: a timed hold the device reverts on its own, queried and
// cancelled. Off the Ethernet allow-list, and a hold refused there is not
// armed.
func TestE2ELinksScheduler(t *testing.T) {
	linkSuite{
		cases: []linkCase{
			{name: "hold", verb: "timer", args: []string{"13", "1", "1h"}, check: func(t *testing.T, out []byte) {
				var r struct {
					Pin      int    `json:"pin"`
					Duration string `json:"duration"`
				}
				jsonInto(t, out, &r)
				assert.Equal(t, 13, r.Pin)
				assert.Equal(t, "1h0m0s", r.Duration)
			}},
			{name: "query held", verb: "timer-query", args: []string{"13"}, check: timerState(true, true)},
			{name: "cancel applying the final level", verb: "timer-cancel", args: []string{"13", "apply"},
				check: func(t *testing.T, out []byte) {
					var r struct {
						Applied bool `json:"applied_final"`
					}
					jsonInto(t, out, &r)
					assert.True(t, r.Applied)
				}},
			{name: "query cancelled", verb: "timer-query", args: []string{"13"}, check: timerState(false, false)},
			{name: "query a pin with no timer", verb: "timer-query", args: []string{"200"}, check: timerState(false, false)},
			{name: "hold out of range", verb: "timer", args: []string{"200", "1", "5s"}, refused: tfw.StatusErrBadPin},
			{name: "cancel with no timer", verb: "timer-cancel", args: []string{"200"}, refused: tfw.StatusErrNoSlot},
			{name: "hold of zero", verb: "timer", args: []string{"13", "1", "0s"}, usage: "outside 1..2147483647"},
			{name: "hold not a duration", verb: "timer", args: []string{"13", "1", "soon"}, usage: `duration "soon"`},
		},
		settled: func(t *testing.T, l link, serial *tfw.Device) {
			if l.name == "serial" {
				return
			}
			var st tfw.TimerState
			cliJSON(t, serial, &st, "timer-query", "13")
			assert.False(t, st.Active, "a hold refused over TCP was armed")
		},
	}.run(t)
}

// netcfgIP checks a netcfg reply's address and that it carries the rest of
// the block.
func netcfgIP(ip string) func(t *testing.T, out []byte) {
	return func(t *testing.T, out []byte) {
		var c map[string]any
		jsonInto(t, out, &c)
		assert.Equal(t, ip, c["ip"])
		assert.Equal(t, "02:00:00:00:00:01", c["mac"])
		assert.Equal(t, "255.255.255.0", c["mask"])
		assert.Equal(t, "192.0.2.1", c["gateway"])
		assert.NotEmpty(t, c["endpoint"])
	}
}

// netcfg reads and writes the network block through EEPROM_READ and
// EEPROM_WRITE, neither on the Ethernet allow-list: the block is set over
// serial, and a set refused over TCP leaves it as it was.
func TestE2ELinksNetcfg(t *testing.T) {
	// The element tier writes the network block only in an operator auth
	// session (the network peer-binding ADR), so the operator is provisioned.
	pub, key := writeOperatorKey(t)
	linkSuite{
		operator: pub,
		cases: []linkCase{
			{name: "get", verb: "netcfg", args: []string{"get"}, check: netcfgIP("192.0.2.10")},
			{name: "set", verb: "netcfg", args: []string{"set", "--mac", "02:00:00:00:00:01", "--ip", "192.0.2.20",
				"--mask", "255.255.255.0", "--gateway", "192.0.2.1", "--endpoint", "192.0.2.1:9",
				"--serial", testSerialHex, "--operator-key", key},
				check: netcfgIP("192.0.2.20")},
			{name: "get after set", verb: "netcfg", args: []string{"get"}, check: netcfgIP("192.0.2.20")},
			{name: "no subcommand", verb: "netcfg", usage: "usage: netcfg"},
			{name: "unknown subcommand", verb: "netcfg", args: []string{"bogus"}, usage: "usage: netcfg"},
			{name: "get with arguments", verb: "netcfg", args: []string{"get", "x"}, usage: "usage: netcfg"},
			{name: "set without a MAC", verb: "netcfg", args: []string{"set"}, usage: "MAC must be 6 bytes"},
		},
		settled: func(t *testing.T, l link, serial *tfw.Device) {
			if l.name == "serial" {
				return
			}
			var c map[string]any
			cliJSON(t, serial, &c, "netcfg", "get")
			assert.Equal(t, "192.0.2.10", c["ip"], "a set refused over TCP rewrote the network block")
		},
	}.run(t)
}
