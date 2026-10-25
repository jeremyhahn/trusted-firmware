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
	"unsafe"

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

	awaitFile(t, staged, time.Minute, "the harness wrote no image")
	img, err := os.ReadFile(staged)
	if err != nil {
		t.Fatal(err)
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
	// The row key's authorization (#137), which tfw-boot verifies at the
	// reset: for the version and measurement in the staged image's trailer.
	sig := simavrActivateSig(img)
	sigFile := filepath.Join(t.TempDir(), "activate.sig")
	if err := os.WriteFile(sigFile, sig[:], 0o600); err != nil {
		t.Fatal(err)
	}
	if err := run(context.Background(), dev, "ota-activate", []string{"--sig-file=" + sigFile}, false, io.Discard); err != nil {
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
	awaitFile(t, staged+".trial", time.Minute, "the trial image did not come up")
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
	// OTA_CONFIRM is authorized by the row key over its own Sig_structure:
	// the firmware stores the signature in the confirm request and tfw-boot
	// verifies it before it settles the trial (docs/ota/api.md, #231). The
	// activate's would roll the trial back.
	if err := dev.OTAAuth(context.Background(), simavrConfirmSig(img)); err != nil {
		t.Fatalf("OTA_AUTH before OTA_CONFIRM: %v", err)
	}
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

// awaitFile waits, bounded, for the harness to put path in place: renamed in
// (the image) or written and closed (the trial marker). inotify wakes the wait
// on that event; the deadline is the bound.
func awaitFile(t *testing.T, path string, bound time.Duration, failure string) {
	t.Helper()
	fd, err := syscall.InotifyInit1(syscall.IN_NONBLOCK | syscall.IN_CLOEXEC)
	if err != nil {
		t.Fatal(err)
	}
	// Non-blocking, so the file is pollable and takes a read deadline.
	events := os.NewFile(uintptr(fd), "inotify")
	defer func() { _ = events.Close() }()
	if _, err := syscall.InotifyAddWatch(fd, filepath.Dir(path), syscall.IN_MOVED_TO|syscall.IN_CLOSE_WRITE); err != nil {
		t.Fatal(err)
	}
	// In place before the watch began.
	if _, err := os.Stat(path); err == nil {
		return
	}
	if err := events.SetReadDeadline(time.Now().Add(bound)); err != nil {
		t.Fatal(err)
	}
	want := filepath.Base(path)
	buf := make([]byte, 4096)
	for {
		n, err := events.Read(buf)
		if err != nil {
			t.Fatalf("%s: %v (the harness's report is logged)", failure, err)
		}
		for off := 0; off+syscall.SizeofInotifyEvent <= n; {
			ev := (*syscall.InotifyEvent)(unsafe.Pointer(&buf[off]))
			name := buf[off+syscall.SizeofInotifyEvent : off+syscall.SizeofInotifyEvent+int(ev.Len)]
			if string(bytes.TrimRight(name, "\x00")) == want {
				return
			}
			off += syscall.SizeofInotifyEvent + int(ev.Len)
		}
	}
}
