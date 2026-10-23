// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package peer

import (
	"bytes"
	"errors"
	"io"
	"io/fs"
	"net/netip"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func TestFromEnvDefaultsToTheSimulatorsBoard(t *testing.T) {
	t.Setenv(PortEnv, "")
	t.Setenv(BoardEnv, "")
	t.Setenv(EthEnv, "")
	t.Setenv(EthListenEnv, "")
	got, err := FromEnv()
	if err != nil {
		t.Fatal(err)
	}
	if got.Hardware() || got.Board != SimBoard {
		t.Fatalf("target = %+v, want the simulator on %s", got, SimBoard)
	}
}

func TestFromEnvNamesTheBoardOnThePort(t *testing.T) {
	t.Setenv(PortEnv, "/dev/ttyACM0")
	t.Setenv(BoardEnv, "atmega2560")
	t.Setenv(EthEnv, "")
	t.Setenv(EthListenEnv, "")
	got, err := FromEnv()
	if err != nil {
		t.Fatal(err)
	}
	if !got.Hardware() || got.Port != "/dev/ttyACM0" || got.Board != "atmega2560" {
		t.Fatalf("target = %+v, want atmega2560 on /dev/ttyACM0", got)
	}
}

func TestFromEnvRefusesHalfATarget(t *testing.T) {
	for _, c := range []struct{ port, board, eth, want string }{
		{"/dev/ttyACM0", "", "", BoardEnv + " names no board"},
		{"", "atmega2560", "", PortEnv + " names no port"},
		{"", "", "192.0.2.1:5000", PortEnv + " names no port"},
		{"", "atmega2560", "192.0.2.1:5000", PortEnv + " names no port"},
	} {
		t.Setenv(PortEnv, c.port)
		t.Setenv(BoardEnv, c.board)
		t.Setenv(EthEnv, c.eth)
		t.Setenv(EthListenEnv, "")
		got, err := FromEnv()
		if err == nil {
			t.Errorf("port %q board %q eth %q: target = %+v, want an error", c.port, c.board, c.eth, got)
		} else if !strings.Contains(err.Error(), c.want) {
			t.Errorf("port %q board %q eth %q: err = %v, want it to say %q", c.port, c.board, c.eth, err, c.want)
		}
	}
}

func TestFromEnvNamesTheBoardsEthernetAddress(t *testing.T) {
	t.Setenv(PortEnv, "/dev/ttyACM0")
	t.Setenv(BoardEnv, "atmega2560")
	t.Setenv(EthEnv, "192.0.2.1:5000")
	t.Setenv(EthListenEnv, "")
	got, err := FromEnv()
	if err != nil {
		t.Fatal(err)
	}
	if got != (Target{Port: "/dev/ttyACM0", Board: "atmega2560", Eth: "192.0.2.1:5000"}) {
		t.Fatalf("target = %+v, want atmega2560 on /dev/ttyACM0 dialing 192.0.2.1:5000", got)
	}
}

func TestFromEnvNamesTheListenAddressApart(t *testing.T) {
	t.Setenv(PortEnv, "/dev/ttyACM0")
	t.Setenv(BoardEnv, "atmega2560")
	t.Setenv(EthEnv, "192.0.2.1:7020")
	t.Setenv(EthListenEnv, "0.0.0.0:7020")
	got, err := FromEnv()
	if err != nil {
		t.Fatal(err)
	}
	if got != (Target{Port: "/dev/ttyACM0", Board: "atmega2560", Eth: "192.0.2.1:7020", EthListen: "0.0.0.0:7020"}) {
		t.Fatalf("target = %+v, want 192.0.2.1:7020 dialed and 0.0.0.0:7020 listened on", got)
	}
}

func TestFromEnvRefusesAListenAddressWithNothingToListenFor(t *testing.T) {
	for _, c := range []struct{ port, board, eth string }{
		{"", "", ""},
		{"", "", "192.0.2.1:7020"},
		{"/dev/ttyACM0", "atmega2560", ""},
	} {
		t.Setenv(PortEnv, c.port)
		t.Setenv(BoardEnv, c.board)
		t.Setenv(EthEnv, c.eth)
		t.Setenv(EthListenEnv, "0.0.0.0:7020")
		if got, err := FromEnv(); err == nil {
			t.Errorf("port %q board %q eth %q: target = %+v, want an error", c.port, c.board, c.eth, got)
		}
	}
}

func TestListenIsALoopbackPortForTheSimulator(t *testing.T) {
	// EthEnv is the board's; the simulator never listens there.
	got, err := Target{Board: SimBoard, Eth: "192.0.2.1:5000"}.Listen()
	if err != nil || got != SimListen {
		t.Fatalf("Listen = %q, %v; want %q", got, err, SimListen)
	}
}

func TestListenDefaultsToTheAddressTheBoardDials(t *testing.T) {
	for _, eth := range []string{"192.0.2.1:5000", "[2001:db8::1]:5000"} {
		got, err := Target{Port: "/dev/ttyACM0", Board: "atmega2560", Eth: eth}.Listen()
		if err != nil || got != eth {
			t.Errorf("Listen = %q, %v; want %q", got, err, eth)
		}
	}
}

func TestListenIsTheListenAddressWhenOneIsNamed(t *testing.T) {
	// Behind a published port: the board dials the bench host's address, the
	// suite listens on a wildcard or on its container's own address.
	for _, listen := range []string{"0.0.0.0:7020", "198.51.100.2:7020", "[::]:7020"} {
		got, err := Target{Port: "/dev/ttyACM0", Board: "atmega2560", Eth: "192.0.2.1:7020", EthListen: listen}.Listen()
		if err != nil || got != listen {
			t.Errorf("Listen = %q, %v; want %q", got, err, listen)
		}
	}
}

func TestListenRefusesABoardWithNoUsableAddress(t *testing.T) {
	for _, eth := range []string{"", "192.0.2.1", "192.0.2.1:0", "board.invalid:5000", "192.0.2.1:http", "0.0.0.0:5000"} {
		got, err := Target{Port: "/dev/ttyACM0", Board: "atmega2560", Eth: eth}.Listen()
		if err == nil {
			t.Errorf("eth %q: Listen = %q, want an error", eth, got)
		}
	}
}

func TestListenRefusesAnUnusableListenAddress(t *testing.T) {
	for _, listen := range []string{"0.0.0.0", "0.0.0.0:0", "198.51.100.2:0", "suite.invalid:7020", "0.0.0.0:http"} {
		got, err := Target{Port: "/dev/ttyACM0", Board: "atmega2560", Eth: "192.0.2.1:7020", EthListen: listen}.Listen()
		if err == nil || !strings.Contains(err.Error(), EthListenEnv) {
			t.Errorf("listen %q: Listen = %q, %v; want an error naming %s", listen, got, err, EthListenEnv)
		}
	}
}

func TestEndpointIsTheAddressTheBoardDials(t *testing.T) {
	want := netip.MustParseAddrPort("192.0.2.1:7020")
	for _, listen := range []string{"", "0.0.0.0:7020"} {
		got, err := Target{Port: "/dev/ttyACM0", Board: "atmega2560", Eth: "192.0.2.1:7020", EthListen: listen}.Endpoint(netip.MustParseAddrPort("[::]:7020"))
		if err != nil || got != want {
			t.Errorf("listen %q: Endpoint = %s, %v; want %s", listen, got, err, want)
		}
	}
}

func TestEndpointIsTheBoundAddressForTheSimulator(t *testing.T) {
	bound := netip.MustParseAddrPort("127.0.0.1:41000")
	got, err := Target{Board: SimBoard}.Endpoint(bound)
	if err != nil || got != bound {
		t.Fatalf("Endpoint = %s, %v; want %s", got, err, bound)
	}
}

func TestEndpointRefusesAWildcardAndNamesTheListenVariable(t *testing.T) {
	// The defect of #171: TFW_HW_ETH=0.0.0.0:7020 wrote [::]:7020 into the
	// network block, which a board cannot dial.
	_, err := Target{Port: "/dev/ttyACM0", Board: "atmega2560", Eth: "0.0.0.0:7020"}.Endpoint(netip.AddrPort{})
	if err == nil || !strings.Contains(err.Error(), EthListenEnv) {
		t.Fatalf("err = %v, want a refusal naming %s", err, EthListenEnv)
	}
}

func TestOpenFailsOnAMissingPort(t *testing.T) {
	missing := filepath.Join(t.TempDir(), "ttyNONE")
	c, err := Open(missing, time.Second)
	if err == nil {
		_ = c.Close()
		t.Fatal("a port that does not exist was opened")
	}
	if !errors.Is(err, fs.ErrNotExist) {
		t.Fatalf("err = %v, want fs.ErrNotExist", err)
	}
}

// fakePort stands in for a serial port: Read returns (0, nil) while it has
// nothing, as serialport.Open's read timeout does, until silent reads have
// passed. Each of those returns at once: the silent reads are counted, and a
// port with nothing at all is bounded by the caller's own deadline.
type fakePort struct {
	silent  int
	data    []byte
	err     error
	written bytes.Buffer
	closed  bool
}

func (f *fakePort) Read(p []byte) (int, error) {
	if f.silent > 0 {
		f.silent--
		return 0, nil
	}
	if f.err != nil {
		return 0, f.err
	}
	if len(f.data) == 0 {
		return 0, nil
	}
	n := copy(p, f.data)
	f.data = f.data[n:]
	return n, nil
}

func (f *fakePort) Write(p []byte) (int, error) { return f.written.Write(p) }
func (f *fakePort) Close() error                { f.closed = true; return nil }

func TestAwaitStartKeepsTheFirstTransmissionForTheCaller(t *testing.T) {
	boot := []byte{0xA5, 0x03, 0x00, 0x80, 0x01}
	f := &fakePort{silent: 3, data: append([]byte(nil), boot...)}
	c, err := awaitStart(f, time.Second)
	if err != nil {
		t.Fatal(err)
	}
	// The wait read the BOOT bytes; the caller must still see every one.
	got := make([]byte, 2)
	var all []byte
	for len(all) < len(boot) {
		n, err := c.Read(got)
		if err != nil {
			t.Fatal(err)
		}
		all = append(all, got[:n]...)
	}
	if !bytes.Equal(all, boot) {
		t.Fatalf("read %x after the wait, want %x", all, boot)
	}
	if _, err := c.Write([]byte{1, 2}); err != nil || !bytes.Equal(f.written.Bytes(), []byte{1, 2}) {
		t.Fatalf("write err = %v, port saw %x; want 0102 passed through", err, f.written.Bytes())
	}
	if err := c.Close(); err != nil || !f.closed {
		t.Fatalf("close err = %v, port closed = %v", err, f.closed)
	}
}

// tfw.Device purges any conn offering ResetInputBuffer before it waits for
// BOOT; the started conn must not offer it, or the purge would discard the very
// event the wait kept.
func TestAwaitStartOffersNoPurge(t *testing.T) {
	c, err := awaitStart(&fakePort{data: []byte{0xA5}}, time.Second)
	if err != nil {
		t.Fatal(err)
	}
	if _, ok := c.(interface{ ResetInputBuffer() error }); ok {
		t.Fatal("the started conn offers ResetInputBuffer")
	}
}

func TestAwaitStartFailsOnASilentBoard(t *testing.T) {
	f := &fakePort{}
	start := time.Now()
	_, err := awaitStart(f, 20*time.Millisecond)
	if !errors.Is(err, ErrSilent) {
		t.Fatalf("err = %v, want %v", err, ErrSilent)
	}
	if took := time.Since(start); took > time.Second {
		t.Fatalf("a 20ms bound took %s", took)
	}
	if len(f.written.Bytes()) != 0 {
		t.Fatalf("wrote %x to a board that had not started", f.written.Bytes())
	}
}

// resettablePort is a fakePort that also drives DTR and purges, recording the
// order it was asked to.
type resettablePort struct {
	fakePort
	calls    []string
	dtrErr   error
	raiseErr error // raising DTR only
	purgeErr error
}

func (r *resettablePort) SetDTR(on bool) error {
	if on {
		r.calls = append(r.calls, "dtr-on")
		if r.raiseErr != nil {
			return r.raiseErr
		}
	} else {
		r.calls = append(r.calls, "dtr-off")
	}
	return r.dtrErr
}

func (r *resettablePort) ResetInputBuffer() error {
	r.calls = append(r.calls, "purge")
	// A purge discards whatever the old application sent before the reset.
	r.data = nil
	return r.purgeErr
}

func TestStartResetsTheBoardThenWaitsForItsFirstTransmission(t *testing.T) {
	r := &resettablePort{fakePort: fakePort{data: []byte("stale")}}
	// The new application's BOOT arrives after the purge, and after a while.
	r.silent = 2
	c, err := start(&bootAfterPurge{r, []byte{0xA5, 0x01}}, time.Second)
	if err != nil {
		t.Fatal(err)
	}
	want := []string{"dtr-off", "dtr-on", "purge"}
	if len(r.calls) != len(want) {
		t.Fatalf("port calls = %v, want %v", r.calls, want)
	}
	for i := range want {
		if r.calls[i] != want[i] {
			t.Fatalf("port calls = %v, want %v", r.calls, want)
		}
	}
	got := make([]byte, 8)
	n, err := c.Read(got)
	if err != nil || !bytes.Equal(got[:n], []byte{0xA5, 0x01}) {
		t.Fatalf("first read = %x, %v; want the new application's a501", got[:n], err)
	}
	if len(r.written.Bytes()) != 0 {
		t.Fatalf("wrote %x before the board had started", r.written.Bytes())
	}
}

// bootAfterPurge delivers boot once the purge has happened, as a board does:
// its application starts after the bootloader, long after the reset.
type bootAfterPurge struct {
	*resettablePort
	boot []byte
}

func (b *bootAfterPurge) ResetInputBuffer() error {
	err := b.resettablePort.ResetInputBuffer()
	b.data = b.boot
	return err
}

func TestStartFailsOnAPortThatCannotReset(t *testing.T) {
	_, err := start(&fakePort{data: []byte{0xA5}}, time.Second)
	if err == nil || !strings.Contains(err.Error(), "cannot drive DTR") {
		t.Fatalf("err = %v, want the no-DTR refusal", err)
	}
}

func TestStartReportsAFailedReset(t *testing.T) {
	boom := errors.New("boom")
	for _, r := range []*resettablePort{{dtrErr: boom}, {raiseErr: boom}, {purgeErr: boom}} {
		if _, err := start(r, time.Second); !errors.Is(err, boom) {
			t.Errorf("err = %v, want it to wrap %v", err, boom)
		}
	}
}

func TestAwaitStartFailsOnAReadError(t *testing.T) {
	_, err := awaitStart(&fakePort{err: io.ErrUnexpectedEOF}, time.Second)
	if !errors.Is(err, io.ErrUnexpectedEOF) {
		t.Fatalf("err = %v, want it to wrap %v", err, io.ErrUnexpectedEOF)
	}
}
