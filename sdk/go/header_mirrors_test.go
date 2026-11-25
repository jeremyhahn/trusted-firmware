package tfw

import (
	"os"
	"regexp"
	"strconv"
	"testing"

	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go/serialport"
)

// The SDK's hand-copied mirrors of include/tfw, src/core/suit.h and the
// ATECC608A driver's slot map that protocol_constants_test.go,
// platform_constants_test.go and scripts/test_status_mirror.py do not cover
// (#629): crypto tiers, CAPS feature bits, algorithm bits, pin flags, the
// netcfg block, the image trailer, reset causes, attest/alias page sizes, the
// signature length, HSM state flags and envelope max, timer and I2C bounds,
// the default baud and the element slots. Each test reads the header and
// fails, naming the macro, when a constant here no longer matches it.

const (
	mirrorTypesH  = "../../include/tfw/types.h"
	mirrorBoardH  = "../../include/tfw/board.h"
	mirrorEthH    = "../../include/tfw/eth.h"
	mirrorCryptoH = "../../include/tfw/crypto.h"
	mirrorSuitH   = "../../src/core/suit.h"
	mirrorAteccH  = "../../src/crypto/atecc608a/atecc.h"
)

// mirrorConstant is one Go constant and the header macro it mirrors.
type mirrorConstant struct {
	header, macro, goName string
	goValue               uint64
}

// headerEnumValue returns the value of a C enum member written `NAME = N`,
// decimal or hex, followed by a comma, a closing brace or a comment.
func headerEnumValue(src []byte, name string) (uint64, bool) {
	re := regexp.MustCompile(`(?m)^\s*` + regexp.QuoteMeta(name) + `\s*=\s*(0x[0-9A-Fa-f]+|\d+)\s*[,}/]`)
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

func mirrorReadHeader(t *testing.T, path string) []byte {
	t.Helper()
	src, err := os.ReadFile(path)
	require.NoError(t, err)
	return src
}

// mirrorCheckDefines compares each constant with its header's numeric #define.
func mirrorCheckDefines(t *testing.T, constants []mirrorConstant) {
	t.Helper()
	headers := map[string][]byte{}
	for _, c := range constants {
		src, ok := headers[c.header]
		if !ok {
			src = mirrorReadHeader(t, c.header)
			headers[c.header] = src
		}
		want, ok := protoHeaderDefine(src, c.macro)
		if !ok {
			t.Fatalf("%s: no numeric #define %s (renamed or removed?)", c.header, c.macro)
		}
		require.Equalf(t, want, c.goValue, "%s = 0x%X in Go, %s = 0x%X in %s", c.goName, c.goValue, c.macro, want, c.header)
	}
}

func TestCryptoTiersMatchTypesH(t *testing.T) {
	typesH := mirrorReadHeader(t, mirrorTypesH)
	tiers := []mirrorConstant{
		{mirrorTypesH, "TFW_CRYPTO_NONE", "CryptoNone", uint64(CryptoNone)},
		{mirrorTypesH, "TFW_CRYPTO_SOFTWARE", "CryptoSoftware", uint64(CryptoSoftware)},
		{mirrorTypesH, "TFW_CRYPTO_SECURE_ELEMENT", "CryptoSecureElement", uint64(CryptoSecureElement)},
		{mirrorTypesH, "TFW_CRYPTO_TPM2", "CryptoTPM2", uint64(CryptoTPM2)},
	}
	for _, c := range tiers {
		want, ok := headerEnumValue(typesH, c.macro)
		if !ok {
			t.Fatalf("%s: no enum member %s = N (renamed or removed?)", c.header, c.macro)
		}
		require.Equalf(t, want, c.goValue, "%s = %d in Go, %s = %d in %s", c.goName, c.goValue, c.macro, want, c.header)
	}
}

func TestHeaderMirrorEnumValueSeesADrift(t *testing.T) {
	src := []byte("typedef enum {\n    TFW_CRYPTO_NONE = 0,\n    TFW_CRYPTO_SOFTWARE   = 0x5, /* moved */\n    TFW_CRYPTO_TPM2 = 7}\n")

	sw, ok := headerEnumValue(src, "TFW_CRYPTO_SOFTWARE")
	require.True(t, ok)
	require.Equal(t, uint64(5), sw)
	require.NotEqual(t, uint64(CryptoSoftware), sw)

	tpm, ok := headerEnumValue(src, "TFW_CRYPTO_TPM2")
	require.True(t, ok)
	require.Equal(t, uint64(7), tpm)
	require.NotEqual(t, uint64(CryptoTPM2), tpm)

	_, ok = headerEnumValue(src, "TFW_CRYPTO_SECURE_ELEMENT")
	require.False(t, ok)
	// A prefix of a member's name is not that member.
	_, ok = headerEnumValue(src, "TFW_CRYPTO_TPM")
	require.False(t, ok)
}

// The Feature constants are CAPS wire bits: FeatureX must be 1 << TFW_CAPBIT_X.
// An inserted Feature constant shifts every later iota bit, and a header bit
// with no SDK constant fails by name.
func TestFeatureBitsMatchConfigH(t *testing.T) {
	configH := mirrorReadHeader(t, protoConfigH)
	features := []struct {
		macro, goName string
		goValue       uint32
	}{
		{"TFW_CAPBIT_GPIO", "FeatureGPIO", FeatureGPIO},
		{"TFW_CAPBIT_SCHED", "FeatureSched", FeatureSched},
		{"TFW_CAPBIT_ANALOG", "FeatureAnalog", FeatureAnalog},
		{"TFW_CAPBIT_PWM", "FeaturePWM", FeaturePWM},
		{"TFW_CAPBIT_TONE", "FeatureTone", FeatureTone},
		{"TFW_CAPBIT_PULSE", "FeaturePulse", FeaturePulse},
		{"TFW_CAPBIT_EEPROM", "FeatureEEPROM", FeatureEEPROM},
		{"TFW_CAPBIT_I2C", "FeatureI2C", FeatureI2C},
		{"TFW_CAPBIT_SPI", "FeatureSPI", FeatureSPI},
		{"TFW_CAPBIT_ONEWIRE", "FeatureOneWire", FeatureOneWire},
		{"TFW_CAPBIT_ATTEST", "FeatureAttest", FeatureAttest},
		{"TFW_CAPBIT_DEADMAN", "FeatureDeadman", FeatureDeadman},
		{"TFW_CAPBIT_PROVISION", "FeatureProvision", FeatureProvision},
		{"TFW_CAPBIT_ALIAS_CERT", "FeatureAliasCert", FeatureAliasCert},
		{"TFW_CAPBIT_AUTH_SESSION", "FeatureAuthSession", FeatureAuthSession},
		{"TFW_CAPBIT_ATTEST_CSR", "FeatureAttestCSR", FeatureAttestCSR},
	}
	mirrored := map[string]bool{}
	for _, f := range features {
		mirrored[f.macro] = true
		bit, ok := protoHeaderDefine(configH, f.macro)
		if !ok {
			t.Fatalf("%s: no numeric #define %s (renamed or removed?)", protoConfigH, f.macro)
		}
		require.Lessf(t, bit, uint64(32), "%s = %d in %s does not fit the 32-bit CAPS Features word", f.macro, bit, protoConfigH)
		require.Equalf(t, uint32(1)<<bit, f.goValue, "%s = 0x%X in Go, 1 << %s = 0x%X in %s", f.goName, f.goValue, f.macro, uint32(1)<<bit, protoConfigH)
	}

	var missing []string
	all := regexp.MustCompile(`(?m)^#define\s+(TFW_CAPBIT_\w+)\s`).FindAllSubmatch(configH, -1)
	for _, m := range all {
		if !mirrored[string(m[1])] {
			missing = append(missing, string(m[1]))
		}
	}
	require.Emptyf(t, missing, "%s has CAPS bits with no SDK Feature constant: %v", protoConfigH, missing)
	require.Equalf(t, len(all), len(features), "%s defines %d TFW_CAPBIT_* bits, the SDK mirrors %d", protoConfigH, len(all), len(features))
}

func TestAlgorithmBitsMatchProtocolH(t *testing.T) {
	mirrorCheckDefines(t, []mirrorConstant{
		{protoProtocolH, "TFW_ALGBIT_ECDSA_P256_SHA256", "AlgECDSAP256SHA256", uint64(AlgECDSAP256SHA256)},
		{protoProtocolH, "TFW_ALGBIT_ED25519", "AlgEd25519", uint64(AlgEd25519)},
		{protoProtocolH, "TFW_ALGBIT_HMAC_SHA256", "AlgHMACSHA256", uint64(AlgHMACSHA256)},
	})
}

func TestPinFlagsMatchBoardH(t *testing.T) {
	mirrorCheckDefines(t, []mirrorConstant{
		{mirrorBoardH, "TFW_PIN_DIGITAL", "PinDigital", uint64(PinDigital)},
		{mirrorBoardH, "TFW_PIN_PWM", "PinPWM", uint64(PinPWM)},
		{mirrorBoardH, "TFW_PIN_ADC", "PinADC", uint64(PinADC)},
		{mirrorBoardH, "TFW_PIN_RESERVED", "PinReserved", uint64(PinReserved)},
		{mirrorBoardH, "TFW_PIN_IN_USE", "PinInUse", uint64(PinInUse)},
		{mirrorBoardH, "TFW_PIN_LED", "PinLED", uint64(PinLED)},
		// The sentinels a pin descriptor carries for "no port" and "no ADC channel".
		{mirrorBoardH, "TFW_WIRE_PORT_NONE", "pinPortNone", uint64(pinPortNone)},
		{mirrorBoardH, "TFW_ADC_NONE", "pinADCChanNone", uint64(pinADCChanNone)},
	})
}

func TestNetConfigMatchesEthH(t *testing.T) {
	mirrorCheckDefines(t, []mirrorConstant{
		{mirrorEthH, "TFW_NETCFG_LEN", "NetConfigLen", uint64(NetConfigLen)},
		{mirrorEthH, "TFW_NETCFG_VERSION", "netcfgVersion", uint64(netcfgVersion)},
		{mirrorEthH, "TFW_NETCFG_DHCP", "netcfgFlagDHCP", uint64(netcfgFlagDHCP)},
	})

	// TFW_NETCFG_ADDR is an expression, so it is pinned as text: the block
	// starts where boot.h's reserved EEPROM region ends.
	ethH := mirrorReadHeader(t, mirrorEthH)
	require.Regexp(t, regexp.MustCompile(`(?m)^#define\s+TFW_NETCFG_ADDR\s+TFW_BOOT_EE_LEN\s*$`), string(ethH),
		"eth.h's TFW_NETCFG_ADDR is no longer TFW_BOOT_EE_LEN; NetConfigAddr must follow it")
	eeLen, ok := protoHeaderDefine(mirrorReadHeader(t, platformBootH), "TFW_BOOT_EE_LEN")
	if !ok {
		t.Fatalf("%s: no numeric #define TFW_BOOT_EE_LEN (renamed or removed?)", platformBootH)
	}
	require.Equalf(t, eeLen, uint64(NetConfigAddr), "NetConfigAddr = 0x%X in Go, TFW_BOOT_EE_LEN = 0x%X in %s", uint64(NetConfigAddr), eeLen, platformBootH)
}

func TestImageTrailerMatchesBootH(t *testing.T) {
	mirrorCheckDefines(t, []mirrorConstant{
		{protoConfigH, "TFW_OTA_PAGE_MAX", "ImagePage", uint64(ImagePage)},
		{platformBootH, "TFW_BOOT_TRAILER_LEN", "ImageTrailerLen", uint64(ImageTrailerLen)},
		{platformBootH, "TFW_BOOT_TRAILER_VERSION", "ImageTrailerVersion", uint64(ImageTrailerVersion)},
		{platformBootH, "TFW_BOOT_TRAILER_MAGIC", "imageTrailerMagic", uint64(imageTrailerMagic)},
	})
}

// ResetCause numbering is wire: it must follow tfw_reset_cause_t.
func TestResetCausesMatchTypesH(t *testing.T) {
	typesH := mirrorReadHeader(t, mirrorTypesH)
	causes := []mirrorConstant{
		{mirrorTypesH, "TFW_RESET_UNKNOWN", "ResetUnknown", uint64(ResetUnknown)},
		{mirrorTypesH, "TFW_RESET_POWER_ON", "ResetPowerOn", uint64(ResetPowerOn)},
		{mirrorTypesH, "TFW_RESET_EXTERNAL", "ResetExternal", uint64(ResetExternal)},
		{mirrorTypesH, "TFW_RESET_BROWNOUT", "ResetBrownout", uint64(ResetBrownout)},
		{mirrorTypesH, "TFW_RESET_WATCHDOG", "ResetWatchdog", uint64(ResetWatchdog)},
		{mirrorTypesH, "TFW_RESET_SOFTWARE", "ResetSoftware", uint64(ResetSoftware)},
		{mirrorTypesH, "TFW_RESET_PANIC", "ResetPanic", uint64(ResetPanic)},
	}
	for _, c := range causes {
		want, ok := headerEnumValue(typesH, c.macro)
		if !ok {
			t.Fatalf("%s: no enum member %s = N (renamed or removed?)", c.header, c.macro)
		}
		require.Equalf(t, want, c.goValue, "%s = %d in Go, %s = %d in %s", c.goName, c.goValue, c.macro, want, c.header)
	}
}

// The attest tests' fake link pages by attestPage and aliasPage, so only this
// test ties those sizes to the device's.
func TestAttestPagesAndSigLenMatchTheHeaders(t *testing.T) {
	mirrorCheckDefines(t, []mirrorConstant{
		{protoProtocolH, "TFW_ATTEST_PAGE", "attestPage", uint64(attestPage)},
		{protoProtocolH, "TFW_ALIAS_PAGE", "aliasPage", uint64(aliasPage)},
		{mirrorCryptoH, "TFW_SIG_LEN", "ecdsaRawSigLen", uint64(ecdsaRawSigLen)},
	})
}

func TestHSMConstantsMatchTheHeaders(t *testing.T) {
	mirrorCheckDefines(t, []mirrorConstant{
		{protoProtocolH, "TFW_HSM_STATE_SET", "HSMSetHeld", uint64(HSMSetHeld)},
		{protoProtocolH, "TFW_HSM_STATE_LOCKED", "HSMLocked", uint64(HSMLocked)},
		{protoProtocolH, "TFW_HSM_STATE_WIPED", "HSMWiped", uint64(HSMWiped)},
		{mirrorSuitH, "TFW_SUIT_INTEGRATED_MAX", "HSMEnvelopeMax", uint64(HSMEnvelopeMax)},
	})
}

// TFW_UART_BAUD sits in an #ifndef: the default value is the one pinned.
func TestDeviceBoundsMatchConfigH(t *testing.T) {
	mirrorCheckDefines(t, []mirrorConstant{
		{protoConfigH, "TFW_SCHED_MAX_MS", "timerMaxMillis", uint64(timerMaxMillis)},
		{protoConfigH, "TFW_I2C_HZ_MIN", "I2CClockMin", uint64(I2CClockMin)},
		{protoConfigH, "TFW_I2C_HZ_MAX", "I2CClockMax", uint64(I2CClockMax)},
		{protoConfigH, "TFW_UART_BAUD", "serialport.DefaultBaud", uint64(serialport.DefaultBaud)},
	})
}

// SlotSecureBootKey comes from the part's datasheet, not a header.
func TestElementSlotsMatchAteccH(t *testing.T) {
	mirrorCheckDefines(t, []mirrorConstant{
		{mirrorAteccH, "TFW_ATECC_SLOT_DEVICEID", "SlotDeviceID", uint64(SlotDeviceID)},
		{mirrorAteccH, "TFW_ATECC_SLOT_UDS", "SlotUDS", uint64(SlotUDS)},
		{mirrorAteccH, "TFW_ATECC_SLOT_IMAGE", "SlotImageSigning", uint64(SlotImageSigning)},
	})
}
