// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package main

import (
	"crypto/ecdh"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"os"
	"strings"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
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

// requireOnCurveP256 rejects a 64-byte X||Y pair that is not an actual point
// on the P-256 curve. crypto/ecdh validates this the same way it would
// before ever using the point, which is the whole reason to reach for it
// here rather than parsing the coordinates by hand: a hand-rolled check that
// missed a case would bake exactly the silently-wrong image this exists to
// catch.
func requireOnCurveP256(xy []byte) error {
	if len(xy) != tfw.OperatorPubkeyLen {
		return fmt.Errorf("%d bytes, want %d", len(xy), tfw.OperatorPubkeyLen)
	}
	// crypto/ecdh takes the SEC1 uncompressed form, 0x04 || X || Y.
	sec1 := append([]byte{0x04}, xy...)
	if _, err := ecdh.P256().NewPublicKey(sec1); err != nil {
		return fmt.Errorf("not a point on P-256: %w", err)
	}
	return nil
}

// hexLengthError reports a hex argument that decoded to the wrong number of
// bytes -- refused HERE, before anything is written, rather than baked into
// an image the device would refuse at boot for a reason nobody printed.
type hexLengthError struct {
	flag      string
	got, want int
}

func (e *hexLengthError) Error() string {
	return fmt.Sprintf("tfwprov: --%s is %d bytes, want %d", e.flag, e.got, e.want)
}

// parseFixedHex decodes hex into exactly want bytes, or a *hexLengthError.
func parseFixedHex(flag, s string, want int) ([]byte, error) {
	b, err := hex.DecodeString(s)
	if err != nil {
		return nil, fmt.Errorf("tfwprov: --%s: %w", flag, err)
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
// This is deliberately the one tfwprov mode that opens no serial port: the
// serial cannot be written over the wire (src/caps/storage.c's reserved-region
// refusal is unconditional, by design -- see include/tfw/boot.h), so it has to
// be in the image before the device is ever flashed, and that is a file
// operation, not a device one. See docs/device-provisioning/.
func runBakeEEPROM(outPath, serialHex, pubkeyHashHex, operatorPubkeyHex string) error {
	serialBytes, err := parseFixedHex("serial", serialHex, tfw.SerialLen)
	if err != nil {
		return err
	}
	var serial [tfw.SerialLen]byte
	copy(serial[:], serialBytes)
	if allZero(serial[:]) {
		// Refused, not merely discouraged: all-zero is the RESERVED sentinel
		// for "unprovisioned" (src/core/platform_id.h, TFW_ATTEST_ID_LEN zero
		// bytes) -- baking it in would produce a CRC-valid, fully
		// "provisioned" platform block whose DeviceID is byte-for-byte the
		// one a device with no platform block at all also reports. A
		// verifier -- and this tool's own --verify-equivalent reasoning --
		// has no way to tell the two apart after the fact.
		return errors.New("tfwprov: --serial is all zero, the reserved unprovisioned sentinel; choose a real serial")
	}

	var pubkeyHash [tfw.PubkeyHashLen]byte
	if pubkeyHashHex != "" {
		hashBytes, err := parseFixedHex("pubkey-hash", pubkeyHashHex, tfw.PubkeyHashLen)
		if err != nil {
			return err
		}
		copy(pubkeyHash[:], hashBytes)
		if allZero(pubkeyHash[:]) {
			// An explicit zero is never a real SHA-256 of a key, and it would
			// silence the warning below while baking exactly the block that
			// warning is about: on BOOT_SIG=ed25519-donna, tfw-boot refuses
			// every image. Omitting the flag is how to ask for a zero hash.
			return errors.New("tfwprov: --pubkey-hash is all zero; omit the flag on the element tier, or pass the real hash for BOOT_SIG=ed25519-donna")
		}
	} else {
		// Not refused: the element tier ignores this field entirely (the
		// element itself holds the verifying key, never handed to the
		// application -- boot/boot_policy.c's TFW_ERR_UNSUPPORTED path). It is
		// load-bearing only for BOOT_SIG=ed25519-donna, where a zero hash
		// means every image refuses to boot until this is set correctly, so
		// the operator is told plainly rather than finding out from a device
		// that will not come up.
		fmt.Fprintln(os.Stderr,
			"tfwprov: --pubkey-hash not given; baking an all-zero hash.")
		fmt.Fprintln(os.Stderr,
			"  Harmless on the element tier (BOOT_SIG=atecc608a): the pin check is skipped.")
		fmt.Fprintln(os.Stderr,
			"  On BOOT_SIG=ed25519-donna, tfw-boot will refuse every image until the real")
		fmt.Fprintln(os.Stderr,
			"  hash is baked in -- see docs/secure-boot/.")
	}

	// The AUTH_CHALLENGE/AUTH_RESPONSE session's trust anchor (issue #4).
	// Element-tier only: a soft-tier or CRYPTO=none image never compiles in
	// the commands that would read this field, so baking zero here is not a
	// gap on those tiers, only on an element-tier image that wants the
	// session to work -- and there the device will simply refuse every
	// AUTH_RESPONSE (TFW_ERR_CRYPTO, no signature verifies against an all-zero
	// point) until the real key is baked in, the same shape as the
	// pubkey-hash gap above.
	var operatorPubkey [tfw.OperatorPubkeyLen]byte
	if operatorPubkeyHex != "" {
		opBytes, err := parseFixedHex("operator-pubkey", operatorPubkeyHex, tfw.OperatorPubkeyLen)
		if err != nil {
			return err
		}
		// Length-correct is not point-correct: a single hex typo still
		// decodes to 64 bytes, and without this check it would bake a
		// CRC-valid image the device refuses every AUTH_RESPONSE against,
		// discoverable only by re-flashing and trying. Checked here, before
		// the CRC is even computed, the same way a bad --serial length is.
		if err := requireOnCurveP256(opBytes); err != nil {
			return fmt.Errorf("tfwprov: --operator-pubkey: %w", err)
		}
		copy(operatorPubkey[:], opBytes)
	} else {
		fmt.Fprintln(os.Stderr,
			"tfwprov: --operator-pubkey not given; baking an all-zero key.")
		fmt.Fprintln(os.Stderr,
			"  AUTH_CHALLENGE/AUTH_RESPONSE will refuse every response until the real")
		fmt.Fprintln(os.Stderr,
			"  operator public key (P-256, X||Y, 64 bytes) is baked in.")
	}

	block := tfw.BuildPlatformBlock(serial, pubkeyHash, operatorPubkey)

	f, err := os.Create(outPath)
	if err != nil {
		return fmt.Errorf("tfwprov: %w", err)
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
		return fmt.Errorf("tfwprov: writing %s: %w", outPath, err)
	}
	if err := f.Close(); err != nil {
		return fmt.Errorf("tfwprov: closing %s: %w", outPath, err)
	}

	fmt.Printf("baked platform block -> %s\n", outPath)
	fmt.Printf("  serial          %s\n", hex.EncodeToString(serial[:]))
	fmt.Printf("  pubkey_hash     %s\n", hex.EncodeToString(pubkeyHash[:]))
	fmt.Printf("  operator_pubkey %s\n", hex.EncodeToString(operatorPubkey[:]))
	fmt.Println()
	fmt.Println("Flash it BEFORE the device is ever powered with production firmware,")
	fmt.Println("with an ISP programmer. tfw-boot has no EEPROM-write command. A stock")
	fmt.Println("Arduino serial bootloader (avrdude -c wiring/arduino) is a bench path")
	fmt.Println("only, and its verify cannot be trusted: the Mega 2560's STK500v2")
	fmt.Println("bootloader writes EEPROM correctly but reads it back from the wrong")
	fmt.Println("address, so avrdude reports a mismatch on a write that landed. On a")
	fmt.Println("bench board, `make platform-bake` writes the block and checks it on")
	fmt.Println("the part. See docs/device-provisioning/user.md.")
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
