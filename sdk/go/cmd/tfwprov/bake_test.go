package main

import (
	"bytes"
	"encoding/hex"
	"errors"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// The P-256 base point G, X || Y (SEC 2 §2.4.2): a fixed point on the curve,
// so the expected operator key below is a literal, not a generated one.
const p256GeneratorHex = "6b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296" +
	"4fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5"

const testSerialHex = "000102030405060708090a0b0c0d0e0f"

func TestPlatformBlockFromFlagsRefusesBadKeys(t *testing.T) {
	for name, c := range map[string]struct{ hash, opKey, want string }{
		"short pubkey hash":      {hash: "abcd"},
		"non-hex pubkey hash":    {hash: strings.Repeat("zz", 32)},
		"short operator key":     {opKey: "04"},
		"non-hex operator key":   {opKey: strings.Repeat("zz", 64)},
		"operator key off curve": {opKey: p256GeneratorHex[:126] + "f6", want: "--operator-pubkey"},
		"operator key all zero":  {opKey: strings.Repeat("00", 64), want: "--operator-pubkey"},
	} {
		t.Run(name, func(t *testing.T) {
			var p bakedPlatform
			err := platformBlockFromFlags(&p, testSerialHex, c.hash, c.opKey)
			if err == nil {
				t.Fatal("accepted")
			}
			if !strings.Contains(err.Error(), c.want) {
				t.Fatalf("error %q does not name %q", err, c.want)
			}
		})
	}
}

func TestPlatformBlockFromFlagsTakesARealOperatorKey(t *testing.T) {
	var p bakedPlatform
	if err := platformBlockFromFlags(&p, testSerialHex, "", p256GeneratorHex); err != nil {
		t.Fatalf("platformBlockFromFlags: %v", err)
	}
	if got := hex.EncodeToString(p.operatorPubkey[:]); got != p256GeneratorHex {
		t.Fatalf("operator key = %s, want %s", got, p256GeneratorHex)
	}
	if got := hex.EncodeToString(p.serial[:]); got != testSerialHex {
		t.Fatalf("serial = %s, want %s", got, testSerialHex)
	}
}

func TestRunBakeStopsOnABadPlatformFlag(t *testing.T) {
	out := filepath.Join(t.TempDir(), "both.eep")
	if err := runBake(out, true, strings.Repeat("00", 16), "", "", true, benchFlags(), ""); err == nil {
		t.Fatal("accepted an all-zero serial")
	}
	if _, err := os.Stat(out); !errors.Is(err, os.ErrNotExist) {
		t.Fatalf("an image was written after a refused flag: %v", err)
	}
}

func TestNetConfigRefusesUnparsableFlags(t *testing.T) {
	for name, c := range map[string]struct {
		edit func(*netblockFlags)
		want string
	}{
		"mac not a mac":   {func(f *netblockFlags) { f.mac = "not-a-mac" }, "--mac"},
		"mac eui-64":      {func(f *netblockFlags) { f.mac = "02:00:00:00:00:00:00:01" }, "want 6"},
		"ip not an ip":    {func(f *netblockFlags) { f.ip = "198.51.100/24" }, "--ip"},
		"ip prefix 33":    {func(f *netblockFlags) { f.ip = "198.51.100.22/33" }, "--ip"},
		"mask not a mask": {func(f *netblockFlags) { f.ip, f.mask = "198.51.100.22", "255.255.255" }, "--mask"},
		"mask ipv6":       {func(f *netblockFlags) { f.ip, f.mask = "198.51.100.22", "ffff:ffff::" }, "IPv4 netmask"},
		"gateway not ip":  {func(f *netblockFlags) { f.gateway = "gateway.example" }, "--gateway"},
	} {
		t.Run(name, func(t *testing.T) {
			f := benchFlags()
			c.edit(&f)
			_, err := f.netConfig()
			if err == nil {
				t.Fatal("accepted")
			}
			if !strings.Contains(err.Error(), c.want) {
				t.Fatalf("error %q does not name %q", err, c.want)
			}
		})
	}
}

func TestWriteBakedImageRefusesNothing(t *testing.T) {
	out := filepath.Join(t.TempDir(), "empty.eep")
	if err := writeBakedImage(out, nil, nil); err == nil || !strings.Contains(err.Error(), "nothing to bake") {
		t.Fatalf("err = %v, want nothing to bake", err)
	}
	if _, err := os.Stat(out); !errors.Is(err, os.ErrNotExist) {
		t.Fatalf("an empty image was created: %v", err)
	}
}

func TestWriteBakedImageReportsACreateFailure(t *testing.T) {
	out := filepath.Join(t.TempDir(), "no-such-dir", "net.eep")
	if err := writeBakedImage(out, nil, []byte{0x01}); err == nil {
		t.Fatal("wrote into a directory that does not exist")
	}
}

// /dev/full accepts the open and fails every write with ENOSPC: the one host
// path to a write error after os.Create succeeded.
func TestWriteBakedImageReportsAWriteFailure(t *testing.T) {
	err := writeBakedImage("/dev/full", nil, []byte{0x01})
	if err == nil || !strings.Contains(err.Error(), "writing /dev/full") {
		t.Fatalf("err = %v, want a write failure", err)
	}
}

// limitWriter accepts n bytes, then fails every write.
type limitWriter struct {
	buf bytes.Buffer
	n   int
}

var errWriterFull = errors.New("writer full")

func (w *limitWriter) Write(p []byte) (int, error) {
	if len(p) > w.n {
		return 0, errWriterFull
	}
	w.n -= len(p)
	return w.buf.Write(p)
}

func TestWriteIHEXSegmentsPropagatesEveryWriteFailure(t *testing.T) {
	// One data byte 0xAB at address 0: 01+00+00+00+AB = AC, checksum 54.
	const want = ":01000000AB54\n:00000001FF\n"
	seg := ihexSegment{base: 0, data: []byte{0xAB}}
	for n := 0; n < len(want); n++ {
		w := &limitWriter{n: n}
		if err := writeIHEXSegments(w, seg); !errors.Is(err, errWriterFull) {
			t.Fatalf("budget %d: err = %v, want the writer's error", n, err)
		}
	}
	w := &limitWriter{n: len(want)}
	if err := writeIHEXSegments(w, seg); err != nil {
		t.Fatalf("full budget: %v", err)
	}
	if w.buf.String() != want {
		t.Fatalf("wrote %q, want %q", w.buf.String(), want)
	}
}

func TestWriteIHEXRecordRefusesAnOverlongRecord(t *testing.T) {
	var w bytes.Buffer
	if err := writeIHEXRecord(&w, 0, 0x00, make([]byte, 256)); err == nil {
		t.Fatal("accepted a 256-byte record")
	}
	if w.Len() != 0 {
		t.Fatalf("wrote %q before refusing", w.String())
	}
}

func TestRunBakeStopsOnABadNetblockFlag(t *testing.T) {
	out := filepath.Join(t.TempDir(), "net.eep")
	f := benchFlags()
	f.gateway = ""
	if err := runBake(out, false, "", "", "", true, f, ""); err == nil {
		t.Fatal("accepted a netblock with no gateway")
	}
	if _, err := os.Stat(out); !errors.Is(err, os.ErrNotExist) {
		t.Fatalf("an image was written after a refused flag: %v", err)
	}
}
