// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfwsim_test

import (
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"fmt"
	"os"
	"runtime"
	"strings"
	"testing"

	"github.com/jeremyhahn/trusted-firmware/sdk/go/tfwsim"
)

// fatalTB records Fatal instead of failing the test, so a bad argument to a
// Start function can be asserted on. Goexit mirrors what the real Fatal does:
// the calling goroutine does not return.
type fatalTB struct {
	testing.TB
	msg string
}

func (f *fatalTB) Helper() {}
func (f *fatalTB) Fatal(args ...any) {
	f.msg = fmt.Sprint(args...)
	runtime.Goexit()
}
func (f *fatalTB) Fatalf(format string, args ...any) {
	f.msg = fmt.Sprintf(format, args...)
	runtime.Goexit()
}

func TestStartElementWithOperatorRefusesAnUnusableKey(t *testing.T) {
	p384, err := ecdsa.GenerateKey(elliptic.P384(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	for name, key := range map[string]*ecdsa.PublicKey{"nil": nil, "P-384": &p384.PublicKey} {
		record := fake(t)
		f := &fatalTB{TB: t}
		done := make(chan struct{})
		go func() {
			defer close(done)
			tfwsim.StartElementWithOperator(f, key)
		}()
		<-done
		if !strings.Contains(f.msg, "must be a P-256 public key") {
			t.Errorf("%s operator key: failed with %q, want the P-256 refusal", name, f.msg)
		}
		if _, err := os.Stat(record); !os.IsNotExist(err) {
			t.Errorf("%s operator key: a simulator was started (stat %v)", name, err)
		}
	}
}
