// Copyright (c) 2026 Jeremy Hahn
// Copyright (c) 2026 Automate The Things, LLC
//
// This file is part of trusted-firmware.

package avr

import (
	"go/parser"
	"go/token"
	"path/filepath"
	"strconv"
	"strings"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// opensALinkOfItsOwn reports whether an import would let this package reach
// the device other than through sdk/go: the network, or a serial port.
func opensALinkOfItsOwn(path string) bool {
	return path == "net" || strings.HasPrefix(path, "net/") ||
		strings.Contains(strings.ToLower(path), "serial")
}

// The backend reaches the wire only through sdk/go: no non-test file of this
// package imports the network or a serial package, so the only link it can
// talk over is the one the caller's tfw.Device was opened on.
func TestPackageReachesTheWireOnlyThroughSDK(t *testing.T) {
	files, err := filepath.Glob("*.go")
	require.NoError(t, err)

	var checked int
	fset := token.NewFileSet()
	for _, name := range files {
		if strings.HasSuffix(name, "_test.go") {
			continue
		}
		f, err := parser.ParseFile(fset, name, nil, parser.ImportsOnly)
		require.NoError(t, err, name)
		checked++
		for _, imp := range f.Imports {
			path, err := strconv.Unquote(imp.Path.Value)
			require.NoError(t, err, name)
			assert.False(t, opensALinkOfItsOwn(path),
				"%s imports %q, which opens a link of its own; reach the device through sdk/go", name, path)
		}
	}
	require.NotZero(t, checked, "no non-test files parsed")
}

func TestOpensALinkOfItsOwnNamesTheNetworkAndSerialPorts(t *testing.T) {
	for path, want := range map[string]bool{
		"net":                    true,
		"net/http":               true,
		"go.bug.st/serial":       true,
		"github.com/tarm/serial": true,
		"github.com/x/goSerial":  true,
		"network":                false,
		"context":                false,
		"crypto/x509":            false,
		"github.com/jeremyhahn/trusted-firmware/sdk/go": false,
	} {
		assert.Equal(t, want, opensALinkOfItsOwn(path), path)
	}
}
