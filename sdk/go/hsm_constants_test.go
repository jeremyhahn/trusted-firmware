package tfw

import (
	"os"
	"regexp"
	"strconv"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// The firmware headers the SDK's HSM channel constants are copied from,
// relative to sdk/go, the directory `go test` runs in.
const (
	hsmProtocolH = "../../include/tfw/protocol.h"
	hsmConfigH   = "../../include/tfw/config.h"
	hsmCoreH     = "../../src/core/hsm.h"
	hsmRecordH   = "../../src/core/hsm_record.h"
	hsmSuitH     = "../../src/core/suit.h"
)

// hsmHeaderDefine returns the value of `#define name <n>` in src, reading a
// decimal or hex literal with or without its u/U and l/L suffixes. The
// define must start at column 0 and name must be the whole macro name.
func hsmHeaderDefine(src []byte, name string) (uint64, bool) {
	re := regexp.MustCompile(`(?m)^#define\s+` + regexp.QuoteMeta(name) + `\s+(0x[0-9A-Fa-f]+|\d+)[uU]?[lL]?\b`)
	m := re.FindSubmatch(src)
	if m == nil {
		return 0, false
	}
	v, err := strconv.ParseUint(string(m[1]), 0, 64)
	if err != nil {
		return 0, false
	}
	return v, true
}

// hsmConstant is one SDK constant and the firmware macro it copies.
type hsmConstant struct {
	header string
	macro  string
	goName string
	goVal  uint64
}

// hsmConstants lists every HSM channel constant in hsm.go that copies a
// firmware macro one for one.
var hsmConstants = []hsmConstant{
	{hsmProtocolH, "TFW_CMD_HSM_HEARTBEAT", "CmdHSMHeartbeat", uint64(CmdHSMHeartbeat)},
	{hsmProtocolH, "TFW_CMD_HSM_REPORT", "CmdHSMReport", uint64(CmdHSMReport)},
	{hsmProtocolH, "TFW_CMD_HSM_VERDICT", "CmdHSMVerdict", uint64(CmdHSMVerdict)},
	{hsmProtocolH, "TFW_CMD_HSM_SELECT", "CmdHSMSelect", uint64(CmdHSMSelect)},
	{hsmProtocolH, "TFW_CMD_HSM_LOAD", "CmdHSMLoad", uint64(CmdHSMLoad)},
	{hsmProtocolH, "TFW_CMD_HSM_RECORD_READ", "CmdHSMRecordRead", uint64(CmdHSMRecordRead)},
	{hsmProtocolH, "TFW_HSM_RECORD_LEN", "HSMRecordLen", uint64(HSMRecordLen)},
	{hsmProtocolH, "TFW_HSM_STATE_SET", "HSMSetHeld", uint64(HSMSetHeld)},
	{hsmProtocolH, "TFW_HSM_STATE_LOCKED", "HSMLocked", uint64(HSMLocked)},
	{hsmProtocolH, "TFW_HSM_STATE_WIPED", "HSMWiped", uint64(HSMWiped)},
	{hsmProtocolH, "TFW_HSM_STATE_LEN", "HSMStateLen", uint64(HSMStateLen)},
	{hsmCoreH, "TFW_HSM_SIG_IMA_VIOLATION", "HSMSignalIMAViolation", uint64(HSMSignalIMAViolation)},
	{hsmCoreH, "TFW_HSM_SIG_HOTPLUG", "HSMSignalHotplug", uint64(HSMSignalHotplug)},
	{hsmCoreH, "TFW_HSM_VERDICT_CLEAR", "HSMVerdictClear", uint64(HSMVerdictClear)},
	{hsmCoreH, "TFW_HSM_VERDICT_COMPROMISED", "HSMVerdictCompromised", uint64(HSMVerdictCompromised)},
	{hsmRecordH, "TFW_HSM_REC_SIGNAL", "HSMRecordSignal", uint64(HSMRecordSignal)},
	{hsmRecordH, "TFW_HSM_REC_DECISION", "HSMRecordDecision", uint64(HSMRecordDecision)},
	{hsmRecordH, "TFW_HSM_REC_ACTION", "HSMRecordAction", uint64(HSMRecordAction)},
	{hsmSuitH, "TFW_SUIT_INTEGRATED_MAX", "HSMEnvelopeMax", uint64(HSMEnvelopeMax)},
}

// TestHSMConstantsMatchTheFirmwareHeaders holds the A55's half of the HSM
// channel to the M7's: a renumbered command, a moved state bit or a changed
// length in a header fails here, under plain `go test`, instead of leaving
// the SDK sending what the firmware does not expect.
func TestHSMConstantsMatchTheFirmwareHeaders(t *testing.T) {
	headers := map[string][]byte{}
	for _, h := range []string{hsmProtocolH, hsmConfigH, hsmCoreH, hsmRecordH, hsmSuitH} {
		src, err := os.ReadFile(h)
		require.NoError(t, err, "read %s", h)
		headers[h] = src
	}
	define := func(t *testing.T, header, macro string) uint64 {
		t.Helper()
		v, ok := hsmHeaderDefine(headers[header], macro)
		if !ok {
			t.Fatalf("%s defines no %s", header, macro)
		}
		return v
	}

	for _, c := range hsmConstants {
		t.Run(c.goName, func(t *testing.T) {
			v := define(t, c.header, c.macro)
			assert.Equal(t, v, c.goVal, "%s is %d in %s but %s is %d", c.macro, v, c.header, c.goName, c.goVal)
		})
	}

	// A load chunk is the payload less the load header (total + offset).
	t.Run("HSMLoadChunk", func(t *testing.T) {
		payload := define(t, hsmConfigH, "TFW_MAX_PAYLOAD")
		hdr := define(t, hsmProtocolH, "TFW_HSM_LOAD_HDR")
		require.Greater(t, payload, hdr, "TFW_MAX_PAYLOAD %d is not above TFW_HSM_LOAD_HDR %d", payload, hdr)
		assert.Equal(t, payload-hdr, uint64(HSMLoadChunk),
			"TFW_MAX_PAYLOAD - TFW_HSM_LOAD_HDR is %d (%d - %d) but HSMLoadChunk is %d",
			payload-hdr, payload, hdr, HSMLoadChunk)
	})
}

// TestHSMHeaderDefineSeesADrift proves the parser reads the literal's
// suffixes and whole macro names, so the check above fails on a drift
// rather than passing on a value it could not read.
func TestHSMHeaderDefineSeesADrift(t *testing.T) {
	src := []byte("/* a drifted header */\n" +
		"#define TFW_CMD_HSM_LOAD 0x7Bu  /* moved */\n" +
		"#ifndef TFW_MAX_PAYLOAD\n" +
		"#define TFW_MAX_PAYLOAD  128u\n" +
		"#endif\n")

	load, ok := hsmHeaderDefine(src, "TFW_CMD_HSM_LOAD")
	require.True(t, ok, "TFW_CMD_HSM_LOAD not read")
	assert.Equal(t, uint64(0x7B), load)
	assert.NotEqual(t, uint64(CmdHSMLoad), load, "a moved command id must not match CmdHSMLoad")

	payload, ok := hsmHeaderDefine(src, "TFW_MAX_PAYLOAD")
	require.True(t, ok, "TFW_MAX_PAYLOAD not read")
	assert.Equal(t, uint64(128), payload)
	assert.NotEqual(t, uint64(HSMLoadChunk), payload-4, "a larger payload must not match HSMLoadChunk")

	_, ok = hsmHeaderDefine(src, "TFW_HSM_STATE_LEN")
	assert.False(t, ok, "an absent define must not be found")
	_, ok = hsmHeaderDefine(src, "TFW_CMD_HSM")
	assert.False(t, ok, "a prefix of a define's name must not be found")
}
