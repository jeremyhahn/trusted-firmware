package main

import (
	"bytes"
	"errors"
	"io/fs"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

var block32 = strings.Repeat("00", 32)

const goodTemplate = "# a comment\n\nname = bench\nconfig_lock_crc = 0x1234\ndata_lock_crc = abcd\nblock2 = "

func writeTemplate(t *testing.T, body string) string {
	t.Helper()
	path := filepath.Join(t.TempDir(), "element.tmpl")
	if err := os.WriteFile(path, []byte(body), 0o600); err != nil {
		t.Fatal(err)
	}
	return path
}

func TestLoadTemplateMissingFile(t *testing.T) {
	_, err := loadTemplate(filepath.Join(t.TempDir(), "absent.tmpl"))
	if !errors.Is(err, fs.ErrNotExist) {
		t.Fatalf("err = %v, want fs.ErrNotExist", err)
	}
}

func TestLoadTemplateRefusals(t *testing.T) {
	crcs := "config_lock_crc = 1\ndata_lock_crc = 1\n"
	for _, tc := range []struct {
		name, body, want string
	}{
		// The template is key = value, not JSON: a JSON document is a
		// malformed first line.
		{"a JSON document", `{"name": "bench"}` + "\n", ":1: expected key = value"},
		{"a line with no =", "name bench\n", ":1: expected key = value"},
		{"an unknown key", "blok2 = " + block32 + "\n", `:1: unknown key "blok2"`},
		{"a CRC past 16 bits", "config_lock_crc = 0x10000\n", ":1: config_lock_crc must be a 16-bit hex value"},
		{"a CRC that is not hex", "data_lock_crc = zz\n", ":1: data_lock_crc must be a 16-bit hex value"},
		{"a block index past a byte", "block256 = " + block32 + "\n", `:1: "block256" is not a block index`},
		{"a block that is not hex", "block2 = zz\n", ":1: encoding/hex: invalid byte"},
		{"a short block", "block2 = 0011\n", ":1: block 2 is 2 bytes, must be 32"},
		{"no blocks", crcs, "template writes no config blocks"},
		// Longer than the scanner's 64 KiB line: refused, not truncated.
		{"a line past the scanner limit", "# " + strings.Repeat("x", 70000) + "\n", "token too long"},
		{"a zero CRC", "config_lock_crc = 0\ndata_lock_crc = 1\nblock2 = " + block32 + "\n",
			"template must carry both lock summary CRCs"},
	} {
		t.Run(tc.name, func(t *testing.T) {
			_, err := loadTemplate(writeTemplate(t, tc.body))
			if err == nil || !strings.Contains(err.Error(), tc.want) {
				t.Fatalf("err = %v, want it to contain %q", err, tc.want)
			}
		})
	}
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
		// A repeated key would silently replace the first. block2 and
		// config_lock_crc repeated: TestTemplateRefusalsNameTheLines.
		{"block repeated, zero-padded", add("block03 = " + strings.Repeat("00", 32))},
		{"name repeated", add("name = other")},
		{"data crc repeated", add("data_lock_crc = 0x4321")},
	}
	for _, m := range muts {
		lines := m.edit(goodTemplateLines())
		if tm, err := loadTemplate(writeTemplateLines(t, lines)); err == nil {
			t.Errorf("%s: loaded %+v", m.name, tm)
		}
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
