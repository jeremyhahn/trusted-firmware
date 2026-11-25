// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package main

import (
	"bytes"
	"context"
	"encoding/json"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
)

// emptyRecordRun runs hsm-records on a fresh engine whose A6 record holds no
// entry: every CmdHSMRecordRead is answered StatusErrRange, the end of the
// record. It returns the output and the record-read payloads sent.
func emptyRecordRun(t *testing.T, asJSON bool) (string, [][]byte) {
	t.Helper()
	var reads [][]byte
	answer := func(f tfw.Frame) (tfw.Status, []byte) {
		if f.Cmd() != tfw.CmdHSMRecordRead {
			t.Errorf("hsm-records sent command %v, not CmdHSMRecordRead", f.Cmd())
			return tfw.StatusErrBadState, nil
		}
		reads = append(reads, append([]byte(nil), f.Payload...))
		return tfw.StatusErrRange, nil
	}
	var out bytes.Buffer
	require.NoError(t, run(context.Background(), startFakeDevice(t, answer), "hsm-records", nil, asJSON, &out))
	return out.String(), reads
}

// An empty record is count 0, dropped 0 with no entry lines, and its JSON
// form carries "entries": [] (never null), as emitHSMRecord promises.
func TestHSMRecordsOverAnEmptyRecordPrintZeroTotalsAndNoEntries(t *testing.T) {
	text, reads := emptyRecordRun(t, false)
	assert.Equal(t, "count 0, dropped 0\n", text)
	assert.Equal(t, [][]byte{{0}}, reads, "one read, of index 0")

	text, reads = emptyRecordRun(t, true)
	assert.Equal(t, [][]byte{{0}}, reads, "one read, of index 0")
	var raw map[string]json.RawMessage
	require.NoError(t, json.Unmarshal([]byte(text), &raw), text)
	assert.Equal(t, "0", string(raw["count"]))
	assert.Equal(t, "0", string(raw["dropped"]))
	assert.Equal(t, "[]", string(raw["entries"]), "entries is an empty array, not null")
	var r hsmRecordJSON
	require.NoError(t, json.Unmarshal([]byte(text), &r), text)
	assert.Equal(t, hsmRecordJSON{Entries: []hsmEntryJSON{}}, r)
}
