// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package main

import (
	"context"
	"flag"
	"fmt"
	"io"
	"net"
	"net/netip"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
)

const netcfgUsage = "usage: netcfg set --mac <mac> (--ip <addr> --mask <addr> --gateway <addr> | --dhcp) --endpoint <addr:port> | netcfg get"

// runNetcfg is `netcfg set` and `netcfg get`: the network block an Ethernet
// build reads at boot, written and read back over this link.
//
// The block is checked by the SDK alone. tfwctl turns each flag's text into
// the type NetConfig holds and nothing more: a flag left out stays the zero
// value, and NetConfig's refusal of it is the error printed, so the CLI and a
// Go caller of WriteNetConfig refuse exactly the same blocks.
func runNetcfg(ctx context.Context, dev *tfw.Device, args []string, asJSON bool, out io.Writer) error {
	if len(args) == 0 {
		return fmt.Errorf("%s", netcfgUsage)
	}
	switch args[0] {
	case "set":
		c, err := parseNetcfgFlags(args[1:])
		if err != nil {
			return err
		}
		if err := dev.WriteNetConfig(ctx, c); err != nil {
			return err
		}
		return emitNetcfg(out, asJSON, c, "wrote the network block; the device dials with it from its next boot or link retry\n")
	case "get":
		if len(args) != 1 {
			return fmt.Errorf("%s", netcfgUsage)
		}
		c, err := dev.ReadNetConfig(ctx)
		if err != nil {
			return err
		}
		return emitNetcfg(out, asJSON, c, "")
	default:
		return fmt.Errorf("%s", netcfgUsage)
	}
}

func parseNetcfgFlags(args []string) (tfw.NetConfig, error) {
	fs := flag.NewFlagSet("netcfg set", flag.ContinueOnError)
	fs.SetOutput(io.Discard)
	mac := fs.String("mac", "", "the interface's MAC")
	ip := fs.String("ip", "", "the device's IPv4 address")
	mask := fs.String("mask", "", "the subnet mask")
	gateway := fs.String("gateway", "", "the default gateway")
	endpoint := fs.String("endpoint", "", "the host:port the device dials")
	dhcp := fs.Bool("dhcp", false, "lease the address, mask and gateway by DHCP")
	if err := fs.Parse(args); err != nil {
		return tfw.NetConfig{}, fmt.Errorf("%s: %v", netcfgUsage, err)
	}
	if fs.NArg() != 0 {
		return tfw.NetConfig{}, fmt.Errorf("%s", netcfgUsage)
	}

	var c tfw.NetConfig
	if *dhcp {
		if *ip != "" || *mask != "" || *gateway != "" {
			return tfw.NetConfig{}, fmt.Errorf("--dhcp leases the address, mask and gateway; --ip, --mask and --gateway go without it")
		}
		// The block carries no address of its own: 0.0.0.0, as it reads back.
		z := netip.IPv4Unspecified()
		c = tfw.NetConfig{DHCP: true, IP: z, Mask: z, Gateway: z}
	}
	var err error
	if *mac != "" {
		if c.MAC, err = net.ParseMAC(*mac); err != nil {
			return tfw.NetConfig{}, fmt.Errorf("--mac: %w", err)
		}
	}
	for _, a := range []struct {
		name string
		text string
		dst  *netip.Addr
	}{
		{"ip", *ip, &c.IP},
		{"mask", *mask, &c.Mask},
		{"gateway", *gateway, &c.Gateway},
	} {
		if a.text == "" {
			continue
		}
		if *a.dst, err = netip.ParseAddr(a.text); err != nil {
			return tfw.NetConfig{}, fmt.Errorf("--%s: %w", a.name, err)
		}
	}
	if *endpoint != "" {
		if c.Endpoint, err = netip.ParseAddrPort(*endpoint); err != nil {
			return tfw.NetConfig{}, fmt.Errorf("--endpoint: %w", err)
		}
	}
	return c, nil
}

func emitNetcfg(out io.Writer, asJSON bool, c tfw.NetConfig, note string) error {
	return emit(out, asJSON, map[string]any{
		"dhcp": c.DHCP, "mac": c.MAC.String(), "ip": c.IP.String(), "mask": c.Mask.String(),
		"gateway": c.Gateway.String(), "endpoint": c.Endpoint.String(),
	}, func() string {
		return fmt.Sprintf("%sdhcp     %t\nmac      %s\nip       %s\nmask     %s\ngateway  %s\nendpoint %s\n",
			note, c.DHCP, c.MAC, c.IP, c.Mask, c.Gateway, c.Endpoint)
	})
}
