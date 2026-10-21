// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package main

import (
	"bytes"
	"context"
	"encoding/hex"
	"errors"
	"io/fs"
	"os"
	"path/filepath"
	"strings"
	"testing"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
)

// The record of testSerialHex joined to the P-256 generator, whose private
// key is 1: a documentation-only key nobody can mistake for a device's. The
// base64url values are literals, computed outside this code (basenc
// --base64url, padding stripped), so a change to the encoder fails here.
const goldenRecord = `{
  "ueid": "AQABAgMEBQYHCAkKCwwNDg8",
  "cnf": {
    "jwk": {
      "kty": "EC",
      "crv": "P-256",
      "x": "axfR8uEsQkf4vOblY6RA8ncDfYEt6zOg9KE5RdiYwpY",
      "y": "T-NC4v4af5uO5-tKfA-eFivOM1drMV7Oy7ZAaDe_UfU"
    }
  }
}
`

// p256DoubleHex is 2G, a second documentation-only point: "another device".
const p256DoubleHex = "7cf27b188d034f7e8a52380304b51ac3c08969e277f21b35a60b48fc47669978" +
	"07775510db8ed040293d9ac69f7430dbba7dade63ce982299e04b79d227873d1"

func mustHex(t *testing.T, s string) []byte {
	t.Helper()
	b, err := hex.DecodeString(s)
	if err != nil {
		t.Fatal(err)
	}
	return b
}

func testSerial(t *testing.T, s string) [tfw.SerialLen]byte {
	t.Helper()
	var serial [tfw.SerialLen]byte
	copy(serial[:], mustHex(t, s))
	return serial
}

func recordPath(t *testing.T) string {
	t.Helper()
	return filepath.Join(t.TempDir(), "device.json")
}

func readFile(t *testing.T, path string) string {
	t.Helper()
	b, err := os.ReadFile(path) // #nosec G304 -- a test temp file
	if err != nil {
		t.Fatal(err)
	}
	return string(b)
}

func writeFile(t *testing.T, path, s string) {
	t.Helper()
	if err := os.WriteFile(path, []byte(s), 0o600); err != nil {
		t.Fatal(err)
	}
}

func wantRecordError(t *testing.T, err error, what string) {
	t.Helper()
	var re *recordError
	if !errors.As(err, &re) {
		t.Fatalf("%s: err = %v, want a *recordError", what, err)
	}
}

func TestRecordJoinsTheSerialToThePointAsGoldenJSON(t *testing.T) {
	path := recordPath(t)
	if err := recordSerial(path, testSerial(t, testSerialHex)); err != nil {
		t.Fatal(err)
	}
	if err := recordPoint(path, mustHex(t, p256GeneratorHex)); err != nil {
		t.Fatal(err)
	}
	if got := readFile(t, path); got != goldenRecord {
		t.Fatalf("record =\n%s\nwant\n%s", got, goldenRecord)
	}
}

func TestRecordSerialAloneCarriesTheUEIDAndNoCnf(t *testing.T) {
	path := recordPath(t)
	if err := recordSerial(path, testSerial(t, testSerialHex)); err != nil {
		t.Fatal(err)
	}
	want := "{\n  \"ueid\": \"AQABAgMEBQYHCAkKCwwNDg8\"\n}\n"
	if got := readFile(t, path); got != want {
		t.Fatalf("record = %q, want %q", got, want)
	}
}

func TestUEIDIsTypeRANDFollowedByTheSerial(t *testing.T) {
	// RFC 9711 §4.2.1: type byte 0x01 (RAND), then the 16 bytes.
	if got := ueidOf(testSerial(t, "ffeeddccbbaa99887766554433221100")); got != "Af_u3cy7qpmId2ZVRDMiEQA" {
		t.Fatalf("ueid = %s", got)
	}
}

func TestRecordSerialKeepsTheSameDevicesRecord(t *testing.T) {
	path := recordPath(t)
	writeFile(t, path, goldenRecord)
	if err := recordSerial(path, testSerial(t, testSerialHex)); err != nil {
		t.Fatal(err)
	}
	if got := readFile(t, path); got != goldenRecord {
		t.Fatalf("a re-bake of the same serial rewrote the record:\n%s", got)
	}
}

func TestRecordSerialRefusesAnotherDevicesRecord(t *testing.T) {
	path := recordPath(t)
	writeFile(t, path, goldenRecord)
	err := recordSerial(path, testSerial(t, "101112131415161718191a1b1c1d1e1f"))
	wantRecordError(t, err, "another serial")
	if got := readFile(t, path); got != goldenRecord {
		t.Fatal("the refused bake touched the record")
	}
}

func TestRecordSerialRefusesAFileThatIsNotARecord(t *testing.T) {
	path := recordPath(t)
	writeFile(t, path, "not a record\n")
	err := recordSerial(path, testSerial(t, testSerialHex))
	wantRecordError(t, err, "a file that is not a record")
	if !strings.HasPrefix(err.Error(), "--record: ") {
		t.Fatalf("message = %q", err)
	}
	if got := readFile(t, path); got != "not a record\n" {
		t.Fatal("the refused bake overwrote the file")
	}
}

func TestRecordPointRefusesAnotherDevicesPoint(t *testing.T) {
	path := recordPath(t)
	writeFile(t, path, goldenRecord)
	wantRecordError(t, recordPoint(path, mustHex(t, p256DoubleHex)), "another point")
	if got := readFile(t, path); got != goldenRecord {
		t.Fatal("the refused join touched the record")
	}
	if err := recordPoint(path, mustHex(t, p256GeneratorHex)); err != nil {
		t.Fatalf("the same point again: %v", err)
	}
}

func TestRecordPointRefusesAPointNotOnTheCurve(t *testing.T) {
	path := recordPath(t)
	if err := recordSerial(path, testSerial(t, testSerialHex)); err != nil {
		t.Fatal(err)
	}
	for name, xy := range map[string][]byte{
		"off the curve": bytes.Repeat([]byte{0x11}, 64),
		"short":         mustHex(t, p256GeneratorHex)[:63],
	} {
		wantRecordError(t, recordPoint(path, xy), name)
	}
}

func TestRecordPointAndReadyNeedTheBakedRecord(t *testing.T) {
	path := recordPath(t)
	if err := recordPoint(path, mustHex(t, p256GeneratorHex)); !errors.Is(err, fs.ErrNotExist) {
		t.Fatalf("recordPoint with no record: %v", err)
	}
	if err := recordReadyForPoint(path); !errors.Is(err, fs.ErrNotExist) {
		t.Fatalf("recordReadyForPoint with no record: %v", err)
	}
	writeFile(t, path, goldenRecord)
	wantRecordError(t, recordReadyForPoint(path), "a record that already holds a point")
}

func TestReadRecordRefusesWhatIsNotARecord(t *testing.T) {
	jwk := func(kty, crv, x, y string) string {
		return `{"ueid":"AQABAgMEBQYHCAkKCwwNDg8","cnf":{"jwk":{"kty":"` + kty + `","crv":"` + crv +
			`","x":"` + x + `","y":"` + y + `"}}}`
	}
	gx, gy := "axfR8uEsQkf4vOblY6RA8ncDfYEt6zOg9KE5RdiYwpY", "T-NC4v4af5uO5-tKfA-eFivOM1drMV7Oy7ZAaDe_UfU"
	for name, body := range map[string]string{
		"not JSON":              "ueid=AQ",
		"an unknown member":     `{"ueid":"AQABAgMEBQYHCAkKCwwNDg8","sn":"x"}`,
		"a padded ueid":         `{"ueid":"AQABAgMEBQYHCAkKCwwNDg8="}`,
		"a ueid of type 0x02":   `{"ueid":"AgABAgMEBQYHCAkKCwwNDg8"}`,
		"a short ueid":          `{"ueid":"AQABAgMEBQYHCAkKCwwNDg"}`,
		"no ueid":               `{}`,
		"cnf without jwk":       `{"ueid":"AQABAgMEBQYHCAkKCwwNDg8","cnf":{}}`,
		"an RSA jwk":            jwk("RSA", "P-256", gx, gy),
		"a P-384 jwk":           jwk("EC", "P-384", gx, gy),
		"a short x":             jwk("EC", "P-256", gx[:40], gy),
		"a y not base64url":     jwk("EC", "P-256", gx, strings.ReplaceAll(gy, "-", "+")),
		"a point off the curve": jwk("EC", "P-256", gy, gx),
	} {
		path := recordPath(t)
		writeFile(t, path, body)
		_, err := readRecord(path)
		wantRecordError(t, err, name)
	}
}

func TestWriteRecordReportsWhereItCannotWrite(t *testing.T) {
	r := &provRecord{UEID: ueidOf(testSerial(t, testSerialHex))}
	if err := writeRecord(filepath.Join(t.TempDir(), "no", "such", "dir.json"), r); err == nil {
		t.Fatal("writeRecord into a missing directory succeeded")
	}
	// A directory where the record goes: the rename onto it fails, and the
	// temporary file is not left behind.
	dir := t.TempDir()
	target := filepath.Join(dir, "device.json")
	if err := os.Mkdir(target, 0o700); err != nil {
		t.Fatal(err)
	}
	if err := writeRecord(target, r); err == nil {
		t.Fatal("writeRecord onto a directory succeeded")
	}
	if left, _ := os.ReadDir(dir); len(left) != 1 {
		t.Fatalf("left behind: %v", left)
	}
}

func TestRunBakeStartsTheRecordBeforeTheImage(t *testing.T) {
	dir := t.TempDir()
	out, path := filepath.Join(dir, "device.eep"), filepath.Join(dir, "device.json")
	if err := runBake(out, true, testSerialHex, "", "", false, netblockFlags{}, path); err != nil {
		t.Fatal(err)
	}
	if _, err := readRecord(path); err != nil {
		t.Fatal(err)
	}
	// Another serial against that record: refused, and no image is written.
	other := filepath.Join(dir, "other.eep")
	err := runBake(other, true, "101112131415161718191a1b1c1d1e1f", "", "", false, netblockFlags{}, path)
	wantRecordError(t, err, "a bake of another serial")
	if _, err := os.Stat(other); !errors.Is(err, fs.ErrNotExist) {
		t.Fatalf("the refused bake wrote an image: %v", err)
	}
}

func TestCommitRecordedJoinsTheGeneratedPoint(t *testing.T) {
	path := recordPath(t)
	if err := recordSerial(path, testSerial(t, testSerialHex)); err != nil {
		t.Fatal(err)
	}
	e := &element{pub: mustHex(t, p256GeneratorHex)}
	if err := commitRecorded(context.Background(), startElement(t, e), testTemplate(), &tfw.ElementInfo{}, path); err != nil {
		t.Fatal(err)
	}
	if got := readFile(t, path); got != goldenRecord {
		t.Fatalf("record after --commit =\n%s", got)
	}
}

func TestCommitRecordedRefusesBeforeTouchingTheElement(t *testing.T) {
	path := recordPath(t)
	writeFile(t, path, goldenRecord)
	e := &element{pub: mustHex(t, p256DoubleHex)}
	err := commitRecorded(context.Background(), startElement(t, e), testTemplate(), &tfw.ElementInfo{}, path)
	wantRecordError(t, err, "a committed device's record")
	if got := e.commands(); len(got) != 0 {
		t.Fatalf("the refused commit sent %v", got)
	}
}

func TestCommitRecordedLeavesTheRecordWhenTheCommitFails(t *testing.T) {
	path := recordPath(t)
	if err := recordSerial(path, testSerial(t, testSerialHex)); err != nil {
		t.Fatal(err)
	}
	before := readFile(t, path)
	e := &element{pub: mustHex(t, p256GeneratorHex)}
	err := commitRecorded(context.Background(), startElement(t, e), testTemplate(), &tfw.ElementInfo{DataLocked: true}, path)
	var ce *commitError
	if !errors.As(err, &ce) {
		t.Fatalf("err = %v, want a *commitError", err)
	}
	if readFile(t, path) != before {
		t.Fatal("a failed commit changed the record")
	}
}

func TestCommitRecordedRefusesAnOffCurvePointAfterTheCommit(t *testing.T) {
	path := recordPath(t)
	if err := recordSerial(path, testSerial(t, testSerialHex)); err != nil {
		t.Fatal(err)
	}
	e := &element{pub: bytes.Repeat([]byte{0x11}, 64)}
	err := commitRecorded(context.Background(), startElement(t, e), testTemplate(), &tfw.ElementInfo{}, path)
	wantRecordError(t, err, "an element that returned no P-256 point")
}

func TestCommitAndVerifyRecordedWithoutARecord(t *testing.T) {
	tm := testTemplate()
	if err := commitRecorded(context.Background(), startElement(t, &element{pub: mustHex(t, p256GeneratorHex)}),
		tm, &tfw.ElementInfo{}, ""); err != nil {
		t.Fatal(err)
	}
	tm.Blocks = nil
	if err := verifyRecorded(context.Background(), startElement(t, &element{pub: mustHex(t, p256GeneratorHex)}),
		tm, &tfw.ElementInfo{ConfigLocked: true, DataLocked: true}, ""); err != nil {
		t.Fatal(err)
	}
}

func TestVerifyRecordedJoinsThenChecksThePoint(t *testing.T) {
	path := recordPath(t)
	if err := recordSerial(path, testSerial(t, testSerialHex)); err != nil {
		t.Fatal(err)
	}
	tm := testTemplate()
	tm.Blocks = nil
	locked := &tfw.ElementInfo{ConfigLocked: true, DataLocked: true}
	verify := func(pub string) error {
		return verifyRecorded(context.Background(), startElement(t, &element{pub: mustHex(t, pub)}), tm, locked, path)
	}
	if err := verify(p256GeneratorHex); err != nil {
		t.Fatal(err)
	}
	if got := readFile(t, path); got != goldenRecord {
		t.Fatalf("record after --verify =\n%s", got)
	}
	wantRecordError(t, verify(p256DoubleHex), "another element")
	if got := readFile(t, path); got != goldenRecord {
		t.Fatal("the refused verify touched the record")
	}
	// A part that fails --verify is never joined.
	if err := verifyRecorded(context.Background(), startElement(t, &element{pub: mustHex(t, p256GeneratorHex)}),
		tm, &tfw.ElementInfo{}, path); err == nil {
		t.Fatal("an unlocked part verified")
	}
}
