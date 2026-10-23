// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build tfwsim

package main

import (
	"bytes"
	"context"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"math/big"
	"path/filepath"
	"testing"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/tfwsim"
)

// The evidence frame's fields (include/tfw/protocol.h): the 16-byte DeviceID,
// which is the serial the platform block carries; the algorithm byte, before
// which everything is signed; and the raw r||s signature.
const (
	offDevice = 17
	offAlg    = 73
	offSig    = 74
)

// TestE2ERecordJoinsTheBakedSerialToTheElementsPoint runs the record across
// the two runs an operator makes, against tfwsim-prov: the element-tier
// firmware linked as a provisioning jig image, booted from the image the first
// run baked, with a blank element. --commit then runs the real PROV_* sequence,
// and the record must name the key the element generated and the serial the
// device claims in the evidence that key signs.
func TestE2ERecordJoinsTheBakedSerialToTheElementsPoint(t *testing.T) {
	dir := t.TempDir()
	eep, rec := filepath.Join(dir, "device.eep"), filepath.Join(dir, "device.json")
	serial := testSerial(t, hex.EncodeToString([]byte("tfwprov-record-1")))

	code, first := runMain(t, "--bake-eeprom", "--serial", hex.EncodeToString(serial[:]),
		"--out", eep, "--record", rec)
	if code != 0 {
		t.Fatalf("--bake-eeprom --record: exit %d: %s", code, first)
	}
	baked := readFile(t, rec)

	dev := tfwsim.StartProvisioningWith(t, []string{"--eeprom", eep,
		"--image-hash", hex.EncodeToString(tfwsim.TestFWHash[:])})
	ctx := context.Background()
	tmpl, err := loadTemplate(filepath.Join("..", "..", "templates", "atecc608a-dice.conf"))
	if err != nil {
		t.Fatal(err)
	}
	info := provInfo(ctx, t, dev)
	if info.ConfigLocked || info.DataLocked {
		t.Fatalf("tfwsim-prov started with a locked element: %+v", info)
	}

	// A record that already holds a point is another device's: refused before
	// the element is sent anything, so its zones are still open.
	other := filepath.Join(dir, "other.json")
	if err := recordSerial(other, serial); err != nil {
		t.Fatal(err)
	}
	if err := recordPoint(other, p256DoubleXY(t)); err != nil {
		t.Fatal(err)
	}
	if err := commitRecorded(ctx, dev, tmpl, info, other); err == nil {
		t.Fatal("--commit onto a record that already holds a point succeeded")
	}
	if again := provInfo(ctx, t, dev); again.ConfigLocked || again.DataLocked {
		t.Fatal("a refused --commit locked a zone")
	}
	if readFile(t, rec) != baked {
		t.Fatal("a refused --commit changed the baked record")
	}

	if err := commitRecorded(ctx, dev, tmpl, info, rec); err != nil {
		t.Fatalf("--commit --record against tfwsim-prov: %v", err)
	}

	r, err := readRecord(rec)
	if err != nil {
		t.Fatal(err)
	}
	if r.UEID != ueidOf(serial) {
		t.Fatalf("record ueid = %s, the bake wrote %s", r.UEID, ueidOf(serial))
	}
	if r.Cnf == nil {
		t.Fatal("--commit left the record without cnf")
	}
	xy, err := pointOf(r.Cnf.JWK)
	if err != nil {
		t.Fatal(err)
	}
	element, err := dev.ProvPubKey(ctx, tfw.SlotDeviceID)
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(xy, element) {
		t.Fatalf("record cnf.jwk = %x, the element's DeviceID is %x", xy, element)
	}

	// The provider decides at startup whether the element holds a UDS, so the
	// commit's key signs from the next boot on, as on a device. tfwsim-prov's
	// RESET restarts the firmware and keeps the element.
	if _, err := dev.Attest(ctx, bytes.Repeat([]byte{0x5a}, tfw.AttestNonceLen)); err == nil {
		t.Fatal("ATTEST signed before the boot that follows the commit")
	}
	if err := dev.Reset(ctx, tfw.ResetSoft); err != nil {
		t.Fatal(err)
	}

	// The evidence `tfwctl attest` fetches (Device.Attest): the UEID in the
	// record is 0x01 followed by its DeviceID field, and it verifies under the
	// record's key -- the key PROV_GENKEY made in this process.
	ev, err := dev.Attest(ctx, bytes.Repeat([]byte{0x5a}, tfw.AttestNonceLen))
	if err != nil {
		t.Fatal(err)
	}
	ueid, err := base64.RawURLEncoding.DecodeString(r.UEID)
	if err != nil {
		t.Fatal(err)
	}
	if want := append([]byte{ueidTypeRAND}, ev[offDevice:offDevice+tfw.SerialLen]...); !bytes.Equal(ueid, want) {
		t.Fatalf("record ueid = %x, the evidence claims %x", ueid, want)
	}
	if !signedBy(xy, ev) {
		t.Fatal("the evidence does not verify under the record's cnf.jwk")
	}

	// --verify says it cannot read the locked config blocks back, finds the
	// same point and leaves the record as it is. A second --commit onto it is
	// refused, and a bake of another serial onto it exits 1.
	committed := readFile(t, rec)
	if err := verifyRecorded(ctx, dev, tmpl, provInfo(ctx, t, dev), rec); err != nil {
		t.Fatalf("--verify --record after --commit: %v", err)
	}
	if err := commitRecorded(ctx, dev, tmpl, provInfo(ctx, t, dev), rec); err == nil {
		t.Fatal("a second --commit onto the committed record succeeded")
	}
	if code, first := runMain(t, "--bake-eeprom", "--serial", testSerialHex,
		"--out", filepath.Join(dir, "next.eep"), "--record", rec); code != 1 {
		t.Fatalf("a bake of another serial onto the record: exit %d: %s", code, first)
	}
	if readFile(t, rec) != committed {
		t.Fatal("--verify, a refused --commit or a refused bake changed the committed record")
	}
}

func provInfo(ctx context.Context, t *testing.T, dev *tfw.Device) *tfw.ElementInfo {
	t.Helper()
	info, err := dev.ProvInfo(ctx)
	if err != nil {
		t.Fatal(err)
	}
	return info
}

// p256DoubleXY is 2G, X||Y: a valid point no element in this test generated.
func p256DoubleXY(t *testing.T) []byte {
	t.Helper()
	xy, err := hex.DecodeString(p256DoubleHex)
	if err != nil {
		t.Fatal(err)
	}
	return xy
}

// signedBy checks an evidence frame the way a verifier does, from the layout
// alone: raw r||s over the SHA-256 of everything before the algorithm byte.
func signedBy(xy, ev []byte) bool {
	if len(ev) != tfw.EvidenceLen {
		return false
	}
	pub, err := ecdsa.ParseUncompressedPublicKey(elliptic.P256(), append([]byte{0x04}, xy...))
	if err != nil {
		return false
	}
	digest := sha256.Sum256(ev[:offAlg])
	return ecdsa.Verify(pub, digest[:], new(big.Int).SetBytes(ev[offSig:offSig+32]),
		new(big.Int).SetBytes(ev[offSig+32:offSig+64]))
}
