// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build tfwsim

package main

import (
	"bytes"
	"context"
	"net"
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
//
// Simulator-only: it needs a device whose network block is erased, which is an
// image the simulator is handed, and on a board `netcfg set` would overwrite
// the block the board was provisioned with.
func TestE2ENetcfgSetPointsTheDeviceAtTheWrittenEndpoint(t *testing.T) {
	a, err := net.ResolveTCPAddr("tcp", "127.0.0.1:0")
	require.NoError(t, err)
	l, err := net.ListenTCP("tcp", a)
	require.NoError(t, err)
	t.Cleanup(func() { _ = l.Close() })

	// The platform block alone: the network block after it stays erased. The
	// element tier writes it only in an operator auth session, so the
	// operator key is provisioned first (the network peer-binding ADR).
	pub, key := writeOperatorKey(t)
	serial := tfwsim.StartElementWith(t, append([]string{"--eth"}, elementArgs(t, pub, nil)...))

	var nerr *tfw.NetConfigError
	err = run(context.Background(), serial, "netcfg", []string{"get"}, false, &bytes.Buffer{})
	require.ErrorAs(t, err, &nerr, "netcfg get before any block was written")

	var set, got map[string]any
	cliJSON(t, serial, &set, "netcfg", "set", "--mac", "02:00:00:00:00:01", "--ip", "192.0.2.10",
		"--mask", "255.255.255.0", "--gateway", "192.0.2.1", "--endpoint", l.Addr().String(),
		"--serial", testSerialHex, "--operator-key", key)

	td, err := acceptDevice(l, deviceWait, tfw.WithTimeout(10*time.Second))
	require.NoError(t, err, "the device never dialled the endpoint netcfg set wrote")
	t.Cleanup(func() { _ = td.Close() })
	cliCaps(t, td.Device)

	cliJSON(t, serial, &got, "netcfg", "get")
	assert.Equal(t, set, got, "netcfg get after netcfg set")
	assert.Equal(t, l.Addr().String(), got["endpoint"])
}

// The block netcfg set --dhcp writes survives the firmware's EEPROM and reads
// back as a DHCP block with no address of its own. tfwsim links the DHCP
// client's stand-in, so the device does not lease and does not dial it.
func TestE2ENetcfgSetDHCPRoundTripsThroughTheDevice(t *testing.T) {
	pub, key := writeOperatorKey(t)
	serial := tfwsim.StartElementWith(t, append([]string{"--eth"}, elementArgs(t, pub, nil)...))

	var set, got map[string]any
	cliJSON(t, serial, &set, "netcfg", "set", "--dhcp", "--mac", "02:00:00:00:00:01",
		"--endpoint", "198.51.100.20:7020", "--serial", testSerialHex, "--operator-key", key)
	cliJSON(t, serial, &got, "netcfg", "get")
	assert.Equal(t, set, got, "netcfg get after netcfg set --dhcp")
	assert.Equal(t, true, got["dhcp"])
	assert.Equal(t, "0.0.0.0", got["ip"])
	assert.Equal(t, "198.51.100.20:7020", got["endpoint"])
}

// TestE2ENetcfgSetWithoutTheOperatorKeyIsRefused pins the #233 gate from the
// host side: on the element tier an EEPROM_WRITE to the network block outside
// an operator auth session answers TFW_ERR_AUTH_REQUIRED, tfwctl names the
// missing input in one line, and the block stays as it was.
func TestE2ENetcfgSetWithoutTheOperatorKeyIsRefused(t *testing.T) {
	pub, _ := writeOperatorKey(t)
	serial := tfwsim.StartElementWith(t, append([]string{"--eth"}, elementArgs(t, pub, nil)...))

	err := run(context.Background(), serial, "netcfg", []string{"set", "--dhcp", "--mac", "02:00:00:00:00:01",
		"--endpoint", "198.51.100.20:7020"}, false, &bytes.Buffer{})
	var st tfw.Status
	require.ErrorAs(t, err, &st, "netcfg set without an operator auth session")
	assert.Equal(t, tfw.StatusErrAuthRequired, st)
	assert.Contains(t, err.Error(), "--operator-key")

	var nerr *tfw.NetConfigError
	err = run(context.Background(), serial, "netcfg", []string{"get"}, false, &bytes.Buffer{})
	require.ErrorAs(t, err, &nerr, "the refused write left the network block erased")
}
