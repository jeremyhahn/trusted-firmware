package main

import (
	"bytes"
	"context"
	"errors"
	"io"
	"os"
	"path/filepath"
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
	// config is the 128-byte config zone PROV_CONFIG_READ answers, and
	// slotLocks the [slot][crc] of every PROV_SLOT_LOCK; a slot-lock clears
	// the slot's bit in config bytes 88-89, as the element does. pub15, if
	// set, is what PROV_PUBKEY answers for slot 15.
	config    [128]byte
	slotLocks [][]byte
	pub15     []byte

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
		if cmd == tfw.CmdProvPubKey && f.Payload[0] == tfw.SlotSecureBootKey && e.pub15 != nil {
			return tfw.StatusOK, e.pub15
		}
		return tfw.StatusOK, e.pub
	case tfw.CmdProvConfigRead:
		b := int(f.Payload[0])
		return tfw.StatusOK, append([]byte(nil), e.config[b*32:(b+1)*32]...)
	case tfw.CmdProvSlotLock:
		e.slotLocks = append(e.slotLocks, append([]byte(nil), f.Payload...))
		s := f.Payload[0]
		e.config[88+int(s)/8] &^= 1 << (s % 8)
		return tfw.StatusOK, nil
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

// trustflexConfig is a shipped TFLXTLS config zone as the (a1) plan wants it:
// slots 6 and 15 slot-lockable and not yet slot-locked, slot 0 not tied to
// the Persistent Latch, slot 6 secret.
func trustflexConfig() [128]byte {
	var c [128]byte
	c[88], c[89] = 0xFF, 0xFF
	c[cfgKeyConfig+2*6] = keyConfigLockable
	c[cfgKeyConfig+2*15] = keyConfigLockable
	c[cfgSlotConfig+2*6] = slotConfigIsSecret
	return c
}

func trustflexElement() *element {
	return &element{pub: bytes.Repeat([]byte{0x11}, 64), config: trustflexConfig()}
}

func trustflexTemplate() *template {
	return &template{Name: "tflx", Plan: planTrustFLEX, ImageKey: bytes.Repeat([]byte{0x22}, 64)}
}

var lockedPart = &tfw.ElementInfo{ConfigLocked: true, DataLocked: true}

// withUDSLockCRC stands in for the slot-6 CRC the wire does not yet carry.
func withUDSLockCRC(t *testing.T, crc uint16, err error) {
	t.Helper()
	trustflexUDSLockCRC = func(context.Context, *tfw.Device) (uint16, error) { return crc, err }
	t.Cleanup(func() { trustflexUDSLockCRC = nil })
}

func countCmd(cmds []tfw.Cmd, c tfw.Cmd) int {
	n := 0
	for _, x := range cmds {
		if x == c {
			n++
		}
	}
	return n
}

func TestATCRC16MatchesTheElementsCommandPacket(t *testing.T) {
	// The Info command packet, count 7, opcode 0x30, mode 0, param 0: the
	// element's CRC over it is 03 5D, low byte first.
	if got := atCRC16([]byte{0x07, 0x30, 0x00, 0x00, 0x00}); got != 0x5D03 {
		t.Fatalf("crc = %#04x, want 0x5d03", got)
	}
}

func TestSlot15ImageIsTheElementsPublicKeyFormat(t *testing.T) {
	key := append(bytes.Repeat([]byte{0xAA}, 32), bytes.Repeat([]byte{0xBB}, 32)...)
	want := append(append(append(make([]byte, 4), key[:32]...), make([]byte, 4)...), key[32:]...)
	if got := slot15Image(key); !bytes.Equal(got, want) {
		t.Fatalf("image = %x", got)
	}
}

func TestTrustFLEXCommitRunsEveryStepInOrder(t *testing.T) {
	withUDSLockCRC(t, 0x1234, nil)
	e := trustflexElement()
	tm := trustflexTemplate()
	pub, err := runCommitTrustFLEX(context.Background(), startElement(t, e), tm, lockedPart, nil)
	if err != nil {
		t.Fatal(err)
	}
	want := []tfw.Cmd{tfw.CmdProvConfigRead, tfw.CmdProvConfigRead, tfw.CmdProvConfigRead, tfw.CmdProvConfigRead,
		tfw.CmdProvPubKey, tfw.CmdProvSeedUDS, tfw.CmdProvWritePubKey, tfw.CmdProvWritePubKey,
		tfw.CmdProvSlotLock, tfw.CmdProvSlotLock}
	if got := e.commands(); !equalCmds(got, want) {
		t.Fatalf("commands = %v, want %v", got, want)
	}
	kc := atCRC16(slot15Image(tm.ImageKey))
	if len(e.slotLocks) != 2 || !bytes.Equal(e.slotLocks[0], []byte{6, 0x34, 0x12}) ||
		!bytes.Equal(e.slotLocks[1], []byte{15, byte(kc), byte(kc >> 8)}) {
		t.Fatalf("slot locks = %x", e.slotLocks)
	}
	if !bytes.Equal(pub, e.pub) || !bytes.Equal(e.slot[:], tm.ImageKey) {
		t.Fatalf("point %x, slot 15 %x", pub, e.slot)
	}
}

func TestRunCommitDispatchesATrustFLEXTemplate(t *testing.T) {
	withUDSLockCRC(t, 0x1234, nil)
	e := trustflexElement()
	if _, err := runCommit(context.Background(), startElement(t, e), trustflexTemplate(), lockedPart); err != nil {
		t.Fatal(err)
	}
	if countCmd(e.commands(), tfw.CmdProvLock) != 0 || countCmd(e.commands(), tfw.CmdProvSlotLock) != 2 {
		t.Fatalf("commands = %v", e.commands())
	}
}

func TestTrustFLEXCommitStopsBeforeWritingAnything(t *testing.T) {
	for _, tc := range []struct {
		name  string
		info  *tfw.ElementInfo
		edit  func(c *[128]byte, tm *template)
		noCRC bool
	}{
		{"slot 6 permanent", lockedPart, func(c *[128]byte, _ *template) { c[cfgKeyConfig+12] = 0 }, false},
		{"slot 15 permanent", lockedPart, func(c *[128]byte, _ *template) { c[cfgKeyConfig+30] = 0 }, false},
		{"slot 0 latched", lockedPart, func(c *[128]byte, _ *template) { c[cfgKeyConfig+1] = keyConfigPersistentDisable >> 8 }, false},
		{"slot 6 slot-locked", lockedPart, func(c *[128]byte, _ *template) { c[88] &^= 1 << 6 }, false},
		{"slot 15 slot-locked", lockedPart, func(c *[128]byte, _ *template) { c[89] &^= 1 << 7 }, false},
		{"zones not locked", &tfw.ElementInfo{}, func(*[128]byte, *template) {}, false},
		{"no image key", lockedPart, func(_ *[128]byte, tm *template) { tm.ImageKey = nil }, false},
		{"no slot-6 CRC", lockedPart, func(*[128]byte, *template) {}, true},
	} {
		t.Run(tc.name, func(t *testing.T) {
			if !tc.noCRC {
				withUDSLockCRC(t, 0x1234, nil)
			}
			e := trustflexElement()
			tm := trustflexTemplate()
			tc.edit(&e.config, tm)
			if _, err := runCommitTrustFLEX(context.Background(), startElement(t, e), tm, tc.info, nil); err == nil {
				t.Fatal("committed")
			}
			for _, c := range e.commands() {
				if c != tfw.CmdProvConfigRead {
					t.Fatalf("commands = %v, want nothing but config reads", e.commands())
				}
			}
		})
	}
}

func TestTrustFLEXCommitStopsAtTheStepThatFails(t *testing.T) {
	for _, tc := range []struct {
		name string
		cmd  tfw.Cmd
		nth  int
		sent int
	}{
		{"config read", tfw.CmdProvConfigRead, 1, 1},
		{"DeviceID point", tfw.CmdProvPubKey, 1, 5},
		{"UDS seed", tfw.CmdProvSeedUDS, 1, 6},
		{"image key", tfw.CmdProvWritePubKey, 1, 7},
		{"slot-6 lock", tfw.CmdProvSlotLock, 1, 9},
		{"slot-15 lock", tfw.CmdProvSlotLock, 2, 10},
	} {
		t.Run(tc.name, func(t *testing.T) {
			withUDSLockCRC(t, 0x1234, nil)
			e := trustflexElement()
			e.refuseCmd, e.refuseNth, e.refuseStatus = tc.cmd, tc.nth, tfw.StatusErrUnknownCmd
			_, err := runCommitTrustFLEX(context.Background(), startElement(t, e), trustflexTemplate(), lockedPart, nil)
			var st tfw.Status
			if !errors.As(err, &st) || st != tfw.StatusErrUnknownCmd {
				t.Fatalf("err = %v, want the element's status wrapped", err)
			}
			if got := e.commands(); len(got) != tc.sent {
				t.Fatalf("commands = %v, want the commit to stop after %d", got, tc.sent)
			}
		})
	}
	t.Run("slot-6 CRC", func(t *testing.T) {
		withUDSLockCRC(t, 0, errors.New("no CRC"))
		e := trustflexElement()
		if _, err := runCommitTrustFLEX(context.Background(), startElement(t, e), trustflexTemplate(), lockedPart, nil); err == nil {
			t.Fatal("committed")
		}
		if countCmd(e.commands(), tfw.CmdProvSlotLock) != 0 {
			t.Fatalf("commands = %v, want no slot lock", e.commands())
		}
	})
	t.Run("record", func(t *testing.T) {
		withUDSLockCRC(t, 0x1234, nil)
		e := trustflexElement()
		refuse := func([]byte, []byte) error { return errors.New("another device's record") }
		if _, err := runCommitTrustFLEX(context.Background(), startElement(t, e), trustflexTemplate(), lockedPart, refuse); err == nil {
			t.Fatal("committed")
		}
		if got := e.commands(); len(got) != 5 {
			t.Fatalf("commands = %v, want nothing written after a refused record", got)
		}
	})
}

func TestTrustFLEXVerifyPassesAProvisionedPart(t *testing.T) {
	e := trustflexElement()
	e.config[88] &^= 1 << 6
	e.config[89] &^= 1 << 7
	tm := trustflexTemplate()
	e.pub15 = tm.ImageKey
	pub, err := runVerify(context.Background(), startElement(t, e), tm, lockedPart)
	if err != nil || !bytes.Equal(pub, e.pub) {
		t.Fatalf("verify = %x, %v", pub, err)
	}
	for _, c := range e.commands() {
		if c != tfw.CmdProvConfigRead && c != tfw.CmdProvPubKey {
			t.Fatalf("verify sent %v", c)
		}
	}
}

func TestTrustFLEXVerifyNamesEveryProblem(t *testing.T) {
	e := trustflexElement()
	e.config[cfgSlotConfig+12] = 0
	e.pub = make([]byte, 64)
	e.pub15 = bytes.Repeat([]byte{0x33}, 64)
	_, err := runVerify(context.Background(), startElement(t, e), trustflexTemplate(), &tfw.ElementInfo{})
	var ve *verifyError
	if !errors.As(err, &ve) {
		t.Fatalf("err = %v", err)
	}
	for _, want := range []string{"not both locked", "slot 6 is readable", "slot 6 is not slot-locked",
		"slot 15 is not slot-locked", "DeviceID slot holds no key", "not the --image-key"} {
		found := false
		for _, p := range ve.problems {
			found = found || strings.Contains(p, want)
		}
		if !found {
			t.Errorf("problems %q do not name %q", ve.problems, want)
		}
	}
	if len(ve.problems) != 6 {
		t.Errorf("problems = %q, want 6", ve.problems)
	}
}

func TestTrustFLEXPlanPrintsTheSevenSteps(t *testing.T) {
	r, w, err := os.Pipe()
	if err != nil {
		t.Fatal(err)
	}
	stdout := os.Stdout
	os.Stdout = w
	plan(trustflexTemplate(), lockedPart)
	os.Stdout = stdout
	_ = w.Close()
	out, err := io.ReadAll(r)
	if err != nil {
		t.Fatal(err)
	}
	for _, want := range []string{"  1. ", "  2. ", "  3. ", "  4. ", "  5. ", "  6. slot-lock slots 6 and 15   IRREVERSIBLE", "  7. "} {
		if !strings.Contains(string(out), want) {
			t.Errorf("plan lacks %q:\n%s", want, out)
		}
	}
	if strings.Contains(string(out), "lock config zone") || strings.Contains(string(out), "NOTE") {
		t.Errorf("plan prints a blank-part step or a note on a locked part:\n%s", out)
	}
}

func TestApplyImageKeyBelongsToATrustFLEXCommitOrVerify(t *testing.T) {
	keyPath := filepath.Join(t.TempDir(), "image.pub")
	key := mustHex(t, p256GeneratorHex)
	if err := os.WriteFile(keyPath, key, 0o600); err != nil {
		t.Fatal(err)
	}
	if err := applyImageKey(trustflexTemplate(), "", true, false); err == nil {
		t.Error("a trustflex --commit without --image-key was accepted")
	}
	if err := applyImageKey(testTemplate(), keyPath, true, false); err == nil {
		t.Error("--image-key with a blank-part template was accepted")
	}
	if err := applyImageKey(trustflexTemplate(), keyPath, false, true); err == nil {
		t.Error("--image-key with --dry-run was accepted")
	}
	tm := trustflexTemplate()
	if err := applyImageKey(tm, keyPath, true, false); err != nil || !bytes.Equal(tm.ImageKey, key) {
		t.Fatalf("applyImageKey = %v, key %x", err, tm.ImageKey)
	}
	if err := applyImageKey(testTemplate(), "", true, false); err != nil {
		t.Errorf("a blank-part --commit: %v", err)
	}
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
