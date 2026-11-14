// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package main

import (
	"context"
	"fmt"
	"io"
	"os"
	"strconv"
	"strings"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
)

// The HSM channel (0x76-0x7A, 0x7C; docs/wire-protocol/api.md), one verb per
// SDK method. Every verb but the record reads prints the engine's state after
// the command; a refusal is the device's status, returned as every other verb
// returns one.

// hsmStateJSON is HSMState with its flag bits named.
type hsmStateJSON struct {
	Flags    uint8  `json:"flags"`
	SetHeld  bool   `json:"set_held"`
	Locked   bool   `json:"locked"`
	Wiped    bool   `json:"wiped"`
	Selected uint8  `json:"selected"`
	Refused  uint32 `json:"refused"`
}

// hsmEntryJSON is one entry of the A6 record, with its index.
type hsmEntryJSON struct {
	Index  int    `json:"index"`
	At     uint32 `json:"at"`
	Kind   string `json:"kind"`
	What   uint8  `json:"what"`
	Result uint8  `json:"result"`
}

// hsmRecordJSON is the record's totals and the entries read.
type hsmRecordJSON struct {
	Count   uint8          `json:"count"`
	Dropped uint16         `json:"dropped"`
	Entries []hsmEntryJSON `json:"entries"`
}

// hsmByte is a named value from names, or a number.
func hsmByte(arg, what string, names map[string]byte) (byte, error) {
	if v, ok := names[arg]; ok {
		return v, nil
	}
	return parseU8(arg, what)
}

func runHSM(ctx context.Context, dev *tfw.Device, cmd string, args []string, asJSON bool, out io.Writer) error {
	var (
		st  *tfw.HSMState
		err error
	)
	switch cmd {
	case "hsm-heartbeat":
		if len(args) != 0 {
			return fmt.Errorf("usage: hsm-heartbeat")
		}
		st, err = dev.HSMHeartbeat(ctx)

	case "hsm-report":
		if len(args) != 1 {
			return fmt.Errorf("usage: hsm-report <ima|hotplug|signal>")
		}
		signal, perr := hsmByte(args[0], "signal",
			map[string]byte{"ima": tfw.HSMSignalIMAViolation, "hotplug": tfw.HSMSignalHotplug})
		if perr != nil {
			return perr
		}
		st, err = dev.HSMReport(ctx, signal)

	case "hsm-verdict":
		if len(args) != 1 {
			return fmt.Errorf("usage: hsm-verdict <clear|compromised|verdict>")
		}
		verdict, perr := hsmByte(args[0], "verdict",
			map[string]byte{"clear": tfw.HSMVerdictClear, "compromised": tfw.HSMVerdictCompromised})
		if perr != nil {
			return perr
		}
		st, err = dev.HSMVerdict(ctx, verdict)

	case "hsm-select":
		if len(args) != 1 {
			return fmt.Errorf("usage: hsm-select <index>")
		}
		index, perr := parseU8(args[0], "index")
		if perr != nil {
			return perr
		}
		st, err = dev.HSMSelect(ctx, index)

	case "hsm-load":
		if len(args) != 1 {
			return fmt.Errorf("usage: hsm-load <envelope file>")
		}
		// No parsing here: the device verifies the envelope, and its
		// verdict comes back with the last chunk.
		env, rerr := os.ReadFile(args[0])
		if rerr != nil {
			return fmt.Errorf("usage: hsm-load <envelope file>: %w", rerr)
		}
		st, err = dev.HSMLoad(ctx, env)

	case "hsm-record":
		if len(args) != 1 {
			return fmt.Errorf("usage: hsm-record <index>")
		}
		index, perr := parseU8(args[0], "index")
		if perr != nil {
			return perr
		}
		e, rerr := dev.HSMRecordRead(ctx, index)
		if rerr != nil {
			return rerr
		}
		return emitHSMRecord(out, asJSON, int(index), []tfw.HSMRecordEntry{*e})

	case "hsm-records":
		if len(args) != 0 {
			return fmt.Errorf("usage: hsm-records")
		}
		entries, rerr := dev.HSMRecordReadAll(ctx)
		if rerr != nil {
			return rerr
		}
		return emitHSMRecord(out, asJSON, 0, entries)

	default:
		return fmt.Errorf("unknown command %q", cmd)
	}
	if err != nil {
		return err
	}
	return emitHSMState(out, asJSON, st)
}

func emitHSMState(out io.Writer, asJSON bool, st *tfw.HSMState) error {
	j := hsmStateJSON{
		Flags: st.Flags, Selected: st.Selected, Refused: st.Refused,
		SetHeld: st.Flags&tfw.HSMSetHeld != 0, Locked: st.Flags&tfw.HSMLocked != 0, Wiped: st.Flags&tfw.HSMWiped != 0,
	}
	return emit(out, asJSON, j, func() string {
		var names []string
		for _, f := range []struct {
			on   bool
			name string
		}{{j.SetHeld, "set held"}, {j.Locked, "locked"}, {j.Wiped, "wiped"}} {
			if f.on {
				names = append(names, f.name)
			}
		}
		if len(names) == 0 {
			names = []string{"none"}
		}
		return fmt.Sprintf("flags    0x%02x (%s)\nselected %d\nrefused  %d\n",
			st.Flags, strings.Join(names, ", "), st.Selected, st.Refused)
	})
}

// hsmKind names an entry's kind; one the SDK does not know is its number.
func hsmKind(k uint8) string {
	switch k {
	case tfw.HSMRecordSignal:
		return "signal"
	case tfw.HSMRecordDecision:
		return "decision"
	case tfw.HSMRecordAction:
		return "action"
	}
	return strconv.Itoa(int(k))
}

// emitHSMRecord prints entries numbered from first, with the record's totals
// from the last entry read (an empty record is count 0, dropped 0).
func emitHSMRecord(out io.Writer, asJSON bool, first int, entries []tfw.HSMRecordEntry) error {
	r := hsmRecordJSON{Entries: []hsmEntryJSON{}}
	for i, e := range entries {
		r.Count, r.Dropped = e.Count, e.Dropped
		r.Entries = append(r.Entries, hsmEntryJSON{Index: first + i, At: e.At, Kind: hsmKind(e.Kind), What: e.What, Result: e.Result})
	}
	return emit(out, asJSON, r, func() string {
		var b strings.Builder
		fmt.Fprintf(&b, "count %d, dropped %d\n", r.Count, r.Dropped)
		for _, e := range r.Entries {
			fmt.Fprintf(&b, "%3d  at %d  %s %d  result %d\n", e.Index, e.At, e.Kind, e.What, e.Result)
		}
		return b.String()
	})
}
