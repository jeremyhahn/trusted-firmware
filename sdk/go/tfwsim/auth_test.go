// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfwsim_test

import (
	"bytes"
	"context"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"errors"
	"fmt"
	"runtime"
	"testing"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/tfwsim"
)

// The operator-signature auth session against the real firmware: the operator's
// key pair is generated HERE, in Go, and its public half is baked into the
// device's platform block exactly as tfwprov does. The element verifies the
// signature for real (ECDSA P-256), so these tests establish that a signature
// made by Go's crypto/ecdsa opens the session and that nothing else does.

func operatorKey(t *testing.T) *ecdsa.PrivateKey {
	t.Helper()

	k, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	return k
}

// platformMagic is the first four bytes of the platform block, 'T','F','P','R'.
// EEPROM_READ of the reserved region needs the session, so reading it back is
// proof the session is open and not merely that AUTH_RESPONSE returned.
var platformMagic = []byte{0x54, 0x46, 0x50, 0x52}

func TestAuthSessionIsAdvertisedByTheElementTier(t *testing.T) {
	c, err := tfwsim.StartElementWithOperator(t, &operatorKey(t).PublicKey).Caps(context.Background())
	if err != nil {
		t.Fatal(err)
	}
	if !c.Has(tfw.FeatureAuthSession) {
		t.Errorf("features %#x lack the auth session", c.Features)
	}
}

func TestAnOperatorSignatureMadeInGoOpensTheSession(t *testing.T) {
	ctx := context.Background()
	op := operatorKey(t)
	d := tfwsim.StartElementWithOperator(t, &op.PublicKey)

	// Closed before: the reserved region is the operator's.
	if _, err := d.EEPROMRead(ctx, 0, 4); !errors.Is(err, tfw.StatusErrAuthRequired) {
		t.Fatalf("reserved read before authenticating: %v, want %v", err, tfw.StatusErrAuthRequired)
	}

	nonce, err := d.AuthChallenge(ctx)
	if err != nil {
		t.Fatal(err)
	}
	if err := d.AuthResponse(ctx, op, nonce, tfwsim.TestSerial[:]); err != nil {
		t.Fatalf("AuthResponse with the operator's own key: %v", err)
	}

	got, err := d.EEPROMRead(ctx, 0, 4)
	if err != nil {
		t.Fatalf("reserved read after authenticating: %v", err)
	}
	if !bytes.Equal(got, platformMagic) {
		t.Errorf("platform block magic = %x, want %x", got, platformMagic)
	}
}

func TestASignatureFromAnotherKeyDoesNotOpenTheSession(t *testing.T) {
	ctx := context.Background()
	d := tfwsim.StartElementWithOperator(t, &operatorKey(t).PublicKey)

	nonce, err := d.AuthChallenge(ctx)
	if err != nil {
		t.Fatal(err)
	}
	err = d.AuthResponse(ctx, operatorKey(t), nonce, tfwsim.TestSerial[:])
	if !errors.Is(err, tfw.StatusErrCrypto) {
		t.Fatalf("AuthResponse with a key the device does not trust: %v, want %v", err, tfw.StatusErrCrypto)
	}
	if _, err := d.EEPROMRead(ctx, 0, 4); !errors.Is(err, tfw.StatusErrAuthRequired) {
		t.Errorf("reserved read after a failed authentication: %v, want %v", err, tfw.StatusErrAuthRequired)
	}
}

// The signed message binds the serial, so a signature the operator made for
// another device is refused by this one.
func TestASignatureBoundToAnotherDevicesSerialIsRefused(t *testing.T) {
	ctx := context.Background()
	op := operatorKey(t)
	d := tfwsim.StartElementWithOperator(t, &op.PublicKey)

	nonce, err := d.AuthChallenge(ctx)
	if err != nil {
		t.Fatal(err)
	}
	other := bytes.Repeat([]byte{0x77}, tfw.SerialLen)
	if err := d.AuthResponse(ctx, op, nonce, other); !errors.Is(err, tfw.StatusErrCrypto) {
		t.Fatalf("AuthResponse bound to another serial: %v, want %v", err, tfw.StatusErrCrypto)
	}
}

// A signature over the previous challenge, made by the right key for the right
// device, is refused once the device has issued a new one: the nonce is the
// freshness.
func TestASignatureOverAStaleChallengeIsRefused(t *testing.T) {
	ctx := context.Background()
	op := operatorKey(t)
	d := tfwsim.StartElementWithOperator(t, &op.PublicKey)

	stale, err := d.AuthChallenge(ctx)
	if err != nil {
		t.Fatal(err)
	}
	fresh, err := d.AuthChallenge(ctx)
	if err != nil {
		t.Fatal(err)
	}
	if bytes.Equal(stale, fresh) {
		t.Fatal("two challenges produced the same nonce")
	}
	if err := d.AuthResponse(ctx, op, stale, tfwsim.TestSerial[:]); !errors.Is(err, tfw.StatusErrCrypto) {
		t.Fatalf("AuthResponse over a stale nonce: %v, want %v", err, tfw.StatusErrCrypto)
	}
}

func TestTheDefaultElementHasNoOperatorToAuthenticate(t *testing.T) {
	// StartElement bakes an all-zero operator key, which is not a point on the
	// curve: no signature, however good, can be verified against it.
	ctx := context.Background()
	d := tfwsim.StartElement(t)

	nonce, err := d.AuthChallenge(ctx)
	if err != nil {
		t.Fatal(err)
	}
	if err := d.AuthResponse(ctx, operatorKey(t), nonce, tfwsim.TestSerial[:]); !errors.Is(err, tfw.StatusErrCrypto) {
		t.Fatalf("AuthResponse on a device with no operator key: %v, want %v", err, tfw.StatusErrCrypto)
	}
}

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

func TestStartElementWithOperatorRefusesAnUnusableKey(t *testing.T) {
	p384, err := ecdsa.GenerateKey(elliptic.P384(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	for name, key := range map[string]*ecdsa.PublicKey{"nil": nil, "P-384": &p384.PublicKey} {
		f := &fatalTB{TB: t}
		done := make(chan struct{})
		go func() {
			defer close(done)
			tfwsim.StartElementWithOperator(f, key)
		}()
		<-done
		if f.msg == "" {
			t.Errorf("%s operator key was accepted", name)
		}
	}
}
