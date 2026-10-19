// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package main

import (
	"bytes"
	"crypto/sha256"
	"encoding/binary"
	"encoding/hex"
	"testing"
)

// The activate Sig_structure for measurement 00..1f, version 7, mode 0: the
// literal sdk/go/cmd/tfwctl/ota_activate_sig_test.go pins, which
// boot/boot_cose.c's bytes match.
const activateVector = "846a5369676e61747572653143a1012640583684707466772d6f74612d61637469766174655820" +
	"000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f0700"

func TestActivateMessageIsTheVector(t *testing.T) {
	s := &signer{version: 7}
	for i := range s.measurement {
		s.measurement[i] = byte(i)
	}
	want, _ := hex.DecodeString(activateVector)
	if got := s.activateMessage(0); !bytes.Equal(got, want) {
		t.Fatalf("structure %x, want %x", got, want)
	}
}

func TestConfirmMessageIsTheConfirmPayload(t *testing.T) {
	s := &signer{version: 300}
	got := s.confirmMessage()
	// ["Signature1", h'A10126', h'', bstr .cbor ["tfw-ota-confirm", bstr32, 300]]
	head := append([]byte{0x84, 0x6A}, "Signature1"...)
	head = append(head, 0x43, 0xA1, 0x01, 0x26, 0x40, 0x58, 1+1+15+2+32+3, 0x83, 0x6F)
	head = append(head, "tfw-ota-confirm"...)
	head = append(head, 0x58, 0x20)
	want := append(append(head, make([]byte, 32)...), 0x19, 0x01, 0x2C)
	if !bytes.Equal(got, want) {
		t.Fatalf("structure %x, want %x", got, want)
	}
}

// image builds an ESP-IDF image with one segment, its appended SHA-256 and a
// trailer v1 carrying version, as l0_seal.py lays it out.
func image(version uint32, measurement []byte) []byte {
	img := make([]byte, espHeaderLen)
	img[0], img[1], img[espHashAppended] = espMagic, 1, 1
	img = binary.LittleEndian.AppendUint32(img, 0x3C000000) // load address
	img = binary.LittleEndian.AppendUint32(img, 37)         // length
	img = append(img, make([]byte, 37)...)
	for len(img)%16 != 15 {
		img = append(img, 0)
	}
	img = append(img, 0xEF) // the checksum byte
	digest := sha256.Sum256(img)
	img = append(img, digest[:]...)
	tr := make([]byte, trailerLen)
	binary.LittleEndian.PutUint32(tr, trailerMagic)
	binary.LittleEndian.PutUint32(tr[trImageVersion:], version)
	if measurement == nil {
		measurement = digest[:]
	}
	copy(tr[trMeasurement:], measurement)
	return append(img, tr...)
}

func TestTrailerOfFindsTheTrailerAfterTheDigest(t *testing.T) {
	img := image(2, nil)
	version, m, err := trailerOf(img)
	if err != nil {
		t.Fatal(err)
	}
	digest := img[len(img)-trailerLen-32 : len(img)-trailerLen]
	if version != 2 || !bytes.Equal(m[:], digest) {
		t.Fatalf("version %d measurement %x", version, m)
	}
}

func TestTrailerOfRefuses(t *testing.T) {
	short := image(1, nil)
	noHash := image(1, nil)
	noHash[espHashAppended] = 0
	notESP := image(1, nil)
	notESP[0] = 0
	noMagic := image(1, nil)
	noMagic[len(noMagic)-trailerLen] ^= 1
	for name, img := range map[string][]byte{
		"not an ESP-IDF image": notESP,
		"no appended SHA-256":  noHash,
		"ends before trailer":  short[:len(short)-1],
		"no trailer magic":     noMagic,
		"measurement differs":  image(1, make([]byte, 32)),
	} {
		if _, _, err := trailerOf(img); err == nil {
			t.Errorf("%s: no error", name)
		}
	}
}
