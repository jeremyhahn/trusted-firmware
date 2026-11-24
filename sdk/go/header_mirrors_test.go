package tfw

import (
	"os"
	"regexp"
	"strconv"
	"testing"

	"github.com/stretchr/testify/require"
)

// The SDK's hand-copied mirrors of include/tfw that protocol_constants_test.go,
// platform_constants_test.go and scripts/test_status_mirror.py do not cover
// (#629): each test reads the header and fails, naming the macro, when a
// constant here no longer matches it.

const (
	mirrorTypesH = "../../include/tfw/types.h"
	mirrorBoardH = "../../include/tfw/board.h"
	mirrorEthH   = "../../include/tfw/eth.h"
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
