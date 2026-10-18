// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build tfwsim || simavr

package main

import (
	"bytes"
	"context"
	"strings"
	"testing"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
)

func expectSimStatus(t *testing.T, dev *tfw.Device, want string) {
	t.Helper()
	var buf bytes.Buffer
	if err := run(context.Background(), dev, "ota-status", nil, false, &buf); err != nil {
		t.Fatalf("ota-status: %v", err)
	}
	if got := strings.TrimSpace(buf.String()); got != want {
		t.Fatalf("ota-status printed %q, want %q", got, want)
	}
}
