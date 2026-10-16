// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package main

import (
	"bufio"
	"encoding/hex"
	"errors"
	"fmt"
	"os"
	"strconv"
	"strings"
)

// A provisioning template: which config blocks to write, and what is expected
// in each slot afterwards.
//
// THE TEMPLATE IS THE REVIEWABLE ARTEFACT. The firmware writes the block it is
// handed and reports what the element said; it has no opinion about slot
// policy, because a policy encoded in C would not be something anyone reads
// before locking a part. This file is, and its hash goes in the provisioning
// record.
type template struct {
	Name string
	// Blocks 2..3 of the config zone, keyed by block index. Blocks 0 and 1 are
	// absent because the device refuses them: they hold the serial number, the
	// revision and the OTP mode, which are not ours to set.
	Blocks map[uint8][]byte
	// The summary CRCs the element must agree with before it will lock. Zero is
	// not a valid value and is refused rather than treated as "skip the check"
	// -- see Device.ProvLock.
	ConfigLockCRC uint16
	DataLockCRC   uint16
}

// A deliberately small parser: `key = value`, with hex blocks.
//
// Not YAML. A YAML library is a dependency, and sdk/go's dependency list is
// load-bearing -- it is imported by go-xkms at Layer 3 and go-trusted-platform
// at Layer 5, so a module added here is a module added to both. The template
// format is six keys; the cost of parsing it by hand is thirty lines and the
// cost of not doing so is a third-party parser in the dependency graph of every
// consumer.
func loadTemplate(path string) (*template, error) {
	f, err := os.Open(path) // #nosec G304 -- path is the operator's own --template argument; tfwprov runs as the operator and can open nothing they could not
	if err != nil {
		return nil, err
	}
	// Read-only: a Close error has lost no data, and a read error is the
	// scanner's to report below.
	defer func() { _ = f.Close() }()

	t := &template{Blocks: map[uint8][]byte{}}
	sc := bufio.NewScanner(f)
	for line := 1; sc.Scan(); line++ {
		s := strings.TrimSpace(sc.Text())
		if s == "" || strings.HasPrefix(s, "#") {
			continue
		}
		key, val, ok := strings.Cut(s, "=")
		if !ok {
			return nil, fmt.Errorf("%s:%d: expected key = value", path, line)
		}
		key, val = strings.TrimSpace(key), strings.TrimSpace(val)
		switch {
		case key == "name":
			t.Name = val
		case key == "config_lock_crc" || key == "data_lock_crc":
			n, err := strconv.ParseUint(strings.TrimPrefix(val, "0x"), 16, 16)
			if err != nil {
				return nil, fmt.Errorf("%s:%d: %s must be a 16-bit hex value", path, line, key)
			}
			if key == "config_lock_crc" {
				t.ConfigLockCRC = uint16(n)
			} else {
				t.DataLockCRC = uint16(n)
			}
		case strings.HasPrefix(key, "block"):
			idx, err := strconv.ParseUint(strings.TrimPrefix(key, "block"), 10, 8)
			if err != nil {
				return nil, fmt.Errorf("%s:%d: %q is not a block index", path, line, key)
			}
			raw, err := hex.DecodeString(strings.ReplaceAll(val, " ", ""))
			if err != nil {
				return nil, fmt.Errorf("%s:%d: %w", path, line, err)
			}
			if len(raw) != 32 {
				return nil, fmt.Errorf("%s:%d: block %d is %d bytes, must be 32", path, line, idx, len(raw))
			}
			t.Blocks[uint8(idx)] = raw
		default:
			// Refused rather than ignored. A typo in a key that silently does
			// nothing is a template that provisions a part differently than it
			// reads, on a part that cannot be corrected afterwards.
			return nil, fmt.Errorf("%s:%d: unknown key %q", path, line, key)
		}
	}
	if err := sc.Err(); err != nil {
		return nil, err
	}
	if len(t.Blocks) == 0 {
		return nil, errors.New("template writes no config blocks")
	}
	if t.ConfigLockCRC == 0 || t.DataLockCRC == 0 {
		return nil, errors.New("template must carry both lock summary CRCs; zero would ask the element to lock without checking")
	}
	return t, nil
}
