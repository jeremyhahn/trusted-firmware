// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package main

import (
	"bytes"
	"context"
	"encoding/binary"
	"encoding/json"
	"errors"
	"strings"
	"testing"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
)

// The example block: documentation addresses (RFC 5737) and a locally
// administered MAC.
var netcfgSetArgs = []string{
	"netcfg", "set", "--mac", "02:00:00:00:00:01", "--ip", "192.0.2.10",
	"--mask", "255.255.255.0", "--gateway", "192.0.2.1", "--endpoint", "198.51.100.20:7020",
}

// eepromDevice answers EEPROM_WRITE and EEPROM_READ against one EEPROM, so a
// netcfg get after a netcfg set on the same device reads what was written.
type eepromDevice struct{ mem [1024]byte }

func (e *eepromDevice) answer(f tfw.Frame) (tfw.Status, []byte) {
	p := f.Payload
	addr := int(binary.LittleEndian.Uint16(p))
	switch f.Cmd() {
	case tfw.CmdEEPROMWrite:
		copy(e.mem[addr:], p[2:])
		return tfw.StatusOK, nil
	case tfw.CmdEEPROMRead:
		return tfw.StatusOK, e.mem[addr : addr+int(p[2])]
	}
	return tfw.StatusErrUnknownCmd, nil
}

func runNetcfgVerb(t *testing.T, e *eepromDevice, asJSON bool, args ...string) (string, error) {
	t.Helper()
	dev := startFakeDevice(t, e.answer)
	var out bytes.Buffer
	err := run(context.Background(), dev, args[0], args[1:], asJSON, &out)
	return out.String(), err
}

func TestNetcfgSetThenGetPrintsTheWrittenBlock(t *testing.T) {
	e := &eepromDevice{}
	got, err := runNetcfgVerb(t, e, false, netcfgSetArgs...)
	if err != nil {
		t.Fatalf("netcfg set: %v", err)
	}
	if !strings.Contains(got, "wrote the network block") || !strings.Contains(got, "endpoint 198.51.100.20:7020") {
		t.Fatalf("netcfg set printed %q", got)
	}
	got, err = runNetcfgVerb(t, e, false, "netcfg", "get")
	if err != nil {
		t.Fatalf("netcfg get: %v", err)
	}
	for _, want := range []string{
		"mac      02:00:00:00:00:01", "ip       192.0.2.10", "mask     255.255.255.0",
		"gateway  192.0.2.1", "endpoint 198.51.100.20:7020",
	} {
		if !strings.Contains(got, want) {
			t.Fatalf("netcfg get printed %q, want %q in it", got, want)
		}
	}
}

func TestNetcfgPrintsJSON(t *testing.T) {
	e := &eepromDevice{}
	for _, args := range [][]string{netcfgSetArgs, {"netcfg", "get"}} {
		got, err := runNetcfgVerb(t, e, true, args...)
		if err != nil {
			t.Fatalf("%v: %v", args, err)
		}
		var v map[string]string
		if err := json.Unmarshal([]byte(got), &v); err != nil {
			t.Fatalf("%v printed %q, want JSON: %v", args, got, err)
		}
		if v["endpoint"] != "198.51.100.20:7020" || v["mac"] != "02:00:00:00:00:01" {
			t.Fatalf("%v printed %v", args, v)
		}
	}
}

// Each refusal is the SDK's: the error printed is a *tfw.NetConfigError and
// nothing reaches the device.
func TestNetcfgSetPrintsTheSDKsRefusal(t *testing.T) {
	with := func(flag, value string) []string {
		args := append([]string(nil), netcfgSetArgs...)
		for i := range args {
			if args[i] == flag {
				args[i+1] = value
			}
		}
		return args
	}
	without := func(flag string) []string {
		var args []string
		for i := 0; i < len(netcfgSetArgs); i++ {
			if netcfgSetArgs[i] == flag {
				i++
				continue
			}
			args = append(args, netcfgSetArgs[i])
		}
		return args
	}
	for name, tc := range map[string]struct {
		args  []string
		field string
	}{
		"no mac":        {without("--mac"), "MAC"},
		"multicast mac": {with("--mac", "03:00:00:00:00:01"), "MAC"},
		"long mac":      {with("--mac", "02:00:00:00:00:00:00:01"), "MAC"},
		"no ip":         {without("--ip"), "IP"},
		"ipv6 ip":       {with("--ip", "2001:db8::10"), "IP"},
		"no mask":       {without("--mask"), "Mask"},
		"no gateway":    {without("--gateway"), "Gateway"},
		"no endpoint":   {without("--endpoint"), "Endpoint"},
		"zero endpoint": {with("--endpoint", "0.0.0.0:7020"), "Endpoint"},
		"zero port":     {with("--endpoint", "198.51.100.20:0"), "Endpoint"},
	} {
		e := &eepromDevice{}
		_, err := runNetcfgVerb(t, e, false, tc.args...)
		var nerr *tfw.NetConfigError
		if !errors.As(err, &nerr) || nerr.Field != tc.field {
			t.Errorf("%s: err = %v, want the SDK's %s refusal", name, err, tc.field)
		}
		if e.mem != ([1024]byte{}) {
			t.Errorf("%s: the refused block reached the EEPROM", name)
		}
	}
}

func TestNetcfgGetPrintsTheRefusalOfABlankBlock(t *testing.T) {
	var nerr *tfw.NetConfigError
	if _, err := runNetcfgVerb(t, &eepromDevice{}, false, "netcfg", "get"); !errors.As(err, &nerr) {
		t.Fatalf("netcfg get on a blank EEPROM: err = %v, want a NetConfigError", err)
	}
}

func TestNetcfgRefusesWhatItCannotParse(t *testing.T) {
	for _, args := range [][]string{
		{"netcfg"},
		{"netcfg", "show"},
		{"netcfg", "get", "extra"},
		{"netcfg", "set", "--port", "1"},
		{"netcfg", "set", "stray"},
		{"netcfg", "set", "--mac", "not-a-mac"},
		{"netcfg", "set", "--ip", "192.0.2"},
		{"netcfg", "set", "--mask", "mask"},
		{"netcfg", "set", "--gateway", "gw"},
		{"netcfg", "set", "--endpoint", "198.51.100.20"},
	} {
		if _, err := runNetcfgVerb(t, &eepromDevice{}, false, args...); err == nil {
			t.Errorf("%v: accepted", args)
		}
	}
}
