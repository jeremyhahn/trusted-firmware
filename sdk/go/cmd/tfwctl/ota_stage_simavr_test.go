//go:build simavr

package main

import (
	"bytes"
	"context"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"syscall"
	"testing"
	"time"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
)

// From the host into the ATmega2560's store, through tfw-boot (#75): the
// simavr boot harness in its bridge mode runs tfw-boot and the application
// built with the stager, with UART0 on its stdin and stdout. The host asserts
// what the wire shows -- StageImage's result and OTA_STATUS; SIGUSR1 resets
// the part, and the harness asserts the boot (the image checked, copied into
// the primary, on trial) from the EEPROM block. On the trial the test reads
// OTA_INFO and sends OTA_CONFIRM; a second SIGUSR1 takes the settling reset,
// which the harness asserts the same way, and it exits non-zero on a failure. The image is the harness's own, signed by its modelled element: the
// simavr suite's one key. `make integration-test-boot` runs it.
func TestOTAStageThroughTfwBoot(t *testing.T) {
	paths := map[string]string{}
	for _, name := range []string{"TFW_BOOT_HARNESS", "TFW_BOOT_ELF", "TFW_APP_ELF"} {
		p := os.Getenv(name)
		if p == "" {
			t.Fatalf("%s is not set -- run `make integration-test-boot`, which builds the harness and both images", name)
		}
		if _, err := os.Stat(p); err != nil {
			t.Fatalf("%s: %v -- run `make integration-test-boot`", name, err)
		}
		paths[name] = p
	}

	staged := filepath.Join(t.TempDir(), "staged.bin")
	cmd := exec.Command(paths["TFW_BOOT_HARNESS"], paths["TFW_BOOT_ELF"], paths["TFW_APP_ELF"], "bridge", staged)
	var report bytes.Buffer
	cmd.Stderr = &report
	in, err := cmd.StdinPipe()
	if err != nil {
		t.Fatal(err)
	}
	out, err := cmd.StdoutPipe()
	if err != nil {
		t.Fatal(err)
	}
	if err := cmd.Start(); err != nil {
		t.Fatal(err)
	}
	waited := false
	t.Cleanup(func() {
		if !waited {
			_ = cmd.Process.Kill()
			_ = cmd.Wait()
		}
		// Only on a failure: the report carries the UART's raw bytes as well
		// as the harness's text, and binary frames are noise in a passing log.
		if t.Failed() {
			t.Logf("boot harness:\n%s", report.String())
		}
	})

	var img []byte
	for deadline := time.Now().Add(time.Minute); ; time.Sleep(50 * time.Millisecond) {
		if img, err = os.ReadFile(staged); err == nil {
			break
		}
		if time.Now().After(deadline) {
			t.Fatalf("the harness wrote no image: %v", err)
		}
	}

	conn := struct {
		io.Reader
		io.Writer
	}{out, in}
	dev := tfw.New(conn, tfw.WithTimeout(time.Minute), tfw.WithBootWait(0))
	// OTA_INFO on the image tfw-boot handed off to (docs/ota/api.md:62): TFW_OK
	// (0), slot 0 the running image, GOOD (2) as the harness stored it, no
	// fallback and no trial. The floor is this CRYPTO=soft image's, the
	// platform block's (src/core/ota_floor_eeprom.c), and is logged, not pinned.
	si, err := dev.OTAInfo(context.Background(), 0)
	if err != nil {
		t.Fatalf("OTA_INFO 0 under tfw-boot: %v, want TFW_OK (0)", err)
	}
	if si.Slots != 2 || si.Active != 0 || si.Flags != 0 || si.State != 2 {
		t.Fatalf("OTA_INFO 0 under tfw-boot: %+v, want 2 slots, active 0, no flags, GOOD (2)", *si)
	}
	t.Logf("OTA_INFO 0 under tfw-boot: floor %d", si.Floor)
	if err := dev.StageImage(context.Background(), img); err != nil {
		t.Fatalf("StageImage: %v", err)
	}
	expectSimStatus(t, dev, fmt.Sprintf("%s, %d bytes received", tfw.OTAComplete, len(img)))
	// OTA_END writes no MARK; OTA_ACTIVATE re-verifies and writes it, so
	// without it the reset takes nothing (OTA ADR decision 8).
	if err := run(context.Background(), dev, "ota-activate", nil, false, io.Discard); err != nil {
		t.Fatalf("ota-activate: %v", err)
	}
	// The staged slot is PENDING (4) at the harness's version 3.
	if si, err = dev.OTAInfo(context.Background(), 1); err != nil {
		t.Fatalf("OTA_INFO 1 after OTA_ACTIVATE: %v, want TFW_OK (0)", err)
	}
	if si.State != 4 || si.ImageVersion != 3 {
		t.Fatalf("OTA_INFO 1 after OTA_ACTIVATE: %+v, want PENDING (4), version 3", *si)
	}

	if err := cmd.Process.Signal(syscall.SIGUSR1); err != nil {
		t.Fatalf("resetting the part: %v", err)
	}
	// The harness asserts the trial boot, bridges the UART again and writes
	// <staged>.trial. The trial deadline runs in emulated time from here.
	for deadline := time.Now().Add(time.Minute); ; time.Sleep(50 * time.Millisecond) {
		if _, err = os.Stat(staged + ".trial"); err == nil {
			break
		}
		if time.Now().After(deadline) {
			t.Fatalf("the trial image did not come up: %v (the harness's report is logged)", err)
		}
	}
	// OTA_STATUS (docs/ota/api.md:19) is the stager's RAM session, which the
	// reset ended: idle (0, api.md:25), 0 bytes. The trial is OTA_INFO's.
	expectSimStatus(t, dev, fmt.Sprintf("%s, 0 bytes received", tfw.OTAIdle))
	// OTA_INFO 0 (api.md:62, :68-71): TFW_OK (0), TRIAL (3), the trial flag
	// 0x04 set. The floor is the platform block's (ota_floor_eeprom.c, linked
	// for CRYPTO=soft by mk/firmware.mk:330), which the harness stores as 0.
	if si, err = dev.OTAInfo(context.Background(), 0); err != nil {
		t.Fatalf("OTA_INFO 0 on trial: %v, want TFW_OK (0)", err)
	}
	if si.State != 3 || si.Flags&tfw.SlotTrial == 0 || si.Floor != 0 {
		t.Fatalf("OTA_INFO 0 on trial: %+v, want TRIAL (3), flag 0x04, floor 0", *si)
	}
	// OTA_CONFIRM (api.md:64) answers TFW_OK (0); BAD_STATE (19, api.md:90)
	// would mean no trial or no handoff.
	if err := dev.OTAConfirm(context.Background()); err != nil {
		t.Fatalf("OTA_CONFIRM on trial: %v, want TFW_OK (0)", err)
	}
	// The confirm is a request record; tfw-boot settles it at the next reset
	// (api.md:78-80), so the block still reads TRIAL (3) until then.
	if si, err = dev.OTAInfo(context.Background(), 0); err != nil {
		t.Fatalf("OTA_INFO 0 after OTA_CONFIRM: %v, want TFW_OK (0)", err)
	}
	if si.State != 3 || si.Floor != 0 {
		t.Fatalf("OTA_INFO 0 after OTA_CONFIRM: %+v, want TRIAL (3) until the reset, floor 0", *si)
	}
	// The settling reset: the harness asserts GOOD and the advanced floor.
	if err := cmd.Process.Signal(syscall.SIGUSR1); err != nil {
		t.Fatalf("resetting the part after OTA_CONFIRM: %v", err)
	}
	err = cmd.Wait()
	waited = true
	if err != nil {
		t.Fatalf("the boot after the reset: %v (the harness's report is logged)", err)
	}
}
