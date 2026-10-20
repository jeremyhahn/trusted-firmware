// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package main

import (
	"errors"
	"testing"
)

func TestRejectBakeFlagsAcceptsAWireModeWithNoBakeFlags(t *testing.T) {
	if err := rejectBakeFlags("", "", "", ""); err != nil {
		t.Fatalf("no bake flags set: got %v, want nil", err)
	}
}

func TestRejectBakeFlagsRefusesEachBakeFlag(t *testing.T) {
	cases := []struct {
		name                            string
		serial, hash, opKey, out, wants string
	}{
		{"serial", "a6e0000000000000000000000000c0de", "", "", "", "serial"},
		{"pubkey-hash", "", "00", "", "", "pubkey-hash"},
		{"operator-pubkey", "", "", "00", "", "operator-pubkey"},
		{"out", "", "", "", "platform.eep", "out"},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			err := rejectBakeFlags(c.serial, c.hash, c.opKey, c.out)
			var be *bakeFlagError
			if !errors.As(err, &be) {
				t.Fatalf("got %v, want *bakeFlagError", err)
			}
			if be.flag != c.wants {
				t.Fatalf("flag = %q, want %q", be.flag, c.wants)
			}
		})
	}
}

func TestLockWord(t *testing.T) {
	for _, tc := range []struct {
		locked bool
		want   string
	}{
		{true, "LOCKED"},
		{false, "open"},
	} {
		if got := lockWord(tc.locked); got != tc.want {
			t.Errorf("lockWord(%v) = %q, want %q", tc.locked, got, tc.want)
		}
	}
}
