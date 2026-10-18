// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build tfwsim

package main

import (
	"bytes"
	"context"
	"encoding/hex"
	"net"
	"os"
	"path/filepath"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/tfwsim"
)

// Issue #53 open item T2, end to end: an Ethernet build with no network block
// is given one by `netcfg set` over serial, and then dials the endpoint it was
// written. No reboot comes between: the firmware's link reads the block on
// every dial attempt while it is down (src/proto/link_eth.c tfw_link_poll),
// and the simulator's EEPROM lives as long as its process, so a restart would
// start from the image it was given, not from what was written.
func TestE2ENetcfgSetPointsTheDeviceAtTheWrittenEndpoint(t *testing.T) {
	a, err := net.ResolveTCPAddr("tcp", "127.0.0.1:0")
	require.NoError(t, err)
	l, err := net.ListenTCP("tcp", a)
	require.NoError(t, err)
	t.Cleanup(func() { _ = l.Close() })

	// The platform block alone: the network block after it stays erased.
	platform := tfw.BuildPlatformBlock(tfwsim.TestSerial, [tfw.PubkeyHashLen]byte{}, [tfw.OperatorPubkeyLen]byte{})
	eep := filepath.Join(t.TempDir(), "eeprom.bin")
	require.NoError(t, os.WriteFile(eep, platform[:], 0o600))
	serial := tfwsim.StartElementWith(t, []string{"--eth", "--eeprom-bin", eep,
		"--provision-element", "--image-hash", hex.EncodeToString(tfwsim.TestFWHash[:])})

	var nerr *tfw.NetConfigError
	err = run(context.Background(), serial, "netcfg", []string{"get"}, false, &bytes.Buffer{})
	require.ErrorAs(t, err, &nerr, "netcfg get before any block was written")

	var set, got map[string]string
	cliJSON(t, serial, &set, "netcfg", "set", "--mac", "02:00:00:00:00:01", "--ip", "192.0.2.10",
		"--mask", "255.255.255.0", "--gateway", "192.0.2.1", "--endpoint", l.Addr().String())

	td, err := acceptDevice(l, deviceWait, tfw.WithTimeout(10*time.Second))
	require.NoError(t, err, "the device never dialled the endpoint netcfg set wrote")
	t.Cleanup(func() { _ = td.Close() })
	cliCaps(t, td.Device)

	cliJSON(t, serial, &got, "netcfg", "get")
	assert.Equal(t, set, got, "netcfg get after netcfg set")
	assert.Equal(t, l.Addr().String(), got["endpoint"])
}
