package peer

import (
	"encoding/json"
	"errors"
	"io"
	"net"
	"path/filepath"
	"strings"
	"testing"
	"time"
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
