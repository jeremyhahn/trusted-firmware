// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package main

import (
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"os"
	"strings"

	ag "github.com/jeremyhahn/arduino-generic/sdk/go"
)

// allZero reports whether every byte in b is zero.
func allZero(b []byte) bool {
	for _, v := range b {
		if v != 0 {
			return false
		}
	}
	return true
}

// hexLengthError reports a hex argument that decoded to the wrong number of
// bytes -- refused HERE, before anything is written, rather than baked into
// an image the device would refuse at boot for a reason nobody printed.
type hexLengthError struct {
	flag      string
	got, want int
}

func (e *hexLengthError) Error() string {
	return fmt.Sprintf("agprov: --%s is %d bytes, want %d", e.flag, e.got, e.want)
}

// parseFixedHex decodes hex into exactly want bytes, or a *hexLengthError.
func parseFixedHex(flag, s string, want int) ([]byte, error) {
	b, err := hex.DecodeString(s)
	if err != nil {
		return nil, fmt.Errorf("agprov: --%s: %w", flag, err)
	}
	if len(b) != want {
		return nil, &hexLengthError{flag: flag, got: len(b), want: want}
	}
	return b, nil
}

// runBakeEEPROM builds the platform-reserved EEPROM block and writes it as an
// Intel HEX file covering ONLY that block's addresses -- see
// writeIHEXPartial for why a partial file is the safe form and not a
// shortcut.
//
// This is deliberately the one agprov mode that opens no serial port: the
// serial cannot be written over the wire (src/caps/storage.c's reserved-region
// refusal is unconditional, by design -- see include/ag/boot.h), so it has to
// be in the image before the device is ever flashed, and that is a file
// operation, not a device one. See docs/device-provisioning/.
func runBakeEEPROM(outPath, serialHex, pubkeyHashHex string) error {
	serialBytes, err := parseFixedHex("serial", serialHex, ag.SerialLen)
	if err != nil {
		return err
	}
	var serial [ag.SerialLen]byte
	copy(serial[:], serialBytes)
	if allZero(serial[:]) {
		// Refused, not merely discouraged: all-zero is the RESERVED sentinel
		// for "unprovisioned" (src/core/platform_id.h, AG_ATTEST_ID_LEN zero
		// bytes) -- baking it in would produce a CRC-valid, fully
		// "provisioned" platform block whose DeviceID is byte-for-byte the
		// one a device with no platform block at all also reports. A
		// verifier -- and this tool's own --verify-equivalent reasoning --
		// has no way to tell the two apart after the fact.
		return errors.New("agprov: --serial is all zero, the reserved unprovisioned sentinel; choose a real serial")
	}

	var pubkeyHash [ag.PubkeyHashLen]byte
	if pubkeyHashHex != "" {
		hashBytes, err := parseFixedHex("pubkey-hash", pubkeyHashHex, ag.PubkeyHashLen)
		if err != nil {
			return err
		}
		copy(pubkeyHash[:], hashBytes)
		if allZero(pubkeyHash[:]) {
			// An explicit zero is never a real SHA-256 of a key, and it would
			// silence the warning below while baking exactly the block that
			// warning is about: on BOOT_SIG=ed25519-donna, ag-boot refuses
			// every image. Omitting the flag is how to ask for a zero hash.
			return errors.New("agprov: --pubkey-hash is all zero; omit the flag on the element tier, or pass the real hash for BOOT_SIG=ed25519-donna")
		}
	} else {
		// Not refused: the element tier ignores this field entirely (the
		// element itself holds the verifying key, never handed to the
		// application -- boot/boot_policy.c's AG_ERR_UNSUPPORTED path). It is
		// load-bearing only for BOOT_SIG=ed25519-donna, where a zero hash
		// means every image refuses to boot until this is set correctly, so
		// the operator is told plainly rather than finding out from a device
		// that will not come up.
		fmt.Fprintln(os.Stderr,
			"agprov: --pubkey-hash not given; baking an all-zero hash.")
		fmt.Fprintln(os.Stderr,
			"  Harmless on the element tier (BOOT_SIG=atecc608a): the pin check is skipped.")
		fmt.Fprintln(os.Stderr,
			"  On BOOT_SIG=ed25519-donna, ag-boot will refuse every image until the real")
		fmt.Fprintln(os.Stderr,
			"  hash is baked in -- see docs/secure-boot/.")
	}

	block := ag.BuildPlatformBlock(serial, pubkeyHash)

	f, err := os.Create(outPath)
	if err != nil {
		return fmt.Errorf("agprov: %w", err)
	}
	// No `defer f.Close()`: a deferred, error-discarding Close is exactly
	// wrong for a file whose correctness is the one thing this command
	// exists to produce. Both writeIHEXPartial's own error and Close's own
	// (a buffered write that fails to reach disk surfaces there, not
	// earlier) are propagated, and the success line below is the LAST
	// thing this function does -- never printed over a file that did not
	// finish writing.
	if err := writeIHEXPartial(f, 0, block[:]); err != nil {
		_ = f.Close()
		return fmt.Errorf("agprov: writing %s: %w", outPath, err)
	}
	if err := f.Close(); err != nil {
		return fmt.Errorf("agprov: closing %s: %w", outPath, err)
	}

	fmt.Printf("baked platform block -> %s\n", outPath)
	fmt.Printf("  serial       %s\n", hex.EncodeToString(serial[:]))
	fmt.Printf("  pubkey_hash  %s\n", hex.EncodeToString(pubkeyHash[:]))
	fmt.Println()
	fmt.Println("Flash it BEFORE the device is ever powered with production firmware,")
	fmt.Println("with an ISP programmer -- NOT a stock Arduino serial bootloader")
	fmt.Println("(avrdude -c wiring/arduino), and not ag-boot, which has no EEPROM-write")
	fmt.Println("command. On real hardware the serial bootloader's programmer")
	fmt.Println("has been observed to corrupt an EEPROM write silently at the protocol")
	fmt.Println("level: bytes kept, then dropped, then a verification mismatch avrdude")
	fmt.Println("itself reports. This firmware treats the resulting bad-CRC block as")
	fmt.Println("unprovisioned rather than a forged identity, which is the correct")
	fmt.Println("failure mode -- but the device will not have the serial you baked.")
	fmt.Println("See docs/device-provisioning/user.md.")
	fmt.Printf("  avrdude -c <isp-prog> -p <part> -U eeprom:w:%s:i\n", outPath)
	return nil
}

// ihexRecordLen is the number of data bytes per record. Sixteen is
// avr-objcopy's own default, chosen here for the same reason: a hex file this
// tool writes and one the standard toolchain writes should look like the same
// kind of file to anyone reading it.
const ihexRecordLen = 16

// writeIHEXPartial writes `data` (bounded by a uint16 address, this tool's
// block is 128 bytes) as Intel HEX data records starting at `base`, followed
// by an EOF record.
//
// PARTIAL ON PURPOSE. avrdude's `-U eeprom:w:file:i` writes only the
// addresses a hex file contains and leaves the rest of the part's EEPROM
// exactly as it was -- so a file holding only [base, base+len) is the whole
// safe operation, not an abbreviation of one that should cover the part. The
// alternative, a full EEPROM-sized image padded with 0xFF, would overwrite
// user data the write command already permits outside the reserved region,
// for no reason this tool has any business having an opinion about.
func writeIHEXPartial(w io.Writer, base uint16, data []byte) error {
	for off := 0; off < len(data); off += ihexRecordLen {
		end := off + ihexRecordLen
		if end > len(data) {
			end = len(data)
		}
		if err := writeIHEXRecord(w, base+uint16(off), 0x00, data[off:end]); err != nil {
			return err
		}
	}
	// The EOF record: zero-length, address zero, type 01. Its checksum is
	// fixed -- 0x00 length + 0x00 0x00 address + 0x01 type sums to 0x01, and
	// the two's-complement checksum of that is 0xFF.
	_, err := fmt.Fprintf(w, ":00000001FF\n")
	return err
}

// writeIHEXRecord writes one Intel HEX record: `:LLAAAATT<data>CC`, LL the
// byte count, AAAA the 16-bit address, TT the record type, CC the
// two's-complement checksum of every byte before it.
func writeIHEXRecord(w io.Writer, addr uint16, recType byte, data []byte) error {
	sum := byte(len(data)) + byte(addr>>8) + byte(addr) + recType
	for _, b := range data {
		sum += b
	}
	checksum := byte(0x100 - int(sum))

	if _, err := fmt.Fprintf(w, ":%02X%04X%02X", len(data), addr, recType); err != nil {
		return err
	}
	if _, err := fmt.Fprint(w, strings.ToUpper(hex.EncodeToString(data))); err != nil {
		return err
	}
	_, err := fmt.Fprintf(w, "%02X\n", checksum)
	return err
}
