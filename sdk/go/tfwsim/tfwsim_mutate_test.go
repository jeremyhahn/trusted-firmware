// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfwsim_test

import (
	"context"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"errors"
	"os"
	"strings"
	"testing"
	"time"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/tfwsim"
)

// Mutation tests (#163): each one kills a fault the other tests let through.

// timedPing pings a fake that never answers and returns the error and how
// long it took to come back.
func timedPing(t *testing.T, d *tfw.Device) (time.Duration, error) {
	t.Helper()
	start := time.Now()
	_, err := d.Ping(context.Background(), []byte("hi"))
	return time.Since(start), err
}

// A key on another curve is refused for its curve, before its encoding is
// looked at.
func TestStartElementWithOperatorRefusesAnotherCurveForItsCurve(t *testing.T) {
	p384, err := ecdsa.GenerateKey(elliptic.P384(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	record := fake(t)
	f := &fatalTB{TB: t}
	done := make(chan struct{})
	go func() {
		defer close(done)
		tfwsim.StartElementWithOperator(f, &p384.PublicKey)
	}()
	<-done
	if !strings.Contains(f.msg, "must be a P-256 public key") {
		t.Fatalf("a P-384 key failed with %q, want the curve named as the reason", f.msg)
	}
	if _, err := os.Stat(record); !os.IsNotExist(err) {
		t.Fatalf("a simulator was started (stat %v)", err)
	}
}

// With no option of its own the Device's request timeout is the boot bound,
// not the SDK's longer default: a wedged simulator costs the test only that.
func TestStartBoundsARequestByTheBootTimeout(t *testing.T) {
	fake(t)
	took, err := timedPing(t, tfwsim.Start(t))
	if !errors.Is(err, tfw.ErrTimeout) {
		t.Fatalf("ping of a silent simulator: %v, want %v", err, tfw.ErrTimeout)
	}
	if took > 8*time.Second {
		t.Fatalf("a silent simulator took %s to time out, want the 5s boot bound", took)
	}
}

// A caller's own timeout wins over the default.
func TestStartLetsTheCallersTimeoutWin(t *testing.T) {
	fake(t)
	took, err := timedPing(t, tfwsim.Start(t, tfw.WithTimeout(50*time.Millisecond)))
	if !errors.Is(err, tfw.ErrTimeout) {
		t.Fatalf("ping of a silent simulator: %v, want %v", err, tfw.ErrTimeout)
	}
	if took > 2*time.Second {
		t.Fatalf("a 50ms timeout took %s", took)
	}
}
