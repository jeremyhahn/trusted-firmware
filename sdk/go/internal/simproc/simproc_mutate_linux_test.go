// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package simproc

import (
	"os"
	"path/filepath"
	"testing"
)

// openFDs counts this process's open file descriptors.
func openFDs(t *testing.T) int {
	t.Helper()
	fds, err := os.ReadDir("/proc/self/fd")
	if err != nil {
		t.Fatal(err)
	}
	return len(fds)
}

// A binary that cannot be run fails the test without leaking the pipe Start
// opened for it (mutation test, #163).
func TestStartLeaksNoDescriptorOnABinaryThatCannotRun(t *testing.T) {
	dir := t.TempDir()
	if err := os.WriteFile(filepath.Join(dir, Auto.Name), []byte("not a program"), 0o600); err != nil {
		t.Fatal(err)
	}
	t.Setenv(DirEnv, dir)
	// The first failure warms whatever the runtime opens lazily.
	if msg := startFatal(t, Auto); msg == "" {
		t.Fatal("Start accepted a binary it could not run")
	}
	before := openFDs(t)
	if msg := startFatal(t, Auto); msg == "" {
		t.Fatal("Start accepted a binary it could not run")
	}
	if after := openFDs(t); after != before {
		t.Fatalf("open descriptors went from %d to %d across a failed Start", before, after)
	}
}
