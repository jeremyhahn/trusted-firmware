// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build tfwsim

package tfw

import (
	"bytes"
	"context"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/sha256"
	"math/big"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/simproc"
)

// The HSM channel is in the element-tier simulator alone (mk/host.mk). The
// engine appends to the A6 record since m7-design.md row 11b-2: a signal on
// entry, the verdict a wipe-enabled rule consults, and every action with its
// port's status. A simulator that has seen no signal answers from an empty
// record, which is what the first two tests pin with the command's refusals:
// an index past the end, and a payload that is not one byte. The last test
// loads a signed set, reports a signal and reads the record back. A missing
// binary fails the test and names `make tfwsim-atecc608a`.
func TestSimHSMRecordReadOfAnEmptyRecordIsRange(t *testing.T) {
	d := New(simproc.Start(t, simproc.Element), WithTimeout(5*time.Second))
	ctx := context.Background()

	for _, index := range []byte{0, 1, 31, 32, 255} {
		_, err := d.HSMRecordRead(ctx, index)
		require.ErrorIs(t, err, StatusErrRange, "index %d", index)
	}

	got, err := d.HSMRecordReadAll(ctx)
	require.NoError(t, err)
	assert.Empty(t, got)
}

func TestSimHSMRecordReadRefusesAPayloadThatIsNotOneByte(t *testing.T) {
	d := New(simproc.Start(t, simproc.Element), WithTimeout(5*time.Second))
	ctx := context.Background()

	for _, payload := range [][]byte{nil, {0, 0}, {0, 0, 0, 0}} {
		_, err := d.Do(ctx, CmdHSMRecordRead, payload)
		require.ErrorIs(t, err, StatusErrBadArgc, "%d payload bytes", len(payload))
	}

	// The refusals left the channel as it was.
	_, err := d.HSMHeartbeat(ctx)
	require.NoError(t, err)
}

// simPolicyEnvelope is testdata/suit_vectors.h's SUIT_POLICY_ENVELOPE: the D5
// policy set, sequence 3, signed by the throwaway tenant key whose public half
// tools/tfwsim/hsm_port.c answers. Its one rule names S1, which the A55 cannot
// report, so simPolicyFor re-signs it for the signal under test.
var simPolicyEnvelope = []byte("" +
	"\xd8\x6b\xa3\x02\x58\x73\x82\x58\x24\x82\x2f\x58\x20\x1e\x5a\x83\xaf\x36\xdd\x6f" +
	"\x71\x21\x9b\x16\x35\x04\xf7\x51\x0a\x2c\x1c\x2f\x35\xa2\xd5\xc7\x69\x14\x67\xa3" +
	"\xe7\xd8\x79\x9b\xc8\x58\x4a\xd2\x84\x43\xa1\x01\x26\xa0\xf6\x58\x40\x34\x16\x54" +
	"\xa6\x68\xff\xae\x69\x29\xcb\x2c\xf5\x36\x99\xd1\x97\xb0\x9d\x96\xe6\x61\x0d\x9e" +
	"\xd7\x79\x9a\xa5\x9d\xb0\x64\xd2\xbd\xaa\x8b\x1a\x5d\x45\xd1\xb3\xdd\x76\xa1\xc8" +
	"\x03\x50\x72\x96\xc4\x3d\x39\x14\x3c\x52\xcc\x4e\x31\x0b\xf7\x4f\x1e\x4c\xed\xe6" +
	"\xde\x03\x58\x73\xa4\x01\x01\x02\x03\x03\x58\x59\xa2\x02\x81\x81\x41\x00\x04\x58" +
	"\x50\x82\x14\xa4\x01\x50\xf5\xce\x00\x48\x68\xe5\x5f\xcf\xb7\x9b\x5b\x1c\xff\xbf" +
	"\x22\xac\x02\x50\xd3\x7d\x83\xb8\x9b\xb6\x52\x35\x99\xae\x46\xe1\x79\xd5\x11\x99" +
	"\x03\x58\x24\x82\x2f\x58\x20\xc0\xe3\xa1\x14\x45\x9d\xde\x65\x0b\xdf\x20\xae\x83" +
	"\x4c\xa8\x1b\x53\x5a\x4a\xcb\x4a\x99\x5f\xb5\x53\xfb\xc1\x00\x53\xf3\x73\x8e\x0e" +
	"\x16\x14\x50\x86\x14\xa1\x15\x67\x23\x70\x6f\x6c\x69\x63\x79\x15\x0f\x03\x0f\x67" +
	"\x23\x70\x6f\x6c\x69\x63\x79\x56\xa5\x01\x01\x02\x81\x81\x83\x01\x82\x03\x05\xf4" +
	"\x03\xa0\x04\x19\x03\xe8\x05\x19\x01\xf4")

// Where simPolicyEnvelope's parts sit: the manifest's SUIT_Digest in the
// authentication wrapper, the COSE_Sign1 signature, the manifest, and the
// 22-byte policy set at the end. The set's rule is [signal, [3, 5], wipe].
const (
	simPolicyDigest   = 9   // [0x82 0x2f 0x58 0x20 digest:32]
	simPolicySig      = 57  // r||s, 64 bytes
	simPolicyManifest = 124 // 115 bytes
	simPolicyBody     = 248 // 22 bytes
	simPolicySignal   = simPolicyBody + 7
	simPolicyWipe     = simPolicyBody + 11
)

// simTenantKey is the throwaway tenant key, private scalar 0x40..0x5F
// (test/integration/cortex-m/hsm_sets.c).
func simTenantKey(t *testing.T) *ecdsa.PrivateKey {
	t.Helper()
	d := make([]byte, 32)
	for i := range d {
		d[i] = byte(0x40 + i)
	}
	k, err := ecdsa.ParseRawPrivateKey(elliptic.P256(), d)
	require.NoError(t, err)
	return k
}

// simSigStructure is the COSE Sig_structure the signature covers: the
// manifest's SUIT_Digest as the detached payload.
func simSigStructure(env []byte) [32]byte {
	s := append([]byte("\x84\x6aSignature1\x43\xa1\x01\x26\x40\x58\x24"),
		env[simPolicyDigest:simPolicyDigest+36]...)
	return sha256.Sum256(s)
}

// simPolicyFor returns simPolicyEnvelope with its one rule naming signal and
// wiping, re-signed. The original is checked first against the offsets and
// the key, so a change to the vector fails here and not in the simulator.
func simPolicyFor(t *testing.T, signal byte) []byte {
	t.Helper()
	k := simTenantKey(t)
	env := bytes.Clone(simPolicyEnvelope)
	// The digest covers the manifest's bstr header and bytes.
	wrapped := env[simPolicyManifest-2 : simPolicyManifest+115]
	manifest := wrapped[2:]
	body := env[simPolicyBody:]
	require.Len(t, body, 22)

	md := sha256.Sum256(wrapped)
	require.Equal(t, md[:], env[simPolicyDigest+4:simPolicyDigest+36], "the manifest digest")
	h := simSigStructure(env)
	var r, s big.Int
	r.SetBytes(env[simPolicySig : simPolicySig+32])
	s.SetBytes(env[simPolicySig+32 : simPolicySig+64])
	require.True(t, ecdsa.Verify(&k.PublicKey, h[:], &r, &s), "the vector's signature")

	old := sha256.Sum256(body)
	at := bytes.Index(manifest, old[:])
	require.Positive(t, at, "the payload digest in the manifest")
	require.Equal(t, byte(0x01), env[simPolicySignal])
	require.Equal(t, byte(0xf4), env[simPolicyWipe])

	env[simPolicySignal] = signal
	env[simPolicyWipe] = 0xf5
	pd := sha256.Sum256(body)
	copy(manifest[at:], pd[:])
	md = sha256.Sum256(wrapped)
	copy(env[simPolicyDigest+4:], md[:])
	h = simSigStructure(env)
	rr, ss, err := ecdsa.Sign(rand.Reader, k, h[:])
	require.NoError(t, err)
	rr.FillBytes(env[simPolicySig : simPolicySig+32])
	ss.FillBytes(env[simPolicySig+32 : simPolicySig+64])
	return env
}

// R3 end to end: a set whose rule for S4 is wipe-enabled, and no verdict yet.
// The engine records the signal, the clear verdict it consulted (so the wipe
// it withheld is explained), then the rule's two actions with the port's
// status. Every entry reads back with the record's count and no drops, in the
// order appended.
func TestSimHSMRecordKeepsTheSignalTheDecisionAndTheActions(t *testing.T) {
	d := New(simproc.Start(t, simproc.Element), WithTimeout(5*time.Second))
	ctx := context.Background()

	_, err := d.HSMLoad(ctx, simPolicyFor(t, HSMSignalIMAViolation))
	require.NoError(t, err)
	got, err := d.HSMRecordReadAll(ctx)
	require.NoError(t, err)
	require.Empty(t, got, "loading a set records nothing")

	_, err = d.HSMReport(ctx, HSMSignalIMAViolation)
	require.NoError(t, err)

	got, err = d.HSMRecordReadAll(ctx)
	require.NoError(t, err)
	type entry struct{ Kind, What, Result byte }
	want := []entry{
		{HSMRecordSignal, HSMSignalIMAViolation, 0},
		{HSMRecordDecision, HSMVerdictClear, 0},
		{HSMRecordAction, 3, 0}, // keys lock
		{HSMRecordAction, 5, 0}, // shutdown
	}
	require.Len(t, got, len(want))
	for i, e := range got {
		assert.Equal(t, want[i], entry{e.Kind, e.What, e.Result}, "entry %d", i)
		assert.Equal(t, uint8(len(want)), e.Count, "entry %d count", i)
		assert.Zero(t, e.Dropped, "entry %d dropped", i)
		if i > 0 {
			assert.GreaterOrEqual(t, e.At, got[i-1].At, "entry %d time", i)
		}
	}
	_, err = d.HSMRecordRead(ctx, byte(len(want)))
	require.ErrorIs(t, err, StatusErrRange, "the index past the last")
}
