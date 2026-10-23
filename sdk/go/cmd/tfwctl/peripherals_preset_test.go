//go:build tfwsim

package main

import (
	"bytes"
	"context"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/x509"
	"encoding/hex"
	"encoding/pem"
	"errors"
	"io"
	"os"
	"path/filepath"
	"testing"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/tfwsim"
)

// The mock HAL tfwsim links answers no peripheral until it is told to. These
// presets (tools/tfwsim/main.c, preset_mock) make each one answer, so the
// success output of every verb below is pinned, text and JSON, as literals.
var peripheralPresets = []string{
	"TFWSIM_PULSE_IN_US=1234",
	"TFWSIM_CAPTURE_US=100,250,4000",
	"TFWSIM_BUS_RESPONSE=a1b2c3d4",
	"TFWSIM_OW_PRESENCE=1",
}

func TestPeripheralSuccessOutputAgainstPresetTfwsim(t *testing.T) {
	dev := startSimWith(t, peripheralPresets...)

	for _, tc := range []struct {
		verb     string
		args     []string
		text     string
		jsonText string
	}{
		{
			"pulse-in", []string{"2", "1", "10ms"},
			"1234 us\n",
			"{\n  \"pin\": 2,\n  \"us\": 1234\n}\n",
		},
		{
			"pulse-capture", []string{"2", "1", "8", "10ms"},
			"100 250 4000\n",
			"{\n  \"edges_us\": [\n    100,\n    250,\n    4000\n  ],\n  \"pin\": 2\n}\n",
		},
		{
			"i2c", []string{"0x20", "-", "4"},
			"a1b2c3d4\n",
			"{\n  \"addr\": 32,\n  \"hex\": \"a1b2c3d4\"\n}\n",
		},
		{
			"1wire-reset", []string{"4"},
			"presence\n",
			"{\n  \"pin\": 4,\n  \"present\": true\n}\n",
		},
		{
			"1wire-write", []string{"4", "cc"},
			"wrote 1 bytes\n",
			"{\n  \"bytes\": 1,\n  \"pin\": 4\n}\n",
		},
	} {
		t.Run(tc.verb, func(t *testing.T) {
			ctx := context.Background()
			var text, js bytes.Buffer
			if err := runPeripheral(ctx, dev, tc.verb, tc.args, false, &text); err != nil {
				t.Fatalf("%s: %v", tc.verb, err)
			}
			if got := text.String(); got != tc.text {
				t.Fatalf("%s printed %q, want %q", tc.verb, got, tc.text)
			}
			if err := runPeripheral(ctx, dev, tc.verb, tc.args, true, &js); err != nil {
				t.Fatalf("%s --json: %v", tc.verb, err)
			}
			if got := js.String(); got != tc.jsonText {
				t.Fatalf("%s --json printed %q, want %q", tc.verb, got, tc.jsonText)
			}
		})
	}
}

// writeOperatorKey writes a fresh P-256 key as the PEM file tfwctl's auth
// verb reads, and returns its public half for the element to trust.
func writeOperatorKey(t *testing.T) (*ecdsa.PublicKey, string) {
	t.Helper()
	key, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	der, err := x509.MarshalECPrivateKey(key)
	if err != nil {
		t.Fatal(err)
	}
	path := filepath.Join(t.TempDir(), "operator.pem")
	if err := os.WriteFile(path, pem.EncodeToMemory(&pem.Block{Type: "EC PRIVATE KEY", Bytes: der}), 0o600); err != nil {
		t.Fatal(err)
	}
	return &key.PublicKey, path
}

func TestAuthOpensTheSessionAndRunsTheChainedVerb(t *testing.T) {
	pub, keyPath := writeOperatorKey(t)
	serial := hex.EncodeToString(tfwsim.TestSerial[:])
	ctx := context.Background()

	t.Run("alone", func(t *testing.T) {
		dev := tfwsim.StartElementWithOperator(t, pub)
		var text, js bytes.Buffer
		if err := runPeripheral(ctx, dev, "auth", []string{serial, keyPath}, false, &text); err != nil {
			t.Fatal(err)
		}
		if got := text.String(); got != "authenticated\n" {
			t.Fatalf("auth printed %q, want \"authenticated\\n\"", got)
		}
		if err := runPeripheral(ctx, dev, "auth", []string{serial, keyPath}, true, &js); err != nil {
			t.Fatal(err)
		}
		if got := js.String(); got != "{\n  \"authenticated\": true\n}\n" {
			t.Fatalf("auth --json printed %q", got)
		}
	})

	t.Run("chained", func(t *testing.T) {
		dev := tfwsim.StartElementWithOperator(t, pub)
		var buf bytes.Buffer
		// The reserved region is refused outside a session, so reading it
		// shows the session open.
		if err := runPeripheral(ctx, dev, "eeprom-read", []string{"0", "4"}, false, io.Discard); !errors.Is(err, tfw.StatusErrAuthRequired) {
			t.Fatalf("eeprom-read 0 4 before auth: err = %v, want %v", err, tfw.StatusErrAuthRequired)
		}
		args := []string{serial, keyPath, "--", "eeprom-read", "0", "4"}
		if err := runPeripheral(ctx, dev, "auth", args, false, &buf); err != nil {
			t.Fatal(err)
		}
		// The chained verb's output only: auth prints nothing of its own
		// when it hands the connection on. 54465052 is the platform
		// block's magic.
		if got := buf.String(); got != "54465052\n" {
			t.Fatalf("auth -- eeprom-read 0 4 printed %q, want \"54465052\\n\"", got)
		}
	})
}
