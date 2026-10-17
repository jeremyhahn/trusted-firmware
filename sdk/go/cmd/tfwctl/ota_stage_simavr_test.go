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
// the primary, on trial) from the EEPROM block and exits non-zero on a
// failure. The image is the harness's own, signed by its modelled element: the
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
	if err := dev.StageImage(context.Background(), img); err != nil {
		t.Fatalf("StageImage: %v", err)
	}
	expectSimStatus(t, dev, fmt.Sprintf("%s, %d bytes received", tfw.OTAComplete, len(img)))
	// OTA_END writes no MARK; OTA_ACTIVATE re-verifies and writes it, so
	// without it the reset takes nothing (OTA ADR decision 8).
	if err := run(context.Background(), dev, "ota-activate", nil, false, io.Discard); err != nil {
		t.Fatalf("ota-activate: %v", err)
	}

	if err := cmd.Process.Signal(syscall.SIGUSR1); err != nil {
		t.Fatalf("resetting the part: %v", err)
	}
	err = cmd.Wait()
	waited = true
	if err != nil {
		t.Fatalf("the boot after the reset: %v (the harness's report is logged)", err)
	}
}
