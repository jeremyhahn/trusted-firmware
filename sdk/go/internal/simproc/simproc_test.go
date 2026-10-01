// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package simproc

import (
	"errors"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestLocateFindsABinaryInTheDirectoryTheEnvironmentNames(t *testing.T) {
	dir := t.TempDir()
	want := filepath.Join(dir, Element.Name)
	if err := os.WriteFile(want, nil, 0o700); err != nil {
		t.Fatal(err)
	}
	t.Setenv(DirEnv, dir)

	got, err := Locate(Element)
	if err != nil {
		t.Fatal(err)
	}
	if got != want {
		t.Errorf("Locate = %s, want %s", got, want)
	}
}

// A missing binary is an error that says how to build it, and is never a skip.
func TestLocateReportsAMissingBinaryWithHowToBuildIt(t *testing.T) {
	t.Setenv(DirEnv, t.TempDir())

	_, err := Locate(Element)
	var nb *NotBuiltError
	if !errors.As(err, &nb) {
		t.Fatalf("err = %v, want *NotBuiltError", err)
	}
	if !errors.Is(err, os.ErrNotExist) {
		t.Errorf("err does not unwrap to the stat failure: %v", err)
	}
	for _, want := range []string{"agsim-atecc608a", "make agsim-atecc608a", DirEnv} {
		if !strings.Contains(err.Error(), want) {
			t.Errorf("error %q does not mention %q", err, want)
		}
	}
}

func TestDirDefaultsToTheCheckoutsBuildDirectory(t *testing.T) {
	t.Setenv(DirEnv, "")
	if got := Dir(); !strings.HasSuffix(filepath.ToSlash(got), "/build/host") {
		t.Errorf("Dir = %s, want <checkout>/build/host", got)
	}
}
