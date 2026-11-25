// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfwsim_test

import (
	"os"
	"strings"
	"testing"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/tfwsim"
)

// The network block may sit anywhere from the end of the platform block
// (0x00C0) to the last 32 bytes of the 4 KiB EEPROM (0x0FE0). These tests pin
// both edges: the last legal block is written, and an answer one byte outside
// either edge is refused before a simulator starts.

func TestStartElementEthernetAcceptsTheLastBlockTheEEPROMHolds(t *testing.T) {
	const at = 0x0FE0
	record := fake(t)
	t.Setenv(netcfgAddrEnv, "0x0FE0")
	_, eth := tfwsim.StartElementEthernet(t)

	image := eeprom(t, commandLine(t, record))
	if len(image) != 4096 {
		t.Fatalf("EEPROM image is %d bytes, want the network block at %#x to end the 4 KiB EEPROM", len(image), at)
	}
	var cfg tfw.NetConfig
	if err := cfg.UnmarshalBinary(image[at:]); err != nil {
		t.Fatalf("the block at %#x: %v", at, err)
	}
	if eth == nil || !cfg.Endpoint.Addr().IsLoopback() {
		t.Fatalf("link %v, endpoint %v in the block at %#x, want a link to a loopback endpoint", eth, cfg.Endpoint, at)
	}
}

func TestStartElementEthernetRefusesAnAddressOneByteOutside(t *testing.T) {
	// 0x00BF overwrites the platform block's last byte; 0x0FE1 runs one byte
	// past the EEPROM.
	for _, answer := range []string{"0x00BF", "0x0FE1"} {
		record := fake(t)
		t.Setenv(netcfgAddrEnv, answer)
		f := &fatalTB{TB: t}
		done := make(chan struct{})
		go func() {
			defer close(done)
			tfwsim.StartElementEthernet(f)
		}()
		<-done
		if !strings.Contains(f.msg, "--netcfg-addr") || !strings.Contains(f.msg, "make tfwsim") {
			t.Errorf("answer %q: StartElementEthernet failed with %q, want it to name --netcfg-addr and make tfwsim",
				answer, f.msg)
		}
		if _, err := os.Stat(record); !os.IsNotExist(err) {
			t.Errorf("answer %q: a simulator was started (stat %v)", answer, err)
		}
	}
}
