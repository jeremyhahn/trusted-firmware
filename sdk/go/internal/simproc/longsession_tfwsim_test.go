// SPDX-License-Identifier: AGPL-3.0-or-later

//go:build tfwsim

package simproc

import (
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/binary"
	"errors"
	"syscall"
	"testing"
	"time"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
)

// Issue #163, against the real firmware built for the host: one tfwsim
// process went deaf after about 4 KiB from the host, so a long session hung
// on its 63rd 60-byte PING or its seventh OTA page. Well past that here: 300
// PINGs and then a 32-page image, every one answered, on one process.
func TestASimAnswersALongSessionPastFourKiB(t *testing.T) {
	d := tfw.New(Start(t, Software), tfw.WithTimeout(5*time.Second))

	msg := make([]byte, 60)
	for i := range msg {
		msg[i] = byte(i*5 + 1)
	}
	within(t, 2*time.Minute, "the long session", func() {
		for i := range 300 {
			msg[0] = byte(i)
			got, err := d.Ping(t.Context(), msg)
			if err != nil || !bytes.Equal(got, msg) {
				t.Errorf("PING %d: %x, %v; want the echo", i, got, err)
				return
			}
		}
		if err := d.StageImage(t.Context(), simImage(32*256, 7000)); err != nil {
			t.Errorf("staging a 32-page image after the PINGs: %v", err)
		}
	})
}

// A sim that stops answering fails a request within the SDK's own timeout,
// as a serial port or a TCP link does, rather than holding the test until
// go test's alarm. SIGSTOP is the sim hung: alive, its pipe open, silent.
func TestAStoppedSimFailsARequestWithinTheTimeout(t *testing.T) {
	const timeout = time.Second
	c := Start(t, Software)
	d := tfw.New(c, tfw.WithTimeout(timeout))

	within(t, time.Minute, "the first PING", func() {
		if _, err := d.Ping(t.Context(), []byte("up")); err != nil {
			t.Errorf("PING before the stop: %v", err)
		}
	})
	if err := c.cmd.Process.Signal(syscall.SIGSTOP); err != nil {
		t.Fatal(err)
	}

	var err error
	start := time.Now()
	within(t, time.Minute, "the PING to a stopped sim", func() {
		_, err = d.Ping(context.Background(), []byte("down"))
	})
	took := time.Since(start)
	var te *tfw.TimeoutError
	if !errors.As(err, &te) {
		t.Fatalf("PING to a stopped sim: %v; want a *tfw.TimeoutError", err)
	}
	if took > timeout+2*time.Second {
		t.Fatalf("PING to a stopped sim failed after %v; want within about %v", took, timeout)
	}
}

// simImage builds a total-byte image whose last 128 bytes are a trailer
// declaring a body-byte image, version 3, with its SHA-256, per
// docs/ota/adr-ota-image-format-and-trust.md; between them is erased flash.
func simImage(total, body int) []byte {
	img := bytes.Repeat([]byte{0xFF}, total)
	for i := range body {
		img[i] = byte(i*7 + 3)
	}
	tr := img[total-128:]
	clear(tr)
	copy(tr, "TFIM")
	binary.LittleEndian.PutUint16(tr[4:], 1)
	tr[6] = 0x01
	binary.LittleEndian.PutUint32(tr[8:], 3)
	binary.LittleEndian.PutUint32(tr[12:], uint32(body))
	sum := sha256.Sum256(img[:body])
	copy(tr[16:], sum[:])
	return img
}
