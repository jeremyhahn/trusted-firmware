// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package main

import (
	"bytes"
	"context"
	"encoding/binary"
	"encoding/json"
	"errors"
	"io"
	"os"
	"path/filepath"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
)

// hsmBench answers the HSM channel as an engine holding a set (flags 0x03,
// policy 2 selected, 7 refused) and a record of two entries, and keeps the
// payload of each command it was sent.
type hsmBench struct {
	sent map[tfw.Cmd][]byte
}

func (b *hsmBench) answer(f tfw.Frame) (tfw.Status, []byte) {
	b.sent[f.Cmd()] = append(b.sent[f.Cmd()], f.Payload...)
	if f.Cmd() == tfw.CmdHSMRecordRead {
		if len(f.Payload) != 1 || f.Payload[0] >= 2 {
			return tfw.StatusErrRange, nil
		}
		e := []byte{2, 1, 0, 0, 0, 0, 0, tfw.HSMRecordSignal + f.Payload[0], 4, 0}
		binary.LittleEndian.PutUint32(e[3:7], 1000+uint32(f.Payload[0]))
		return tfw.StatusOK, e
	}
	return tfw.StatusOK, []byte{0x03, 2, 7, 0, 0, 0}
}

func hsmRun(t *testing.T, asJSON bool, verb string, args ...string) (string, *hsmBench) {
	t.Helper()
	b := &hsmBench{sent: map[tfw.Cmd][]byte{}}
	var out bytes.Buffer
	require.NoError(t, run(context.Background(), startFakeDevice(t, b.answer), verb, args, asJSON, &out), "%s %v", verb, args)
	return out.String(), b
}

func TestHSMStateVerbsSendTheirArgumentAndPrintTheState(t *testing.T) {
	env := filepath.Join(t.TempDir(), "envelope.cbor")
	require.NoError(t, os.WriteFile(env, []byte{0xD8, 0x6B}, 0o600))
	for _, tc := range []struct {
		verb string
		args []string
		cmd  tfw.Cmd
		sent []byte
	}{
		{"hsm-heartbeat", nil, tfw.CmdHSMHeartbeat, nil},
		{"hsm-report", []string{"ima"}, tfw.CmdHSMReport, []byte{tfw.HSMSignalIMAViolation}},
		{"hsm-report", []string{"hotplug"}, tfw.CmdHSMReport, []byte{tfw.HSMSignalHotplug}},
		{"hsm-report", []string{"0x05"}, tfw.CmdHSMReport, []byte{5}},
		{"hsm-verdict", []string{"clear"}, tfw.CmdHSMVerdict, []byte{tfw.HSMVerdictClear}},
		{"hsm-verdict", []string{"compromised"}, tfw.CmdHSMVerdict, []byte{tfw.HSMVerdictCompromised}},
		{"hsm-verdict", []string{"1"}, tfw.CmdHSMVerdict, []byte{1}},
		{"hsm-select", []string{"3"}, tfw.CmdHSMSelect, []byte{3}},
		{"hsm-load", []string{env}, tfw.CmdHSMLoad, nil},
	} {
		t.Run(tc.verb, func(t *testing.T) {
			text, b := hsmRun(t, false, tc.verb, tc.args...)
			assert.Equal(t, "flags    0x03 (set held, locked)\nselected 2\nrefused  7\n", text)
			if tc.cmd == tfw.CmdHSMLoad {
				// The chunk header is the SDK's; the envelope is its tail.
				assert.Equal(t, []byte{0xD8, 0x6B}, b.sent[tc.cmd][len(b.sent[tc.cmd])-2:])
			} else {
				assert.Equal(t, tc.sent, b.sent[tc.cmd])
			}

			out, _ := hsmRun(t, true, tc.verb, tc.args...)
			var st hsmStateJSON
			require.NoError(t, json.Unmarshal([]byte(out), &st), "%q", out)
			assert.Equal(t, hsmStateJSON{Flags: 3, SetHeld: true, Locked: true, Selected: 2, Refused: 7}, st)
		})
	}
}

func TestHSMStateTextNamesNoFlagsWhenThereAreNone(t *testing.T) {
	var out bytes.Buffer
	require.NoError(t, emitHSMState(&out, false, &tfw.HSMState{Flags: tfw.HSMWiped}))
	assert.Contains(t, out.String(), "flags    0x04 (wiped)\n")
	out.Reset()
	require.NoError(t, emitHSMState(&out, false, &tfw.HSMState{}))
	assert.Contains(t, out.String(), "flags    0x00 (none)\n")
}

func TestHSMRecordVerbsPrintTheEntries(t *testing.T) {
	text, b := hsmRun(t, false, "hsm-record", "1")
	assert.Equal(t, "count 2, dropped 1\n  1  at 1001  decision 4  result 0\n", text)
	assert.Equal(t, []byte{1}, b.sent[tfw.CmdHSMRecordRead])

	text, _ = hsmRun(t, false, "hsm-records")
	assert.Equal(t, "count 2, dropped 1\n  0  at 1000  signal 4  result 0\n  1  at 1001  decision 4  result 0\n", text)

	out, _ := hsmRun(t, true, "hsm-records")
	var r hsmRecordJSON
	require.NoError(t, json.Unmarshal([]byte(out), &r), "%q", out)
	assert.Equal(t, hsmRecordJSON{Count: 2, Dropped: 1, Entries: []hsmEntryJSON{
		{Index: 0, At: 1000, Kind: "signal", What: 4},
		{Index: 1, At: 1001, Kind: "decision", What: 4},
	}}, r)
}

func TestHSMKindNamesEveryKindAndNumbersTheRest(t *testing.T) {
	assert.Equal(t, "action", hsmKind(tfw.HSMRecordAction))
	assert.Equal(t, "9", hsmKind(9))
}

func TestHSMVerbsReturnTheDevicesRefusal(t *testing.T) {
	refuse := func(tfw.Frame) (tfw.Status, []byte) { return tfw.StatusErrBadState, nil }
	for _, args := range [][]string{
		{"hsm-heartbeat"}, {"hsm-select", "0"}, {"hsm-record", "0"}, {"hsm-records"},
	} {
		var out bytes.Buffer
		err := run(context.Background(), startFakeDevice(t, refuse), args[0], args[1:], false, &out)
		assert.True(t, errors.Is(err, tfw.StatusErrBadState), "%v: %v", args, err)
		assert.Empty(t, out.String(), "%v printed after a refusal", args)
	}
}

func TestHSMVerbsReportALinkFailure(t *testing.T) {
	dev := tfw.New(deadConn{}, tfw.WithBootWait(0))
	for _, args := range [][]string{
		{"hsm-heartbeat"}, {"hsm-report", "ima"}, {"hsm-verdict", "clear"}, {"hsm-select", "0"},
		{"hsm-record", "0"}, {"hsm-records"},
	} {
		err := run(context.Background(), dev, args[0], args[1:], false, io.Discard)
		var le *tfw.LinkError
		assert.True(t, errors.As(err, &le), "%v over a dead link: %v, want a LinkError", args, err)
	}
}

func TestHSMMalformedArgumentsAreRefusedBeforeTheWire(t *testing.T) {
	for _, args := range [][]string{
		{"hsm-heartbeat", "x"},
		{"hsm-report"}, {"hsm-report", "loud"}, {"hsm-report", "9"},
		{"hsm-verdict"}, {"hsm-verdict", "maybe"}, {"hsm-verdict", "2"},
		{"hsm-select"}, {"hsm-select", "256"},
		{"hsm-load"}, {"hsm-load", filepath.Join(t.TempDir(), "absent")},
		{"hsm-record"}, {"hsm-record", "x"},
		{"hsm-records", "0"},
	} {
		if err := run(context.Background(), nil, args[0], args[1:], false, io.Discard); err == nil {
			t.Fatalf("%v was accepted", args)
		}
	}
	assert.Error(t, runHSM(context.Background(), nil, "hsm-nothing", nil, false, io.Discard))
}
