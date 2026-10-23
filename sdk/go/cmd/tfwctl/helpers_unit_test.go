package main

import (
	"crypto"
	"path/filepath"
	"testing"

	"github.com/stretchr/testify/require"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
)

func TestFeatureList(t *testing.T) {
	for _, tc := range []struct {
		features uint32
		want     string
	}{
		{0, "(none)"},
		{tfw.FeatureGPIO, "gpio"},
		{tfw.FeatureGPIO | tfw.FeatureSPI | tfw.FeatureAttestCSR, "gpio spi attest-csr"},
	} {
		require.Equal(t, tc.want, featureList(&tfw.Caps{Features: tc.features}))
	}
}

func TestB2i(t *testing.T) {
	require.Equal(t, 1, b2i(true))
	require.Equal(t, 0, b2i(false))
}

func TestExternalKeySignerRefusesAMissingKey(t *testing.T) {
	missing := filepath.Join(t.TempDir(), "absent.pem")
	_, err := externalECKeySigner(missing)
	require.Error(t, err)

	s := &externalKeySigner{path: missing}
	require.Nil(t, s.Public())
	sig, err := s.Sign(nil, make([]byte, 32), crypto.SHA256)
	require.Error(t, err)
	require.Nil(t, sig)
}
