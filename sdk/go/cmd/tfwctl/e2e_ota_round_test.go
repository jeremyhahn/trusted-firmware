//go:build tfwsim

package main

// The whole OTA round (#52): tfwctl over the pseudo-terminal of a tfwsim that
// boots through the real tfw-boot at power on and at every reset, verifying
// with the public half of a P-256 key this test makes for itself. No private
// key exists anywhere but in this process's memory.

import (
	"bufio"
	"bytes"
	"context"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/sha256"
	"encoding/binary"
	"encoding/hex"
	"errors"
	"io"
	"os/exec"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/simproc"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/serialport"
)

const (
	bootStateGood  = 2 // TFW_BOOT_STATE_GOOD, include/tfw/boot.h
	bootStateTrial = 3 // TFW_BOOT_STATE_TRIAL
	otaPending     = 4 // TFW_BOOT_OTA_PENDING, slot 1's state with a MARK
	infoFlagTrial  = 0x04
	recoveryExit   = 3 // tools/tfwsim/boot_host.c
	roundImageLen  = 1024
)

// bootSim is one tfwsim process with tfw-boot in front of it.
type bootSim struct {
	dev  *tfw.Device
	key  *ecdsa.PrivateKey
	port io.Closer

	cmd    *exec.Cmd
	mu     sync.Mutex
	stderr bytes.Buffer
	copied chan struct{}
}

// startBootSim starts the soft tfwsim on a pty with a fresh P-256 key, the
// factory image built here at factoryVersion, and extra tfwsim options.
func startBootSim(t *testing.T, factoryVersion uint32, extra ...string) *bootSim {
	t.Helper()
	key, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	require.NoError(t, err)
	ek, err := key.ECDH()
	require.NoError(t, err)
	pub := ek.PublicKey().Bytes() // 0x04 || X || Y
	require.Len(t, pub, 65)

	s := &bootSim{key: key, copied: make(chan struct{})}
	path, err := simproc.Locate(simproc.Software)
	require.NoError(t, err)
	args := append([]string{"--pty",
		"--boot-key", writeFile(t, "boot-key.bin", pub[1:]),
		"--app-image", writeFile(t, "factory.bin", s.image(t, 600, factoryVersion)),
	}, extra...)
	s.cmd = exec.Command(path, args...) // #nosec G204 -- Locate's answer, the tfwsim this repo built; the args are this test's
	errPipe, err := s.cmd.StderrPipe()
	require.NoError(t, err)
	require.NoError(t, s.cmd.Start())
	t.Cleanup(func() {
		_ = s.cmd.Process.Kill()
		<-s.copied
		_ = s.cmd.Wait()
	})

	r := bufio.NewReader(errPipe)
	pty, err := r.ReadString('\n')
	if err != nil {
		close(s.copied)
		t.Fatalf("tfwsim printed no pty: %v", err)
	}
	go func() {
		defer close(s.copied)
		var b [256]byte
		for {
			n, rerr := r.Read(b[:])
			s.mu.Lock()
			s.stderr.Write(b[:n])
			s.mu.Unlock()
			if rerr != nil {
				return
			}
		}
	}()

	conn, err := serialport.Open(strings.TrimSpace(pty), serialport.DefaultBaud)
	require.NoError(t, err)
	t.Cleanup(func() { _ = conn.Close() })
	s.port = conn
	s.dev = tfw.New(conn, tfw.WithTimeout(5*time.Second), tfw.WithBootWait(0))
	return s
}

// sign is ES256 as tfw-boot verifies it: SHA-256 of msg, r || s.
func (s *bootSim) sign(t *testing.T, msg []byte) []byte {
	t.Helper()
	h := sha256.Sum256(msg)
	r, ss, err := ecdsa.Sign(rand.Reader, s.key, h[:])
	require.NoError(t, err)
	sig := make([]byte, 64)
	r.FillBytes(sig[:32])
	ss.FillBytes(sig[32:])
	return sig
}

// image is stageImage's form at version, its trailer signed by this run's key
// over the trailer's first 48 bytes (the signed span, boot/boot_image.c).
func (s *bootSim) image(t *testing.T, body int, version uint32) []byte {
	t.Helper()
	img := stageImage(roundImageLen, body)
	tr := img[roundImageLen-128:]
	binary.LittleEndian.PutUint32(tr[8:], version)
	copy(tr[48:], s.sign(t, tr[:48]))
	return img
}

// activateSig is the row key's OTA_ACTIVATE authorization for measurement and
// version at mode 0, as tfwctl's --sig= takes it.
func (s *bootSim) activateSig(t *testing.T, measurement [32]byte, version uint32) string {
	t.Helper()
	return "--sig=" + hex.EncodeToString(s.sign(t, activateSigStructure(measurement, version, 0)))
}

// stageActivated stages img and activates it, at the next reset, under sig.
func (s *bootSim) stageActivated(t *testing.T, img []byte, sig string) {
	t.Helper()
	cliStage(t, s.dev, img, "--activate", sig)
	assert.Equal(t, otaPending, cliOTAInfo(t, s.dev, "1").State, "activated: PENDING in the store")
}

// reset is tfwctl reset: acknowledged, then tfw-boot's pass and startup again.
func (s *bootSim) reset(t *testing.T) {
	t.Helper()
	require.NoError(t, run(context.Background(), s.dev, "reset", nil, false, io.Discard))
}

// exit hangs up, as a recovery that answers nothing waits for, then waits for
// tfwsim to end and returns its exit status and stderr.
func (s *bootSim) exit(t *testing.T) (int, string) {
	t.Helper()
	require.NoError(t, s.port.Close())
	select {
	case <-s.copied:
	case <-time.After(10 * time.Second):
		t.Fatal("tfwsim did not exit")
	}
	err := s.cmd.Wait()
	s.mu.Lock()
	defer s.mu.Unlock()
	var ee *exec.ExitError
	switch {
	case err == nil:
		return 0, s.stderr.String()
	case errors.As(err, &ee):
		return ee.ExitCode(), s.stderr.String()
	}
	t.Fatal(err)
	return -1, ""
}

// trial takes a v3 image through stage, activate and reset onto its trial.
func (s *bootSim) trial(t *testing.T) {
	t.Helper()
	img := s.image(t, 700, 3)
	s.stageActivated(t, img, s.activateSig(t, sha256.Sum256(img[:700]), 3))
	s.reset(t)
}

func TestE2EOTARoundBootsTheActivatedImageOnTrial(t *testing.T) {
	s := startBootSim(t, 1)
	s.trial(t)

	si := cliOTAInfo(t, s.dev, "0")
	assert.Equal(t, bootStateTrial, si.State, "running on trial")
	assert.NotZero(t, si.Flags&infoFlagTrial, "OTA_INFO flags TRIAL")
	assert.Equal(t, 0, si.Floor, "the floor waits for the confirm")
}

func TestE2EOTARoundUnconfirmedTrialEndsAtTheNextReset(t *testing.T) {
	s := startBootSim(t, 1)
	s.trial(t)
	s.reset(t)

	// One slot and no PREV restore on this row (boot/boot_swap_none.c):
	// the unconfirmed trial is ended and recovery is another OTA round
	// (boot/boot_main.c, TFW_ERR_BAD_STATE).
	code, stderr := s.exit(t)
	assert.Equal(t, recoveryExit, code, stderr)
	assert.Contains(t, stderr, "tfw-boot recovery, status 19", "TFW_ERR_BAD_STATE")
}

func TestE2EOTARoundImageBelowTheFloorIsNotTaken(t *testing.T) {
	s := startBootSim(t, 3, "--boot-floor", "3")
	img := s.image(t, 700, 2)
	// The application stages without the floor (src/core/ota_stage.c), and
	// activate does not read it: the refusal is tfw-boot's, at the take.
	s.stageActivated(t, img, s.activateSig(t, sha256.Sum256(img[:700]), 2))
	s.reset(t)

	si := cliOTAInfo(t, s.dev, "0")
	assert.Equal(t, bootStateGood, si.State, "the running image stays, not on trial")
	assert.Zero(t, si.Flags&infoFlagTrial)
	assert.Equal(t, 3, si.Floor)

	// tfw-boot's refusal has no wire status: main() drops the take's result
	// (boot/boot_main.c), and only the auth refusal forgets the MARK
	// (boot/boot_stage.c). The image below the floor stays offered.
	st := cliOTAInfo(t, s.dev, "1")
	assert.Equal(t, otaPending, st.State, "the MARK stays")
	assert.Equal(t, 2, st.ImageVersion)
}

func TestE2EOTARoundActivateSignedForAnotherMeasurementIsRefused(t *testing.T) {
	s := startBootSim(t, 1)
	img := s.image(t, 700, 3)
	other := sha256.Sum256(img[:699])
	s.stageActivated(t, img, s.activateSig(t, other, 3))
	s.reset(t)

	si := cliOTAInfo(t, s.dev, "0")
	assert.Equal(t, bootStateGood, si.State, "the running image stays, not on trial")
	assert.Zero(t, si.Flags&infoFlagTrial)
	// An image its row key did not authorize is never offered again (#137):
	// the MARK is forgotten.
	assert.NotEqual(t, otaPending, cliOTAInfo(t, s.dev, "1").State)
}
