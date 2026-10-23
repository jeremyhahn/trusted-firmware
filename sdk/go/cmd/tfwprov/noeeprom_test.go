package main

import (
	"bufio"
	"bytes"
	"context"
	"os"
	"regexp"
	"strconv"
	"strings"
	"testing"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
)

// Row device-provisioning, R5: the element tool cannot undo the serial's
// step, because no frame tfwprov sends is an EEPROM write. The serial lives in
// the platform block, and the only command that writes EEPROM over the wire
// is one of these. The ids come from the firmware's command table, so a new
// EEPROM command is held to this the day it is added.

const (
	commandTable = "../../../../src/core/commands.def"
	protocolH    = "../../../../include/tfw/protocol.h"
)

var (
	tableIf    = regexp.MustCompile(`^#if\s+(\w+)`)
	tableEntry = regexp.MustCompile(`^TFW_CMD_ENTRY\(\s*(TFW_CMD_\w+)\s*,`)
	cmdDefine  = regexp.MustCompile(`^#define\s+(TFW_CMD_\w+)\s+0x([0-9A-Fa-f]+)u?\b`)
)

// eepromWrites reads the command table and returns every command the table
// gates on TFW_FEATURE_EEPROM that is not a read, by id from protocol.h.
func eepromWrites(t *testing.T) map[tfw.Cmd]string {
	t.Helper()
	ids := map[string]tfw.Cmd{}
	scanLines(t, protocolH, func(line string) {
		if m := cmdDefine.FindStringSubmatch(line); m != nil {
			v, err := strconv.ParseUint(m[2], 16, 8)
			if err != nil {
				t.Fatalf("%s: %s: %v", protocolH, m[1], err)
			}
			ids[m[1]] = tfw.Cmd(v)
		}
	})

	writes := map[tfw.Cmd]string{}
	var gates []string
	scanLines(t, commandTable, func(line string) {
		switch {
		case strings.HasPrefix(line, "#if"):
			gate := ""
			if m := tableIf.FindStringSubmatch(line); m != nil {
				gate = m[1]
			}
			gates = append(gates, gate)
		case strings.HasPrefix(line, "#endif"):
			if len(gates) == 0 {
				t.Fatalf("%s: #endif without #if", commandTable)
			}
			gates = gates[:len(gates)-1]
		}
		m := tableEntry.FindStringSubmatch(line)
		if m == nil || len(gates) == 0 || gates[len(gates)-1] != "TFW_FEATURE_EEPROM" {
			return
		}
		if strings.HasSuffix(m[1], "_READ") {
			return
		}
		id, ok := ids[m[1]]
		if !ok {
			t.Fatalf("%s names %s, which %s does not define", commandTable, m[1], protocolH)
		}
		writes[id] = m[1]
	})
	// The table read nothing, or read it wrong, if the SDK's own EEPROM write
	// is not among what it found.
	if _, ok := writes[tfw.CmdEEPROMWrite]; !ok {
		t.Fatalf("%s: no EEPROM write found under TFW_FEATURE_EEPROM (want 0x%02x among %v)",
			commandTable, uint8(tfw.CmdEEPROMWrite), writes)
	}
	return writes
}

func scanLines(t *testing.T, path string, fn func(string)) {
	t.Helper()
	b, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	s := bufio.NewScanner(bytes.NewReader(b))
	for s.Scan() {
		fn(strings.TrimSpace(s.Text()))
	}
	if err := s.Err(); err != nil {
		t.Fatalf("%s: %v", path, err)
	}
}

// requireNoEEPROMWrite fails on every frame in seen whose id is an EEPROM
// write.
func requireNoEEPROMWrite(t *testing.T, name string, writes map[tfw.Cmd]string, seen []tfw.Cmd) {
	t.Helper()
	for i, c := range seen {
		if w, ok := writes[c]; ok {
			t.Errorf("%s: frame %d is %s (0x%02x); tfwprov must never write EEPROM", name, i, w, uint8(c))
		}
	}
}

func TestTfwprovNeverSendsAnEEPROMWrite(t *testing.T) {
	writes := eepromWrites(t)
	pub := bytes.Repeat([]byte{0x11}, 64)
	blank := [2]byte{0x55, 0x55}
	configLocked := [2]byte{0x55, 0x00}
	locked := [2]byte{0x00, 0x00}

	type run struct {
		name string
		mode string
		e    *element
	}
	runs := []run{
		{"dry-run blank", "--dry-run", &element{pub: pub, lock: blank}},
		{"dry-run locked", "--dry-run", &element{pub: pub, lock: locked}},
		{"verify blank", "--verify", &element{pub: pub, lock: blank}},
		{"verify locked", "--verify", &element{pub: pub, lock: locked}},
		{"commit blank", "--commit", &element{pub: pub, lock: blank}},
		{"commit config locked", "--commit", &element{pub: pub, lock: configLocked}},
		{"commit locked", "--commit", &element{pub: pub, lock: locked}},
		{"no element", "--verify", &element{refuseCmd: tfw.CmdProvInfo, refuseNth: 1, refuseStatus: tfw.StatusErrNACK}},
	}
	// Every step of --commit refused in turn: the paths that stop half way.
	for _, step := range []struct {
		cmd tfw.Cmd
		nth int
	}{
		{tfw.CmdProvWriteConfig, 1}, {tfw.CmdProvWriteConfig, 2}, {tfw.CmdProvLock, 1},
		{tfw.CmdProvGenKey, 1}, {tfw.CmdProvSeedUDS, 1}, {tfw.CmdProvLock, 2},
	} {
		runs = append(runs, run{
			"commit refused at 0x" + strconv.FormatUint(uint64(step.cmd), 16) + " #" + strconv.Itoa(step.nth), "--commit",
			&element{pub: pub, lock: blank, refuseCmd: step.cmd, refuseNth: step.nth, refuseStatus: tfw.StatusErrNACK},
		})
	}

	for _, r := range runs {
		// A run may fail: the refusals are the point of some. What it sent
		// is all this test reads.
		_ = provision(context.Background(), startElement(t, r.e), testTemplate(), r.mode, "")
		seen := r.e.commands()
		if len(seen) == 0 || seen[0] != tfw.CmdProvInfo {
			t.Errorf("%s: frames %v, want ProvInfo first", r.name, seen)
		}
		requireNoEEPROMWrite(t, r.name, writes, seen)
	}
}

// The check above passes only if it would see a write: the same recorder,
// sent an EEPROM write, must report it.
func TestTheEEPROMWriteCheckSeesAWrite(t *testing.T) {
	writes := eepromWrites(t)
	e := &element{}
	_ = startElement(t, e).EEPROMWrite(context.Background(), tfw.EEPROMReservedEnd, []byte{1})
	seen := e.commands()
	found := false
	for _, c := range seen {
		if _, ok := writes[c]; ok {
			found = true
		}
	}
	if !found {
		t.Fatalf("recorded %v; the check would not have caught an EEPROM write", seen)
	}
}
