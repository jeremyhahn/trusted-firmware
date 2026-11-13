// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build tfwsim

package main

import (
	"bytes"
	"context"
	"io"
	"os"
	"path/filepath"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/tfwsim"
)

// The HSM channel's verbs against the element-tier simulator, the one build
// with the channel (mk/host.mk). It starts with no policy set held and an
// empty A6 record: the verbs that need a set are refused as the firmware
// refuses them, and the ones that do not answer the engine's state.

func cliHSMState(t *testing.T, dev *tfw.Device, verb string, args ...string) hsmStateJSON {
	t.Helper()
	var st hsmStateJSON
	cliJSON(t, dev, &st, verb, args...)
	return st
}

func TestSimHSMHeartbeatAnswersTheEngineState(t *testing.T) {
	dev := tfwsim.StartElement(t)
	st := cliHSMState(t, dev, "hsm-heartbeat")
	assert.False(t, st.SetHeld, "a fresh engine holds no set: %+v", st)
	assert.Equal(t, st.Flags&tfw.HSMSetHeld != 0, st.SetHeld)

	var out bytes.Buffer
	require.NoError(t, run(context.Background(), dev, "hsm-heartbeat", nil, false, &out))
	assert.Contains(t, out.String(), "flags    0x")
	assert.Contains(t, out.String(), "refused  ")
}

func TestSimHSMReportTakesS4AndS5AndRefusesAnyOtherSignal(t *testing.T) {
	dev := tfwsim.StartElement(t)
	cliHSMState(t, dev, "hsm-report", "ima")
	cliHSMState(t, dev, "hsm-report", "hotplug")
	// Any other signal is the SDK's refusal: nothing is sent.
	require.Error(t, run(context.Background(), dev, "hsm-report", []string{"9"}, false, io.Discard))
	cliHSMState(t, dev, "hsm-heartbeat")
}

func TestSimHSMVerdictTakesClearAndCompromisedAndRefusesAnyOther(t *testing.T) {
	dev := tfwsim.StartElement(t)
	cliHSMState(t, dev, "hsm-verdict", "clear")
	cliHSMState(t, dev, "hsm-verdict", "compromised")
	require.Error(t, run(context.Background(), dev, "hsm-verdict", []string{"2"}, false, io.Discard))
}

func TestSimHSMSelectWithNoSetHeldIsRange(t *testing.T) {
	dev := tfwsim.StartElement(t)
	assert.Equal(t, tfw.StatusErrRange, cliStatus(t, dev, "hsm-select", "0"))
}

func TestSimHSMLoadOfAnUnsignedEnvelopeIsRefused(t *testing.T) {
	dev := tfwsim.StartElement(t)
	before := cliHSMState(t, dev, "hsm-heartbeat")

	env := filepath.Join(t.TempDir(), "envelope.cbor")
	require.NoError(t, os.WriteFile(env, bytes.Repeat([]byte{0xA5}, 2*tfw.HSMLoadChunk+7), 0o600))
	st := cliStatus(t, dev, "hsm-load", env)
	assert.NotEqual(t, tfw.StatusOK, st)

	after := cliHSMState(t, dev, "hsm-heartbeat")
	assert.False(t, after.SetHeld, "a refused envelope is not held: %+v", after)
	assert.Greater(t, after.Refused, before.Refused, "the refusal is counted")
}

func TestSimHSMRecordOfTheEmptyRecord(t *testing.T) {
	dev := tfwsim.StartElement(t)
	assert.Equal(t, tfw.StatusErrRange, cliStatus(t, dev, "hsm-record", "0"))

	var r hsmRecordJSON
	cliJSON(t, dev, &r, "hsm-records")
	assert.Equal(t, hsmRecordJSON{Entries: []hsmEntryJSON{}}, r)

	var out bytes.Buffer
	require.NoError(t, run(context.Background(), dev, "hsm-records", nil, false, &out))
	assert.Equal(t, "count 0, dropped 0\n", out.String())
}
