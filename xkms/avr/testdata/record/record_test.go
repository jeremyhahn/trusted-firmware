// Copyright (c) 2026 Jeremy Hahn
// Copyright (c) 2026 Automate The Things, LLC
//
// This file is part of trusted-firmware.

// Package record captures the frames the element simulator answers a
// device-attest-01 enrolment with, and prints them as the Go literals
// xkms/avr/device_attest_vectors_test.go pins.
//
// It is a recorder, not a test: it lives under testdata so `./...` never
// compiles it and no gate runs it. It was run once, as
//
//	make tfwsim-atecc608a
//	cd xkms && TFWSIM_DIR=$PWD/../build/host go test -count=1 -v ./avr/testdata/record
//
// and its output pasted. Running it again prints new signatures (the element
// signs with a fresh k), so its output is never compared with the pinned
// literals; a re-recording replaces them, with this command beside them.
package record

import (
	"bytes"
	"context"
	"crypto/x509"
	"encoding/hex"
	"fmt"
	"io"
	"strings"
	"sync"
	"testing"
	"time"

	deviceattest01 "github.com/jeremyhahn/go-xkms/pkg/acme/challenges/device-attest-01"
	"github.com/jeremyhahn/go-xkms/pkg/types"
	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/tfwsim"
	"github.com/jeremyhahn/trusted-firmware/xkms/avr"
	"github.com/stretchr/testify/require"
)

// tap keeps every byte the simulator writes, so the responses can be decoded
// after the exchange exactly as they crossed the wire.
type tap struct {
	io.ReadWriter
	mu sync.Mutex
	rx bytes.Buffer
}

func (t *tap) Read(p []byte) (int, error) {
	n, err := t.ReadWriter.Read(p)
	t.mu.Lock()
	t.rx.Write(p[:n])
	t.mu.Unlock()
	return n, err
}

// drain decodes the responses received since the last drain.
func (t *tap) drain() []tfw.Frame {
	t.mu.Lock()
	buf := append([]byte(nil), t.rx.Bytes()...)
	t.rx.Reset()
	t.mu.Unlock()
	var out []tfw.Frame
	for len(buf) > 0 {
		f, n, err := tfw.Decode(buf)
		if err != nil || n == 0 {
			buf = buf[1:]
			continue
		}
		if f.IsResponse() {
			out = append(out, f)
		}
		buf = buf[n:]
	}
	return out
}

func literal(name string, b []byte) string {
	h := hex.EncodeToString(b)
	var s strings.Builder
	fmt.Fprintf(&s, "%s = \"\" +\n", name)
	for len(h) > 64 {
		fmt.Fprintf(&s, "\t\"%s\" +\n", h[:64])
		h = h[64:]
	}
	fmt.Fprintf(&s, "\t\"%s\"\n", h)
	return s.String()
}

func show(t *testing.T, name string, frames []tfw.Frame) {
	for i, f := range frames {
		t.Logf("%s[%d] cmd=%#x status=%d\n%s", name, i, uint8(f.Cmd()), f.Status, literal(name, f.Payload))
	}
}

func TestRecord(t *testing.T) {
	link := &tap{}
	device := tfwsim.StartElementVia(t, func(rw io.ReadWriter) io.ReadWriter {
		link.ReadWriter = rw
		return link
	})
	cfg := avr.DefaultConfig()
	backend, err := avr.NewBackend(context.Background(), cfg, device)
	require.NoError(t, err)
	show(t, "caps", link.drain())

	spki, err := x509.MarshalPKIXPublicKey(tfwsim.TestDeviceIDPublicKey())
	require.NoError(t, err)
	t.Logf("%s", literal("deviceIDSPKI", spki))
	t.Logf("%s", literal("ueid", append([]byte{0x01}, tfwsim.TestSerial[:]...)))

	for _, keyAuth := range []string{"recorded-token.recorded-thumbprint", "another-token.another-thumbprint"} {
		nonce := deviceattest01.DICENonce(keyAuth)
		ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
		_, err := backend.AttestKey(ctx, &types.KeyAttributes{CN: avr.DefaultKeyCN}, nonce)
		require.NoError(t, err)
		show(t, "cert "+keyAuth, link.drain())

		var b [tfw.AliasNonceLen]byte
		copy(b[:], nonce)
		_, err = device.AttestCSR(ctx, b)
		cancel()
		require.NoError(t, err)
		show(t, "csr "+keyAuth, link.drain())
	}
}
