// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build tfwsim

package main

import (
	"context"
	"crypto/elliptic"
	"errors"
	"path/filepath"
	"testing"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/tfwsim"
)

// TestE2EWritePubKeyWritesASlotOnceTheConfigIsLocked runs --write-pubkey's
// path, key file to element, against tfwsim-prov: the firmware's
// PROV_WRITE_PUBKEY on the element model. The element takes no data-zone write
// before its config is locked, so the first write is refused with the slot
// untouched. After the config lock the way --commit locks it, a PEM key and
// then a raw one each read back as written, and a slot-locked slot and a slot
// that holds no public key are refused.
func TestE2EWritePubKeyWritesASlotOnceTheConfigIsLocked(t *testing.T) {
	dev := tfwsim.StartProvisioningWith(t, nil)
	ctx := context.Background()
	g := mustHex(t, p256GeneratorHex)
	pemG, err := readPubKey(writeKeyFile(t, pubKeyPEM(t, elliptic.P256(), append([]byte{0x04}, g...))))
	if err != nil {
		t.Fatal(err)
	}
	raw2G, err := readPubKey(writeKeyFile(t, mustHex(t, p256DoubleHex)))
	if err != nil {
		t.Fatal(err)
	}

	info := provInfo(ctx, t, dev)
	if info.ConfigLocked || info.DataLocked {
		t.Fatalf("tfwsim-prov started with a locked element: %+v", info)
	}
	var st tfw.Status
	if err := writePubKey(ctx, dev, tfw.SlotSecureBootKey, pemG); !errors.As(err, &st) || st != tfw.StatusErrBadState {
		t.Fatalf("--write-pubkey with the config unlocked: %v, want BAD_STATE", err)
	}

	tmpl, err := loadTemplate(filepath.Join("..", "..", "templates", "atecc608a-dice.conf"))
	if err != nil {
		t.Fatal(err)
	}
	// The template's placeholder block 2 clears SlotLocked (bytes 88-89), which
	// slot-locks every slot. Here slot 2 stays writable and slot 15 is locked.
	tmpl.Blocks[2] = append([]byte(nil), tmpl.Blocks[2]...)
	tmpl.Blocks[2][88-64], tmpl.Blocks[2][89-64] = 0xff, 0x7f
	for _, b := range tmpl.blocks() {
		if err := dev.ProvWriteConfig(ctx, b, tmpl.Blocks[b]); err != nil {
			t.Fatalf("config block %d: %v", b, err)
		}
	}
	if err := dev.ProvLock(ctx, tfw.ZoneConfig, tmpl.ConfigLockCRC); err != nil {
		t.Fatalf("lock config zone: %v", err)
	}

	if err := writePubKey(ctx, dev, tfw.SlotImageSigning, pemG); err != nil {
		t.Fatalf("--write-pubkey of G into slot 2: %v", err)
	}
	// A second write replaces the first: the slot is not locked.
	if err := writePubKey(ctx, dev, tfw.SlotImageSigning, raw2G); err != nil {
		t.Fatalf("--write-pubkey of 2G into slot 2: %v", err)
	}
	if err := writePubKey(ctx, dev, tfw.SlotSecureBootKey, pemG); !errors.As(err, &st) || st != tfw.StatusErrProtectedRegion {
		t.Fatalf("--write-pubkey into slot-locked slot 15: %v, want PROTECTED_REGION", err)
	}
	// Any slot but the two public-key slots is out of range.
	if err := writePubKey(ctx, dev, tfw.SlotDeviceID, pemG); !errors.As(err, &st) || st != tfw.StatusErrRange {
		t.Fatalf("--write-pubkey into the DeviceID slot: %v, want RANGE", err)
	}
}
