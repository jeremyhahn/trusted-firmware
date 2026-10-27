package main

import (
	"bytes"
	"context"
	"errors"
	"io"
	"os"
	"strings"
	"sync"
	"testing"
	"time"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
)

// element is a fake provisioning image: it answers every PROV command, and
// refuses the nth occurrence (1-based) of refuseCmd with refuseStatus.
type element struct {
	refuseCmd    tfw.Cmd
	refuseNth    int
	refuseStatus tfw.Status
	pub          []byte
	// lock is ProvInfo's data and config lock bytes; 0x55 is unlocked.
	lock [2]byte
	// slot is the X||Y PROV_WRITE_PUBKEY writes and answers; a set
	// misread flips a bit of each answer, as a slot that did not take
	// the write would read back.
	slot    [64]byte
	misread bool

	mu   sync.Mutex
	seen []tfw.Cmd
}

func (e *element) answer(f tfw.Frame) (tfw.Status, []byte) {
	e.mu.Lock()
	defer e.mu.Unlock()
	cmd := f.Cmd()
	e.seen = append(e.seen, cmd)
	if cmd == e.refuseCmd {
		n := 0
		for _, c := range e.seen {
			if c == cmd {
				n++
			}
		}
		if n == e.refuseNth {
			return e.refuseStatus, nil
		}
	}
	switch cmd {
	case tfw.CmdProvInfo:
		return tfw.StatusOK, append(make([]byte, 4+32), e.lock[:]...)
	case tfw.CmdProvGenKey, tfw.CmdProvPubKey:
		return tfw.StatusOK, e.pub
	case tfw.CmdProvWriteConfig, tfw.CmdProvLock, tfw.CmdProvSeedUDS:
		return tfw.StatusOK, nil
	case tfw.CmdProvWritePubKey:
		copy(e.slot[int(f.Payload[1])*32:], f.Payload[2:])
		back := append([]byte(nil), e.slot[:]...)
		if e.misread {
			back[63] ^= 1
		}
		return tfw.StatusOK, back
	}
	return tfw.StatusErrUnknownCmd, nil
}

func (e *element) commands() []tfw.Cmd {
	e.mu.Lock()
	defer e.mu.Unlock()
	return append([]tfw.Cmd(nil), e.seen...)
}

func startElement(t *testing.T, e *element) *tfw.Device {
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
					t.Errorf("fake element: undecodable request: %v", err)
					return
				}
				buf = buf[used:]
				st, payload := e.answer(f)
				reply, err := tfw.EncodeResponse(f.Seq, f.Cmd(), st, payload)
				if err != nil {
					t.Errorf("fake element: encoding the reply: %v", err)
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

func testTemplate() *template {
	return &template{
		Name:          "bench",
		Blocks:        map[uint8][]byte{2: make([]byte, 32), 3: make([]byte, 32)},
		ConfigLockCRC: 0x1234,
		DataLockCRC:   0xabcd,
	}
}

func TestRunCommitRunsEveryStepInOrder(t *testing.T) {
	e := &element{pub: bytes.Repeat([]byte{0x11}, 64)}
	_, err := runCommit(context.Background(), startElement(t, e), testTemplate(), &tfw.ElementInfo{})
	if err != nil {
		t.Fatal(err)
	}
	want := []tfw.Cmd{tfw.CmdProvWriteConfig, tfw.CmdProvWriteConfig, tfw.CmdProvLock,
		tfw.CmdProvGenKey, tfw.CmdProvSeedUDS, tfw.CmdProvLock}
	if got := e.commands(); !equalCmds(got, want) {
		t.Fatalf("commands = %v, want %v", got, want)
	}
}

func TestRunCommitSkipsTheConfigStepsOnALockedConfigZone(t *testing.T) {
	e := &element{pub: bytes.Repeat([]byte{0x11}, 64)}
	_, err := runCommit(context.Background(), startElement(t, e), testTemplate(), &tfw.ElementInfo{ConfigLocked: true})
	if err != nil {
		t.Fatal(err)
	}
	want := []tfw.Cmd{tfw.CmdProvGenKey, tfw.CmdProvSeedUDS, tfw.CmdProvLock}
	if got := e.commands(); !equalCmds(got, want) {
		t.Fatalf("commands = %v, want %v", got, want)
	}
}

func TestRunCommitRefusesALockedDataZone(t *testing.T) {
	e := &element{}
	_, err := runCommit(context.Background(), startElement(t, e), testTemplate(), &tfw.ElementInfo{DataLocked: true})
	if err == nil || err.Error() != "the data zone is already locked; this part cannot be reconfigured" {
		t.Fatalf("err = %v", err)
	}
	if got := e.commands(); len(got) != 0 {
		t.Fatalf("a locked part was sent %v", got)
	}
}

// A step that fails part way stops the commit there: nothing after it reaches
// the element, and the error names the step.
func TestRunCommitStopsAtTheStepThatFails(t *testing.T) {
	for _, tc := range []struct {
		name  string
		cmd   tfw.Cmd
		nth   int
		first string
		sent  int
	}{
		{"the first config block", tfw.CmdProvWriteConfig, 1, "config block 2: ", 1},
		{"the second config block", tfw.CmdProvWriteConfig, 2, "config block 3: ", 2},
		{"the config lock", tfw.CmdProvLock, 1, "lock config zone: ", 3},
		{"the key generation", tfw.CmdProvGenKey, 1, "generate DeviceID key: ", 4},
		{"the UDS seed", tfw.CmdProvSeedUDS, 1, "seed UDS: ", 5},
		{"the data lock", tfw.CmdProvLock, 2, "lock data zone: ", 6},
	} {
		t.Run(tc.name, func(t *testing.T) {
			e := &element{refuseCmd: tc.cmd, refuseNth: tc.nth, refuseStatus: tfw.StatusErrUnknownCmd,
				pub: bytes.Repeat([]byte{0x11}, 64)}
			_, err := runCommit(context.Background(), startElement(t, e), testTemplate(), &tfw.ElementInfo{})
			if err == nil || !strings.HasPrefix(err.Error(), tc.first) {
				t.Fatalf("err = %v, want prefix %q", err, tc.first)
			}
			var st tfw.Status
			if !errors.As(err, &st) || st != tfw.StatusErrUnknownCmd {
				t.Fatalf("err = %v, want the element's status wrapped", err)
			}
			if got := e.commands(); len(got) != tc.sent {
				t.Fatalf("commands = %v, want the commit to stop after %d", got, tc.sent)
			}
		})
	}
}

func TestRunVerifyNamesEveryProblem(t *testing.T) {
	for _, tc := range []struct {
		name string
		e    *element
		info tfw.ElementInfo
		want []string
	}{
		{"an unlocked, unkeyed part", &element{pub: make([]byte, 64)}, tfw.ElementInfo{},
			[]string{"config zone is not locked", "data zone is not locked", "DeviceID slot holds no key"}},
		{"an unreadable key", &element{refuseCmd: tfw.CmdProvPubKey, refuseNth: 1, refuseStatus: tfw.StatusErrUnknownCmd},
			tfw.ElementInfo{ConfigLocked: true, DataLocked: true},
			[]string{"DeviceID public key unreadable: "}},
	} {
		t.Run(tc.name, func(t *testing.T) {
			_, err := runVerify(context.Background(), startElement(t, tc.e), testTemplate(), &tc.info)
			if err == nil {
				t.Fatal("verify passed")
			}
			first, _, _ := strings.Cut(err.Error(), "\n")
			if first != "the part does not match the template:" {
				t.Fatalf("first line = %q", first)
			}
			for _, w := range tc.want {
				if !strings.Contains(err.Error(), "\n  - "+w) {
					t.Errorf("err = %q, missing %q", err, w)
				}
			}
		})
	}
}

func TestRunVerifyPassesAKeyedLockedPartWithNoBlocks(t *testing.T) {
	tm := testTemplate()
	tm.Blocks = nil
	e := &element{pub: bytes.Repeat([]byte{0x11}, 64)}
	if _, err := runVerify(context.Background(), startElement(t, e), tm, &tfw.ElementInfo{ConfigLocked: true, DataLocked: true}); err != nil {
		t.Fatal(err)
	}
}

// --verify end to end on a template loadTemplate accepts: one that writes
// config blocks, as every template must. A locked, keyed part passes, with
// the unread config blocks said in the output rather than counted as a
// mismatch (cli.md, "--verify"); an unlocked or unkeyed part fails.
func TestVerifyModePassesAMatchingPartOfALoadedTemplate(t *testing.T) {
	tmpl, err := loadTemplate(writeTemplate(t, goodTemplate+block32+"\n"))
	if err != nil {
		t.Fatal(err)
	}
	for _, tc := range []struct {
		name string
		e    *element
		pass bool
	}{
		{"a locked, keyed part", &element{pub: bytes.Repeat([]byte{0x11}, 64)}, true},
		{"an unlocked config zone", &element{pub: bytes.Repeat([]byte{0x11}, 64), lock: [2]byte{0x00, 0x55}}, false},
		{"an unlocked data zone", &element{pub: bytes.Repeat([]byte{0x11}, 64), lock: [2]byte{0x55, 0x00}}, false},
		{"an empty DeviceID slot", &element{pub: make([]byte, 64)}, false},
	} {
		t.Run(tc.name, func(t *testing.T) {
			err := provision(context.Background(), startElement(t, tc.e), tmpl, "--verify", "")
			if tc.pass && err != nil {
				t.Fatalf("--verify of a matching part: %v", err)
			}
			if !tc.pass && err == nil {
				t.Fatal("--verify of a mismatched part passed")
			}
		})
	}
}

func TestPlanAndReportPrintWithoutTouchingTheDevice(t *testing.T) {
	// Neither takes a device: they print from the ElementInfo already read.
	r, w, err := os.Pipe()
	if err != nil {
		t.Fatal(err)
	}
	stdout := os.Stdout
	os.Stdout = w
	report(&tfw.ElementInfo{ConfigLocked: true})
	os.Stdout = stdout
	_ = w.Close()
	got, err := io.ReadAll(r)
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(got), "zones     config LOCKED  data open\n") {
		t.Fatalf("report printed %q, want the zone lock states", got)
	}
	for _, info := range []tfw.ElementInfo{{}, {ConfigLocked: true, DataLocked: true}} {
		if out := planOutput(t, &info); !strings.Contains(out, "DRY RUN, nothing will be written") {
			t.Fatalf("plan printed %q, want the dry-run banner", out)
		}
	}
}

func equalCmds(a, b []tfw.Cmd) bool {
	if len(a) != len(b) {
		return false
	}
	for i := range a {
		if a[i] != b[i] {
			return false
		}
	}
	return true
}

// planOutput captures what plan prints to stdout.
func planOutput(t *testing.T, info *tfw.ElementInfo) string {
	t.Helper()
	r, w, err := os.Pipe()
	if err != nil {
		t.Fatal(err)
	}
	stdout := os.Stdout
	os.Stdout = w
	plan(testTemplate(), info)
	os.Stdout = stdout
	_ = w.Close()
	out, err := io.ReadAll(r)
	if err != nil {
		t.Fatal(err)
	}
	return string(out)
}

// plan says what runCommit will do: it skips the config steps on a locked
// config zone (steps.go:113) and refuses a locked data zone outright.
func TestPlanSaysWhatCommitWillDoOnALockedPart(t *testing.T) {
	for _, tc := range []struct {
		name      string
		info      tfw.ElementInfo
		want, not []string
	}{
		{"config locked, data open", tfw.ElementInfo{ConfigLocked: true},
			[]string{"--commit will skip the config steps"},
			[]string{"will fail", "--commit will refuse"}},
		{"both zones locked", tfw.ElementInfo{ConfigLocked: true, DataLocked: true},
			[]string{"--commit will refuse this part"},
			[]string{"will fail", "skip the config steps"}},
	} {
		t.Run(tc.name, func(t *testing.T) {
			out := planOutput(t, &tc.info)
			for _, w := range tc.want {
				if !strings.Contains(out, w) {
					t.Errorf("plan printed %q, missing %q", out, w)
				}
			}
			for _, n := range tc.not {
				if strings.Contains(out, n) {
					t.Errorf("plan printed %q, which says %q", out, n)
				}
			}
		})
	}
}
