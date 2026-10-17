package main

import (
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

func TestLoadTemplateReadsEveryKey(t *testing.T) {
	tm, err := loadTemplate(writeTemplate(t, goodTemplate+block32+"\n"))
	if err != nil {
		t.Fatal(err)
	}
	if tm.Name != "bench" || tm.ConfigLockCRC != 0x1234 || tm.DataLockCRC != 0xabcd || len(tm.Blocks[2]) != 32 {
		t.Fatalf("template = %+v", tm)
	}
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
