// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

// Command tfwprov provisions an trusted-firmware device's secure element.
//
// THE GUARD IS THE REASON THIS TOOL EXISTS. It is not a convenience wrapper
// around a handful of writes; it is three modes around a procedure whose middle
// step cannot be undone. An ATECC608A's config and data zones, once locked,
// stay locked -- no erase, no reset, no factory command -- so a wrong template
// is a part that goes in the bin and, if it is soldered down, a board that goes
// with it.
//
//	--dry-run   prints the plan and touches nothing
//	--verify    reads the part back and compares it against the template
//	--commit    writes, and demands --yes-i-mean-it before it will
//
// Run --commit on a spare part first. That is the procedure, not a precaution.
//
// The device must be running a PROVISIONING image. A production firmware is
// built with TFW_FEATURE_PROVISION 0 and has no handler for any of these
// commands -- it answers "unknown command" -- because a device that can be
// re-provisioned over its own command channel has no identity worth attesting
// to.
//
// A fourth, unrelated mode, --bake-eeprom, writes the platform-reserved
// EEPROM block -- the device serial, among other fields -- as a .eep image
// flashed BEFORE any of the above. It opens no port and touches no element:
// see include/tfw/boot.h and docs/device-provisioning/ for why that field
// cannot be written any other way.
package main

import (
	"context"
	"encoding/hex"
	"errors"
	"flag"
	"fmt"
	"os"
	"time"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/serialport"
)

func usage() {
	fmt.Fprint(os.Stderr, `tfwprov -- provision an trusted-firmware device's secure element

  tfwprov [flags] <port>

exactly one mode is required

  --dry-run    print the plan; write nothing
  --verify     read the part back and compare against the template
  --commit     write. IRREVERSIBLE. requires --yes-i-mean-it

a separate mode, taking no port and no --config:

  --bake-eeprom --serial <hex16> --out <file.eep>   write a platform-block .eep image
                [--pubkey-hash <hex32>] [--operator-pubkey <hex64>]

flags
`)
	flag.PrintDefaults()
	fmt.Fprint(os.Stderr, `
The element cannot be unlocked. Run --commit on a spare part first.
See docs/device-provisioning/user.md.
`)
}

func main() {
	var (
		dryRun     = flag.Bool("dry-run", false, "print the plan and change nothing")
		verify     = flag.Bool("verify", false, "read the part back and compare")
		commit     = flag.Bool("commit", false, "write; irreversible")
		bakeEEPROM = flag.Bool("bake-eeprom", false, "write a platform-block .eep image; touches no device (see --serial, --out)")
		confirm    = flag.Bool("yes-i-mean-it", false, "required by --commit")
		cfgPath    = flag.String("config", "", "provisioning template (key = value; see sdk/go/templates)")
		baud       = flag.Int("baud", serialport.DefaultBaud, "serial baud rate")
		timeout    = flag.Duration("timeout", 30*time.Second, "how long to wait for a reply")
		confirm2   = flag.Bool("force", false, "deprecated alias; refused, use --yes-i-mean-it")
		serialHex  = flag.String("serial", "", "--bake-eeprom: 16-byte device serial, hex")
		hashHex    = flag.String("pubkey-hash", "", "--bake-eeprom: 32-byte SHA-256 of the bootloader's Ed25519 pubkey, hex (element tier: omit)")
		opKeyHex   = flag.String("operator-pubkey", "", "--bake-eeprom: 64-byte P-256 operator public key (X||Y), hex -- the AUTH_CHALLENGE/AUTH_RESPONSE trust anchor, element tier only")
		outPath    = flag.String("out", "", "--bake-eeprom: the .eep file to write")
	)
	flag.Usage = usage
	flag.Parse()

	if *confirm2 {
		// Refused rather than accepted as a synonym. --force is the flag
		// someone types from muscle memory, which is exactly the reflex this
		// tool exists to interrupt.
		fail(errors.New("--force is not accepted; --commit requires --yes-i-mean-it"))
	}

	// --bake-eeprom is its own mode: it opens no serial port and needs no
	// element template, because it writes a FILE the device has not been
	// flashed with yet. Kept out of the modes/NArg check below, which is
	// entirely about the wire-provisioning modes.
	if *bakeEEPROM {
		if *dryRun || *verify || *commit {
			fail(errors.New("--bake-eeprom is its own mode; it does not combine with --dry-run/--verify/--commit"))
		}
		if flag.NArg() != 0 {
			fail(errors.New("--bake-eeprom takes no positional argument (it opens no port); did you mean to drop the port, or to run one of --dry-run/--verify/--commit instead?"))
		}
		if *outPath == "" {
			fail(errors.New("--bake-eeprom requires --out"))
		}
		if *serialHex == "" {
			fail(errors.New("--bake-eeprom requires --serial (16 bytes, hex)"))
		}
		if err := runBakeEEPROM(*outPath, *serialHex, *hashHex, *opKeyHex); err != nil {
			fail(err)
		}
		return
	}

	modes := 0
	for _, m := range []bool{*dryRun, *verify, *commit} {
		if m {
			modes++
		}
	}
	if modes != 1 || flag.NArg() != 1 {
		usage()
		os.Exit(2)
	}
	if err := rejectBakeFlags(*serialHex, *hashHex, *opKeyHex, *outPath); err != nil {
		fail(err)
	}
	if *commit && !*confirm {
		fail(errors.New("--commit is irreversible on this part: pass --yes-i-mean-it"))
	}
	if *cfgPath == "" {
		fail(errors.New("--config is required: the template is what the element is checked against"))
	}

	tmpl, err := loadTemplate(*cfgPath)
	if err != nil {
		fail(err)
	}

	link, err := serialport.Open(flag.Arg(0), *baud)
	if err != nil {
		fail(err)
	}
	defer link.Close()

	dev := tfw.New(link, tfw.WithTimeout(*timeout))
	ctx := context.Background()

	info, err := dev.ProvInfo(ctx)
	if err != nil {
		// The most common cause by far, and worth saying rather than making
		// someone infer it from "unknown command".
		fail(fmt.Errorf("%w (is this device running a PROVISION=1 image?)", err))
	}
	report(info)

	switch {
	case *dryRun:
		plan(tmpl, info)
	case *verify:
		if err := runVerify(ctx, dev, tmpl, info); err != nil {
			fail(err)
		}
		fmt.Println("verify: the part matches the template")
	case *commit:
		if err := runCommit(ctx, dev, tmpl, info); err != nil {
			fail(err)
		}
		fmt.Println("commit: done. Run --verify before locking anything else.")
	}
}

func report(i *tfw.ElementInfo) {
	fmt.Printf("element   revision %s  serial %s\n",
		hex.EncodeToString(i.Revision[:]), hex.EncodeToString(i.Serial[:]))
	fmt.Printf("zones     config %s  data %s\n",
		lockWord(i.ConfigLocked), lockWord(i.DataLocked))
}

func lockWord(locked bool) string {
	if locked {
		return "LOCKED"
	}
	return "open"
}

// bakeFlagError reports a --bake-eeprom option passed to a wire mode, which
// would ignore it. Refused rather than ignored: `--commit --serial X` reads as
// "provision this identity", and an irreversible run that silently drops the
// identity it was handed is the one mistake this tool cannot undo.
type bakeFlagError struct {
	flag string
}

func (e *bakeFlagError) Error() string {
	return fmt.Sprintf("--%s belongs to --bake-eeprom; --dry-run/--verify/--commit do not use it", e.flag)
}

// rejectBakeFlags refuses any --bake-eeprom option set outside that mode.
func rejectBakeFlags(serialHex, hashHex, opKeyHex, outPath string) error {
	for _, f := range []struct{ name, value string }{
		{"serial", serialHex},
		{"pubkey-hash", hashHex},
		{"operator-pubkey", opKeyHex},
		{"out", outPath},
	} {
		if f.value != "" {
			return &bakeFlagError{flag: f.name}
		}
	}
	return nil
}

func fail(err error) {
	fmt.Fprintln(os.Stderr, "tfwprov:", err)
	os.Exit(1)
}
