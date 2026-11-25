// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package main

import (
	"encoding/hex"
	"errors"
	"io/fs"
	"os"
	"path/filepath"
	"strings"
	"testing"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// writeSigFile writes body to a file in a fresh t.TempDir() and returns its path.
func writeSigFile(t *testing.T, body []byte) string {
	t.Helper()
	path := filepath.Join(t.TempDir(), "sig")
	require.NoError(t, os.WriteFile(path, body, 0o600))
	return path
}

// TestTakeOTASigKeepsTheBytesInOrder gives a signature of 64 distinct bytes in
// each form and proves takeOTASig returns them in order, and the other
// arguments in order: a fixture of one repeated byte cannot show either.
func TestTakeOTASigKeepsTheBytesInOrder(t *testing.T) {
	want := make([]byte, tfw.OTAAuthSigLen)
	for i := range want {
		want[i] = byte(i)
	}
	text := hex.EncodeToString(want)

	cases := []struct {
		name string
		flag string
	}{
		{"hex flag", "--sig=" + text},
		{"raw file", "--sig-file=" + writeSigFile(t, want)},
		{"upper-case hex file", "--sig-file=" + writeSigFile(t, []byte(strings.ToUpper(text)+"\n"))},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			sig, rest, found, err := takeOTASig([]string{"--activate", tc.flag, "image.bin"})
			require.NoError(t, err)
			require.True(t, found)
			assert.Equal(t, want, sig[:])
			assert.Equal(t, []string{"--activate", "image.bin"}, rest)
		})
	}
}

// TestTakeOTASigWithoutAFlagReturnsTheArgs: no --sig or --sig-file leaves the
// arguments as they were and the signature zero.
func TestTakeOTASigWithoutAFlagReturnsTheArgs(t *testing.T) {
	sig, rest, found, err := takeOTASig([]string{"now"})
	require.NoError(t, err)
	assert.False(t, found)
	assert.Equal(t, []string{"now"}, rest)
	assert.Equal(t, [tfw.OTAAuthSigLen]byte{}, sig)
}

// TestTakeOTASigRefusals covers each refusal: a second flag, a hex of the wrong
// length, a raw file of the wrong length and a file that cannot be read.
func TestTakeOTASigRefusals(t *testing.T) {
	good := "--sig=" + strings.Repeat("ee", tfw.OTAAuthSigLen)
	missing := "--sig-file=" + filepath.Join(t.TempDir(), "absent")

	cases := []struct {
		name string
		args []string
		want string
	}{
		{"twice", []string{good, good}, "give the authorization once"},
		{"126 hex digits", []string{"--sig=" + strings.Repeat("ee", tfw.OTAAuthSigLen-1)}, "the authorization is 64 bytes"},
		{"130 hex digits", []string{"--sig=" + strings.Repeat("ee", tfw.OTAAuthSigLen+1)}, "the authorization is 64 bytes"},
		{"63-byte file", []string{"--sig-file=" + writeSigFile(t, fill(tfw.OTAAuthSigLen-1, 0xEE))}, "the authorization is 64 bytes"},
		{"65-byte file", []string{"--sig-file=" + writeSigFile(t, fill(tfw.OTAAuthSigLen+1, 0xEE))}, "the authorization is 64 bytes"},
		{"missing file", []string{missing}, missing + ": "},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			_, rest, found, err := takeOTASig(tc.args)
			require.Error(t, err)
			assert.False(t, found)
			assert.Nil(t, rest)
			assert.Contains(t, err.Error(), tc.want)
		})
	}

	t.Run("missing file wraps the os error", func(t *testing.T) {
		_, _, _, err := takeOTASig([]string{missing})
		require.Error(t, err)
		assert.True(t, strings.HasPrefix(err.Error(), missing), err.Error())
		assert.True(t, errors.Is(err, fs.ErrNotExist), err.Error())
	})
}

// TestNeedOTASigNamesTheVerbAndBothForms: the refusal names the verb and both
// ways to give the authorization.
func TestNeedOTASigNamesTheVerbAndBothForms(t *testing.T) {
	msg := needOTASig("ota-confirm").Error()
	assert.True(t, strings.HasPrefix(msg, "ota-confirm needs the row key's authorization"), msg)
	assert.Contains(t, msg, otaSigUsage)
}
