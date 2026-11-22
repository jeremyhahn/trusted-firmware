// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package main

import (
	"errors"
	"os"
	"os/signal"
	"path/filepath"
	"strings"
	"syscall"
	"testing"
)

// A --bake-eeprom whose write fails partway must not leave a truncated
// Intel HEX image at --out: the record it starts is replaced whole, and
// the image it pairs with must not be half a file either. RLIMIT_FSIZE
// makes the write fail with EFBIG after 64 bytes of a ~540-byte image.
// Not parallel: the limit and the signal disposition are process-wide.
func TestBakeEEPROMLeavesNoPartialImageWhenTheWriteFails(t *testing.T) {
	signal.Ignore(syscall.SIGXFSZ)
	t.Cleanup(func() { signal.Reset(syscall.SIGXFSZ) })

	var old syscall.Rlimit
	if err := syscall.Getrlimit(syscall.RLIMIT_FSIZE, &old); err != nil {
		t.Fatalf("Getrlimit: %v", err)
	}
	restore := func() {
		if err := syscall.Setrlimit(syscall.RLIMIT_FSIZE, &old); err != nil {
			t.Errorf("restoring RLIMIT_FSIZE: %v", err)
		}
	}
	t.Cleanup(restore)
	lim := syscall.Rlimit{Cur: 64, Max: old.Max}
	if err := syscall.Setrlimit(syscall.RLIMIT_FSIZE, &lim); err != nil {
		t.Fatalf("Setrlimit: %v", err)
	}

	out := filepath.Join(t.TempDir(), "platform.eep")
	err := runBakeEEPROM(out, "0102030405060708090a0b0c0d0e0f10", "", "")
	restore()

	if err == nil || !strings.Contains(err.Error(), "writing") {
		t.Fatalf("runBakeEEPROM error = %v, want one containing %q", err, "writing")
	}
	if _, serr := os.Stat(out); !errors.Is(serr, os.ErrNotExist) {
		t.Fatalf("os.Stat(%s) = %v, want os.ErrNotExist: a partial image was left at --out", out, serr)
	}
}
