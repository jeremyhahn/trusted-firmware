// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package main

import (
	"bytes"
	"context"
	"encoding/hex"
	"fmt"
	"sort"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
)

// blocks returns the template's block indices in order, so a plan, a write and
// a verify all visit them the same way. Map iteration order is random in Go,
// and a provisioning run whose step order varies between invocations is one
// whose failure partway through leaves a different part each time.
func (t *template) blocks() []uint8 {
	out := make([]uint8, 0, len(t.Blocks))
	for b := range t.Blocks {
		out = append(out, b)
	}
	sort.Slice(out, func(i, j int) bool { return out[i] < out[j] })
	return out
}

func plan(t *template, info *tfw.ElementInfo) {
	fmt.Printf("\nplan (template %q) -- DRY RUN, nothing will be written\n\n", t.Name)
	for _, b := range t.blocks() {
		fmt.Printf("  write config block %d   %s\n", b, hex.EncodeToString(t.Blocks[b][:8])+"...")
	}
	fmt.Printf("  lock config zone       summary crc 0x%04x   IRREVERSIBLE\n", t.ConfigLockCRC)
	fmt.Printf("  generate DeviceID key  slot %d\n", tfw.SlotDeviceID)
	fmt.Printf("  seed UDS from the element's TRNG  slot %d\n", tfw.SlotUDS)
	fmt.Printf("  lock data zone         summary crc 0x%04x   IRREVERSIBLE\n", t.DataLockCRC)

	fmt.Println()
	if info.ConfigLocked {
		fmt.Println("  NOTE: the config zone is already LOCKED. --commit will fail at that step,")
		fmt.Println("        and every step before it will already have been attempted.")
	}
	if info.DataLocked {
		fmt.Println("  NOTE: the data zone is already LOCKED. This part is already provisioned")
		fmt.Println("        or was provisioned differently. It cannot be reconfigured.")
	}
}

// runVerify reads the part back and compares. It writes nothing, and every call
// it makes is one that cannot: ProvInfo reads config block 0, and ProvPubKey
// recomputes a public point from a key that already exists rather than
// generating one. The separation of ProvPubKey from ProvGenKey is what makes
// this safe to run against a provisioned device.
func runVerify(ctx context.Context, dev *tfw.Device, t *template, info *tfw.ElementInfo) error {
	var problems []string

	if !info.ConfigLocked {
		problems = append(problems, "config zone is not locked")
	}
	if !info.DataLocked {
		problems = append(problems, "data zone is not locked")
	}

	// The config blocks the template writes are in blocks 2 and above, which
	// ProvInfo does not return -- it reads block 0, where the serial and the
	// lock bytes live. Comparing what we can and saying plainly what we cannot
	// is better than a green result that checked less than it implies.
	if len(t.Blocks) > 0 {
		problems = append(problems,
			"config blocks 2+ are not read back: the element does not return a locked config zone")
	}

	pub, err := dev.ProvPubKey(ctx, tfw.SlotDeviceID)
	if err != nil {
		problems = append(problems, fmt.Sprintf("DeviceID public key unreadable: %v", err))
	} else if bytes.Equal(pub, make([]byte, 64)) {
		problems = append(problems, "DeviceID slot holds no key")
	} else {
		fmt.Printf("deviceid  %s\n", hex.EncodeToString(pub))
	}

	if len(problems) > 0 {
		return &verifyError{problems}
	}
	return nil
}

// runCommit performs the irreversible sequence, in the one order that works.
//
// Config blocks, then lock config, then the keys, then lock data. The order is
// the element's: the data zone cannot be locked before the config zone, and
// slot contents cannot be written until the config zone that describes those
// slots is locked. Getting it wrong does not corrupt anything -- the element
// refuses -- but it leaves a part partway through a procedure that cannot be
// restarted.
func runCommit(ctx context.Context, dev *tfw.Device, t *template, info *tfw.ElementInfo) error {
	if info.DataLocked {
		return &commitError{"the data zone is already locked; this part cannot be reconfigured"}
	}

	if !info.ConfigLocked {
		for _, b := range t.blocks() {
			fmt.Printf("  writing config block %d\n", b)
			if err := dev.ProvWriteConfig(ctx, b, t.Blocks[b]); err != nil {
				return fmt.Errorf("config block %d: %w", b, err)
			}
		}
		fmt.Println("  locking config zone (irreversible)")
		if err := dev.ProvLock(ctx, tfw.ZoneConfig, t.ConfigLockCRC); err != nil {
			return fmt.Errorf("lock config zone: %w", err)
		}
	} else {
		fmt.Println("  config zone already locked; skipping the config steps")
	}

	fmt.Println("  generating the DeviceID key inside the element")
	pub, err := dev.ProvGenKey(ctx, tfw.SlotDeviceID)
	if err != nil {
		return fmt.Errorf("generate DeviceID key: %w", err)
	}
	fmt.Printf("  deviceid %s\n", hex.EncodeToString(pub))

	fmt.Println("  seeding the UDS from the element's TRNG")
	if err := dev.ProvSeedUDS(ctx, tfw.SlotUDS); err != nil {
		return fmt.Errorf("seed UDS: %w", err)
	}

	fmt.Println("  locking data zone (irreversible)")
	if err := dev.ProvLock(ctx, tfw.ZoneData, t.DataLockCRC); err != nil {
		return fmt.Errorf("lock data zone: %w", err)
	}

	// The public key belongs in the provisioning record, which is the authority
	// on whether this device was provisioned correctly -- the device itself
	// cannot prove that, since anything it reports about its own state an
	// attacker with code execution could also report.
	fmt.Printf("\nrecord this, it is the provisioning record's DeviceID:\n  %s\n",
		hex.EncodeToString(pub))
	return nil
}

type verifyError struct{ problems []string }

func (e *verifyError) Error() string {
	s := "the part does not match the template:"
	for _, p := range e.problems {
		s += "\n  - " + p
	}
	return s
}

type commitError struct{ why string }

func (e *commitError) Error() string { return "tfwprov: " + e.why }
