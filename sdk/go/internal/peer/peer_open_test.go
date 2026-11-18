package peer

import (
	"encoding/json"
	"errors"
	"io"
	"net"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/simproc"
)

// stubOpenPort makes openPort hand back port (or err) for the test's span.
func stubOpenPort(t *testing.T, port io.ReadWriteCloser, err error) {
	t.Helper()
	restore := openPort
	t.Cleanup(func() { openPort = restore })
	openPort = func(string, int) (io.ReadWriteCloser, error) { return port, err }
}

// resettingQMP is a QMP server that takes the reset and sends its event.
func resettingQMP(t *testing.T) string {
	t.Helper()
	return fakeQMP(t, func(c net.Conn, dec *json.Decoder) {
		_, _ = c.Write([]byte(qmpGreeting))
		expect(dec)
		_, _ = c.Write([]byte(`{"return": {}}` + "\n"))
		expect(dec)
		_, _ = c.Write([]byte(`{"return": {}}` + "\n"))
		_, _ = c.Write([]byte(`{"timestamp": {"seconds": 1, "microseconds": 3}, "event": "RESET"}` + "\n"))
	})
}

func TestOpenCmdAndOpenQMP(t *testing.T) {
	const port = "ttyBOARD"
	absent := func(t *testing.T) string { return filepath.Join(t.TempDir(), "absent") }
	tests := []struct {
		name    string
		openErr error
		purge   error
		open    func(t *testing.T) (io.ReadWriteCloser, error)
		wantErr string // "" for success
	}{
		{
			name:    "cmd: the port does not open",
			openErr: errors.New("no such port"),
			open:    func(*testing.T) (io.ReadWriteCloser, error) { return OpenCmd(port, []string{"true"}, time.Second) },
			wantErr: "no such port",
		},
		{
			name:    "cmd: the reset command fails",
			open:    func(*testing.T) (io.ReadWriteCloser, error) { return OpenCmd(port, []string{"false"}, time.Second) },
			wantErr: "resetting the board",
		},
		{
			name: "cmd: the board boots",
			open: func(*testing.T) (io.ReadWriteCloser, error) { return OpenCmd(port, []string{"true"}, time.Second) },
		},
		{
			name:    "qmp: the port does not open",
			openErr: errors.New("no such port"),
			open: func(t *testing.T) (io.ReadWriteCloser, error) {
				return OpenQMP(port, absent(t), time.Second)
			},
			wantErr: "no such port",
		},
		{
			name: "qmp: nothing listens on the socket",
			open: func(t *testing.T) (io.ReadWriteCloser, error) {
				return OpenQMP(port, absent(t), time.Second)
			},
			wantErr: ResetEnv,
		},
		{
			name:  "qmp: the purge fails",
			purge: errors.New("purge refused"),
			open: func(t *testing.T) (io.ReadWriteCloser, error) {
				return OpenQMP(port, absent(t), time.Second)
			},
			wantErr: "purging the receive buffer",
		},
		{
			name: "qmp: the board boots",
			open: func(t *testing.T) (io.ReadWriteCloser, error) {
				return OpenQMP(port, resettingQMP(t), time.Second)
			},
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			r := &resettablePort{purgeErr: tt.purge}
			var p io.ReadWriteCloser = &bootAfterPurge{r, []byte{0xA5, 0x01}}
			if tt.openErr != nil {
				p = nil
			}
			stubOpenPort(t, p, tt.openErr)
			c, err := tt.open(t)
			if tt.wantErr != "" {
				if err == nil || !strings.Contains(err.Error(), tt.wantErr) {
					t.Fatalf("err = %v, want it to mention %q", err, tt.wantErr)
				}
				if tt.openErr == nil {
					if !strings.Contains(err.Error(), port) {
						t.Fatalf("err = %v, want it to name the port %s", err, port)
					}
					if !r.closed {
						t.Fatal("the port was left open after a failed start")
					}
				}
				return
			}
			if err != nil {
				t.Fatal(err)
			}
			got := make([]byte, 8)
			n, err := c.Read(got)
			if err != nil || string(got[:n]) != "\xa5\x01" {
				t.Fatalf("first read = %x, %v; want the BOOT event a501", got[:n], err)
			}
		})
	}
}

// A tcp: port with a reset command (make hw-test BOARD=stm32h573i-dk
// PORT=tcp:<board>:21574 HW_RESET=cmd:...) runs the command, then dials the
// board over TCP: the serial opener is never asked for the tcp: address.
func TestStartOnATCPPortResetsThenDials(t *testing.T) {
	stubOpenPort(t, nil, errors.New("the serial opener was asked for a tcp: port"))
	l, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer func() { _ = l.Close() }()
	mark := filepath.Join(t.TempDir(), "reset")
	resetFirst := make(chan bool, 1)
	go func() {
		c, err := l.Accept()
		if err != nil {
			resetFirst <- false
			return
		}
		_, statErr := os.Stat(mark)
		resetFirst <- statErr == nil
		_, _ = c.Write([]byte{0xA5})
		_, _ = io.Copy(io.Discard, c)
	}()
	t.Setenv(PortEnv, TCPPrefix+l.Addr().String())
	t.Setenv(BoardEnv, "stm32h573i-dk")
	t.Setenv(ResetEnv, "cmd:touch "+mark)
	c := Start(t, simproc.Binary{})
	if !<-resetFirst {
		t.Fatal("the board was dialled before its reset command ran")
	}
	got := make([]byte, 1)
	if n, err := c.Read(got); err != nil || n != 1 || got[0] != 0xA5 {
		t.Fatalf("first read = %x, %v; want the board's a5", got[:n], err)
	}
}

// A board still booting after its reset refuses the first dials: they are
// made again until it answers, within the bound.
func TestOpenCmdRedialsATCPBoardStillBooting(t *testing.T) {
	saved, pause := dialTCP, tcpRedialPause
	t.Cleanup(func() { dialTCP, tcpRedialPause = saved, pause })
	tcpRedialPause = time.Millisecond
	dials := 0
	a, b := net.Pipe()
	t.Cleanup(func() { _ = a.Close(); _ = b.Close() })
	dialTCP = func(string, string, time.Duration) (io.ReadWriteCloser, error) {
		dials++
		if dials < 3 {
			return nil, errors.New("connection refused")
		}
		return nopDeadline{a}, nil
	}
	c, err := OpenCmd(TCPPrefix+"board:21574", []string{"true"}, time.Second)
	if err != nil {
		t.Fatalf("OpenCmd: %v", err)
	}
	_ = c.Close()
	if dials != 3 {
		t.Fatalf("dials = %d, want 3", dials)
	}
}

// A failed reset command, or a board that never answers, is reported with
// the port; a failed reset dials nothing.
func TestOpenCmdOnATCPPortReportsItsFailures(t *testing.T) {
	saved, pause := dialTCP, tcpRedialPause
	t.Cleanup(func() { dialTCP, tcpRedialPause = saved, pause })
	tcpRedialPause = time.Millisecond
	dials := 0
	dialTCP = func(string, string, time.Duration) (io.ReadWriteCloser, error) {
		dials++
		return nil, errors.New("connection refused")
	}
	const port = TCPPrefix + "board:21574"
	if _, err := OpenCmd(port, []string{"false"}, time.Second); err == nil ||
		!strings.Contains(err.Error(), "resetting the board") || !strings.Contains(err.Error(), port) {
		t.Fatalf("err = %v, want the failed reset on %s", err, port)
	}
	if dials != 0 {
		t.Fatalf("a failed reset dialled %d times", dials)
	}
	if _, err := OpenCmd(port, []string{"true"}, 20*time.Millisecond); err == nil ||
		!strings.Contains(err.Error(), "connection refused") || !strings.Contains(err.Error(), port) {
		t.Fatalf("err = %v, want the last dial's refusal on %s", err, port)
	}
}

// nopDeadline is a connection that is not a net.Conn, so openTCP takes it
// as it is.
type nopDeadline struct{ io.ReadWriteCloser }

func TestStartCmdAndStartQMPNeedAPurgeablePort(t *testing.T) {
	sock := filepath.Join(t.TempDir(), "absent")
	tests := []struct {
		name  string
		start func(io.ReadWriteCloser) error
	}{
		{"cmd", func(p io.ReadWriteCloser) error {
			_, err := startCmd(p, func() error { return nil }, time.Second)
			return err
		}},
		{"qmp", func(p io.ReadWriteCloser) error {
			_, err := startQMP(p, sock, time.Second)
			return err
		}},
	}
	for _, tt := range tests {
		t.Run(tt.name+": no purge", func(t *testing.T) {
			if err := tt.start(&fakePort{}); err == nil || !strings.Contains(err.Error(), "cannot purge") {
				t.Fatalf("err = %v, want a port that cannot purge refused", err)
			}
		})
		t.Run(tt.name+": purge fails", func(t *testing.T) {
			r := &resettablePort{purgeErr: errors.New("purge refused")}
			if err := tt.start(r); err == nil || !strings.Contains(err.Error(), "purge refused") {
				t.Fatalf("err = %v, want the purge failure", err)
			}
		})
	}
}
