// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package main

import (
	"fmt"
	"net"
	"strings"
	"time"
)

// unixScheme marks a device whose serial line is a Unix stream socket: QEMU's
// UART under -serial unix:<path>,server=on, the form the Cortex-M QEMU rows
// use (test/integration/cortex-m/qemu_test.sh, #161). Unlike tcp://, tfwctl
// DIALS it: the socket is the UART, and the emulator holds its listening end.
const unixScheme = "unix:"

// unixPath reports whether port is a unix: address, and returns the socket
// path to dial.
func unixPath(port string) (path string, ok bool, err error) {
	path, ok = strings.CutPrefix(port, unixScheme)
	if !ok {
		return "", false, nil
	}
	if path == "" {
		return "", true, fmt.Errorf("device address %q: want %s<socket path>", port, unixScheme)
	}
	return path, ok, nil
}

// dialUnix connects to the UART socket at path, giving up after wait.
func dialUnix(path string, wait time.Duration) (net.Conn, error) {
	c, err := net.DialTimeout("unix", path, wait)
	if err != nil {
		return nil, fmt.Errorf("no device listening on %s%s: %w", unixScheme, path, err)
	}
	return c, nil
}
