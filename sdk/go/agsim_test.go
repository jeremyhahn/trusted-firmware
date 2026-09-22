// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package aggeneric

import (
	"context"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"testing"
	"time"
)

// agsim is the FIRMWARE, compiled for the host: the same src/proto, src/core
// and src/caps the device runs, linked against a mock HAL. So these are not
// tests against a hand-written fake that would drift the moment either side
// changed -- they are the Go SDK and the C firmware agreeing on the wire, with
// no AVR toolchain and no hardware involved.

type simConn struct {
	cmd *exec.Cmd
	in  io.WriteCloser
	out io.ReadCloser
}

func (s *simConn) Read(p []byte) (int, error)  { return s.out.Read(p) }
func (s *simConn) Write(p []byte) (int, error) { return s.in.Write(p) }
func (s *simConn) Close() error {
	_ = s.in.Close()
	_ = s.cmd.Process.Kill()
	_, _ = s.cmd.Process.Wait()
	return nil
}

func startSim(t *testing.T) *Device {
	t.Helper()

	bin, err := filepath.Abs("../../build/host/agsim")
	if err != nil {
		t.Fatalf("resolving agsim path: %v", err)
	}
	// Deliberately a failure and not a skip. A skipped cross-implementation
	// test reports success while checking nothing, and this is the only test
	// that proves the two codecs agree.
	if _, err := os.Stat(bin); err != nil {
		t.Fatalf("agsim not built at %s -- run `make agsim` first: %v", bin, err)
	}

	cmd := exec.Command(bin)
	in, err := cmd.StdinPipe()
	if err != nil {
		t.Fatal(err)
	}
	out, err := cmd.StdoutPipe()
	if err != nil {
		t.Fatal(err)
	}
	if err := cmd.Start(); err != nil {
		t.Fatal(err)
	}
	c := &simConn{cmd: cmd, in: in, out: out}
	t.Cleanup(func() { _ = c.Close() })
	return New(c, WithTimeout(5*time.Second))
}

func TestAgainstFirmwarePing(t *testing.T) {
	d := startSim(t)
	msg := []byte("pong")
	got, err := d.Ping(context.Background(), msg)
	if err != nil {
		t.Fatal(err)
	}
	if string(got) != string(msg) {
		t.Fatalf("Ping echoed %q, want %q", got, msg)
	}
}

func TestAgainstFirmwarePingWithMaximumPayload(t *testing.T) {
	d := startSim(t)
	msg := make([]byte, MaxPayload)
	for i := range msg {
		msg[i] = byte(i)
	}
	got, err := d.Ping(context.Background(), msg)
	if err != nil {
		t.Fatal(err)
	}
	if string(got) != string(msg) {
		t.Fatalf("Ping echoed %d bytes, want %d identical", len(got), len(msg))
	}
}

func TestAgainstFirmwareCaps(t *testing.T) {
	d := startSim(t)
	c, err := d.Caps(context.Background())
	if err != nil {
		t.Fatal(err)
	}
	if c.ProtoMajor != 1 {
		t.Errorf("protocol major = %d, want 1", c.ProtoMajor)
	}
	if c.MaxPayload != MaxPayload {
		t.Errorf("device MaxPayload = %d, host constant = %d -- the two must agree or a host will send frames the device refuses",
			c.MaxPayload, MaxPayload)
	}
	if !c.Has(FeatureGPIO) {
		t.Error("GPIO not reported; the default build should have it")
	}
	// Attestation is not compiled in yet, and CAPS must say so rather than
	// advertising a capability whose command would return unknown-command.
	if c.Has(FeatureAttest) {
		t.Error("attestation reported present before a crypto provider exists")
	}
	if c.CryptoTier != CryptoNone {
		t.Errorf("crypto tier = %v, want none until a provider lands", c.CryptoTier)
	}
}

func TestAgainstFirmwareSysInfo(t *testing.T) {
	d := startSim(t)
	si, err := d.SysInfo(context.Background())
	if err != nil {
		t.Fatal(err)
	}
	if si.FreeRAM == 0 {
		t.Error("free RAM reported as zero")
	}
	if si.ResetCause != ResetPowerOn {
		t.Errorf("reset cause = %v, want power-on", si.ResetCause)
	}
}

func TestAgainstFirmwareUnknownCommandIsAnswered(t *testing.T) {
	d := startSim(t)
	_, err := d.Do(context.Background(), Cmd(0x7E), nil)
	if err == nil {
		t.Fatal("expected an error for an unknown command")
	}
	// The device must ANSWER rather than ignore: a host that got silence
	// could not tell an unknown command from a dead link.
	if err.Error() != StatusErrUnknownCmd.String() {
		t.Fatalf("err = %v, want %v", err, StatusErrUnknownCmd)
	}
}

func TestAgainstFirmwareWrongArgumentCountIsRefused(t *testing.T) {
	d := startSim(t)
	// RESET takes exactly one byte.
	if _, err := d.Do(context.Background(), CmdReset, nil); err == nil {
		t.Fatal("expected AG_ERR_BAD_ARGC for a missing argument")
	}
	if _, err := d.Do(context.Background(), CmdReset, []byte{0, 0}); err == nil {
		t.Fatal("expected AG_ERR_BAD_ARGC for a surplus argument")
	}
}

// The device announces itself on boot, and the SDK must surface that rather
// than discard it: an unexpected boot event means the device reset underneath
// the host, which invalidates everything the host believed about its state.
func TestAgainstFirmwareBootEventIsDelivered(t *testing.T) {
	d := startSim(t)
	if _, err := d.Ping(context.Background(), nil); err != nil {
		t.Fatal(err)
	}
	events := d.Events()
	if len(events) == 0 {
		t.Fatal("no boot event was observed")
	}
	if !events[0].IsEvent() || events[0].Payload[0] != EventBoot {
		t.Fatalf("first event = %+v, want a boot event", events[0])
	}
}

// Many commands in sequence, to catch a framer that only works once -- a
// leftover byte, an uncleared buffer, a sequence number that stops advancing.
func TestAgainstFirmwareSustainedExchange(t *testing.T) {
	d := startSim(t)
	ctx := context.Background()
	for i := 0; i < 200; i++ {
		want := []byte{byte(i), byte(i >> 8)}
		got, err := d.Ping(ctx, want)
		if err != nil {
			t.Fatalf("iteration %d: %v", i, err)
		}
		if string(got) != string(want) {
			t.Fatalf("iteration %d: echoed %x, want %x", i, got, want)
		}
	}
}
