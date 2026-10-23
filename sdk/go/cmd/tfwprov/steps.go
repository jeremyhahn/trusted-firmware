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
	// Says what runCommit will do: refuse a locked data zone before anything,
	// and skip the config steps on a locked config zone.
	switch {
	case info.DataLocked:
		fmt.Println("  NOTE: the data zone is already LOCKED. --commit will refuse this part: it is")
		fmt.Println("        already provisioned, or was provisioned differently, and cannot be reconfigured.")
	case info.ConfigLocked:
		fmt.Println("  NOTE: the config zone is already LOCKED. --commit will skip the config steps")
		fmt.Println("        and start at the DeviceID key; the template's config blocks are not written.")
	}
}

// runVerify reads the part back and compares. It writes nothing, and every call
// it makes is one that cannot: ProvInfo reads config block 0, and ProvPubKey
// recomputes a public point from a key that already exists rather than
// generating one. The separation of ProvPubKey from ProvGenKey is what makes
// this safe to run against a provisioned device.
func runVerify(ctx context.Context, dev *tfw.Device, t *template, info *tfw.ElementInfo) ([]byte, error) {
	var problems []string
	var point []byte

	if !info.ConfigLocked {
		problems = append(problems, "config zone is not locked")
	}
	if !info.DataLocked {
		problems = append(problems, "data zone is not locked")
	}

	// The config blocks the template writes are blocks 2 and 3, which
	// ProvInfo does not return -- it reads block 0, where the serial and the
	// lock bytes live. Comparing what we can and saying plainly what we cannot
	// is better than a green result that checked less than it implies. It is
	// said, not counted: no part can pass a check nothing can read, and every
	// template writes blocks (cli.md, "--verify").
	if len(t.Blocks) > 0 {
		fmt.Println("unchecked config blocks 2..3 are not read back: the element does not return a locked config zone")
	}

	pub, err := dev.ProvPubKey(ctx, tfw.SlotDeviceID)
	switch {
	case err != nil:
		problems = append(problems, fmt.Sprintf("DeviceID public key unreadable: %v", err))
	case bytes.Equal(pub, make([]byte, 64)):
		problems = append(problems, "DeviceID slot holds no key")
	default:
		fmt.Printf("deviceid  %s\n", hex.EncodeToString(pub))
		point = pub
	}

	if len(problems) > 0 {
		return nil, &verifyError{problems}
	}
	return point, nil
}

// runCommit performs the irreversible sequence, in the one order that works.
//
// Config blocks, then lock config, then the keys, then lock data. The order is
// the element's: the data zone cannot be locked before the config zone, and
// slot contents cannot be written until the config zone that describes those
// slots is locked. Getting it wrong does not corrupt anything -- the element
// refuses -- but it leaves a part partway through a procedure that cannot be
// restarted.
func runCommit(ctx context.Context, dev *tfw.Device, t *template, info *tfw.ElementInfo) ([]byte, error) {
	if info.DataLocked {
		return nil, &commitError{"the data zone is already locked; this part cannot be reconfigured"}
	}

	if !info.ConfigLocked {
		for _, b := range t.blocks() {
			fmt.Printf("  writing config block %d\n", b)
			if err := dev.ProvWriteConfig(ctx, b, t.Blocks[b]); err != nil {
				return nil, fmt.Errorf("config block %d: %w", b, err)
			}
		}
		fmt.Println("  locking config zone (irreversible)")
		if err := dev.ProvLock(ctx, tfw.ZoneConfig, t.ConfigLockCRC); err != nil {
			return nil, fmt.Errorf("lock config zone: %w", err)
		}
	} else {
		fmt.Println("  config zone already locked; skipping the config steps")
	}

	fmt.Println("  generating the DeviceID key inside the element")
	pub, err := dev.ProvGenKey(ctx, tfw.SlotDeviceID)
	if err != nil {
		return nil, fmt.Errorf("generate DeviceID key: %w", err)
	}
	fmt.Printf("  deviceid %s\n", hex.EncodeToString(pub))

	fmt.Println("  seeding the UDS from the element's TRNG")
	if err := dev.ProvSeedUDS(ctx, tfw.SlotUDS); err != nil {
		return nil, fmt.Errorf("seed UDS: %w", err)
	}

	fmt.Println("  locking data zone (irreversible)")
	if err := dev.ProvLock(ctx, tfw.ZoneData, t.DataLockCRC); err != nil {
		return nil, fmt.Errorf("lock data zone: %w", err)
	}

	// The public key belongs in the provisioning record, which is the authority
	// on whether this device was provisioned correctly -- the device itself
	// cannot prove that, since anything it reports about its own state an
	// attacker with code execution could also report.
	fmt.Printf("\nrecord this, it is the provisioning record's DeviceID:\n  %s\n",
		hex.EncodeToString(pub))
	return pub, nil
}

// commitRecorded is --commit, and with --record it joins the generated point
// to the record --bake-eeprom started. The record is checked before the
// element is touched: a refusal after the data zone locks would leave a
// device no record names.
func commitRecorded(ctx context.Context, dev *tfw.Device, t *template, info *tfw.ElementInfo, recordPath string) error {
	if recordPath != "" {
		if err := recordReadyForPoint(recordPath); err != nil {
			return err
		}
	}
	pub, err := runCommit(ctx, dev, t, info)
	if err != nil || recordPath == "" {
		return err
	}
	if err := recordPoint(recordPath, pub); err != nil {
		return err
	}
	fmt.Printf("record: %s joins the serial to this DeviceID\n", recordPath)
	return nil
}

// verifyRecorded is --verify, and with --record it checks the element's point
// against the record, or joins it when --commit ran without one.
func verifyRecorded(ctx context.Context, dev *tfw.Device, t *template, info *tfw.ElementInfo, recordPath string) error {
	pub, err := runVerify(ctx, dev, t, info)
	if err != nil || recordPath == "" {
		return err
	}
	if err := recordPoint(recordPath, pub); err != nil {
		return err
	}
	fmt.Printf("record: %s names this element's DeviceID\n", recordPath)
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

func (e *commitError) Error() string { return e.why }
