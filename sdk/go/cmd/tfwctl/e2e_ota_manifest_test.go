// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build tfwsim

package main

import (
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/sha256"
	"encoding/binary"
	"encoding/hex"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/simproc"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/tfwsim"
)

// OTA_ACTIVATE's SUIT manifest gate end to end (#52; docs/ota/architecture.md):
// tfwsim-manifest is the element tier with src/core/ota_manifest.c linked, and
// suit-emit is mk/suit.mk's emitter built against the same host board's
// profile. Both come from `make tfwsim-manifest`; a missing one fails the
// test as every other simulator does (simproc.NotBuiltError), never a skip.
var (
	manifestSim  = simproc.Binary{Name: "tfwsim-manifest", Make: "make tfwsim-manifest"}
	manifestEmit = simproc.Binary{Name: "suit-emit", Make: "make tfwsim-manifest"}
)

const (
	manifestTotal = 2048 // whole pages, room for the body, the envelope and the trailer
	manifestBody  = 700
	manifestSeq   = 3 // stageImageFor's trailer image_version: the sequence must equal it
)

// manifestPeer starts tfwsim-manifest with an operator the test holds the key
// of and an image slot holding manifestKey's public half (--provision-element
// seeds it), and opens the operator
// session OTA_ACTIVATE needs; nothing is staged yet, so the chained activate
// is refused BAD_STATE.
func manifestPeer(t *testing.T) *tfw.Device {
	t.Helper()
	pub, keyPath := writeOperatorKey(t)
	d := tfw.New(simproc.Start(t, manifestSim, elementArgs(t, pub, nil)...), tfw.WithTimeout(10*time.Second))
	requireFeature(t, d, tfw.FeatureAuthSession)
	serial := hex.EncodeToString(tfwsim.TestSerial[:])
	require.Equal(t, tfw.StatusErrBadState, cliStatus(t, d, "auth", serial, keyPath, "--", "ota-activate", heldSig), "nothing staged")
	return d
}

// suitEnvelope is the envelope mk/suit.mk's suit-tbs and suit-envelope make
// for body at manifestSeq, its Sig_structure signed ES256 by key, as go-xkms
// signs it.
func suitEnvelope(t *testing.T, key *ecdsa.PrivateKey, body []byte) []byte {
	t.Helper()
	emit, err := simproc.Locate(manifestEmit)
	require.NoError(t, err)
	dir := t.TempDir()
	in := func(name string) string { return filepath.Join(dir, name) }
	run := func(args ...string) {
		out, err := exec.Command(emit, args...).CombinedOutput() // #nosec G204 -- emit is Locate's answer, the suit-emit this repo's make target built; args are the test's temporary files
		require.NoError(t, err, "suit-emit %v: %s", args[0], out)
	}

	require.NoError(t, os.WriteFile(in("image.bin"), body, 0o600))
	run("tbs", in("image.bin"), strconv.Itoa(manifestSeq), in("manifest"), in("tbs"))
	tbs, err := os.ReadFile(in("tbs"))
	require.NoError(t, err)
	digest := sha256.Sum256(tbs)
	r, s, err := ecdsa.Sign(rand.Reader, key, digest[:])
	require.NoError(t, err)
	var sig [64]byte
	r.FillBytes(sig[:32])
	s.FillBytes(sig[32:])
	require.NoError(t, os.WriteFile(in("sig"), sig[:], 0o600))
	run("envelope", in("manifest"), in("sig"), in("suit"))
	env, err := os.ReadFile(in("suit"))
	require.NoError(t, err)
	return env
}

// manifestImage is stageImageFor's stream with env after the body as
// image-seal places it, a little-endian u16 length then the envelope; nil env
// leaves the erased 0xFF there.
func manifestImage(t *testing.T, env []byte) []byte {
	t.Helper()
	img := stageImageFor(manifestTotal, manifestBody, tfw.BoardATmega328P)
	if env != nil {
		require.Less(t, manifestBody+2+len(env), manifestTotal-128, "the envelope overlaps the trailer")
		binary.LittleEndian.PutUint16(img[manifestBody:], uint16(len(env)))
		copy(img[manifestBody+2:], env)
	}
	return img
}

// manifestKeySeed is tools/tfwsim/tier_atecc608a.c's kImageKeySeed, the
// published string --provision-element derives the image slot's TEST key
// from: manifestKey is that key, SHA-256 of the string re-hashed while it is
// not a P-256 scalar, as the element model's tfw_model_p256_key derives it.
const manifestKeySeed = "tfwsim-atecc608a TEST image key v1"

// manifestKey is the private half of the image slot's TEST key.
func manifestKey(t *testing.T) *ecdsa.PrivateKey {
	t.Helper()
	d := sha256.Sum256([]byte(manifestKeySeed))
	for {
		key, err := ecdsa.ParseRawPrivateKey(elliptic.P256(), d[:])
		if err == nil {
			return key
		}
		d = sha256.Sum256(d[:])
	}
}

func TestE2EOTAManifestAcceptsAnImageItNames(t *testing.T) {
	key := manifestKey(t)
	d := manifestPeer(t)
	img := manifestImage(t, nil)
	img = manifestImage(t, suitEnvelope(t, key, img[:manifestBody]))

	cliStage(t, d, img, "--activate", heldSig)
	sum := sha256.Sum256(img[:manifestBody])
	si := cliOTAInfo(t, d, "1")
	assert.Equal(t, 4, si.State, "PENDING")
	assert.Equal(t, manifestSeq, si.ImageVersion)
	assert.Equal(t, hex.EncodeToString(sum[:]), si.Measurement)
}

// test_an_image_the_manifest_does_not_name_is_refused's status: the digest
// the manifest names is not the staged image's.
func TestE2EOTAManifestRefusesAnImageItDoesNotName(t *testing.T) {
	key := manifestKey(t)
	d := manifestPeer(t)
	other := manifestImage(t, nil)[:manifestBody]
	other[0] ^= 0x01

	cliStage(t, d, manifestImage(t, suitEnvelope(t, key, other)))
	assert.Equal(t, tfw.StatusErrCrypto, cliStatus(t, d, "ota-activate", heldSig))
	assert.NotEqual(t, 4, cliOTAInfo(t, d, "1").State, "a refused image is not PENDING")
}

// test_an_image_without_an_envelope_is_refused's status: the length after the
// body is the erased 0xFFFF, which the padding cannot hold.
func TestE2EOTAManifestRefusesAnImageWithoutOne(t *testing.T) {
	d := manifestPeer(t)

	cliStage(t, d, manifestImage(t, nil))
	assert.Equal(t, tfw.StatusErrRange, cliStatus(t, d, "ota-activate", heldSig))
	assert.NotEqual(t, 4, cliOTAInfo(t, d, "1").State, "a refused image is not PENDING")
}
