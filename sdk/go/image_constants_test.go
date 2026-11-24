package tfw

import (
	"bytes"
	"fmt"
	"os"
	"regexp"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

const (
	suitH = "../../src/core/suit.h"
	bootH = "../../include/tfw/boot.h"
)

// imageConstant is one SDK constant and the firmware #define it copies.
// ImagePage stays out: TFW_OTA_PAGE_MAX is per board (boards/*/board_profile.h).
type imageConstant struct {
	header, macro, name string
	sdk                 uint64
}

var imageConstants = []imageConstant{
	{suitH, "TFW_SUIT_ENVELOPE_MAX", "ImageEnvelopeMax", ImageEnvelopeMax},
	{bootH, "TFW_BOOT_TRAILER_LEN", "ImageTrailerLen", ImageTrailerLen},
	{bootH, "TFW_BOOT_TRAILER_VERSION", "ImageTrailerVersion", ImageTrailerVersion},
	{bootH, "TFW_BOOT_TRAILER_MAGIC", "imageTrailerMagic", imageTrailerMagic},
}

// imageConstantDrift holds each constant copied from header to its #define
// in src, the header's bytes. It returns one message per constant whose value
// differs, and an error naming the header and macro when src does not define
// one, so a rename fails rather than skips.
func imageConstantDrift(header string, src []byte) ([]string, error) {
	var drift []string
	for _, c := range imageConstants {
		if c.header != header {
			continue
		}
		// The one strict parser: a value that is not a whole literal (an
		// expression such as `320u + 1u`) is an error, not its first number.
		v, ok := protoHeaderDefine(src, c.macro)
		if !ok {
			return nil, fmt.Errorf("%s defines no literal %s", header, c.macro)
		}
		if v != c.sdk {
			drift = append(drift, fmt.Sprintf("%s: %s is %d, %s copies %d", header, c.macro, v, c.name, c.sdk))
		}
	}
	return drift, nil
}

func TestImageConstantsMatchTheFirmwareHeaders(t *testing.T) {
	for _, h := range []string{suitH, bootH} {
		src, err := os.ReadFile(h)
		require.NoError(t, err)
		drift, err := imageConstantDrift(h, src)
		if err != nil {
			t.Fatalf("%v", err)
		}
		assert.Empty(t, drift)
	}
}

func TestTheImageConstantsCheckSeesADrift(t *testing.T) {
	src, err := os.ReadFile(suitH)
	require.NoError(t, err)
	re := regexp.MustCompile(`(?m)^(#define\s+TFW_SUIT_ENVELOPE_MAX\s+)320u\b`)
	require.True(t, re.Match(src), "%s no longer defines TFW_SUIT_ENVELOPE_MAX as 320u", suitH)
	changed := re.ReplaceAll(src, []byte("${1}321u"))
	require.False(t, bytes.Equal(src, changed))

	drift, err := imageConstantDrift(suitH, changed)
	require.NoError(t, err)
	require.Len(t, drift, 1)
	assert.Contains(t, drift[0], fmt.Sprintf("TFW_SUIT_ENVELOPE_MAX is 321, ImageEnvelopeMax copies %d", ImageEnvelopeMax))

	// An expression is not read as its first number.
	expr := re.ReplaceAll(src, []byte("${1}320u + 1u"))
	_, err = imageConstantDrift(suitH, expr)
	require.Error(t, err)
}
