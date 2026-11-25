// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"testing"
)

// The board id is TFW_BOARD_ID in boards/<board>/board_profile.h and
// tfw.BoardID here (docs/boards/board-id-registry.md). These tests read
// the headers, so the two copies cannot drift apart unseen. sdk/go is the
// module root, so the repository root is ../../.

var boardIDDefine = regexp.MustCompile(`(?m)^#define TFW_BOARD_ID\s+0x([0-9A-Fa-f]{2})u\b`)

// TestBoardProfilesNameTheirBoard: every board_profile.h defines
// TFW_BOARD_ID once, that id's String() is the board's directory name,
// and no two boards share an id.
func TestBoardProfilesNameTheirBoard(t *testing.T) {
	paths, err := filepath.Glob("../../boards/*/board_profile.h")
	if err != nil {
		t.Fatal(err)
	}
	if len(paths) == 0 {
		t.Fatal("no ../../boards/*/board_profile.h: the tree moved")
	}
	seen := map[BoardID]string{}
	for _, path := range paths {
		src, err := os.ReadFile(path)
		if err != nil {
			t.Fatal(err)
		}
		m := boardIDDefine.FindAllSubmatch(src, -1)
		if len(m) != 1 {
			t.Errorf("%s: %d TFW_BOARD_ID defines, want 1", path, len(m))
			continue
		}
		n, err := strconv.ParseUint(string(m[0][1]), 16, 8)
		if err != nil {
			t.Fatalf("%s: %v", path, err)
		}
		v := BoardID(n)
		board := filepath.Base(filepath.Dir(path))
		if got := v.String(); got != board {
			t.Errorf("%s: board %#02x is %q in tfw.BoardID, want %q", path, uint8(v), got, board)
		}
		if prev, ok := seen[v]; ok {
			t.Errorf("%s: board %#02x is also %s's id", path, uint8(v), prev)
		}
		seen[v] = board
	}
}

// TestEveryKnownBoardIDHasABoardDir: an id kept in the SDK after its board
// is removed or renamed fails.
func TestEveryKnownBoardIDHasABoardDir(t *testing.T) {
	for v, name := range boardNames {
		path := filepath.Join("../../boards", name, "board_profile.h")
		if _, err := os.Stat(path); err != nil {
			t.Errorf("board %#02x %q: %v", uint8(v), name, err)
		}
	}
}
