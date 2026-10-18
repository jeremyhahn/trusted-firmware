// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build tfwsim

package simproc_test

import (
	"bytes"
	"context"
	"testing"
	"time"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/simproc"
)

// Start hands back a live link to the real firmware: a PING over it is
// answered with its own payload, and Close ends the process without error.
func TestStartRunsTheSimulatorAndCloseEndsIt(t *testing.T) {
	c := simproc.Start(t, simproc.Software)
	dev := tfw.New(c, tfw.WithTimeout(5*time.Second))
	got, err := dev.Ping(context.Background(), []byte("simproc"))
	if err != nil {
		t.Fatalf("ping over the simulator: %v", err)
	}
	if !bytes.Equal(got, []byte("simproc")) {
		t.Fatalf("ping echoed %q, want %q", got, "simproc")
	}
	if err := c.Close(); err != nil {
		t.Fatalf("closing the simulator: %v", err)
	}
}
