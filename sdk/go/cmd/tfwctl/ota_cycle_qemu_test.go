// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build qemu

package main

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

// qemuStagedEnv names the directory cm_image wrote staged.bin and
// activate.sig into (test/integration/cortex-m/cm_image.c).
const qemuStagedEnv = "TFW_QEMU_STAGED"

// The trailer's fields this row reads (include/tfw/boot.h), and where the
// evidence frame carries the running image's measurement (docs/attest).
const (
	qemuTrailerLen   = 128
	qemuTrImageLen   = 12
	qemuTrMeasure    = 16
	qemuEvidenceHash = 33
)

// The OTA cycle on a QEMU MPS2 board's store row (docs/cortex-m/README.md):
// the image running is the application built with the stager, and the row
// stages the base application, signed at version 2, ends the session,
// authorizes and activates it at the next reset, resets through QMP, and
// checks in the boot that follows -- before any further reset -- that the
// image running is the one it staged: ATTEST's evidence carries the
// measurement tfw-boot handed off, which must be staged.bin's, and the image
// has no stager, so OTA_STATUS is unknown. The board is single-slot (no
// BOOT_AB), so OTA_INFO and OTA_CONFIRM are refused as unknown commands:
// checked by status code, as CAPS has no OTA bit to read.
//
// test/integration/cortex-m/qemu_wire_test.sh starts it on a QEMU of its own
// (`make cortex-m-qemu-cycle`), so the store holds no MARK another row left.
// Every wait is the SDK's or the peer seam's, and each is bounded.
func TestQEMUOTACycle(t *testing.T) {
	target, err := peer.FromEnv()
	require.NoError(t, err)
	require.NotEmpty(t, target.QMP,
		"%s names no QMP socket: run `make integration-test-cortex-m-wire`, whose store row starts this row", peer.ResetEnv)
	dir := os.Getenv(qemuStagedEnv)
	require.NotEmpty(t, dir, "%s is not set: run `make integration-test-cortex-m-wire`", qemuStagedEnv)

	img, err := os.ReadFile(filepath.Join(dir, "staged.bin"))
	require.NoError(t, err)
	raw, err := os.ReadFile(filepath.Join(dir, "activate.sig"))
	require.NoError(t, err)
	require.Len(t, raw, tfw.OTAAuthSigLen, "activate.sig")
	var sig [tfw.OTAAuthSigLen]byte
	copy(sig[:], raw)

	// The measurement the image must report once it runs: its trailer's,
	// checked against its bytes so a bad file fails here and not at the boot.
	require.Greater(t, len(img), qemuTrailerLen, "staged.bin")
	tr := img[len(img)-qemuTrailerLen:]
	n := int(binary.LittleEndian.Uint32(tr[qemuTrImageLen:]))
	require.LessOrEqual(t, n, len(img)-qemuTrailerLen, "staged.bin's image length")
	want := sha256.Sum256(img[:n])
	require.Equal(t, hex.EncodeToString(want[:]), hex.EncodeToString(tr[qemuTrMeasure:qemuTrMeasure+sha256.Size]),
		"staged.bin's trailer measurement")

	ctx := context.Background()
	conn, dev := qemuBoot(t, target)
	require.NotEqual(t, hex.EncodeToString(want[:]), qemuRunning(t, dev),
		"the image running already measures as staged.bin: the cycle would show nothing")

	require.NoError(t, dev.StageImage(ctx, img), "StageImage")
	state, got, err := dev.OTAStatus(ctx)
	require.NoError(t, err, "OTA_STATUS after OTA_END")
	require.Equal(t, tfw.OTAComplete, state, "OTA_STATUS after OTA_END")
	require.EqualValues(t, len(img), got, "bytes received")

	_, err = dev.OTAInfo(ctx, 1)
	qemuRefused(t, err, "OTA_INFO on a single-slot board")

	require.NoError(t, dev.OTAAuth(ctx, sig), "OTA_AUTH")
	require.NoError(t, dev.OTAActivate(ctx, 0), "OTA_ACTIVATE at the next reset")

	// After the activate, so the authorization it took is spent and the
	// refusal is the command's own. An unknown command is refused before
	// its payload is read (src/core/dispatch.c).
	_, err = dev.Do(ctx, tfw.CmdOTAConfirm, nil)
	qemuRefused(t, err, "OTA_CONFIRM on a single-slot board")

	// The reset: the QMP seam's system_reset, then the new boot's first byte.
	require.NoError(t, conn.Close())
	_, dev = qemuBoot(t, target)
	require.Equal(t, hex.EncodeToString(want[:]), qemuRunning(t, dev),
		"the image tfw-boot handed off after the reset is not staged.bin")
	_, err = dev.Do(ctx, tfw.CmdOTAStatus, nil)
	qemuRefused(t, err, "OTA_STATUS on the installed image, which has no stager")
}

// qemuBoot resets the guest through the QMP seam and opens its UART once the
// new boot speaks; both waits are internal/peer's, bounded by StartBound.
func qemuBoot(t *testing.T, target peer.Target) (io.Closer, *tfw.Device) {
	t.Helper()
	conn, err := peer.OpenQMP(target.Port, target.QMP, peer.StartBound)
	require.NoError(t, err)
	t.Cleanup(func() { _ = conn.Close() })
	return conn, tfw.New(conn, tfw.WithTimeout(10*time.Second))
}

// qemuRunning is the measurement of the image running, as ATTEST reports it.
func qemuRunning(t *testing.T, dev *tfw.Device) string {
	t.Helper()
	nonce := make([]byte, tfw.AttestNonceLen)
	for i := range nonce {
		nonce[i] = byte(0xC0 + i)
	}
	ev, err := dev.Attest(context.Background(), nonce)
	require.NoError(t, err, "ATTEST")
	require.Greater(t, len(ev), qemuEvidenceHash+sha256.Size, "the evidence frame")
	return hex.EncodeToString(ev[qemuEvidenceHash : qemuEvidenceHash+sha256.Size])
}

// qemuRefused requires err to be the device's ERR_UNKNOWN_CMD.
func qemuRefused(t *testing.T, err error, what string) {
	t.Helper()
	var st tfw.Status
	require.True(t, errorsAsStatus(err, &st), "%s: %v, want a device status", what, err)
	require.Equal(t, tfw.StatusErrUnknownCmd, st, what)
}
