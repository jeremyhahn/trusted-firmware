package main

import (
	"testing"

	"github.com/stretchr/testify/require"
)

func TestParseHexOrEmpty(t *testing.T) {
	b, err := parseHexOrEmpty("-")
	require.NoError(t, err)
	require.Nil(t, b, "- is the empty payload")

	b, err = parseHexOrEmpty("0a0B")
	require.NoError(t, err)
	require.Equal(t, []byte{0x0A, 0x0B}, b)

	_, err = parseHexOrEmpty("zz")
	require.Error(t, err)
}
