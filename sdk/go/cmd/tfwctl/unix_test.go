// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package main

import (
	"net"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

func TestUnixPathParsesTheDeviceAddress(t *testing.T) {
	for _, tc := range []struct {
		port   string
		path   string
		isUnix bool
		errMsg string
	}{
		{"/dev/ttyACM0", "", false, ""},
		{"tcp://127.0.0.1:9000", "", false, ""},
		{"unix:/tmp/q/genuine.sock", "/tmp/q/genuine.sock", true, ""},
		{"unix:rel.sock", "rel.sock", true, ""},
		{"unix:", "", true, "want unix:<socket path>"},
		{"UNIX:/tmp/q.sock", "", false, ""}, // a serial port name, not the scheme
	} {
		t.Run(tc.port, func(t *testing.T) {
			path, isUnix, err := unixPath(tc.port)
			assert.Equal(t, tc.isUnix, isUnix)
			assert.Equal(t, tc.path, path)
			if tc.errMsg == "" {
				assert.NoError(t, err)
			} else {
				require.Error(t, err)
				assert.Contains(t, err.Error(), tc.errMsg)
			}
		})
	}
}

// shortSocketPath is a socket path under a directory of its own in the system
// temp directory: a Unix socket's path is limited to about a hundred bytes,
// which t.TempDir's can pass.
func shortSocketPath(t *testing.T) string {
	t.Helper()
	dir, err := os.MkdirTemp("", "tfwctl")
	require.NoError(t, err)
	t.Cleanup(func() { _ = os.RemoveAll(dir) })
	return filepath.Join(dir, "uart.sock")
}

func TestDialUnixNamesTheSocketNobodyListensOn(t *testing.T) {
	path := shortSocketPath(t)
	_, err := dialUnix(path, time.Second)
	require.Error(t, err)
	assert.Contains(t, err.Error(), "no device listening on unix:"+path)
}

// The emulator's side of -serial unix:<path>,server=on: it listens, tfwctl
// dials, and the verb runs over the socket as over a serial port, the boot
// event included.
func TestMainRunsAVerbOverAUnixSocket(t *testing.T) {
	path := shortSocketPath(t)
	l, err := net.Listen("unix", path)
	require.NoError(t, err)
	t.Cleanup(func() { _ = l.Close() })
	go func() {
		c, err := l.Accept()
		if err != nil {
			return
		}
		defer func() { _ = c.Close() }()
		serveFrames(t, c, c, (&benchDevice{}).answer, bootEvent(t))
	}()
	stdout, stderr := callMain(t, "-boot-wait=0", "-timeout=10s", "unix:"+path, "ping", "hi")
	if !strings.Contains(stdout, `pong: "hi"`) {
		t.Fatalf("stdout %q, want the device's pong", stdout)
	}
	if !strings.Contains(stderr, "boot event") {
		t.Fatalf("stderr %q, want the boot event noted", stderr)
	}
}
