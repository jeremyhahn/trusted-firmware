package main

import (
	"bytes"
	"encoding/hex"
	"errors"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"testing"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
)

// The P-256 base point G, X || Y (SEC 2 §2.4.2): a fixed point on the curve,
// so the expected operator key below is a literal, not a generated one.
const p256GeneratorHex = "6b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296" +
	"4fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5"

const testSerialHex = "000102030405060708090a0b0c0d0e0f"

func TestPlatformBlockFromFlagsRefusesBadKeys(t *testing.T) {
	for name, c := range map[string]struct{ hash, opKey, want string }{
		"short pubkey hash":      {hash: "abcd", want: "--pubkey-hash is 2 bytes, want 32"},
		"non-hex pubkey hash":    {hash: strings.Repeat("zz", 32), want: "--pubkey-hash"},
		"short operator key":     {opKey: "04", want: "--operator-pubkey is 1 bytes, want 64"},
		"non-hex operator key":   {opKey: strings.Repeat("zz", 64), want: "--operator-pubkey"},
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
	err := runBake(out, true, strings.Repeat("00", 16), "", "", true, benchFlags(), "")
	if err == nil || !strings.Contains(err.Error(), "--serial is all zero") {
		t.Fatalf("err = %v, want the all-zero serial refusal", err)
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
	if err := writeBakedImage(out, nil, []byte{0x01}); !errors.Is(err, os.ErrNotExist) {
		t.Fatalf("err = %v, want the create's os.ErrNotExist", err)
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
	err := runBake(out, false, "", "", "", true, f, "")
	if err == nil || !strings.Contains(err.Error(), "requires --gateway") {
		t.Fatalf("err = %v, want the missing --gateway refusal", err)
	}
	if _, err := os.Stat(out); !errors.Is(err, os.ErrNotExist) {
		t.Fatalf("an image was written after a refused flag: %v", err)
	}
}

// Mutation tests for what runBake parses: the platform-block flags, the
// network-block flags and the image it bakes. A bad input is refused before a
// file is created; a good one round-trips byte for byte. Every case here is
// deterministic -- the mutations are enumerated, not drawn -- so a failure
// names its case and nothing else is needed to reproduce it.

const mutateHashHex = "1111111111111111111111111111111111111111111111111111111111111111"

// hexMutations returns every way a fixed-length hex flag is broken: one or
// two characters short or long, a prefix, a space, a newline, and each
// position replaced in turn by a character that is not hex.
func hexMutations(good string) []string {
	out := []string{
		"",
		good[:len(good)-1],
		good[:len(good)-2],
		good + "0",
		good + "00",
		"0x" + good,
		good[:len(good)/2] + " " + good[len(good)/2:],
		good + "\n",
		" " + good,
	}
	for i := range good {
		out = append(out, good[:i]+"g"+good[i+1:])
	}
	return out
}

// requireRefusedBeforeFile runs runBake with the given inputs and requires an
// error and no file at outPath.
func requireRefusedBeforeFile(t *testing.T, label string, platform bool, serial, hash, opkey string,
	netblock bool, nf netblockFlags) {
	t.Helper()
	out := filepath.Join(t.TempDir(), "image.hex")
	err := runBake(out, platform, serial, hash, opkey, netblock, nf, "")
	if err == nil {
		t.Fatalf("%s: accepted", label)
	}
	if _, statErr := os.Stat(out); !errors.Is(statErr, os.ErrNotExist) {
		t.Fatalf("%s: refused (%v) but %s exists (stat: %v)", label, err, out, statErr)
	}
}

func TestEveryMutatedPlatformFlagIsRefusedBeforeAFile(t *testing.T) {
	type field struct {
		name string
		good string
		set  func(s, h, k *string, v string)
	}
	fields := []field{
		{"serial", testSerialHex, func(s, _, _ *string, v string) { *s = v }},
		{"pubkey-hash", mutateHashHex, func(_, h, _ *string, v string) { *h = v }},
		{"operator-pubkey", p256GeneratorHex, func(_, _, k *string, v string) { *k = v }},
	}
	cases := 0
	for _, f := range fields {
		muts := hexMutations(f.good)
		if f.name == "pubkey-hash" || f.name == "operator-pubkey" {
			// The empty string is how to ask for a zero field there; it is
			// the documented omission, not a mutation.
			muts = muts[1:]
		}
		muts = append(muts, strings.Repeat("0", len(f.good)))
		for i, v := range muts {
			s, h, k := testSerialHex, mutateHashHex, p256GeneratorHex
			f.set(&s, &h, &k, v)
			requireRefusedBeforeFile(t, f.name+" mutation "+strconv.Itoa(i)+" "+quoteShort(v), true, s, h, k, false, netblockFlags{})
			cases++
		}
	}

	// Length-correct, hex-correct, not on the curve: every byte of X||Y
	// flipped in turn.
	good, err := hex.DecodeString(p256GeneratorHex)
	if err != nil {
		t.Fatal(err)
	}
	for i := range good {
		for _, mask := range []byte{0x01, 0x80} {
			bad := bytes.Clone(good)
			bad[i] ^= mask
			requireRefusedBeforeFile(t, "operator-pubkey byte "+strconv.Itoa(i)+" ^ "+strconv.Itoa(int(mask)),
				true, testSerialHex, mutateHashHex, hex.EncodeToString(bad), false, netblockFlags{})
			cases++
		}
	}
	t.Logf("%d platform-flag mutations refused", cases)
}

// netblockMutations is every field of benchFlags broken one way at a time.
var netblockMutations = []struct {
	name string
	edit func(*netblockFlags)
}{
	{"mac empty", func(f *netblockFlags) { f.mac = "" }},
	{"mac short", func(f *netblockFlags) { f.mac = "5a:ed:09:10:bf" }},
	{"mac long", func(f *netblockFlags) { f.mac = "5a:ed:09:10:bf:59:00" }},
	{"mac eui64", func(f *netblockFlags) { f.mac = "5a:ed:09:10:bf:59:00:01" }},
	{"mac not hex", func(f *netblockFlags) { f.mac = "5a:ed:09:10:bf:5g" }},
	{"mac mixed separators", func(f *netblockFlags) { f.mac = "5a:ed-09:10:bf:59" }},
	{"mac multicast", func(f *netblockFlags) { f.mac = "5b:ed:09:10:bf:59" }},
	{"mac broadcast", func(f *netblockFlags) { f.mac = "ff:ff:ff:ff:ff:ff" }},
	{"ip empty", func(f *netblockFlags) { f.ip = "" }},
	{"ip octet 256", func(f *netblockFlags) { f.ip = "198.51.100.256/24" }},
	{"ip three octets", func(f *netblockFlags) { f.ip = "198.51.100/24" }},
	{"ip five octets", func(f *netblockFlags) { f.ip = "198.51.100.22.1/24" }},
	{"ip leading zero", func(f *netblockFlags) { f.ip = "198.51.100.022/24" }},
	{"ip ipv6", func(f *netblockFlags) { f.ip = "2001:db8::16/64" }},
	{"ip 4in6", func(f *netblockFlags) { f.ip = "::ffff:198.51.100.22/120" }},
	{"ip 4in6 with mask", func(f *netblockFlags) { f.ip, f.mask = "::ffff:198.51.100.22", "255.255.255.0" }},
	{"ip zone", func(f *netblockFlags) { f.ip = "198.51.100.22%eth0/24" }},
	{"ip prefix 33", func(f *netblockFlags) { f.ip = "198.51.100.22/33" }},
	{"ip prefix -1", func(f *netblockFlags) { f.ip = "198.51.100.22/-1" }},
	{"ip prefix leading zero", func(f *netblockFlags) { f.ip = "198.51.100.22/024" }},
	{"ip prefix empty", func(f *netblockFlags) { f.ip = "198.51.100.22/" }},
	{"ip prefix text", func(f *netblockFlags) { f.ip = "198.51.100.22/x" }},
	{"ip prefix twice", func(f *netblockFlags) { f.ip = "198.51.100.22/24/24" }},
	{"ip no mask", func(f *netblockFlags) { f.ip = "198.51.100.22" }},
	{"mask and prefix", func(f *netblockFlags) { f.mask = "255.255.255.0" }},
	{"mask holed", func(f *netblockFlags) { f.ip, f.mask = "198.51.100.22", "255.255.0.255" }},
	{"mask low bit", func(f *netblockFlags) { f.ip, f.mask = "198.51.100.22", "255.255.255.1" }},
	{"mask inverted", func(f *netblockFlags) { f.ip, f.mask = "198.51.100.22", "0.0.0.255" }},
	{"mask octet 256", func(f *netblockFlags) { f.ip, f.mask = "198.51.100.22", "255.255.256.0" }},
	{"mask ipv6", func(f *netblockFlags) { f.ip, f.mask = "198.51.100.22", "ffff:ffff::" }},
	{"mask 4in6", func(f *netblockFlags) { f.ip, f.mask = "198.51.100.22", "::ffff:255.255.255.0" }},
	{"mask prefix form", func(f *netblockFlags) { f.ip, f.mask = "198.51.100.22", "24" }},
	{"gateway empty", func(f *netblockFlags) { f.gateway = "" }},
	{"gateway octet 256", func(f *netblockFlags) { f.gateway = "198.51.100.256" }},
	{"gateway ipv6", func(f *netblockFlags) { f.gateway = "2001:db8::1" }},
	{"gateway 4in6", func(f *netblockFlags) { f.gateway = "::ffff:198.51.100.1" }},
	{"gateway with port", func(f *netblockFlags) { f.gateway = "198.51.100.1:80" }},
	{"gateway hostname", func(f *netblockFlags) { f.gateway = "router" }},
	{"endpoint empty", func(f *netblockFlags) { f.endpoint = "" }},
	{"endpoint no port", func(f *netblockFlags) { f.endpoint = "198.51.100.1" }},
	{"endpoint empty port", func(f *netblockFlags) { f.endpoint = "198.51.100.1:" }},
	{"endpoint port 0", func(f *netblockFlags) { f.endpoint = "198.51.100.1:0" }},
	{"endpoint port 65536", func(f *netblockFlags) { f.endpoint = "198.51.100.1:65536" }},
	{"endpoint port -1", func(f *netblockFlags) { f.endpoint = "198.51.100.1:-1" }},
	{"endpoint port text", func(f *netblockFlags) { f.endpoint = "198.51.100.1:70x0" }},
	{"endpoint 0.0.0.0", func(f *netblockFlags) { f.endpoint = "0.0.0.0:7020" }},
	{"endpoint ipv6", func(f *netblockFlags) { f.endpoint = "[2001:db8::1]:7020" }},
	{"endpoint 4in6", func(f *netblockFlags) { f.endpoint = "[::ffff:198.51.100.1]:7020" }},
	{"endpoint hostname", func(f *netblockFlags) { f.endpoint = "bench.example:7020" }},
	{"endpoint octet 256", func(f *netblockFlags) { f.endpoint = "198.51.100.256:7020" }},
}

func TestEveryMutatedNetblockFlagIsRefusedBeforeAFile(t *testing.T) {
	for _, m := range netblockMutations {
		f := benchFlags()
		m.edit(&f)
		requireRefusedBeforeFile(t, "netblock "+m.name, false, "", "", "", true, f)
		// And beside a good platform block: one bad half bakes nothing.
		requireRefusedBeforeFile(t, "both, netblock "+m.name, true, testSerialHex, mutateHashHex, p256GeneratorHex, true, f)
	}
}

// readSparseIHEX parses an Intel HEX file of data and EOF records into an
// address -> byte map, refusing anything a well-formed file would not hold.
func readSparseIHEX(t *testing.T, s string) map[int]byte {
	t.Helper()
	image := map[int]byte{}
	sawEOF := false
	for _, line := range strings.Split(strings.TrimSpace(s), "\n") {
		if sawEOF {
			t.Fatalf("record %q after the EOF record", line)
		}
		if !strings.HasPrefix(line, ":") {
			t.Fatalf("record %q has no start code", line)
		}
		raw, err := hex.DecodeString(line[1:])
		if err != nil || len(raw) < 5 || len(raw) != 5+int(raw[0]) {
			t.Fatalf("bad record %q", line)
		}
		var sum byte
		for _, b := range raw {
			sum += b
		}
		if sum != 0 {
			t.Fatalf("record %q: checksum", line)
		}
		switch raw[3] {
		case 0x00:
			addr := int(raw[1])<<8 | int(raw[2])
			for i, b := range raw[4 : 4+int(raw[0])] {
				if _, dup := image[addr+i]; dup {
					t.Fatalf("address 0x%04X written twice", addr+i)
				}
				image[addr+i] = b
			}
		case 0x01:
			sawEOF = true
		default:
			t.Fatalf("record %q: type %02x not expected in an EEPROM image", line, raw[3])
		}
	}
	if !sawEOF {
		t.Fatal("no EOF record")
	}
	return image
}

func TestABakedImageRoundTripsByteForByte(t *testing.T) {
	out := filepath.Join(t.TempDir(), "image.hex")
	if err := runBake(out, true, testSerialHex, mutateHashHex, p256GeneratorHex, true, benchFlags(), ""); err != nil {
		t.Fatal(err)
	}
	text, err := os.ReadFile(out) // #nosec G304 -- the test's own temp file
	if err != nil {
		t.Fatal(err)
	}
	image := readSparseIHEX(t, string(text))

	var serial [tfw.SerialLen]byte
	var hash [tfw.PubkeyHashLen]byte
	var opkey [tfw.OperatorPubkeyLen]byte
	for _, d := range []struct {
		dst  []byte
		text string
	}{{serial[:], testSerialHex}, {hash[:], mutateHashHex}, {opkey[:], p256GeneratorHex}} {
		b, err := hex.DecodeString(d.text)
		if err != nil {
			t.Fatal(err)
		}
		copy(d.dst, b)
	}
	wantPlatform := tfw.BuildPlatformBlock(serial, hash, opkey)

	cfg, err := benchFlags().netConfig()
	if err != nil {
		t.Fatal(err)
	}
	wantNet, err := cfg.MarshalBinary()
	if err != nil {
		t.Fatal(err)
	}

	want := map[int]byte{}
	for i, b := range wantPlatform {
		want[i] = b
	}
	for i, b := range wantNet {
		want[tfw.NetConfigAddr+i] = b
	}
	if len(image) != len(want) {
		t.Fatalf("image holds %d bytes, want %d", len(image), len(want))
	}
	for addr, w := range want {
		if g, ok := image[addr]; !ok || g != w {
			t.Fatalf("address 0x%04X = %02x (present %v), want %02x", addr, g, ok, w)
		}
	}

	// The net block read back decodes to the flags that made it, and encodes
	// to the same bytes again.
	got := make([]byte, tfw.NetConfigLen)
	for i := range got {
		got[i] = image[tfw.NetConfigAddr+i]
	}
	var back tfw.NetConfig
	if err := back.UnmarshalBinary(got); err != nil {
		t.Fatal(err)
	}
	again, err := back.MarshalBinary()
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(again, got) {
		t.Fatalf("re-encoded % x, read % x", again, got)
	}
	if back.IP != cfg.IP || back.Mask != cfg.Mask || back.Gateway != cfg.Gateway ||
		back.Endpoint != cfg.Endpoint || !bytes.Equal(back.MAC, cfg.MAC) {
		t.Fatalf("decoded %+v, baked %+v", back, cfg)
	}
}

func FuzzParseFixedHex(f *testing.F) {
	f.Add(testSerialHex, 16)
	f.Add(p256GeneratorHex, 64)
	f.Add("0x00", 1)
	f.Add("abc", 2)
	f.Add("", 0)
	f.Fuzz(func(t *testing.T, s string, want int) {
		if want < 0 || want > 128 {
			return
		}
		b, err := parseFixedHex("fuzz", s, want)
		if err != nil {
			return
		}
		if len(b) != want {
			t.Fatalf("%q want %d: got %d bytes", s, want, len(b))
		}
		if hex.EncodeToString(b) != strings.ToLower(s) {
			t.Fatalf("%q decoded to % x, which is not what it says", s, b)
		}
	})
}

func quoteShort(s string) string {
	if len(s) > 24 {
		s = s[:24] + "..."
	}
	return "\"" + s + "\""
}
