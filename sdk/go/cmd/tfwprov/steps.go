// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package main

import (
	"bytes"
	"context"
	"encoding/binary"
	"encoding/hex"
	"fmt"
	"sort"
	"strings"

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
	if t.Plan == planTrustFLEX {
		planTrustFLEXSteps(t, info)
		return
	}
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
	if t.Plan == planTrustFLEX {
		return runVerifyTrustFLEX(ctx, dev, t, info)
	}
	var problems []string
	var point []byte

	if !info.ConfigLocked {
		problems = append(problems, "config zone is not locked")
	}
	if !info.DataLocked {
		problems = append(problems, "data zone is not locked")
	}

	// The config blocks the template writes are blocks 2 and 3, which
	// ProvInfo does not return -- it reads block 0, where the serial lives, and
	// the two lock bytes separately. Comparing what we can and saying plainly what we cannot
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
	if t.Plan == planTrustFLEX {
		return runCommitTrustFLEX(ctx, dev, t, info, nil)
	}
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
	if t.Plan == planTrustFLEX {
		return commitRecordedTrustFLEX(ctx, dev, t, info, recordPath)
	}
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
	if t.Plan == planTrustFLEX {
		// The point is Microchip's: the record must already name it, from
		// the commit, rather than learn it here.
		r, err := readRecord(recordPath)
		if err != nil {
			return err
		}
		if r.Cnf == nil {
			return &recordError{recordPath + " names no DeviceID point: run --commit with --record first"}
		}
	}
	if err := recordPoint(recordPath, pub); err != nil {
		return err
	}
	fmt.Printf("record: %s names this element's DeviceID\n", recordPath)
	return nil
}

// The config-zone fields the TrustFLEX plan reads (ATECC608 config map;
// DS40002515B §3.2.3 for the shipped values). Each is a little-endian 16-bit
// word: SlotConfig[n] at 20+2n, SlotLocked at 88 (bit n clear: slot n is
// slot-locked), KeyConfig[n] at 96+2n.
const (
	cfgSlotConfig = 20
	cfgSlotLocked = 88
	cfgKeyConfig  = 96

	slotConfigIsSecret         = 1 << 7  // the slot is never read in the clear
	keyConfigLockable          = 1 << 5  // option 1: a SlotLock can still lock it
	keyConfigPersistentDisable = 1 << 12 // usable only after a SecureBoot sets the Persistent Latch
)

func cfgWord(cfg []byte, off int) uint16 { return binary.LittleEndian.Uint16(cfg[off:]) }

func cfgSlotIsLocked(cfg []byte, slot uint8) bool {
	return cfgWord(cfg, cfgSlotLocked)&(1<<slot) == 0
}

// trustflexUDSLockCRC gives the SlotLock CRC of slot 6 once step 3 has seeded
// it. The UDS is drawn by the element's TRNG and never crosses the host link
// (Device.ProvSeedUDS), so the host cannot compute the CRC of slot 6's
// contents, and PROV_SLOT_LOCK carries no way for the firmware to supply it.
// Until the wire does, this is nil and --commit refuses a TrustFLEX part
// before writing anything; the tests set it.
var trustflexUDSLockCRC func(ctx context.Context, dev *tfw.Device) (uint16, error)

// atCRC16 is the element's CRC-16 (polynomial 0x8005, each byte taken least
// significant bit first, register starting at zero), the summary a SlotLock
// checks the slot's contents against. It goes on the wire low byte first.
func atCRC16(data []byte) uint16 {
	const poly = 0x8005
	var crc uint16
	for _, b := range data {
		for shift := 0; shift < 8; shift++ {
			dataBit := uint16(b>>shift) & 1
			crcBit := crc >> 15
			crc <<= 1
			if dataBit != crcBit {
				crc ^= poly
			}
		}
	}
	return crc
}

// slot15Image is slot 15's 72 bytes after PROV_WRITE_PUBKEY: X in its first
// 32-byte block, Y in its second, and the last 8 bytes zero. An element whose
// slot holds anything else answers the SlotLock with an error and locks
// nothing, so a wrong image fails safe.
func slot15Image(key []byte) []byte {
	return append(append([]byte(nil), key...), make([]byte, 8)...)
}

// readConfigZone reads the whole 128-byte config zone, block by block.
func readConfigZone(ctx context.Context, dev *tfw.Device) ([]byte, error) {
	cfg := make([]byte, 0, elementConfigLen)
	for b := uint8(0); b < 4; b++ {
		blk, err := dev.ProvConfigRead(ctx, b)
		if err != nil {
			return nil, fmt.Errorf("read config block %d: %w", b, err)
		}
		cfg = append(cfg, blk...)
	}
	return cfg, nil
}

// trustflexStops is step 1's stop conditions, from the config zone alone.
func trustflexStops(cfg []byte) []string {
	var stops []string
	for _, s := range []uint8{tfw.SlotTrustFLEXUDS, tfw.SlotSecureBootKey} {
		if cfgWord(cfg, cfgKeyConfig+2*int(s))&keyConfigLockable == 0 {
			stops = append(stops, fmt.Sprintf("slot %d is permanent (option 2): it cannot be written and slot-locked", s))
		}
		if cfgSlotIsLocked(cfg, s) {
			stops = append(stops, fmt.Sprintf("slot %d is already slot-locked (config bytes 88-89)", s))
		}
	}
	if cfgWord(cfg, cfgKeyConfig+2*int(tfw.SlotDeviceID))&keyConfigPersistentDisable != 0 {
		stops = append(stops, "slot 0 is tied to the Persistent Latch: it signs only after a SecureBoot, which tfw-boot does not run")
	}
	return stops
}

func planTrustFLEXSteps(t *template, info *tfw.ElementInfo) {
	fmt.Printf("\nplan (template %q, TrustFLEX) -- DRY RUN, nothing will be written\n\n", t.Name)
	fmt.Println("  1. read the config zone into the record; stop if slot 6 or 15 is permanent,")
	fmt.Println("     slot 0 is latched, or slot 6 or 15 is already slot-locked")
	fmt.Printf("  2. read the DeviceID point  slot %d, into the record\n", tfw.SlotDeviceID)
	fmt.Printf("  3. seed UDS from the element's TRNG  slot %d\n", tfw.SlotTrustFLEXUDS)
	fmt.Printf("  4. check HMAC keyed by slot %d  (the firmware, before the lock in step 6)\n", tfw.SlotTrustFLEXUDS)
	fmt.Printf("  5. write the image-signing key (--image-key)  slot %d\n", tfw.SlotSecureBootKey)
	fmt.Printf("  6. slot-lock slots %d and %d   IRREVERSIBLE\n", tfw.SlotTrustFLEXUDS, tfw.SlotSecureBootKey)
	fmt.Println("  7. then run --verify: slot 0 holds a key, slot 6 is not readable, slot 15 holds")
	fmt.Println("     the key, and slots 6 and 15 are slot-locked")
	fmt.Println()
	if !info.ConfigLocked || !info.DataLocked {
		fmt.Println("  NOTE: this part's zones are not both locked: it is not a pre-locked TrustFLEX")
		fmt.Println("        part, and --commit will refuse it. Use the blank-part template.")
	}
}

// runCommitTrustFLEX runs the TrustFLEX (a1) plan's steps 1 to 6 in order and
// stops at the first failure. Every stop condition is checked before anything
// is written, and no SlotLock is sent until both slots' CRCs are known.
// record, if set, takes the config zone and the slot-0 point after step 2,
// before anything is written.
func runCommitTrustFLEX(ctx context.Context, dev *tfw.Device, t *template, info *tfw.ElementInfo,
	record func(cfg, xy []byte) error) ([]byte, error) {
	if len(t.ImageKey) != 64 {
		return nil, &commitError{"the TrustFLEX plan needs the image-signing key: pass --image-key"}
	}
	if !info.ConfigLocked || !info.DataLocked {
		return nil, &commitError{"this part's zones are not both locked: it is not a pre-locked TrustFLEX part"}
	}
	if trustflexUDSLockCRC == nil {
		return nil, &commitError{fmt.Sprintf("slot %d cannot be slot-locked: its CRC is of a UDS the host never sees, "+
			"and PROV_SLOT_LOCK has no way for the firmware to supply it; nothing written", tfw.SlotTrustFLEXUDS)}
	}

	fmt.Println("  1. reading the config zone")
	cfg, err := readConfigZone(ctx, dev)
	if err != nil {
		return nil, err
	}
	if stops := trustflexStops(cfg); len(stops) > 0 {
		return nil, &commitError{"nothing written: " + strings.Join(stops, "; ")}
	}

	fmt.Println("  2. reading the DeviceID point")
	pub, err := dev.ProvPubKey(ctx, tfw.SlotDeviceID)
	if err != nil {
		return nil, fmt.Errorf("read DeviceID point: %w", err)
	}
	if bytes.Equal(pub, make([]byte, 64)) {
		return nil, &commitError{"slot 0 holds no key; nothing written"}
	}
	fmt.Printf("  deviceid %s\n", hex.EncodeToString(pub))
	if record != nil {
		if err := record(cfg, pub); err != nil {
			return nil, err
		}
	}

	fmt.Println("  3. seeding the UDS from the element's TRNG")
	if err := dev.ProvSeedUDS(ctx, tfw.SlotTrustFLEXUDS); err != nil {
		return nil, fmt.Errorf("seed UDS: %w", err)
	}

	fmt.Println("  4. the HMAC check keyed by slot 6 runs in the firmware before its lock in step 6")

	fmt.Println("  5. writing the image-signing key")
	if err := dev.ProvWritePubKey(ctx, tfw.SlotSecureBootKey, t.ImageKey); err != nil {
		return nil, fmt.Errorf("write image-signing key: %w", err)
	}

	udsCRC, err := trustflexUDSLockCRC(ctx, dev)
	if err != nil {
		return nil, fmt.Errorf("slot %d lock CRC: %w", tfw.SlotTrustFLEXUDS, err)
	}
	keyCRC := atCRC16(slot15Image(t.ImageKey))
	if udsCRC == 0 || keyCRC == 0 {
		return nil, &commitError{"a slot's lock CRC is zero, which would lock without checking; nothing locked"}
	}
	fmt.Println("  6. slot-locking slots 6 and 15 (irreversible)")
	if err := dev.ProvSlotLock(ctx, tfw.SlotTrustFLEXUDS, udsCRC); err != nil {
		return nil, fmt.Errorf("slot-lock slot %d: %w", tfw.SlotTrustFLEXUDS, err)
	}
	if err := dev.ProvSlotLock(ctx, tfw.SlotSecureBootKey, keyCRC); err != nil {
		return nil, fmt.Errorf("slot-lock slot %d: %w", tfw.SlotSecureBootKey, err)
	}

	fmt.Printf("\nrecord this, it is the provisioning record's DeviceID:\n  %s\n", hex.EncodeToString(pub))
	return pub, nil
}

func commitRecordedTrustFLEX(ctx context.Context, dev *tfw.Device, t *template, info *tfw.ElementInfo, recordPath string) error {
	var record func(cfg, xy []byte) error
	if recordPath != "" {
		// Refused before touching the element, as on a blank part.
		if _, err := readRecord(recordPath); err != nil {
			return err
		}
		record = func(cfg, xy []byte) error { return recordTrustFLEX(recordPath, cfg, xy) }
	}
	if _, err := runCommitTrustFLEX(ctx, dev, t, info, record); err != nil || recordPath == "" {
		return err
	}
	fmt.Printf("record: %s joins the serial to this DeviceID and config zone\n", recordPath)
	return nil
}

// runVerifyTrustFLEX checks step 7's expectations. It reads the config zone
// and public points and writes nothing.
func runVerifyTrustFLEX(ctx context.Context, dev *tfw.Device, t *template, info *tfw.ElementInfo) ([]byte, error) {
	var problems []string
	var point []byte
	if !info.ConfigLocked || !info.DataLocked {
		problems = append(problems, "the zones are not both locked: not a pre-locked TrustFLEX part")
	}

	cfg, err := readConfigZone(ctx, dev)
	if err != nil {
		problems = append(problems, fmt.Sprintf("config zone unreadable: %v", err))
	} else {
		if cfgWord(cfg, cfgSlotConfig+2*int(tfw.SlotTrustFLEXUDS))&slotConfigIsSecret == 0 {
			problems = append(problems, "slot 6 is readable: its SlotConfig is not secret")
		}
		for _, s := range []uint8{tfw.SlotTrustFLEXUDS, tfw.SlotSecureBootKey} {
			if !cfgSlotIsLocked(cfg, s) {
				problems = append(problems, fmt.Sprintf("slot %d is not slot-locked", s))
			}
		}
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

	key, err := dev.ProvPubKey(ctx, tfw.SlotSecureBootKey)
	switch {
	case err != nil:
		problems = append(problems, fmt.Sprintf("slot 15 unreadable: %v", err))
	case bytes.Equal(key, make([]byte, 64)):
		problems = append(problems, "slot 15 holds no key")
	case t.ImageKey != nil && !bytes.Equal(key, t.ImageKey):
		problems = append(problems, fmt.Sprintf("slot 15 holds %s, not the --image-key", hex.EncodeToString(key)))
	case t.ImageKey == nil:
		fmt.Println("unchecked slot 15 against a key: pass --image-key to compare")
	}

	if len(problems) > 0 {
		return nil, &verifyError{problems}
	}
	return point, nil
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
