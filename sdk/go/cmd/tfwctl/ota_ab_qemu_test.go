// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build qemu

package main

// The A/B row on a QEMU Cortex-M board: OTA_INFO, OTA_ACTIVATE, OTA_CONFIRM,
// the rollback of an unconfirmed trial and the floor, on the two-slot build
// tfw-boot swaps (docs/cortex-m/README.md, two slots). qemu_wire_test.sh's ab
// mode starts each test against a QEMU of its own, booted from the factory
// copies, and every reset here is QMP's system_reset, which reloads those
// copies and leaves the slots and the platform block as the last boot left
// them. One test per outcome; the expected values are literals.

import (
	"context"
	"crypto/sha256"
	"encoding/binary"
	"encoding/hex"
	"io"
	"os"
	"path/filepath"
	"testing"
	"time"

	"github.com/stretchr/testify/require"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/peer"
)

// The platform block's states as OTA_INFO reports them (include/tfw/boot.h).
const (
	qemuBootGood  = 2
	qemuBootTrial = 3
	qemuOTAIdle   = 0
	qemuOTAFailed = 3
	qemuOTAMarked = 4 // PENDING: the store's MARK names a whole image

	qemuTrVersion = 8 // the trailer's image_version (include/tfw/boot.h)
)

// qemuABImage is one image cm_image signed for the A/B row, with the
// activate and confirm authorizations it signed beside it.
type qemuABImage struct {
	img     []byte
	sig     [tfw.OTAAuthSigLen]byte
	confirm [tfw.OTAAuthSigLen]byte
	version uint32
	measure string
}

// qemuABTarget is the boot qemu_wire_test.sh started and the directory cm_image
// wrote into.
func qemuABTarget(t *testing.T) (peer.Target, string) {
	t.Helper()
	target, err := peer.FromEnv()
	require.NoError(t, err)
	require.NotEmpty(t, target.QMP,
		"%s names no QMP socket: run `make integration-test-cortex-m-wire`, whose A/B row starts this test", peer.ResetEnv)
	dir := os.Getenv(qemuStagedEnv)
	require.NotEmpty(t, dir, "%s is not set: run `make integration-test-cortex-m-wire`", qemuStagedEnv)
	return target, dir
}

// qemuABLoad reads <name>.bin and its OTA_ACTIVATE and OTA_CONFIRM
// authorizations, <sig> and <confirm>, and checks the
// trailer's measurement against the bytes, so a bad file fails here and not at
// the boot.
func qemuABLoad(t *testing.T, dir, name, sigName, confirmName string) qemuABImage {
	t.Helper()
	var out qemuABImage
	var err error
	out.img, err = os.ReadFile(filepath.Join(dir, name))
	require.NoError(t, err, "%s: run `make integration-test-cortex-m-wire`, whose A/B row writes it", name)
	for _, s := range []struct {
		name string
		to   []byte
	}{{sigName, out.sig[:]}, {confirmName, out.confirm[:]}} {
		raw, err := os.ReadFile(filepath.Join(dir, s.name))
		require.NoError(t, err, "%s: run `make integration-test-cortex-m-wire`, whose A/B row writes it", s.name)
		require.Len(t, raw, tfw.OTAAuthSigLen, s.name)
		copy(s.to, raw)
	}

	require.Greater(t, len(out.img), qemuTrailerLen, name)
	tr := out.img[len(out.img)-qemuTrailerLen:]
	n := int(binary.LittleEndian.Uint32(tr[qemuTrImageLen:]))
	require.LessOrEqual(t, n, len(out.img)-qemuTrailerLen, "%s's image length", name)
	sum := sha256.Sum256(out.img[:n])
	out.measure = hex.EncodeToString(sum[:])
	require.Equal(t, out.measure, hex.EncodeToString(tr[qemuTrMeasure:qemuTrMeasure+sha256.Size]),
		"%s's trailer measurement", name)
	out.version = binary.LittleEndian.Uint32(tr[qemuTrVersion:])
	return out
}

// qemuABActivate stages im, authorizes and activates it at the next reset, and
// checks OTA_INFO names it in the store.
func qemuABActivate(t *testing.T, dev *tfw.Device, im qemuABImage) {
	t.Helper()
	ctx := context.Background()
	require.NoError(t, dev.StageImage(ctx, im.img), "StageImage")
	state, got, err := dev.OTAStatus(ctx)
	require.NoError(t, err, "OTA_STATUS after OTA_END")
	require.Equal(t, tfw.OTAComplete, state, "OTA_STATUS after OTA_END")
	require.EqualValues(t, len(im.img), got, "bytes received")

	require.NoError(t, dev.OTAAuth(ctx, im.sig), "OTA_AUTH")
	require.NoError(t, dev.OTAActivate(ctx, tfw.OTAActivateAtReset), "OTA_ACTIVATE at the next reset")

	si, err := dev.OTAInfo(ctx, 1)
	require.NoError(t, err, "OTA_INFO 1 after OTA_ACTIVATE")
	require.EqualValues(t, qemuOTAMarked, si.State, "OTA_INFO 1 after OTA_ACTIVATE: the MARK")
	require.Equal(t, im.version, si.ImageVersion, "OTA_INFO 1 after OTA_ACTIVATE: the version")
	require.Equal(t, im.measure, hex.EncodeToString(si.Measurement[:]), "OTA_INFO 1 after OTA_ACTIVATE: the measurement")
}

// qemuABConfirm confirms the image on trial with the row key's OTA_CONFIRM
// authorization. The application puts the signature in the confirm request
// (src/core/ota_ctl.c) and tfw-boot verifies it over the confirm's own
// Sig_structure before it settles the trial (#231, boot/boot_swap.c): the
// activate's never verifies there, and the trial falls back at the reset.
func qemuABConfirm(t *testing.T, dev *tfw.Device, im qemuABImage) error {
	t.Helper()
	ctx := context.Background()
	require.NoError(t, dev.OTAAuth(ctx, im.confirm), "OTA_AUTH before OTA_CONFIRM")
	return dev.OTAConfirm(ctx)
}

// qemuABSlot0 reads OTA_INFO 0 and checks the fields every A/B reply shares.
func qemuABSlot0(t *testing.T, dev *tfw.Device, what string) *tfw.SlotInfo {
	t.Helper()
	si, err := dev.OTAInfo(context.Background(), 0)
	require.NoError(t, err, "OTA_INFO 0 %s", what)
	require.EqualValues(t, 2, si.Slots, "OTA_INFO 0 %s: slots", what)
	require.EqualValues(t, 0, si.Active, "OTA_INFO 0 %s: active", what)
	require.EqualValues(t, 0, si.ImageVersion, "OTA_INFO 0 %s: the running image's version is attestation's", what)
	return si
}

// qemuABReset is the QMP system_reset, then the new boot's first byte.
func qemuABReset(t *testing.T, target peer.Target, dev **tfw.Device, conn *io.Closer) {
	t.Helper()
	require.NoError(t, (*conn).Close())
	c, d := qemuBoot(t, target)
	*conn, *dev = c, d
}

// qemuABConfirmed takes the factory image to staged.bin, confirmed: the floor
// is 2 after it.
func qemuABConfirmed(t *testing.T, target peer.Target, staged qemuABImage) (io.Closer, *tfw.Device, string) {
	t.Helper()
	c, dev := qemuBoot(t, target)
	old := qemuRunning(t, dev)
	require.NotEqual(t, staged.measure, old,
		"the image running already measures as staged.bin: the swap would show nothing")
	qemuABActivate(t, dev, staged)

	qemuABReset(t, target, &dev, &c)
	require.Equal(t, staged.measure, qemuRunning(t, dev), "the image on trial after the swap is not staged.bin")
	si := qemuABSlot0(t, dev, "on trial")
	require.EqualValues(t, qemuBootTrial, si.State, "OTA_INFO 0 on trial: state")
	require.EqualValues(t, tfw.SlotTrial, si.Flags, "OTA_INFO 0 on trial: flags")
	require.EqualValues(t, 0, si.Floor, "OTA_INFO 0 on trial: the floor before the confirm")
	require.NoError(t, qemuABConfirm(t, dev, staged), "OTA_CONFIRM on trial")

	// tfw-boot takes the confirm at the reset the trial ends in: GOOD, and the
	// floor at the image's version.
	qemuABReset(t, target, &dev, &c)
	require.Equal(t, staged.measure, qemuRunning(t, dev), "the confirmed image was not kept across the reset")
	si = qemuABSlot0(t, dev, "after the confirm")
	require.EqualValues(t, qemuBootGood, si.State, "OTA_INFO 0 after the confirm: state")
	require.EqualValues(t, 0, si.Flags, "OTA_INFO 0 after the confirm: flags")
	require.EqualValues(t, 2, si.Floor, "OTA_INFO 0 after the confirm: the floor")
	return c, dev, old
}

func TestQEMUABInfo(t *testing.T) {
	target, _ := qemuABTarget(t)
	_, dev := qemuBoot(t, target)

	si := qemuABSlot0(t, dev, "on the factory image")
	require.EqualValues(t, qemuBootGood, si.State, "OTA_INFO 0: state")
	require.EqualValues(t, 0, si.Flags, "OTA_INFO 0: flags")
	require.EqualValues(t, 0, si.Floor, "OTA_INFO 0: the floor cm_image wrote")
	require.Equal(t, [32]byte{}, si.Measurement, "OTA_INFO 0: the running image's measurement is attestation's")

	si, err := dev.OTAInfo(context.Background(), 1)
	require.NoError(t, err, "OTA_INFO 1")
	require.EqualValues(t, 2, si.Slots, "OTA_INFO 1: slots")
	require.EqualValues(t, 0, si.Active, "OTA_INFO 1: active")
	require.EqualValues(t, 0, si.Flags, "OTA_INFO 1: flags")
	require.EqualValues(t, 0, si.Floor, "OTA_INFO 1: the floor")
	require.EqualValues(t, qemuOTAIdle, si.State, "OTA_INFO 1: an empty store")
	require.EqualValues(t, 0, si.ImageVersion, "OTA_INFO 1: an empty store's version")
	require.Equal(t, [32]byte{}, si.Measurement, "OTA_INFO 1: an empty store's measurement")

	_, err = dev.OTAInfo(context.Background(), 2)
	var st tfw.Status
	require.True(t, errorsAsStatus(err, &st), "OTA_INFO 2: %v, want a device status", err)
	require.Equal(t, tfw.StatusErrRange, st, "OTA_INFO 2: there are two slots")
}

func TestQEMUABConfirm(t *testing.T) {
	target, dir := qemuABTarget(t)
	staged := qemuABLoad(t, dir, "staged.bin", "activate.sig", "confirm.sig")
	require.EqualValues(t, 2, staged.version, "staged.bin's version")

	conn, dev, _ := qemuABConfirmed(t, target, staged)

	// A second reset: nothing is left to take, and the image stays.
	qemuABReset(t, target, &dev, &conn)
	require.Equal(t, staged.measure, qemuRunning(t, dev), "the confirmed image was not kept across a second reset")
	si := qemuABSlot0(t, dev, "after a second reset")
	require.EqualValues(t, qemuBootGood, si.State, "OTA_INFO 0 after a second reset: state")
	require.EqualValues(t, 0, si.Flags, "OTA_INFO 0 after a second reset: flags")
	require.EqualValues(t, 2, si.Floor, "OTA_INFO 0 after a second reset: the floor")
}

func TestQEMUABRollback(t *testing.T) {
	target, dir := qemuABTarget(t)
	staged := qemuABLoad(t, dir, "staged.bin", "activate.sig", "confirm.sig")

	conn, dev := qemuBoot(t, target)
	old := qemuRunning(t, dev)
	require.NotEqual(t, staged.measure, old,
		"the image running already measures as staged.bin: the swap would show nothing")
	qemuABActivate(t, dev, staged)

	qemuABReset(t, target, &dev, &conn)
	require.Equal(t, staged.measure, qemuRunning(t, dev), "the image on trial after the swap is not staged.bin")
	si := qemuABSlot0(t, dev, "on trial")
	require.EqualValues(t, qemuBootTrial, si.State, "OTA_INFO 0 on trial: state")
	require.EqualValues(t, tfw.SlotTrial, si.Flags, "OTA_INFO 0 on trial: flags")

	// No confirm: the reset ends the trial, and tfw-boot swaps back.
	qemuABReset(t, target, &dev, &conn)
	require.Equal(t, old, qemuRunning(t, dev), "the unconfirmed image was not rolled back")
	si = qemuABSlot0(t, dev, "after the rollback")
	require.EqualValues(t, qemuBootGood, si.State, "OTA_INFO 0 after the rollback: state")
	require.EqualValues(t, tfw.SlotFellBack, si.Flags, "OTA_INFO 0 after the rollback: flags")
	require.EqualValues(t, 0, si.Floor, "OTA_INFO 0 after the rollback: the floor")
	si, err := dev.OTAInfo(context.Background(), 1)
	require.NoError(t, err, "OTA_INFO 1 after the rollback")
	require.EqualValues(t, qemuOTAFailed, si.State, "OTA_INFO 1 after the rollback: the MARK is forgotten")

	var st tfw.Status
	require.True(t, errorsAsStatus(qemuABConfirm(t, dev, staged), &st), "OTA_CONFIRM after the rollback: want a device status")
	require.Equal(t, tfw.StatusErrBadState, st, "OTA_CONFIRM after the rollback: no trial")
}

// qemuWatchdogBite is how long after its last pet the trial's watchdog resets
// the core: two periods of src/hal/cortex-m/mps2/hal_soc.c's 4 s, the first
// raising the NMI, the second, with the interrupt still raised, the reset.
const qemuWatchdogBite = 8 * time.Second

// The trial's watchdog bites (the ADR's §3, "the watchdog NMI"): an image on
// trial that stops petting is reset by the CMSDK watchdog, tfw-boot rolls it
// back, and SYS_INFO in the factory image reads WATCHDOG.
//
// The image is stopped from outside, through QEMU's gdbstub: the test reads
// VTOR and the running table's NMI vector (word 2, tfw_cm_unhandled, the
// park loop) and points the guest's PC at it. The main loop never runs
// again, so nothing pets. The first expiry raises the NMI, which QEMU wires
// to the watchdog's interrupt (hw/arm/mps2.c on the AN500, hw/arm/armsse.c on
// the AN505); its handler parks in the same loop; the second expiry, with the
// interrupt still raised and RESEN set, resets the system
// (hw/watchdog/cmsdk-apb-watchdog.c). tfw_cm_soc_watchdog_arm wrote the
// watchdog marker when the trial armed it, so tfw-boot's reset handler
// decodes WATCHDOG (src/hal/cortex-m/hal_reset_decode.h), 4 in
// include/tfw/types.h. No update request is in the block, so tfw-boot's
// gate stays shut (boot/boot_request.c) and the trial ends in the rollback.
func TestQEMUABWatchdog(t *testing.T) {
	target, dir := qemuABTarget(t)
	staged := qemuABLoad(t, dir, "staged.bin", "activate.sig", "confirm.sig")

	conn, dev := qemuBoot(t, target)
	old := qemuRunning(t, dev)
	require.NotEqual(t, staged.measure, old,
		"the image running already measures as staged.bin: the swap would show nothing")
	qemuABActivate(t, dev, staged)

	qemuABReset(t, target, &dev, &conn)
	require.Equal(t, staged.measure, qemuRunning(t, dev), "the image on trial after the swap is not staged.bin")
	si := qemuABSlot0(t, dev, "on trial")
	require.EqualValues(t, qemuBootTrial, si.State, "OTA_INFO 0 on trial: state")

	gdb := target.QMP + ".wdog.gdb"
	require.NoError(t, hmp(target.QMP, "gdbserver unix:"+gdb+",server=on,wait=off"))
	vtor, err := readWords(gdb, 0xE000ED08, 1)
	require.NoError(t, err, "VTOR")
	nmi, err := readWords(gdb, vtor[0]+8, 1)
	require.NoError(t, err, "the running table's NMI vector")
	require.NotZero(t, nmi[0]&1, "the NMI vector %#08x is not a Thumb address", nmi[0])
	require.NoError(t, faultAt(gdb, nmi[0]&^1))

	// The same pty: the next bytes are the factory image's boot.
	rw, ok := conn.(io.ReadWriter)
	require.True(t, ok, "OpenQMP's conn reads and writes")
	dev = tfw.New(rw, tfw.WithTimeout(10*time.Second), tfw.WithBootWait(qemuWatchdogBite+peer.StartBound))
	sys, err := dev.SysInfo(context.Background())
	require.NoError(t, err, "SYS_INFO after the watchdog's reset")
	require.Equal(t, tfw.ResetWatchdog, sys.ResetCause, "the trial's watchdog reset the core")
	require.Equal(t, old, qemuRunning(t, dev), "the image the watchdog reset was not rolled back")
	si = qemuABSlot0(t, dev, "after the watchdog's rollback")
	require.EqualValues(t, qemuBootGood, si.State, "OTA_INFO 0 after the watchdog's rollback: state")
	require.EqualValues(t, tfw.SlotFellBack, si.Flags, "OTA_INFO 0 after the watchdog's rollback: flags")
	si, err = dev.OTAInfo(context.Background(), 1)
	require.NoError(t, err, "OTA_INFO 1 after the watchdog's rollback")
	require.EqualValues(t, qemuOTAFailed, si.State, "OTA_INFO 1 after the watchdog's rollback")
	require.NoError(t, conn.Close())
}

func TestQEMUABFloor(t *testing.T) {
	target, dir := qemuABTarget(t)
	staged := qemuABLoad(t, dir, "staged.bin", "activate.sig", "confirm.sig")
	below := qemuABLoad(t, dir, "below-floor.bin", "below-floor.sig", "below-floor-confirm.sig")
	require.EqualValues(t, 2, staged.version, "staged.bin's version")
	require.EqualValues(t, 1, below.version, "below-floor.bin's version")

	conn, dev, old := qemuABConfirmed(t, target, staged)
	require.NotEqual(t, old, below.measure, "below-floor.bin measures as the factory image")
	qemuABActivate(t, dev, below)

	// tfw-boot refuses version 1 under floor 2 at the reset
	// (boot/boot_image.c tfw_boot_rollback_permit): nothing is copied, no
	// trial starts, and the confirmed image boots on.
	qemuABReset(t, target, &dev, &conn)
	require.Equal(t, staged.measure, qemuRunning(t, dev), "an image below the floor replaced the confirmed one")
	si := qemuABSlot0(t, dev, "after the refusal")
	require.EqualValues(t, qemuBootGood, si.State, "OTA_INFO 0 after the refusal: state")
	require.EqualValues(t, 0, si.Flags, "OTA_INFO 0 after the refusal: flags")
	require.EqualValues(t, 2, si.Floor, "OTA_INFO 0 after the refusal: the floor")

	var st tfw.Status
	require.True(t, errorsAsStatus(qemuABConfirm(t, dev, below), &st), "OTA_CONFIRM after the refusal: want a device status")
	require.Equal(t, tfw.StatusErrBadState, st, "OTA_CONFIRM after the refusal: no trial")
}
