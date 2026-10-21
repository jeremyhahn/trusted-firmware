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

// Mutation tests for what tfwprov parses: the platform-block flags, the
// network-block flags, the block it bakes and the provisioning template. A
// bad input is refused before a file is created; a good one round-trips byte
// for byte. Every case here is deterministic -- the mutations are enumerated,
// not drawn -- so a failure names its case and nothing else is needed to
// reproduce it.

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

func TestEveryMutatedNetBlockIsRefused(t *testing.T) {
	good, err := netblockFromFlags(benchFlags())
	if err != nil {
		t.Fatal(err)
	}
	var c tfw.NetConfig
	if err := c.UnmarshalBinary(good); err != nil {
		t.Fatalf("the good block: %v", err)
	}
	cases := 0
	for i := range good {
		for _, mask := range []byte{0x01, 0x80, 0xFF} {
			bad := bytes.Clone(good)
			bad[i] ^= mask
			if err := c.UnmarshalBinary(bad); err == nil {
				t.Fatalf("byte %d ^ %02x accepted: % x", i, mask, bad)
			}
			cases++
		}
	}
	for n := 0; n < len(good); n++ {
		if err := c.UnmarshalBinary(good[:n]); err == nil {
			t.Fatalf("truncated to %d bytes accepted", n)
		}
		cases++
	}
	for _, tail := range [][]byte{{0x00}, {0xFF}, {0x00, 0x00}, bytes.Repeat([]byte{0xA5}, 32)} {
		if err := c.UnmarshalBinary(append(bytes.Clone(good), tail...)); err == nil {
			t.Fatalf("extended by % x accepted", tail)
		}
		cases++
	}
	t.Logf("%d net-block mutations refused", cases)
}

// goodTemplateLines is a template loadTemplate accepts, one line per field so
// each can be broken alone.
func goodTemplateLines() []string {
	return []string{
		"# bench template",
		"name = bench",
		"config_lock_crc = 0x1234",
		"data_lock_crc = abcd",
		"block2 = " + strings.Repeat("a5", 32),
		"block3 = " + strings.Repeat("5a ", 31) + "5a",
	}
}

func writeTemplateLines(t *testing.T, lines []string) string {
	t.Helper()
	path := filepath.Join(t.TempDir(), "t.conf")
	if err := os.WriteFile(path, []byte(strings.Join(lines, "\n")+"\n"), 0o600); err != nil {
		t.Fatal(err)
	}
	return path
}

// The refusal names the line: a block outside 2..3 its own, a repeated key
// both its own and the first.
func TestTemplateRefusalsNameTheLines(t *testing.T) {
	for _, tc := range []struct {
		name, line, want string
	}{
		{"block 4", "block4 = " + strings.Repeat("00", 32), ":7: block 4 is outside the config blocks a template may write, 2..3"},
		{"block 0", "block0 = " + strings.Repeat("00", 32), ":7: block 0 is outside"},
		{"block2 again", "block2 = " + strings.Repeat("00", 32), ":7: block2 repeats "},
		{"block02", "block02 = " + strings.Repeat("00", 32), ":7: block02 repeats "},
		{"config crc again", "config_lock_crc = 0x4321", ":7: config_lock_crc repeats "},
	} {
		t.Run(tc.name, func(t *testing.T) {
			path := writeTemplateLines(t, append(goodTemplateLines(), tc.line))
			_, err := loadTemplate(path)
			if err == nil {
				t.Fatal("loaded")
			}
			if !strings.HasPrefix(err.Error(), path+tc.want) {
				t.Fatalf("err = %q, want prefix %q", err, path+tc.want)
			}
		})
	}
	path := writeTemplateLines(t, append(goodTemplateLines(), "block2 = "+strings.Repeat("00", 32)))
	_, err := loadTemplate(path)
	if want := path + ":7: block2 repeats " + path + ":5"; err == nil || err.Error() != want {
		t.Fatalf("err = %v, want %q", err, want)
	}
}

func TestTheGoodTemplateLoads(t *testing.T) {
	tm, err := loadTemplate(writeTemplateLines(t, goodTemplateLines()))
	if err != nil {
		t.Fatal(err)
	}
	if tm.Name != "bench" || tm.ConfigLockCRC != 0x1234 || tm.DataLockCRC != 0xabcd ||
		!bytes.Equal(tm.Blocks[2], bytes.Repeat([]byte{0xa5}, 32)) ||
		!bytes.Equal(tm.Blocks[3], bytes.Repeat([]byte{0x5a}, 32)) || len(tm.Blocks) != 2 {
		t.Fatalf("loaded %+v", tm)
	}
}

func TestEveryMutatedTemplateFieldIsRefused(t *testing.T) {
	replace := func(i int, s string) func([]string) []string {
		return func(l []string) []string { l[i] = s; return l }
	}
	drop := func(i int) func([]string) []string {
		return func(l []string) []string { return append(l[:i], l[i+1:]...) }
	}
	add := func(s string) func([]string) []string {
		return func(l []string) []string { return append(l, s) }
	}
	block := strings.Repeat("a5", 32)
	muts := []struct {
		name string
		edit func([]string) []string
	}{
		{"no equals", replace(1, "name bench")},
		{"unknown key", add("names = bench")},
		{"key case", replace(1, "Name = bench")},
		{"key typo", replace(2, "config_lock_cr = 0x1234")},
		{"config crc zero", replace(2, "config_lock_crc = 0")},
		{"config crc 0x0000", replace(2, "config_lock_crc = 0x0000")},
		{"config crc 17 bits", replace(2, "config_lock_crc = 0x10000")},
		{"config crc empty", replace(2, "config_lock_crc =")},
		{"config crc bare 0x", replace(2, "config_lock_crc = 0x")},
		{"config crc 0X", replace(2, "config_lock_crc = 0X1234")},
		{"config crc 0x0x", replace(2, "config_lock_crc = 0x0x12")},
		{"config crc negative", replace(2, "config_lock_crc = -1")},
		{"config crc plus", replace(2, "config_lock_crc = +1234")},
		{"config crc not hex", replace(2, "config_lock_crc = 12g4")},
		{"config crc spaced", replace(2, "config_lock_crc = 12 34")},
		{"config crc missing", drop(2)},
		{"data crc zero", replace(3, "data_lock_crc = 0000")},
		{"data crc 17 bits", replace(3, "data_lock_crc = 1abcd")},
		{"data crc not hex", replace(3, "data_lock_crc = abcz")},
		{"data crc missing", drop(3)},
		{"no blocks", func(l []string) []string { return l[:4] }},
		{"block 31 bytes", replace(4, "block2 = "+block[:62])},
		{"block 33 bytes", replace(4, "block2 = "+block+"a5")},
		{"block odd hex", replace(4, "block2 = "+block[:63])},
		{"block not hex", replace(4, "block2 = "+block[:10]+"zz"+block[12:])},
		{"block empty", replace(4, "block2 =")},
		{"block 0x prefix", replace(4, "block2 = 0x"+block[2:])},
		{"block tab", replace(4, "block2 = "+block[:32]+"\t"+block[32:])},
		{"block no index", replace(4, "block = "+block)},
		{"block index 256", replace(4, "block256 = "+block)},
		{"block index negative", replace(4, "block-2 = "+block)},
		{"block index text", replace(4, "block2x = "+block)},
		{"block index hex", replace(4, "block0x2 = "+block)},
		{"block index spaced", replace(4, "block 2 = "+block)},
		// Only blocks 2..3 are a template's: 0 and 1 are the element's, and
		// the config zone ends at 3.
		{"block index 0", replace(4, "block0 = "+block)},
		{"block index 1", replace(4, "block1 = "+block)},
		{"block index 4", add("block4 = " + block)},
		{"block index 255", add("block255 = " + block)},
		// A repeated key would silently replace the first.
		{"block repeated", add("block2 = " + strings.Repeat("00", 32))},
		{"block repeated, zero-padded", add("block03 = " + strings.Repeat("00", 32))},
		{"name repeated", add("name = other")},
		{"config crc repeated", add("config_lock_crc = 0x4321")},
		{"data crc repeated", add("data_lock_crc = 0x4321")},
	}
	for _, m := range muts {
		lines := m.edit(goodTemplateLines())
		if tm, err := loadTemplate(writeTemplateLines(t, lines)); err == nil {
			t.Errorf("%s: loaded %+v", m.name, tm)
		}
	}
	if _, err := loadTemplate(filepath.Join(t.TempDir(), "absent.conf")); err == nil {
		t.Error("a missing template loaded")
	}
}

// A template that fails to load never reaches the element: the commit path
// is load first, then write, so the refusal above is the refusal before
// anything is written. Pinned here against a template one byte short.
func TestABadTemplateWritesNothingToTheElement(t *testing.T) {
	lines := goodTemplateLines()
	lines[5] = "block3 = " + strings.Repeat("5a", 31)
	if _, err := loadTemplate(writeTemplateLines(t, lines)); err == nil {
		t.Fatal("a 31-byte block loaded")
	}
}

func FuzzLoadTemplate(f *testing.F) {
	f.Add(strings.Join(goodTemplateLines(), "\n"))
	f.Add("config_lock_crc = 1\ndata_lock_crc = 1\nblock2 = " + strings.Repeat("00", 32))
	f.Add("block2 = zz")
	f.Add("=\n=\n")
	f.Add("block99999999999999999999 = 00")
	f.Add("config_lock_crc = 0x\n")
	f.Fuzz(func(t *testing.T, text string) {
		path := filepath.Join(t.TempDir(), "t.conf")
		if err := os.WriteFile(path, []byte(text), 0o600); err != nil {
			t.Fatal(err)
		}
		tm, err := loadTemplate(path)
		if err != nil {
			return
		}
		if len(tm.Blocks) == 0 || tm.ConfigLockCRC == 0 || tm.DataLockCRC == 0 {
			t.Fatalf("loaded an incomplete template %+v from %q", tm, text)
		}
		for idx, b := range tm.Blocks {
			if len(b) != 32 {
				t.Fatalf("block %d is %d bytes from %q", idx, len(b), text)
			}
		}
	})
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

func FuzzNetblockFlags(f *testing.F) {
	g := benchFlags()
	f.Add(g.mac, g.ip, g.mask, g.gateway, g.endpoint)
	f.Add(g.mac, "198.51.100.22", "255.255.255.0", g.gateway, g.endpoint)
	for _, m := range netblockMutations {
		b := benchFlags()
		m.edit(&b)
		f.Add(b.mac, b.ip, b.mask, b.gateway, b.endpoint)
	}
	f.Fuzz(func(t *testing.T, mac, ip, mask, gateway, endpoint string) {
		nf := netblockFlags{mac: mac, ip: ip, mask: mask, gateway: gateway, endpoint: endpoint}
		block, err := netblockFromFlags(nf)
		if err != nil {
			return
		}
		if len(block) != tfw.NetConfigLen {
			t.Fatalf("%+v: block is %d bytes", nf, len(block))
		}
		var c tfw.NetConfig
		if err := c.UnmarshalBinary(block); err != nil {
			t.Fatalf("%+v baked a block its own reader refuses: %v", nf, err)
		}
		again, err := c.MarshalBinary()
		if err != nil || !bytes.Equal(again, block) {
			t.Fatalf("%+v: re-encoded % x (%v), baked % x", nf, again, err, block)
		}
	})
}

func quoteShort(s string) string {
	if len(s) > 24 {
		s = s[:24] + "..."
	}
	return "\"" + s + "\""
}
