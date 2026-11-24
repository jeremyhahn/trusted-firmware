package tfw

import (
	"os"
	"regexp"
	"sort"
	"strconv"
	"testing"

	"github.com/stretchr/testify/require"
)

// The SDK's frame constants, protocol version and command ids are hand-copied
// from include/tfw/protocol.h and include/tfw/config.h (codec.go, device.go,
// ota.go, ...). These tests read the headers, so a renumbered TFW_CMD_* or a
// protocol major bump fails here instead of leaving the host on the old wire.
// The HSM ids are pinned by hsm_constants_test.go (row #50).

const (
	protoProtocolH = "../../include/tfw/protocol.h"
	protoConfigH   = "../../include/tfw/config.h"
)

// protoHeaderDefine returns the numeric value of `#define name <number>[u][l]`
// in src. The match is anchored at a line start only, so a default inside an
// #ifndef block is found as well.
func protoHeaderDefine(src []byte, name string) (uint64, bool) {
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

func protoReadHeaders(t *testing.T) (protocolH, configH []byte) {
	t.Helper()
	protocolH, err := os.ReadFile(protoProtocolH)
	require.NoError(t, err)
	configH, err = os.ReadFile(protoConfigH)
	require.NoError(t, err)
	return protocolH, configH
}

func TestProtocolFrameConstantsMatchTheHeaders(t *testing.T) {
	protocolH, configH := protoReadHeaders(t)
	headers := map[string][]byte{protoProtocolH: protocolH, protoConfigH: configH}

	protoConstants := []struct {
		header, macro, goName string
		goValue               uint64
	}{
		{protoProtocolH, "TFW_SOF", "SOF", uint64(SOF)},
		{protoProtocolH, "TFW_OP_RESPONSE_BIT", "opResponseBit", uint64(opResponseBit)},
		{protoProtocolH, "TFW_OP_EVENT", "opEvent", uint64(opEvent)},
		{protoProtocolH, "TFW_FRAME_OVERHEAD", "frameOverhead", uint64(frameOverhead)},
		{protoProtocolH, "TFW_LEN_MIN", "lenMin", uint64(lenMin)},
		{protoProtocolH, "TFW_SEQ_NONE", "SeqNone", uint64(SeqNone)},
		{protoProtocolH, "TFW_PROTO_VERSION_MAJOR", "ProtoVersionMajor", uint64(ProtoVersionMajor)},
		{protoProtocolH, "TFW_PROTO_VERSION_MINOR", "ProtoVersionMinor", uint64(ProtoVersionMinor)},
		{protoConfigH, "TFW_MAX_PAYLOAD", "MaxPayload", uint64(MaxPayload)},
	}
	for _, c := range protoConstants {
		want, ok := protoHeaderDefine(headers[c.header], c.macro)
		if !ok {
			t.Fatalf("%s: no numeric #define %s (renamed or removed?)", c.header, c.macro)
		}
		require.Equalf(t, want, c.goValue, "%s = %d in Go, %s = %d in %s", c.goName, c.goValue, c.macro, want, c.header)
	}

	// TFW_MAX_LEN is an expression, so its formula is pinned as text.
	require.Regexp(t, regexp.MustCompile(`(?m)^#define\s+TFW_MAX_LEN\s+\(\(uint8_t\)\(TFW_MAX_PAYLOAD \+ 3u\)\)`), string(configH),
		"config.h's TFW_MAX_LEN is no longer TFW_MAX_PAYLOAD + 3u; maxLen must follow it")
	require.Equal(t, MaxPayload+3, maxLen, "maxLen must be config.h's TFW_MAX_PAYLOAD + 3u")
}

// protoUnmirrored names every TFW_CMD_* id in protocol.h with no Go Cmd in
// the table below, and why.
var protoUnmirrored = map[string]string{
	"TFW_CMD_WDT_SET":           "reserved, not implemented",
	"TFW_CMD_SLEEP":             "reserved, not implemented",
	"TFW_CMD_PULSE_COUNT_START": "reserved, not implemented",
	"TFW_CMD_PULSE_COUNT_READ":  "reserved, not implemented",
	"TFW_CMD_PULSE_COUNT_STOP":  "reserved, not implemented",
	"TFW_CMD_HSM_HEARTBEAT":     "pinned by hsm_constants_test.go, row #50",
	"TFW_CMD_HSM_REPORT":        "pinned by hsm_constants_test.go, row #50",
	"TFW_CMD_HSM_VERDICT":       "pinned by hsm_constants_test.go, row #50",
	"TFW_CMD_HSM_SELECT":        "pinned by hsm_constants_test.go, row #50",
	"TFW_CMD_HSM_LOAD":          "pinned by hsm_constants_test.go, row #50",
	"TFW_CMD_HSM_RECORD_READ":   "pinned by hsm_constants_test.go, row #50",
}

var protoCommands = []struct {
	macro, goName string
	cmd           Cmd
}{
	{"TFW_CMD_CAPS", "CmdCaps", CmdCaps},
	{"TFW_CMD_PING", "CmdPing", CmdPing},
	{"TFW_CMD_RESET", "CmdReset", CmdReset},
	{"TFW_CMD_SYS_INFO", "CmdSysInfo", CmdSysInfo},
	{"TFW_CMD_APP_INFO", "CmdAppInfo", CmdAppInfo},
	{"TFW_CMD_PIN_MODE", "CmdPinMode", CmdPinMode},
	{"TFW_CMD_DIGITAL_READ", "CmdDigitalRead", CmdDigitalRead},
	{"TFW_CMD_DIGITAL_WRITE", "CmdDigitalWrite", CmdDigitalWrite},
	{"TFW_CMD_TIMER_SET", "CmdTimerSet", CmdTimerSet},
	{"TFW_CMD_TIMER_CANCEL", "CmdTimerCancel", CmdTimerCancel},
	{"TFW_CMD_TIMER_QUERY", "CmdTimerQuery", CmdTimerQuery},
	{"TFW_CMD_ANALOG_READ", "CmdAnalogRead", CmdAnalogRead},
	{"TFW_CMD_ADC_REF", "CmdADCRef", CmdADCRef},
	{"TFW_CMD_EEPROM_READ", "CmdEEPROMRead", CmdEEPROMRead},
	{"TFW_CMD_EEPROM_WRITE", "CmdEEPROMWrite", CmdEEPROMWrite},
	{"TFW_CMD_PWM_WRITE", "CmdPWMWrite", CmdPWMWrite},
	{"TFW_CMD_TONE", "CmdTone", CmdTone},
	{"TFW_CMD_NO_TONE", "CmdNoTone", CmdNoTone},
	{"TFW_CMD_PULSE_IN", "CmdPulseIn", CmdPulseIn},
	{"TFW_CMD_PULSE_CAPTURE", "CmdPulseCapture", CmdPulseCapture},
	{"TFW_CMD_I2C_XFER", "CmdI2CXfer", CmdI2CXfer},
	{"TFW_CMD_SPI_XFER", "CmdSPIXfer", CmdSPIXfer},
	{"TFW_CMD_ONEWIRE_RESET", "CmdOneWireReset", CmdOneWireReset},
	{"TFW_CMD_ONEWIRE_WRITE", "CmdOneWireWrite", CmdOneWireWrite},
	{"TFW_CMD_ONEWIRE_READ", "CmdOneWireRead", CmdOneWireRead},
	{"TFW_CMD_I2C_CONFIG", "CmdI2CConfig", CmdI2CConfig},
	{"TFW_CMD_SPI_CONFIG", "CmdSPIConfig", CmdSPIConfig},
	{"TFW_CMD_ATTEST", "CmdAttest", CmdAttest},
	{"TFW_CMD_ATTEST_CERT", "CmdAttestCert", CmdAttestCert},
	{"TFW_CMD_ATTEST_CSR", "CmdAttestCSR", CmdAttestCSR},
	{"TFW_CMD_AUTH_CHALLENGE", "CmdAuthChallenge", CmdAuthChallenge},
	{"TFW_CMD_AUTH_RESPONSE", "CmdAuthResponse", CmdAuthResponse},
	{"TFW_CMD_OTA_BEGIN", "CmdOTABegin", CmdOTABegin},
	{"TFW_CMD_OTA_DATA", "CmdOTAData", CmdOTAData},
	{"TFW_CMD_OTA_END", "CmdOTAEnd", CmdOTAEnd},
	{"TFW_CMD_OTA_STATUS", "CmdOTAStatus", CmdOTAStatus},
	{"TFW_CMD_OTA_INFO", "CmdOTAInfo", CmdOTAInfo},
	{"TFW_CMD_OTA_ACTIVATE", "CmdOTAActivate", CmdOTAActivate},
	{"TFW_CMD_OTA_CONFIRM", "CmdOTAConfirm", CmdOTAConfirm},
	{"TFW_CMD_OTA_AUTH", "CmdOTAAuth", CmdOTAAuth},
	{"TFW_CMD_PROV_INFO", "CmdProvInfo", CmdProvInfo},
	{"TFW_CMD_PROV_WRITE_CONFIG", "CmdProvWriteConfig", CmdProvWriteConfig},
	{"TFW_CMD_PROV_LOCK", "CmdProvLock", CmdProvLock},
	{"TFW_CMD_PROV_GENKEY", "CmdProvGenKey", CmdProvGenKey},
	{"TFW_CMD_PROV_PUBKEY", "CmdProvPubKey", CmdProvPubKey},
	{"TFW_CMD_PROV_SEED_UDS", "CmdProvSeedUDS", CmdProvSeedUDS},
	{"TFW_CMD_PROV_WRITE_PUBKEY", "CmdProvWritePubKey", CmdProvWritePubKey},
}

func TestProtocolCommandIDsMatchTheHeader(t *testing.T) {
	protocolH, _ := protoReadHeaders(t)
	require.Len(t, protoCommands, 47)

	protoTabled := map[string]bool{}
	protoIDs := map[Cmd]string{}
	for _, c := range protoCommands {
		want, ok := protoHeaderDefine(protocolH, c.macro)
		if !ok {
			t.Fatalf("%s: no numeric #define %s (renamed or removed?)", protoProtocolH, c.macro)
		}
		require.Equalf(t, want, uint64(c.cmd), "%s = 0x%02X in Go, %s = 0x%02X in %s", c.goName, uint8(c.cmd), c.macro, want, protoProtocolH)
		if other, dup := protoIDs[c.cmd]; dup {
			t.Fatalf("%s and %s share id 0x%02X", other, c.goName, uint8(c.cmd))
		}
		protoIDs[c.cmd] = c.goName
		protoTabled[c.macro] = true
	}

	// Every numeric command id in the header has a Go Cmd or a stated reason.
	var missing []string
	for _, m := range regexp.MustCompile(`(?m)^#define\s+(TFW_CMD_\w+)\s+0x`).FindAllStringSubmatch(string(protocolH), -1) {
		name := m[1]
		if protoTabled[name] {
			continue
		}
		if _, ok := protoUnmirrored[name]; ok {
			continue
		}
		missing = append(missing, name)
	}
	sort.Strings(missing)
	require.Emptyf(t, missing, "%s has command ids with no SDK Cmd and no protoUnmirrored reason: %v", protoProtocolH, missing)
}

func TestProtoHeaderDefineSeesADrift(t *testing.T) {
	src := []byte("/* drifted */\n#define TFW_PROTO_VERSION_MAJOR 3u\n#ifndef X\n#define TFW_CMD_PING            0x09u  /* moved */\n#endif\n")

	major, ok := protoHeaderDefine(src, "TFW_PROTO_VERSION_MAJOR")
	require.True(t, ok)
	require.Equal(t, uint64(3), major)
	require.NotEqual(t, uint64(ProtoVersionMajor), major)

	ping, ok := protoHeaderDefine(src, "TFW_CMD_PING")
	require.True(t, ok)
	require.Equal(t, uint64(9), ping)
	require.NotEqual(t, uint64(CmdPing), ping)

	_, ok = protoHeaderDefine(src, "TFW_CMD_RESET")
	require.False(t, ok)
	// A prefix of a defined name is not that name.
	_, ok = protoHeaderDefine(src, "TFW_CMD_PIN")
	require.False(t, ok)
}
