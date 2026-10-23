package main

import (
	"bytes"
	"context"
	"errors"
	"io"
	"strings"
	"testing"

	"github.com/stretchr/testify/require"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
)

// answerLink answers every request with one status and payload.
type answerLink struct {
	status  tfw.Status
	payload []byte
	rx      bytes.Buffer
}

func (l *answerLink) Write(p []byte) (int, error) {
	f, _, err := tfw.Decode(p)
	if err != nil {
		return 0, err
	}
	resp, err := tfw.EncodeResponse(f.Seq, f.Cmd(), l.status, l.payload)
	if err != nil {
		return 0, err
	}
	l.rx.Write(resp)
	return len(p), nil
}

func (l *answerLink) Read(p []byte) (int, error) { return l.rx.Read(p) }

// queueLink answers each request OK with the next payload in turn.
type queueLink struct {
	replies [][]byte
	answerLink
}

func (l *queueLink) Write(p []byte) (int, error) {
	l.payload, l.replies = l.replies[0], l.replies[1:]
	return l.answerLink.Write(p)
}

func TestRunPeripheralRefusesBadArguments(t *testing.T) {
	nonce32 := strings.Repeat("00", tfw.AttestNonceLen)
	for _, tc := range []struct {
		verb string
		args []string
	}{
		{"analog", nil}, {"analog", []string{"x"}},
		{"adc-ref", nil}, {"adc-ref", []string{"aref"}},
		{"pwm", []string{"3"}}, {"pwm", []string{"x", "1"}}, {"pwm", []string{"3", "256"}},
		{"tone", []string{"3"}}, {"tone", []string{"x", "1"}}, {"tone", []string{"3", "70000"}},
		{"notone", nil}, {"notone", []string{"x"}},
		{"pulse-in", []string{"3", "1"}}, {"pulse-in", []string{"x", "1", "1ms"}}, {"pulse-in", []string{"3", "1", "soon"}},
		{"pulse-capture", []string{"3", "1", "4"}}, {"pulse-capture", []string{"x", "1", "4", "1ms"}},
		{"pulse-capture", []string{"3", "1", "x", "1ms"}}, {"pulse-capture", []string{"3", "1", "4", "soon"}},
		{"eeprom-read", []string{"0"}}, {"eeprom-read", []string{"x", "1"}}, {"eeprom-read", []string{"0", "x"}},
		{"eeprom-write", []string{"0"}}, {"eeprom-write", []string{"x", "aa"}}, {"eeprom-write", []string{"0", "zz"}},
		{"i2c", []string{"8", "-"}}, {"i2c", []string{"x", "-", "1"}}, {"i2c", []string{"8", "zz", "1"}}, {"i2c", []string{"8", "-", "x"}},
		{"spi", []string{"10"}}, {"spi", []string{"x", "aa"}}, {"spi", []string{"10", "zz"}},
		{"i2c-config", nil}, {"i2c-config", []string{"fast"}},
		{"spi-config", []string{"0", "0"}}, {"spi-config", []string{"x", "0", "msb"}},
		{"spi-config", []string{"0", "x", "msb"}}, {"spi-config", []string{"0", "0", "middle"}},
		{"1wire-reset", nil}, {"1wire-reset", []string{"x"}},
		{"1wire-write", []string{"4"}}, {"1wire-write", []string{"x", "cc"}}, {"1wire-write", []string{"4", "zz"}},
		{"1wire-read", []string{"4"}}, {"1wire-read", []string{"x", "1"}}, {"1wire-read", []string{"4", "x"}},
		{"attest", nil}, {"attest", []string{"00"}},
		{"attest-cert", nil}, {"attest-cert", []string{"00"}},
		{"auth", []string{nonce32}}, {"auth", []string{"00", "key.pem"}},
		{"auth", []string{strings.Repeat("00", tfw.SerialLen), "/nonexistent/key.pem"}},
		{"no-such-verb", nil},
	} {
		t.Run(tc.verb+" "+strings.Join(tc.args, " "), func(t *testing.T) {
			var out bytes.Buffer
			require.Error(t, runPeripheral(context.Background(), nil, tc.verb, tc.args, false, &out))
			require.Zero(t, out.Len(), "nothing may be written on failure")
		})
	}
}

// deviceVerbs are the verbs whose reply payload is either ignored or returned
// verbatim, so one canned answer drives each of them to completion.
var deviceVerbs = []struct {
	verb string
	args []string
	want string
}{
	{"adc-ref", []string{"vcc"}, "adc reference -> vcc\n"},
	{"adc-ref", []string{"external"}, "adc reference -> external\n"},
	{"adc-ref", []string{"internal"}, "adc reference -> internal\n"},
	{"pwm", []string{"3", "128"}, "pin 3 <- duty 128\n"},
	{"tone", []string{"11", "1000"}, "pin 11 <- 1000 Hz\n"},
	{"notone", []string{"11"}, "pin 11 tone off\n"},
	{"spi", []string{"10", "9f00"}, "ef40\n"},
	{"i2c-config", []string{"100000"}, "i2c clock -> 100000 Hz\n"},
	{"spi-config", []string{"0", "0", "msb"}, "spi -> mode 0, clkdiv 0, msb first\n"},
	{"spi-config", []string{"3", "1", "lsb"}, "spi -> mode 3, clkdiv 1, lsb first\n"},
	{"1wire-write", []string{"4", "cc44"}, "wrote 2 bytes\n"},
	{"1wire-read", []string{"4", "2"}, "ef40\n"},
}

func TestRunPeripheralDrivesTheDevice(t *testing.T) {
	for _, tc := range deviceVerbs {
		t.Run(tc.verb+" "+strings.Join(tc.args, " "), func(t *testing.T) {
			dev := tfw.New(&answerLink{payload: []byte{0xEF, 0x40}})
			var out bytes.Buffer
			require.NoError(t, runPeripheral(context.Background(), dev, tc.verb, tc.args, false, &out))
			require.Equal(t, tc.want, out.String())

			out.Reset()
			require.NoError(t, runPeripheral(context.Background(), dev, tc.verb, tc.args, true, &out))
			require.True(t, strings.HasPrefix(out.String(), "{"), "json output: %q", out.String())
		})
	}
}

// An unknown status from the device reaches the caller as that status and
// nothing is printed.
func TestRunPeripheralPassesAnUnknownStatusThrough(t *testing.T) {
	nonce32 := strings.Repeat("00", tfw.AttestNonceLen)
	alias := strings.Repeat("00", tfw.AliasNonceLen)
	verbs := append([]struct {
		verb string
		args []string
		want string
	}{
		{"analog", []string{"14"}, ""},
		{"pulse-in", []string{"3", "1", "1ms"}, ""},
		{"pulse-capture", []string{"3", "0", "4", "1ms"}, ""},
		{"eeprom-read", []string{"0x100", "2"}, ""},
		{"eeprom-write", []string{"0x100", "aa"}, ""},
		{"i2c", []string{"8", "-", "1"}, ""},
		{"1wire-reset", []string{"4"}, ""},
		{"attest", []string{nonce32}, ""},
		{"attest-cert", []string{alias}, ""},
		{"attest-csr", []string{"--nonce", alias}, ""},
	}, deviceVerbs...)
	for _, tc := range verbs {
		t.Run(tc.verb+" "+strings.Join(tc.args, " "), func(t *testing.T) {
			dev := tfw.New(&answerLink{status: 0xEE})
			var out bytes.Buffer
			err := runPeripheral(context.Background(), dev, tc.verb, tc.args, false, &out)
			var st tfw.Status
			require.True(t, errors.As(err, &st), "error %v is not a Status", err)
			require.Equal(t, tfw.Status(0xEE), st)
			require.Zero(t, out.Len())
		})
	}
}

func TestAttestCSRPrintsTheSignature(t *testing.T) {
	sig := bytes.Repeat([]byte{0xA5}, 64)
	nonce := strings.Repeat("11", tfw.AliasNonceLen)

	// AttestCSR reads the capability bitmap first, then sends the command.
	f := tfw.FeatureAttestCSR
	caps := []byte{2, 0, 0, 1, 0, 0x01, 22, 8, tfw.MaxPayload, byte(tfw.CryptoSoftware),
		byte(f), byte(f >> 8), byte(f >> 16), byte(f >> 24), 0, 0}
	newDev := func() *tfw.Device { return tfw.New(&queueLink{replies: [][]byte{caps, sig}}) }

	var out bytes.Buffer
	require.NoError(t, attestCSR(context.Background(), newDev(), []string{"--nonce", nonce}, false, &out))
	require.Equal(t, strings.Repeat("a5", 64)+"\n", out.String())

	out.Reset()
	require.NoError(t, attestCSR(context.Background(), newDev(), []string{"--nonce", nonce}, true, &out))
	require.Contains(t, out.String(), strings.Repeat("a5", 64))

	for _, args := range [][]string{{"--bogus"}, {"--nonce", nonce, "extra"}, {}, {"--nonce", "00"}} {
		require.Error(t, attestCSR(context.Background(), nil, args, false, io.Discard), "%v", args)
	}
}
