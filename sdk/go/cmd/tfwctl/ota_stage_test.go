package main

import (
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

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
}

func (o *otaDevice) answer(f tfw.Frame) (tfw.Status, []byte) {
	cmd := f.Cmd()
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
	case tfw.CmdOTAStatus:
		return tfw.StatusOK, binary.LittleEndian.AppendUint32([]byte{byte(o.state)}, o.received)
	default:
		return tfw.StatusErrUnknownCmd, nil
	}
	return tfw.StatusOK, nil
}

// startOTADevice wires an otaDevice to a tfw.Device over two pipes.
func startOTADevice(t *testing.T, o *otaDevice) *tfw.Device {
	t.Helper()
	reqR, reqW := io.Pipe()
	repR, repW := io.Pipe()
	go func() {
		var buf []byte
		chunk := make([]byte, 512)
		for {
			n, err := reqR.Read(chunk)
			if err != nil {
				return
			}
			buf = append(buf, chunk[:n]...)
			for {
				f, used, err := tfw.Decode(buf)
				if errors.Is(err, tfw.ErrShortFrame) {
					break
				}
				if err != nil {
					t.Errorf("fake device: undecodable request: %v", err)
					return
				}
				buf = buf[used:]
				st, payload := o.answer(f)
				reply, err := tfw.EncodeResponse(f.Seq, f.Cmd(), st, payload)
				if err != nil {
					t.Errorf("fake device: encoding the reply: %v", err)
					return
				}
				if _, err := repW.Write(reply); err != nil {
					return
				}
			}
		}
	}()
	t.Cleanup(func() {
		_ = reqW.Close()
		_ = repW.Close()
	})
	conn := struct {
		io.Reader
		io.Writer
	}{repR, reqW}
	return tfw.New(conn, tfw.WithTimeout(5*time.Second), tfw.WithBootWait(0))
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
			if err == nil || !strings.HasPrefix(err.Error(), "usage: ota-stage <image>") {
				t.Fatalf("err = %v, want a usage error", err)
			}
		})
	}
}

// simImage is an image the firmware's stager accepts: whole pages, the body,
// then the v1 trailer naming the body's length and SHA-256.
func simImage(total, body int) []byte {
	img := bytes.Repeat([]byte{0xFF}, total)
	copy(img, patterned(body))
	tr := img[total-128:]
	clear(tr)
	copy(tr, "TFIM")
	binary.LittleEndian.PutUint16(tr[4:], 1)
	tr[6] = 0x01
	binary.LittleEndian.PutUint32(tr[8:], 3)
	binary.LittleEndian.PutUint32(tr[12:], uint32(body))
	sum := sha256.Sum256(img[:body])
	copy(tr[16:], sum[:])
	return img
}

// Against the firmware: tfwsim links the application stager and a store
// (mk/host.mk, TFWSIM_STAGE_SRCS), so the verb stages a real image and the
// device's own gate refuses a tampered one at END.
func TestOTAStageAgainstFirmware(t *testing.T) {
	dev := startSim(t)
	ctx := context.Background()

	var buf bytes.Buffer
	if err := run(ctx, dev, "ota-stage", []string{writeImage(t, simImage(1024, 700))}, false, &buf); err != nil {
		t.Fatalf("ota-stage: %v", err)
	}
	if got := strings.TrimSpace(buf.String()); got != tfw.OTAComplete.String()+", 1024 bytes received" {
		t.Fatalf("ota-stage printed %q", got)
	}
	expectSimStatus(t, dev, tfw.OTAComplete.String()+", 1024 bytes received")

	img := simImage(1024, 700)
	img[100] ^= 0x40
	err := run(ctx, dev, "ota-stage", []string{writeImage(t, img)}, false, io.Discard)
	if !errors.Is(err, tfw.StatusErrCrypto) {
		t.Fatalf("tampered image: %v, want %v", err, tfw.StatusErrCrypto)
	}
	// The stager's fail() (src/core/ota_stage.c) sets FAILED and leaves the
	// count alone; only OTA_BEGIN zeroes it.
	expectSimStatus(t, dev, tfw.OTAFailed.String()+", 1024 bytes received")
}

func expectSimStatus(t *testing.T, dev *tfw.Device, want string) {
	t.Helper()
	var buf bytes.Buffer
	if err := run(context.Background(), dev, "ota-status", nil, false, &buf); err != nil {
		t.Fatalf("ota-status: %v", err)
	}
	if got := strings.TrimSpace(buf.String()); got != want {
		t.Fatalf("ota-status printed %q, want %q", got, want)
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
