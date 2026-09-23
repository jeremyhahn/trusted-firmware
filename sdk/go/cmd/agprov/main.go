// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

// Command agprov provisions an arduino-generic device's secure element.
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
// built with AG_FEATURE_PROVISION 0 and has no handler for any of these
// commands -- it answers "unknown command" -- because a device that can be
// re-provisioned over its own command channel has no identity worth attesting
// to.
package main

import (
	"context"
	"encoding/hex"
	"errors"
	"flag"
	"fmt"
	"os"
	"time"

	ag "github.com/jeremyhahn/arduino-generic/sdk/go"
	"github.com/jeremyhahn/arduino-generic/sdk/go/serialport"
)

func usage() {
	fmt.Fprint(os.Stderr, `agprov -- provision an arduino-generic device's secure element

  agprov [flags] <port>

exactly one mode is required

  --dry-run    print the plan; write nothing
  --verify     read the part back and compare against the template
  --commit     write. IRREVERSIBLE. requires --yes-i-mean-it

flags
`)
	flag.PrintDefaults()
	fmt.Fprint(os.Stderr, `
The element cannot be unlocked. Run --commit on a spare part first.
See docs/security/provisioning.md.
`)
}

func main() {
	var (
		dryRun   = flag.Bool("dry-run", false, "print the plan and change nothing")
		verify   = flag.Bool("verify", false, "read the part back and compare")
		commit   = flag.Bool("commit", false, "write; irreversible")
		confirm  = flag.Bool("yes-i-mean-it", false, "required by --commit")
		cfgPath  = flag.String("config", "", "provisioning template (YAML)")
		baud     = flag.Int("baud", serialport.DefaultBaud, "serial baud rate")
		timeout  = flag.Duration("timeout", 30*time.Second, "how long to wait for a reply")
		confirm2 = flag.Bool("force", false, "deprecated alias; refused, use --yes-i-mean-it")
	)
	flag.Usage = usage
	flag.Parse()

	if *confirm2 {
		// Refused rather than accepted as a synonym. --force is the flag
		// someone types from muscle memory, which is exactly the reflex this
		// tool exists to interrupt.
		fail(errors.New("--force is not accepted; --commit requires --yes-i-mean-it"))
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

	dev := ag.New(link, ag.WithTimeout(*timeout))
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

func report(i *ag.ElementInfo) {
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

func fail(err error) {
	fmt.Fprintln(os.Stderr, "agprov:", err)
	os.Exit(1)
}
