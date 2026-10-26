// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package peer

import (
	"encoding/json"
	"net"
	"os"
	"path/filepath"
	"reflect"
	"strings"
	"testing"
	"time"
)

func TestFromEnvResetsByDTRByDefault(t *testing.T) {
	t.Setenv(PortEnv, "/dev/ttyACM0")
	t.Setenv(BoardEnv, "atmega2560")
	t.Setenv(EthEnv, "")
	t.Setenv(ResetEnv, "")
	got, err := FromEnv()
	if err != nil {
		t.Fatal(err)
	}
	if got.QMP != "" {
		t.Fatalf("target = %+v, want no QMP socket: an unset %s resets by DTR", got, ResetEnv)
	}
}

func TestFromEnvReadsTheQMPSocket(t *testing.T) {
	t.Setenv(PortEnv, "/dev/pts/7")
	t.Setenv(BoardEnv, "mps2-an505")
	t.Setenv(EthEnv, "")
	t.Setenv(ResetEnv, "qmp:/tmp/cm/qmp.sock")
	got, err := FromEnv()
	if err != nil {
		t.Fatal(err)
	}
	want := Target{Port: "/dev/pts/7", Board: "mps2-an505", QMP: "/tmp/cm/qmp.sock"}
	if got != want {
		t.Fatalf("target = %+v, want %+v", got, want)
	}
}

func TestFromEnvRefusesAMalformedReset(t *testing.T) {
	t.Setenv(PortEnv, "/dev/pts/7")
	t.Setenv(BoardEnv, "mps2-an505")
	t.Setenv(EthEnv, "")
	for _, v := range []string{"qmp:", "qmp", "dtr", "QMP:/tmp/q.sock", "unix:/tmp/q.sock", " qmp:/tmp/q.sock"} {
		t.Setenv(ResetEnv, v)
		got, err := FromEnv()
		if err == nil {
			t.Errorf("%s=%q: target = %+v, want an error", ResetEnv, v, got)
			continue
		}
		if !strings.Contains(err.Error(), ResetEnv) {
			t.Errorf("%s=%q: err = %v, want it to name %s", ResetEnv, v, err, ResetEnv)
		}
	}
}

func TestFromEnvRefusesAResetWithNoPort(t *testing.T) {
	t.Setenv(PortEnv, "")
	t.Setenv(BoardEnv, "")
	t.Setenv(EthEnv, "")
	t.Setenv(ResetEnv, "qmp:/tmp/q.sock")
	if got, err := FromEnv(); err == nil || !strings.Contains(err.Error(), ResetEnv) {
		t.Fatalf("target = %+v, err = %v; want an error naming %s", got, err, ResetEnv)
	}
}

// qmpGreeting is the first message a QEMU 8 monitor sends, verbatim.
const qmpGreeting = `{"QMP": {"version": {"qemu": {"micro": 0, "minor": 2, "major": 8}, "package": ""}, "capabilities": ["oob"]}}` + "\n"

// fakeQMP listens on a Unix socket for one connection and hands it to serve.
// The path is short: a socket path has a hard limit near 108 bytes, which
// t.TempDir can exceed.
func fakeQMP(t *testing.T, serve func(c net.Conn, dec *json.Decoder)) string {
	t.Helper()
	dir, err := os.MkdirTemp("", "qmp")
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _ = os.RemoveAll(dir) })
	sock := filepath.Join(dir, "s")
	l, err := net.Listen("unix", sock)
	if err != nil {
		t.Fatal(err)
	}
	done := make(chan struct{})
	t.Cleanup(func() { _ = l.Close(); <-done })
	go func() {
		defer close(done)
		c, err := l.Accept()
		if err != nil {
			return
		}
		defer func() { _ = c.Close() }()
		_ = c.SetDeadline(time.Now().Add(5 * time.Second))
		serve(c, json.NewDecoder(c))
	}()
	return sock
}

// expect reads one command and reports its name; empty on a read error.
func expect(dec *json.Decoder) string {
	var cmd struct {
		Execute string `json:"execute"`
	}
	if dec.Decode(&cmd) != nil {
		return ""
	}
	return cmd.Execute
}

func TestQMPResetNegotiatesThenResetsAndWaitsForTheEvent(t *testing.T) {
	got := make(chan []string, 1)
	sock := fakeQMP(t, func(c net.Conn, dec *json.Decoder) {
		var seen []string
		_, _ = c.Write([]byte(qmpGreeting))
		seen = append(seen, expect(dec))
		_, _ = c.Write([]byte(`{"return": {}}` + "\n"))
		seen = append(seen, expect(dec))
		// An unrelated event first, then the return, then the reset itself.
		_, _ = c.Write([]byte(`{"timestamp": {"seconds": 1, "microseconds": 2}, "event": "NIC_RX_FILTER_CHANGED"}` + "\n"))
		_, _ = c.Write([]byte(`{"return": {}}` + "\n"))
		_, _ = c.Write([]byte(`{"timestamp": {"seconds": 1, "microseconds": 3}, "event": "RESET", "data": {"guest": false, "reason": "host-qmp-system-reset"}}` + "\n"))
		got <- seen
	})
	if err := qmpReset(sock, time.Second); err != nil {
		t.Fatal(err)
	}
	seen := <-got
	if want := []string{"qmp_capabilities", "system_reset"}; !reflect.DeepEqual(seen, want) {
		t.Fatalf("commands = %v, want %v", seen, want)
	}
}

func TestQMPResetTakesTheEventBeforeTheReturn(t *testing.T) {
	sock := fakeQMP(t, func(c net.Conn, dec *json.Decoder) {
		_, _ = c.Write([]byte(qmpGreeting))
		expect(dec)
		_, _ = c.Write([]byte(`{"return": {}}` + "\n"))
		expect(dec)
		_, _ = c.Write([]byte(`{"event": "RESET"}` + "\n" + `{"return": {}}` + "\n"))
		// Nothing more: a client still waiting for the event would time out.
		_ = expect(dec)
	})
	if err := qmpReset(sock, time.Second); err != nil {
		t.Fatal(err)
	}
}

func TestQMPResetReportsARefusal(t *testing.T) {
	sock := fakeQMP(t, func(c net.Conn, dec *json.Decoder) {
		_, _ = c.Write([]byte(qmpGreeting))
		expect(dec)
		_, _ = c.Write([]byte(`{"return": {}}` + "\n"))
		expect(dec)
		_, _ = c.Write([]byte(`{"error": {"class": "GenericError", "desc": "reset refused"}}` + "\n"))
	})
	err := qmpReset(sock, time.Second)
	if err == nil || !strings.Contains(err.Error(), "system_reset") || !strings.Contains(err.Error(), "reset refused") {
		t.Fatalf("err = %v, want system_reset's refusal", err)
	}
}

func TestQMPResetRefusesASocketThatIsNotQMP(t *testing.T) {
	sock := fakeQMP(t, func(c net.Conn, _ *json.Decoder) {
		_, _ = c.Write([]byte(`{"hello": 1}` + "\n"))
	})
	if err := qmpReset(sock, time.Second); err == nil || !strings.Contains(err.Error(), "greeting") {
		t.Fatalf("err = %v, want no QMP greeting", err)
	}
}

// Each wait QEMU could stall -- the greeting, a return, the RESET event -- ends
// at the bound with an error, not a hung test.
func TestQMPResetIsBounded(t *testing.T) {
	const bound = 200 * time.Millisecond
	for name, serve := range map[string]func(net.Conn, *json.Decoder){
		"no greeting": func(_ net.Conn, dec *json.Decoder) { expect(dec) },
		"no return": func(c net.Conn, dec *json.Decoder) {
			_, _ = c.Write([]byte(qmpGreeting))
			expect(dec)
			expect(dec)
		},
		"no reset event": func(c net.Conn, dec *json.Decoder) {
			_, _ = c.Write([]byte(qmpGreeting))
			expect(dec)
			_, _ = c.Write([]byte(`{"return": {}}` + "\n"))
			expect(dec)
			_, _ = c.Write([]byte(`{"return": {}}` + "\n"))
			expect(dec)
		},
	} {
		t.Run(name, func(t *testing.T) {
			sock := fakeQMP(t, serve)
			begun := time.Now()
			err := qmpReset(sock, bound)
			if err == nil {
				t.Fatal("qmpReset returned nil from a monitor that never finished")
			}
			if took := time.Since(begun); took > 10*bound {
				t.Fatalf("took %s for a %s bound", took, bound)
			}
		})
	}
}

func TestQMPResetNamesTheVariableWhenNothingListens(t *testing.T) {
	sock := filepath.Join(t.TempDir(), "absent")
	if err := qmpReset(sock, time.Second); err == nil || !strings.Contains(err.Error(), ResetEnv) {
		t.Fatalf("err = %v, want it to name %s", err, ResetEnv)
	}
}
