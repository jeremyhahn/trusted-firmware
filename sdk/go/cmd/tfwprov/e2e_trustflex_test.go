// SPDX-License-Identifier: Apache-2.0

//go:build tfwsim

package main

import (
	"bytes"
	"context"
	"path/filepath"
	"testing"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/tfwsim"
)

// TestE2ETrustFLEXCommitSlotLocksSlots6And15 runs the TrustFLEX (a1) plan of
// #53 against tfwsim-prov started on a pre-locked ATECC608C-TFLXTLS
// (--trustflex-element): the firmware seeds slot 6, takes its lock CRC from
// that seed (PROV_SLOT_LOCK with crc 0), and slot 15 is locked over the
// --image-key. --verify then passes, and a second --commit is refused before
// it writes anything.
func TestE2ETrustFLEXCommitSlotLocksSlots6And15(t *testing.T) {
	dev := tfwsim.StartProvisioningWith(t, []string{"--trustflex-element"})
	ctx := context.Background()
	tmpl, err := loadTemplate(filepath.Join("..", "..", "templates", "atecc608a-trustflex.conf"))
	if err != nil {
		t.Fatal(err)
	}
	tmpl.ImageKey = mustHex(t, p256GeneratorHex)

	info := provInfo(ctx, t, dev)
	if !info.ConfigLocked || !info.DataLocked {
		t.Fatalf("--trustflex-element started with a zone open: %+v", info)
	}
	if err := commitRecorded(ctx, dev, tmpl, info, ""); err != nil {
		t.Fatalf("--commit of the TrustFLEX template: %v", err)
	}

	cfg, err := readConfigZone(ctx, dev)
	if err != nil {
		t.Fatal(err)
	}
	for _, s := range []uint8{tfw.SlotTrustFLEXUDS, tfw.SlotSecureBootKey} {
		if !cfgSlotIsLocked(cfg, s) {
			t.Fatalf("slot %d is not slot-locked after --commit (config 88-89 = %x)", s, cfg[88:90])
		}
	}
	key, err := dev.ProvPubKey(ctx, tfw.SlotSecureBootKey)
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(key, tmpl.ImageKey) {
		t.Fatalf("slot 15 holds %x, want the --image-key", key)
	}

	if err := verifyRecorded(ctx, dev, tmpl, provInfo(ctx, t, dev), ""); err != nil {
		t.Fatalf("--verify after --commit: %v", err)
	}

	// The second --commit stops at step 1, slots 6 and 15 already
	// slot-locked: the config zone and slot 15 are as the first left them.
	if err := commitRecorded(ctx, dev, tmpl, provInfo(ctx, t, dev), ""); err == nil {
		t.Fatal("a second --commit of the TrustFLEX template succeeded")
	}
	again, err := readConfigZone(ctx, dev)
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(again, cfg) {
		t.Fatalf("a refused --commit changed the config zone:\n%x\nwant\n%x", again, cfg)
	}
	if key, err := dev.ProvPubKey(ctx, tfw.SlotSecureBootKey); err != nil || !bytes.Equal(key, tmpl.ImageKey) {
		t.Fatalf("a refused --commit changed slot 15: %x, %v", key, err)
	}
}
