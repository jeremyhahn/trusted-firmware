// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfwsim_test

import (
	"encoding/binary"
	"errors"
	"net"
	"strconv"
	"testing"
	"time"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/tfwsim"
)

// Mutation tests (#163): each one kills a fault the other tests let through.

// The listener and the accepted link are both closed when the test that
// opened them ends.
func TestStartElementEthernetClosesTheListenerAndLinkWhenItsTestEnds(t *testing.T) {
	record := fake(t)
	var eth *tfw.TCPDevice
	var port uint16
	t.Run("start", func(t *testing.T) {
		_, eth = tfwsim.StartElementEthernet(t)
		// The endpoint in the network block is the listener's port.
		image := eeprom(t, commandLine(t, record))
		port = binary.BigEndian.Uint16(image[tfw.NetConfigAddr+28 : tfw.NetConfigAddr+30])
	})
	if eth == nil {
		t.Fatal("StartElementEthernet returned no link")
	}
	if err := eth.Close(); !errors.Is(err, net.ErrClosed) {
		t.Errorf("closing the link after its test: %v, want it already closed", err)
	}
	if c, err := net.DialTimeout("tcp", net.JoinHostPort("127.0.0.1", strconv.Itoa(int(port))), time.Second); err == nil {
		_ = c.Close()
		t.Errorf("the listener on port %d still accepts after its test", port)
	}
}

// With no option of its own the Ethernet link's request timeout is the boot
// bound, not the SDK's longer default.
func TestStartElementEthernetBoundsARequestByTheBootTimeout(t *testing.T) {
	fake(t)
	_, eth := tfwsim.StartElementEthernet(t)
	took, err := timedPing(t, eth.Device)
	if !errors.Is(err, tfw.ErrTimeout) {
		t.Fatalf("ping of a silent link: %v, want %v", err, tfw.ErrTimeout)
	}
	if took > 8*time.Second {
		t.Fatalf("a silent link took %s to time out, want the 5s boot bound", took)
	}
}

// The caller's options reach the Ethernet link too, and its timeout wins.
func TestStartElementEthernetLetsTheCallersTimeoutWin(t *testing.T) {
	fake(t)
	_, eth := tfwsim.StartElementEthernet(t, tfw.WithTimeout(50*time.Millisecond))
	took, err := timedPing(t, eth.Device)
	if !errors.Is(err, tfw.ErrTimeout) {
		t.Fatalf("ping of a silent link: %v, want %v", err, tfw.ErrTimeout)
	}
	if took > 2*time.Second {
		t.Fatalf("a 50ms timeout took %s", took)
	}
}
