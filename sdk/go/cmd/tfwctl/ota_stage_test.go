package main

import (
	"bytes"
	"context"
	"encoding/binary"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
	"testing"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
)

// otaDevice answers the four OTA staging ids the way the stager does, and
// refuses the first request carrying refuseCmd with refuseStatus. It records
// what reached it, so a test can see the image arrive piece by piece.
type otaDevice struct {
	refuseCmd    tfw.Cmd
	refuseStatus tfw.Status

	total    uint32
	chunks   []int
	image    []byte
	state    tfw.OTAState
	received uint32
	ends     int
	modes    []byte
	sigs     [][]byte
	cmds     []tfw.Cmd
}

func (o *otaDevice) answer(f tfw.Frame) (tfw.Status, []byte) {
	cmd := f.Cmd()
	o.cmds = append(o.cmds, cmd)
	if o.refuseStatus != 0 && cmd == o.refuseCmd {
		// One-shot, as the stager's refusal is for the session it fails.
		st := o.refuseStatus
		o.refuseStatus, o.refuseCmd = 0, 0
		o.state = tfw.OTAFailed
		return st, nil
	}
	switch cmd {
	case tfw.CmdOTABegin:
		o.total = binary.LittleEndian.Uint32(f.Payload)
		o.state, o.received = tfw.OTAReceiving, 0
	case tfw.CmdOTAData:
		data := f.Payload[4:]
		o.chunks = append(o.chunks, len(data))
		o.image = append(o.image, data...)
		o.received += uint32(len(data))
	case tfw.CmdOTAEnd:
		o.ends++
		o.state = tfw.OTAComplete
	case tfw.CmdOTAAuth:
		o.sigs = append(o.sigs, append([]byte(nil), f.Payload...))
	case tfw.CmdOTAConfirm:
	case tfw.CmdOTAActivate:
		if o.state != tfw.OTAComplete {
			return tfw.StatusErrBadState, nil
		}
		o.modes = append(o.modes, f.Payload[0])
	case tfw.CmdOTAStatus:
		return tfw.StatusOK, binary.LittleEndian.AppendUint32([]byte{byte(o.state)}, o.received)
	case tfw.CmdOTAInfo:
		// Slot 1 PENDING (4) at version 3, floor 7, measurement 0xAB...
		p := []byte{2, 0, tfw.SlotTrial, 7, 0, 0, 0, 4, 3, 0, 0, 0}
		return tfw.StatusOK, append(p, bytes.Repeat([]byte{0xAB}, 32)...)
	default:
		return tfw.StatusErrUnknownCmd, nil
	}
	return tfw.StatusOK, nil
}

// startOTADevice wires an otaDevice to a tfw.Device over two pipes.
func startOTADevice(t *testing.T, o *otaDevice) *tfw.Device {
	t.Helper()
	return startFakeDevice(t, o.answer)
}

func writeImage(t *testing.T, image []byte) string {
	t.Helper()
	path := filepath.Join(t.TempDir(), "app.bin")
	if err := os.WriteFile(path, image, 0o600); err != nil {
		t.Fatal(err)
	}
	return path
}

func patterned(n int) []byte {
	b := make([]byte, n)
	for i := range b {
		b[i] = byte(i*7 + 3)
	}
	return b
}

func TestOTAStageSendsTheImageInChunksAndPrintsTheFinalState(t *testing.T) {
	o := &otaDevice{}
	dev := startOTADevice(t, o)
	image := patterned(3*tfw.OTAChunk + 5)

	var buf bytes.Buffer
	if err := run(context.Background(), dev, "ota-stage", []string{writeImage(t, image)}, false, &buf); err != nil {
		t.Fatal(err)
	}
	if o.total != uint32(len(image)) {
		t.Errorf("OTA_BEGIN total = %d, want %d", o.total, len(image))
	}
	want := []int{tfw.OTAChunk, tfw.OTAChunk, tfw.OTAChunk, 5}
	if len(o.chunks) != len(want) {
		t.Fatalf("chunks = %v, want %v", o.chunks, want)
	}
	for i := range want {
		if o.chunks[i] != want[i] {
			t.Fatalf("chunks = %v, want %v", o.chunks, want)
		}
	}
	if !bytes.Equal(o.image, image) || o.ends != 1 {
		t.Fatalf("the device holds %d bytes after %d ENDs; want the image and one END", len(o.image), o.ends)
	}
	wantOut := fmt.Sprintf("%s, %d bytes received", tfw.OTAComplete, len(image))
	if got := strings.TrimSpace(buf.String()); got != wantOut {
		t.Fatalf("ota-stage printed %q, want %q", got, wantOut)
	}
}

func TestOTAStageJSONCarriesTheStateAndCount(t *testing.T) {
	dev := startOTADevice(t, &otaDevice{})
	var buf bytes.Buffer
	if err := run(context.Background(), dev, "ota-stage", []string{writeImage(t, patterned(10))}, true, &buf); err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(buf.String(), `"received": 10`) || !strings.Contains(buf.String(), tfw.OTAComplete.String()) {
		t.Fatalf("json output %q does not carry the state and count", buf.String())
	}
}

// A refusal at any of the three steps is the device's status, returned so
// main prints it and exits non-zero; nothing after the refusal is sent.
func TestOTAStageRefusalsAreTheDevicesStatus(t *testing.T) {
	for _, tc := range []struct {
		name   string
		cmd    tfw.Cmd
		status tfw.Status
		chunks int
		ends   int
	}{
		{"at BEGIN", tfw.CmdOTABegin, tfw.StatusErrRange, 0, 0},
		{"at DATA", tfw.CmdOTAData, tfw.StatusErrFraming, 0, 0},
		{"at END", tfw.CmdOTAEnd, tfw.StatusErrCrypto, 3, 0},
	} {
		t.Run(tc.name, func(t *testing.T) {
			o := &otaDevice{refuseCmd: tc.cmd, refuseStatus: tc.status}
			dev := startOTADevice(t, o)
			var buf bytes.Buffer
			err := run(context.Background(), dev, "ota-stage",
				[]string{writeImage(t, patterned(2*tfw.OTAChunk+1))}, false, &buf)
			if !errors.Is(err, tc.status) {
				t.Fatalf("err = %v, want %v", err, tc.status)
			}
			if !strings.Contains(err.Error(), tc.status.String()) {
				t.Fatalf("the error %q does not name the status %q", err, tc.status)
			}
			if len(o.chunks) != tc.chunks || o.ends != tc.ends {
				t.Fatalf("after the refusal the device saw %d chunks and %d ENDs; want %d and %d",
					len(o.chunks), o.ends, tc.chunks, tc.ends)
			}
			if buf.Len() != 0 {
				t.Fatalf("a refused stage printed %q", buf.String())
			}
		})
	}
}

func TestOTAStageArgumentErrorsAreUsageErrors(t *testing.T) {
	missing := filepath.Join(t.TempDir(), "absent.bin")
	for _, tc := range []struct {
		name string
		args []string
	}{
		{"no image", nil},
		{"two images", []string{"a.bin", "b.bin"}},
		{"a missing file", []string{missing}},
	} {
		t.Run(tc.name, func(t *testing.T) {
			// A nil device: a usage error must be found before the wire.
			err := run(context.Background(), nil, "ota-stage", tc.args, false, io.Discard)
			if err == nil || !strings.HasPrefix(err.Error(), "usage: ota-stage [--activate[=now]] <image>") {
				t.Fatalf("err = %v, want a usage error", err)
			}
		})
	}
}

func TestOTAStageRefusesAMalformedAuthorizationBeforeTheWire(t *testing.T) {
	for _, tc := range []struct {
		name string
		args []string
		want string
	}{
		{"a short signature", []string{"--activate", "--sig=00", "a.bin"}, "the authorization is"},
		{"two signatures", []string{"--activate", testSig, testSig, "a.bin"}, "give the authorization once"},
	} {
		t.Run(tc.name, func(t *testing.T) {
			// A nil device: the signature is read before anything is sent.
			err := run(context.Background(), nil, "ota-stage", tc.args, false, io.Discard)
			if err == nil || !strings.HasPrefix(err.Error(), tc.want) {
				t.Fatalf("err = %v, want one starting %q", err, tc.want)
			}
		})
	}
}

func TestOTAStageActivateRefusalsStopTheExchange(t *testing.T) {
	for _, tc := range []struct {
		name   string
		cmd    tfw.Cmd
		status tfw.Status
	}{
		{"at OTA_AUTH", tfw.CmdOTAAuth, tfw.StatusErrCrypto},
		{"at OTA_ACTIVATE", tfw.CmdOTAActivate, tfw.StatusErrAuthRequired},
	} {
		t.Run(tc.name, func(t *testing.T) {
			o := &otaDevice{refuseCmd: tc.cmd, refuseStatus: tc.status}
			dev := startOTADevice(t, o)
			var buf bytes.Buffer
			err := run(context.Background(), dev, "ota-stage",
				[]string{"--activate", testSig, writeImage(t, patterned(tfw.OTAChunk))}, false, &buf)
			if !errors.Is(err, tc.status) {
				t.Fatalf("err = %v, want %v", err, tc.status)
			}
			// The refused command is the last one sent: no OTA_ACTIVATE after
			// a refused authorization, no OTA_STATUS after a refused activation.
			if n := len(o.cmds); n == 0 || o.cmds[n-1] != tc.cmd {
				t.Fatalf("the device saw %v; want it to end at the refused %v", o.cmds, tc.cmd)
			}
			if o.ends != 1 || len(o.modes) != 0 {
				t.Fatalf("the device saw %d ENDs and activations %v; want 1 END and none", o.ends, o.modes)
			}
			if buf.Len() != 0 {
				t.Fatalf("a refused activation printed %q", buf.String())
			}
		})
	}
}

func TestOTAStatusPrintsEachState(t *testing.T) {
	for _, st := range []tfw.OTAState{tfw.OTAIdle, tfw.OTAReceiving, tfw.OTAComplete, tfw.OTAFailed} {
		t.Run(st.String(), func(t *testing.T) {
			dev := startOTADevice(t, &otaDevice{state: st, received: 384})
			var buf bytes.Buffer
			if err := run(context.Background(), dev, "ota-status", nil, false, &buf); err != nil {
				t.Fatal(err)
			}
			want := fmt.Sprintf("%s, 384 bytes received", st)
			if got := strings.TrimSpace(buf.String()); got != want {
				t.Fatalf("ota-status printed %q, want %q", got, want)
			}
		})
	}
}

func TestOTAStatusJSONCarriesTheStateAndCount(t *testing.T) {
	dev := startOTADevice(t, &otaDevice{state: tfw.OTAReceiving, received: 128})
	var buf bytes.Buffer
	if err := run(context.Background(), dev, "ota-status", nil, true, &buf); err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(buf.String(), `"received": 128`) ||
		!strings.Contains(buf.String(), `"state": "`+tfw.OTAReceiving.String()+`"`) {
		t.Fatalf("json output %q does not carry the state and count", buf.String())
	}
}

func TestOTAStatusTakesNoArguments(t *testing.T) {
	// A nil device: the usage error must be found before the wire.
	err := run(context.Background(), nil, "ota-status", []string{"now"}, false, io.Discard)
	if err == nil || !strings.HasPrefix(err.Error(), "usage: ota-status") {
		t.Fatalf("err = %v, want a usage error", err)
	}
}

func TestOTAInfoPrintsTheSlot(t *testing.T) {
	meas := strings.Repeat("ab", 32)
	dev := startOTADevice(t, &otaDevice{})
	var buf bytes.Buffer
	if err := run(context.Background(), dev, "ota-info", []string{"1"}, false, &buf); err != nil {
		t.Fatal(err)
	}
	want := "slots 2, active 0, flags 0x04\nfloor 7\nstate 4\nimage version 3\nmeasurement " + meas + "\n"
	if buf.String() != want {
		t.Fatalf("ota-info printed %q, want %q", buf.String(), want)
	}

	buf.Reset()
	if err := run(context.Background(), dev, "ota-info", []string{"1"}, true, &buf); err != nil {
		t.Fatal(err)
	}
	var got map[string]any
	if err := json.Unmarshal(buf.Bytes(), &got); err != nil {
		t.Fatalf("ota-info --json printed %q: %v", buf.String(), err)
	}
	for k, v := range map[string]any{
		"slots": 2.0, "active": 0.0, "flags": 4.0, "floor": 7.0, "state": 4.0,
		"image_version": 3.0, "measurement": meas,
	} {
		if got[k] != v {
			t.Errorf("ota-info --json %s = %v, want %v", k, got[k], v)
		}
	}
}

func TestOTAInfoCarriesTheDevicesRefusal(t *testing.T) {
	dev := startOTADevice(t, &otaDevice{refuseCmd: tfw.CmdOTAInfo, refuseStatus: tfw.StatusErrRange})
	err := run(context.Background(), dev, "ota-info", []string{"2"}, false, io.Discard)
	var st tfw.Status
	if !errorsAsStatus(err, &st) || st != tfw.StatusErrRange {
		t.Fatalf("ota-info 2: %v, want %v", err, tfw.StatusErrRange)
	}
}

// ota-stage stops at END: it never activates, and ota-activate is the step
// that does, at the next reset or now.
func TestOTAActivateIsItsOwnStepAfterStage(t *testing.T) {
	for _, tc := range []struct {
		args []string
		mode byte
		out  string
	}{
		{nil, tfw.OTAActivateAtReset, "activated; taken at the next reset"},
		{[]string{"now"}, tfw.OTAActivateResetNow, "activated; the device resets now"},
	} {
		o := &otaDevice{}
		dev := startOTADevice(t, o)
		if err := run(context.Background(), dev, "ota-stage", []string{writeImage(t, patterned(10))}, false, io.Discard); err != nil {
			t.Fatal(err)
		}
		if len(o.modes) != 0 {
			t.Fatalf("ota-stage sent OTA_ACTIVATE %v; it must stop at END", o.modes)
		}
		var buf bytes.Buffer
		if err := run(context.Background(), dev, "ota-activate", append(tc.args, testSig), false, &buf); err != nil {
			t.Fatal(err)
		}
		if len(o.modes) != 1 || o.modes[0] != tc.mode {
			t.Fatalf("OTA_ACTIVATE modes = %v, want [%d]", o.modes, tc.mode)
		}
		if got := strings.TrimSpace(buf.String()); got != tc.out {
			t.Fatalf("ota-activate printed %q, want %q", got, tc.out)
		}
	}
}

func TestOTAActivateBeforeAVerifiedImageIsTheDevicesRefusal(t *testing.T) {
	dev := startOTADevice(t, &otaDevice{})
	err := run(context.Background(), dev, "ota-activate", []string{testSig}, true, io.Discard)
	if !errors.Is(err, tfw.StatusErrBadState) {
		t.Fatalf("err = %v, want %v", err, tfw.StatusErrBadState)
	}
}

func TestOTAActivateArgumentErrorsAreUsageErrors(t *testing.T) {
	for _, args := range [][]string{{"later"}, {"now", "now"}} {
		err := run(context.Background(), nil, "ota-activate", args, false, io.Discard)
		if err == nil || err.Error() != "usage: ota-activate [now]" {
			t.Fatalf("%v: err = %v, want a usage error", args, err)
		}
	}
}

// openResetting opens a fresh Device on o as the CLI does on every run, over a
// transport that resets the board on open: the RAM-only session is lost.
func openResetting(t *testing.T, o *otaDevice) *tfw.Device {
	t.Helper()
	o.state, o.total, o.received = 0, 0, 0
	return startOTADevice(t, o)
}

// On a board that resets on open, a separate ota-activate run finds no staged
// session; ota-stage --activate stages and activates over the one connection.
func TestOTAStageActivateIsOneConnectionOnABoardThatResetsOnOpen(t *testing.T) {
	image := writeImage(t, patterned(10))
	o := &otaDevice{}
	if err := run(context.Background(), openResetting(t, o), "ota-stage", []string{image}, false, io.Discard); err != nil {
		t.Fatal(err)
	}
	err := run(context.Background(), openResetting(t, o), "ota-activate", []string{testSig}, false, io.Discard)
	if !errors.Is(err, tfw.StatusErrBadState) {
		t.Fatalf("separate ota-activate: err = %v, want %v", err, tfw.StatusErrBadState)
	}

	for _, tc := range []struct {
		flag string
		mode byte
	}{
		{"--activate", byte(tfw.OTAActivateAtReset)},
		{"--activate=now", byte(tfw.OTAActivateResetNow)},
	} {
		o := &otaDevice{}
		if err := run(context.Background(), openResetting(t, o), "ota-stage", []string{tc.flag, testSig, image}, false, io.Discard); err != nil {
			t.Fatalf("%s: %v", tc.flag, err)
		}
		if len(o.modes) != 1 || o.modes[0] != tc.mode {
			t.Fatalf("%s: OTA_ACTIVATE modes %v, want [%d]", tc.flag, o.modes, tc.mode)
		}
	}
	err = run(context.Background(), nil, "ota-stage", []string{"--activate=later", image}, false, io.Discard)
	if err == nil || err.Error() != "usage: ota-stage [--activate[=now]] <image>" {
		t.Fatalf("err = %v, want a usage error", err)
	}
}

// The board resets once the reset-now reply drains, so ota-stage
// --activate=now sends nothing after OTA_ACTIVATE; --activate may still ask
// for the status.
func TestOTAStageActivateNowSendsNothingAfterTheReply(t *testing.T) {
	image := writeImage(t, patterned(10))
	for _, tc := range []struct {
		flag  string
		after int
	}{
		{"--activate=now", 0},
		{"--activate", 1},
	} {
		o := &otaDevice{}
		var out bytes.Buffer
		if err := run(context.Background(), openResetting(t, o), "ota-stage", []string{tc.flag, testSig, image}, true, &out); err != nil {
			t.Fatalf("%s: %v", tc.flag, err)
		}
		last := -1
		for i, c := range o.cmds {
			if c == tfw.CmdOTAActivate {
				last = i
			}
		}
		if last < 0 || len(o.cmds)-last-1 != tc.after {
			t.Fatalf("%s: commands %v, want %d after OTA_ACTIVATE", tc.flag, o.cmds, tc.after)
		}
		if tc.after == 0 && !bytes.Contains(out.Bytes(), []byte(`"reset_now": true`)) {
			t.Fatalf("%s: output %q, want the activation result", tc.flag, out.String())
		}
	}
}

// testSig is a well-formed authorization; the fake, like the device, does not
// verify it (tfw-boot does, at reset).
var testSig = "--sig=" + strings.Repeat("a5", tfw.OTAAuthSigLen)

// Since #137 the device refuses OTA_ACTIVATE and OTA_CONFIRM without OTA_AUTH
// just before: tfwctl refuses first, sending nothing, and with the
// authorization sends OTA_AUTH immediately before the command.
func TestOTAActivateAndConfirmSendTheAuthorizationFirst(t *testing.T) {
	ctx := context.Background()
	image := writeImage(t, patterned(10))
	for _, tc := range []struct {
		cmd  string
		args []string
	}{
		{"ota-activate", nil},
		{"ota-confirm", nil},
		{"ota-stage", []string{"--activate", image}},
	} {
		o := &otaDevice{}
		err := run(ctx, startOTADevice(t, o), tc.cmd, tc.args, false, io.Discard)
		if err == nil || !strings.Contains(err.Error(), "--sig=") || len(o.cmds) != 0 {
			t.Fatalf("%s without --sig: err = %v, sent %v; want a refusal and nothing sent", tc.cmd, err, o.cmds)
		}
	}

	raw := bytes.Repeat([]byte{0x5a}, tfw.OTAAuthSigLen)
	path := filepath.Join(t.TempDir(), "auth.sig")
	if err := os.WriteFile(path, raw, 0o600); err != nil {
		t.Fatal(err)
	}
	for _, tc := range []struct {
		cmd  string
		args []string
		next tfw.Cmd
		sig  []byte
	}{
		{"ota-confirm", []string{testSig}, tfw.CmdOTAConfirm, bytes.Repeat([]byte{0xa5}, tfw.OTAAuthSigLen)},
		{"ota-confirm", []string{"--sig-file=" + path}, tfw.CmdOTAConfirm, raw},
		{"ota-stage", []string{"--activate=now", "--sig-file=" + path, image}, tfw.CmdOTAActivate, raw},
	} {
		o := &otaDevice{}
		if err := run(ctx, startOTADevice(t, o), tc.cmd, tc.args, false, io.Discard); err != nil {
			t.Fatalf("%s %v: %v", tc.cmd, tc.args, err)
		}
		n := len(o.cmds)
		if n < 2 || o.cmds[n-2] != tfw.CmdOTAAuth || o.cmds[n-1] != tc.next || len(o.sigs) != 1 || !bytes.Equal(o.sigs[0], tc.sig) {
			t.Fatalf("%s %v: sent %v with %x; want OTA_AUTH with the signature, then %v", tc.cmd, tc.args, o.cmds, o.sigs, tc.next)
		}
	}
	if err := run(ctx, nil, "ota-confirm", []string{"--sig=abcd"}, false, io.Discard); err == nil {
		t.Fatal("a short --sig: no error")
	}
}
