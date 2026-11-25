package main

import (
	"bytes"
	"context"
	"net"
	"net/netip"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
)

// Every `netcfg set` flag refusal is parseNetcfgFlags' own: none needs a
// simulator or a device, and each returns the zero block and no operator.
func TestParseNetcfgFlagsRefusals(t *testing.T) {
	cases := []struct {
		name string
		args []string
		want []string
	}{
		{"unknown flag", []string{"--bogus"}, []string{"usage: netcfg set", "flag provided but not defined"}},
		{"stray argument", []string{"--mac", "02:00:00:00:00:01", "extra"}, []string{"usage: netcfg set"}},
		{"dhcp with ip", []string{"--dhcp", "--ip", "192.0.2.10"}, []string{"go without it"}},
		{"dhcp with mask", []string{"--dhcp", "--mask", "255.255.255.0"}, []string{"go without it"}},
		{"dhcp with gateway", []string{"--dhcp", "--gateway", "192.0.2.1"}, []string{"go without it"}},
		{"bad mac", []string{"--mac", "zz:00:00:00:00:01"}, []string{"--mac: "}},
		{"bad ip", []string{"--ip", "192.0.2.300"}, []string{"--ip: "}},
		{"bad mask", []string{"--mask", "nope"}, []string{"--mask: "}},
		{"bad gateway", []string{"--gateway", "192.0.2"}, []string{"--gateway: "}},
		{"endpoint without port", []string{"--endpoint", "192.0.2.10"}, []string{"--endpoint: "}},
		{"serial without key", []string{"--serial", "00"}, []string{"--serial and --operator-key go together"}},
		{"key without serial", []string{"--operator-key", "k.pem"}, []string{"--serial and --operator-key go together"}},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			c, op, err := parseNetcfgFlags(tc.args)
			require.Error(t, err)
			for _, w := range tc.want {
				assert.Contains(t, err.Error(), w)
			}
			assert.Equal(t, tfw.NetConfig{}, c)
			assert.Equal(t, netcfgOperator{}, op)
		})
	}
}

// --dhcp writes 0.0.0.0 into the address, mask and gateway, as the block
// reads back, not the zero netip.Addr.
func TestParseNetcfgFlagsDHCPReadsBackUnspecified(t *testing.T) {
	c, op, err := parseNetcfgFlags([]string{"--dhcp", "--mac", "02:00:00:00:00:01", "--endpoint", "198.51.100.20:7020"})
	require.NoError(t, err)
	z := netip.IPv4Unspecified()
	assert.True(t, c.DHCP)
	assert.Equal(t, z, c.IP)
	assert.Equal(t, z, c.Mask)
	assert.Equal(t, z, c.Gateway)
	assert.Equal(t, net.HardwareAddr{0x02, 0, 0, 0, 0, 0x01}, c.MAC)
	assert.Equal(t, netip.MustParseAddrPort("198.51.100.20:7020"), c.Endpoint)
	assert.Equal(t, netcfgOperator{}, op)
}

// A static block puts each flag in its own slot and carries the operator.
func TestParseNetcfgFlagsStaticAndOperator(t *testing.T) {
	c, op, err := parseNetcfgFlags([]string{
		"--mac", "02:00:00:00:00:01",
		"--ip", "192.0.2.10",
		"--mask", "255.255.255.0",
		"--gateway", "192.0.2.1",
		"--endpoint", "198.51.100.20:7020",
		"--serial", "0011",
		"--operator-key", "k.pem",
	})
	require.NoError(t, err)
	assert.False(t, c.DHCP)
	assert.Equal(t, net.HardwareAddr{0x02, 0, 0, 0, 0, 0x01}, c.MAC)
	assert.Equal(t, netip.MustParseAddr("192.0.2.10"), c.IP)
	assert.Equal(t, netip.MustParseAddr("255.255.255.0"), c.Mask)
	assert.Equal(t, netip.MustParseAddr("192.0.2.1"), c.Gateway)
	assert.Equal(t, netip.MustParseAddrPort("198.51.100.20:7020"), c.Endpoint)
	assert.Equal(t, "0011", op.serial)
	assert.Equal(t, "k.pem", op.key)
}

// runNetcfg refuses bad arguments before it touches the device: a nil
// device does not panic and nothing is written.
func TestRunNetcfgRefusesBeforeTheDevice(t *testing.T) {
	cases := []struct {
		name string
		args []string
	}{
		{"set serial without key", []string{"set", "--serial", "00"}},
		{"set dhcp with ip", []string{"set", "--dhcp", "--ip", "192.0.2.10"}},
		{"get with argument", []string{"get", "x"}},
		{"unknown subcommand", []string{"bogus"}},
		{"no arguments", nil},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			var buf bytes.Buffer
			require.NotPanics(t, func() {
				assert.Error(t, runNetcfg(context.Background(), nil, tc.args, false, &buf))
			})
			assert.Zero(t, buf.Len())
		})
	}
}
