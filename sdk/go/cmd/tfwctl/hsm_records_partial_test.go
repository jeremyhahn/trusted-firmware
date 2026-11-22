// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package main

import (
	"bytes"
	"context"
	"encoding/binary"
	"encoding/json"
	"errors"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
)

// A refusal partway through the record keeps the entries read before it:
// the SDK hands them back with the error, and hsm-records prints them before
// it returns the refusal.
func TestHSMRecordsPrintsTheEntriesReadBeforeARefusal(t *testing.T) {
	answer := func(f tfw.Frame) (tfw.Status, []byte) {
		if f.Cmd() != tfw.CmdHSMRecordRead || len(f.Payload) != 1 || f.Payload[0] != 0 {
			return tfw.StatusErrBadState, nil
		}
		e := []byte{3, 0, 0, 0, 0, 0, 0, tfw.HSMRecordSignal, 4, 0}
		binary.LittleEndian.PutUint32(e[3:7], 1000)
		return tfw.StatusOK, e
	}

	var out bytes.Buffer
	err := run(context.Background(), startFakeDevice(t, answer), "hsm-records", nil, false, &out)
	assert.True(t, errors.Is(err, tfw.StatusErrBadState), "%v", err)
	assert.Equal(t, "count 3, dropped 0\n  0  at 1000  signal 4  result 0\n", out.String())

	out.Reset()
	err = run(context.Background(), startFakeDevice(t, answer), "hsm-records", nil, true, &out)
	assert.True(t, errors.Is(err, tfw.StatusErrBadState), "%v", err)
	var r hsmRecordJSON
	require.NoError(t, json.Unmarshal(out.Bytes(), &r), "%q", out.String())
	assert.Equal(t, hsmRecordJSON{Count: 3, Dropped: 0, Entries: []hsmEntryJSON{
		{Index: 0, At: 1000, Kind: "signal", What: 4},
	}}, r)
}
