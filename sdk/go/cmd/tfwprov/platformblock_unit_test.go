package main

import (
	"strings"
	"testing"

	"github.com/stretchr/testify/require"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
)

func TestPlatformBlockFromFlagsRefusals(t *testing.T) {
	serial := strings.Repeat("01", tfw.SerialLen)
	for _, tc := range []struct {
		name                      string
		serial, hash, operatorKey string
		want                      string
	}{
		{"short serial", "01", "", "", "--serial"},
		{"zero serial", strings.Repeat("00", tfw.SerialLen), "", "", "all zero"},
		{"short pubkey hash", serial, "01", "", "--pubkey-hash"},
		{"zero pubkey hash", serial, strings.Repeat("00", tfw.PubkeyHashLen), "", "--pubkey-hash is all zero"},
		{"short operator key", serial, "", "01", "--operator-pubkey"},
		{"operator key off the curve", serial, "", strings.Repeat("01", tfw.OperatorPubkeyLen), "--operator-pubkey"},
	} {
		t.Run(tc.name, func(t *testing.T) {
			var dst bakedPlatform
			err := platformBlockFromFlags(&dst, tc.serial, tc.hash, tc.operatorKey)
			require.Error(t, err)
			require.Contains(t, err.Error(), tc.want)
			require.Equal(t, bakedPlatform{}, dst, "nothing is baked on a refusal")
		})
	}
}

func TestPlatformBlockFromFlagsBakesTheSerial(t *testing.T) {
	var dst bakedPlatform
	require.NoError(t, platformBlockFromFlags(&dst, strings.Repeat("01", tfw.SerialLen), strings.Repeat("ab", tfw.PubkeyHashLen), ""))
	require.Equal(t, byte(0x01), dst.serial[0])
	require.Equal(t, byte(0xAB), dst.pubkeyHash[tfw.PubkeyHashLen-1])
	require.Equal(t, [tfw.OperatorPubkeyLen]byte{}, dst.operatorPubkey)
}
