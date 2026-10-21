//go:build codec_json

// Copyright (c) 2026 Jeremy Hahn
// Copyright (c) 2026 Automate The Things, LLC
//
// This file is part of go-xkms.
//
// go-xkms is dual-licensed:
//
// 1. GNU Affero General Public License v3.0 (AGPL-3.0)
//    See LICENSE file or visit https://www.gnu.org/licenses/agpl-3.0.html
//
// 2. Commercial License
//    Contact licensing@automatethethings.com for commercial licensing options.

package e2e

import (
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/x509"
	"encoding/hex"
	"encoding/pem"
	"fmt"
	"net"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/serialport"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/tfwsim"
	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	deviceattest01 "github.com/jeremyhahn/go-xkms/pkg/acme/challenges/device-attest-01"
	"github.com/jeremyhahn/go-xkms/pkg/acme/client"
)

// The hardware enrollment run (#7): one device-attest-01 enrollment, through
// the flow the tests above prove, of a device and against an ACME server that
// are both parameters. `make hw-enroll` sets these; run by hand\n// with none of them set it is the simulator's. CI does not run it: `make\n// xkms-test` deselects it (#163 decision 2). Procedure: docs/development/hardware-enrollment.md.
const (
	// envDevice is the board: a serial port path, or the "host:port" this
	// host listens on for a board over Ethernet. Unset, the simulator.
	envDevice = "TFW_HW_DEVICE"
	// envBootWait is how long to wait for the board's BOOT event after the
	// port opens (Go duration). Unset, tfw.DefaultBootWait.
	envBootWait = "TFW_HW_BOOT_WAIT"
	// envConnectWait is how long to wait for a board over Ethernet to
	// connect (Go duration). Unset, defaultConnectWait.
	envConnectWait = "TFW_HW_CONNECT_WAIT"
	// envDeviceID is the board's DeviceID public key as tfwprov --commit
	// printed it (hex X||Y). The in-process server's registry holds it; a
	// control plane holds its own and does not read this.
	envDeviceID = "TFW_HW_DEVICEID"
	// envACMEDirectory is an external ACME directory URL: the
	// go-trusted-platform test control plane. Unset, the in-process server.
	envACMEDirectory = "TFW_HW_ACME_DIRECTORY"
	// envACMERoots is a PEM file of the roots the directory's TLS chains to.
	// Unset, the system pool.
	envACMERoots = "TFW_HW_ACME_ROOTS"
	// envRecordDir is where the run record is written. Unset, a temporary
	// directory the test reads it back from.
	envRecordDir = "TFW_HW_RECORD_DIR"
)

// defaultConnectWait covers a board's boot, its link coming up and its first
// dial, which follows a backoff when the host was not yet listening.
const defaultConnectWait = time.Minute

// enrollTarget is a device to enroll and what a registry knows about it.
type enrollTarget struct {
	name      string
	transport string
	open      func(t *testing.T) *tfw.Device
	// deviceID is the provisioned DeviceID key the in-process registry holds.
	deviceID func(t *testing.T) *ecdsa.PublicKey
}

// enrollTargets is the operator's board when one is named, else the
// simulator over each transport.
//
// The transport is read from the form of the name: a "host:port" is TCP, and
// is the address this host listens on for the board to dial; anything else is
// a serial port path.
func enrollTargets(t *testing.T) []enrollTarget {
	t.Helper()
	spec := os.Getenv(envDevice)
	if spec == "" {
		return []enrollTarget{{
			name:      "tfwsim-serial",
			transport: "serial",
			open:      func(t *testing.T) *tfw.Device { return tfwsim.StartElement(t) },
			deviceID:  func(*testing.T) *ecdsa.PublicKey { return tfwsim.TestDeviceIDPublicKey() },
		}, {
			name:      "tfwsim-tcp",
			transport: "tcp",
			open: func(t *testing.T) *tfw.Device {
				_, eth := tfwsim.StartElementEthernet(t)
				return eth.Device
			},
			deviceID: func(*testing.T) *ecdsa.PublicKey { return tfwsim.TestDeviceIDPublicKey() },
		}}
	}
	if _, _, err := net.SplitHostPort(spec); err == nil {
		return []enrollTarget{{
			name:      "operator-tcp",
			transport: "tcp",
			open:      func(t *testing.T) *tfw.Device { return acceptTCPBoard(t, spec) },
			deviceID:  operatorDeviceID,
		}}
	}
	return []enrollTarget{{
		name:      "operator-serial",
		transport: "serial",
		open:      func(t *testing.T) *tfw.Device { return openSerialBoard(t, spec) },
		deviceID:  operatorDeviceID,
	}}
}

func openSerialBoard(t *testing.T, path string) *tfw.Device {
	t.Helper()
	bootWait := tfw.DefaultBootWait
	if s := os.Getenv(envBootWait); s != "" {
		d, err := time.ParseDuration(s)
		require.NoError(t, err, "%s", envBootWait)
		bootWait = d
	}
	port, err := serialport.Open(path, serialport.DefaultBaud)
	require.NoError(t, err, "opening %s", path)
	t.Cleanup(func() { _ = port.Close() })
	return tfw.New(port, tfw.WithBootWait(bootWait))
}

// acceptTCPBoard listens on addr and takes the first device that connects.
// The device dials the host (sdk/go/tcp.go), so addr is this host's address
// and the board's network block names it as the endpoint.
func acceptTCPBoard(t *testing.T, addr string) *tfw.Device {
	t.Helper()
	connectWait := defaultConnectWait
	if s := os.Getenv(envConnectWait); s != "" {
		d, err := time.ParseDuration(s)
		require.NoError(t, err, "%s", envConnectWait)
		connectWait = d
	}
	raw, err := net.Listen("tcp", addr)
	require.NoError(t, err, "listening on %s", addr)
	ln := tfw.NewListener(raw)
	t.Cleanup(func() { _ = ln.Close() })
	require.NoError(t, raw.(*net.TCPListener).SetDeadline(time.Now().Add(connectWait)))
	eth, err := ln.Accept()
	require.NoError(t, err, "no device connected to %s within %s: check the board's network block names "+
		"this address as its endpoint, and raise %s if it boots slowly", addr, connectWait, envConnectWait)
	t.Cleanup(func() { _ = eth.Close() })
	return eth.Device
}

func operatorDeviceID(t *testing.T) *ecdsa.PublicKey {
	t.Helper()
	s := os.Getenv(envDeviceID)
	if s == "" {
		t.Fatalf("the in-process ACME server needs the board's provisioned DeviceID key: set %s to "+
			"the hex X||Y tfwprov --commit printed, or name a control plane with %s", envDeviceID, envACMEDirectory)
	}
	return parseDeviceID(t, s)
}

// parseDeviceID reads the 64-byte X||Y point tfwprov prints.
func parseDeviceID(t *testing.T, s string) *ecdsa.PublicKey {
	t.Helper()
	point, err := hex.DecodeString(strings.TrimSpace(s))
	require.NoError(t, err, "%s", envDeviceID)
	require.Len(t, point, 64, "%s is the 64-byte X||Y point", envDeviceID)
	key, err := ecdsa.ParseUncompressedPublicKey(elliptic.P256(), append([]byte{0x04}, point...))
	require.NoError(t, err, "%s is not a P-256 point", envDeviceID)
	return key
}

// enrollment is the ACME side of a run: the client, and what is known about
// the server that answered it.
type enrollment struct {
	client *client.Client
	// verdict is what the evidence was judged, by whom.
	verdict func(t *testing.T) string
	// issuer is the issuing CA when this host knows it; nil when a control
	// plane issued.
	issuer *x509.Certificate
	// confirm checks the server's side of an issuance, where it can be seen.
	confirm func(t *testing.T, cert *x509.Certificate)
}

func newRunEnrollment(t *testing.T, target enrollTarget, identity string) *enrollment {
	t.Helper()
	if directory := os.Getenv(envACMEDirectory); directory != "" {
		var roots []string
		if path := os.Getenv(envACMERoots); path != "" {
			pemBytes, err := os.ReadFile(path)
			require.NoError(t, err, "%s", envACMERoots)
			roots = []string{string(pemBytes)}
		}
		return &enrollment{
			client: newDeviceAttestClient(t, directory, roots),
			verdict: func(*testing.T) string {
				return "accepted by the external ACME server (it issued); its verifier's, not this host's"
			},
			confirm: func(*testing.T, *x509.Certificate) {},
		}
	}

	local := newDiceEnrollment(t, provisioningRecord(t, identity, target.deviceID(t)))
	return &enrollment{
		client: local.client,
		verdict: func(t *testing.T) string {
			require.NoError(t, local.verifier.Refusal())
			return "accepted by go-xkms deviceattest01.TrustAnchorVerifier (DICE, device-signed leaf, provisioned DeviceID)"
		},
		issuer: local.issuer,
		confirm: func(t *testing.T, cert *x509.Certificate) {
			select {
			case issued := <-local.server.issued:
				assert.Equal(t, issued.Raw, cert.Raw)
			default:
				t.Fatal("the server issued no certificate")
			}
		},
	}
}

// runRecord is what a run leaves behind (docs/development/hardware-enrollment.md).
// It names no address, port or URL: a record is committed, and the bench's
// are private-fleet values (scripts/releasability.patterns).
type runRecord struct {
	Board       string
	Transport   string
	Firmware    string
	CryptoTier  string
	AttestLevel string
	Identity    string
	ACME        string
	Verdict     string
	Time        time.Time
	Chain       []*x509.Certificate
}

// enrollOnce enrolls the target through the same flow as
// TestDeviceAttestDICEEnrollsTheRealFirmware and checks what it was issued.
func enrollOnce(t *testing.T, target enrollTarget) *runRecord {
	t.Helper()
	element := newDiceElementOn(t, target.open(t))
	acme := newRunEnrollment(t, target, element.identity)

	cert, err := deviceAttest(acme.client, element)
	require.NoError(t, err)
	verdict := acme.verdict(t)
	acme.confirm(t, cert)

	att, err := element.attest(deviceattest01.DICENonce("run-record"))
	require.NoError(t, err)
	attested, ok := att.Certificate.PublicKey.(*ecdsa.PublicKey)
	require.True(t, ok, "the device attested a %T", att.Certificate.PublicKey)
	assert.True(t, cert.PublicKey.(*ecdsa.PublicKey).Equal(attested),
		"the certificate must be for the key the device attested")
	assert.Equal(t, element.identity, permanentIdentifier(t, cert),
		"the certificate must be bound to the device's ueid")

	caps := element.backend.Caps()
	where := "in-process go-xkms test server"
	if os.Getenv(envACMEDirectory) != "" {
		where = "external (operator-supplied directory)"
	}
	chain := []*x509.Certificate{cert}
	if acme.issuer != nil {
		chain = append(chain, acme.issuer)
	}
	return &runRecord{
		Board:       caps.BoardID.String(),
		Transport:   target.transport,
		Firmware:    fmt.Sprintf("%d.%d.%d", caps.FirmwareMajor, caps.FirmwareMinor, caps.FirmwarePatch),
		CryptoTier:  att.CryptoTier.String(),
		AttestLevel: att.AttestLevel.String(),
		Identity:    element.identity,
		ACME:        where,
		Verdict:     verdict,
		Time:        time.Now().UTC(),
		Chain:       chain,
	}
}

// Markdown renders the record as the file the operator commits.
func (r *runRecord) Markdown() []byte {
	var b strings.Builder
	fmt.Fprintf(&b, "# Enrollment run: %s over %s\n\n", r.Board, r.Transport)
	b.WriteString("Written by `make hw-enroll` (docs/development/hardware-enrollment.md).\n\n")
	b.WriteString("| Field | Value |\n|---|---|\n")
	for _, row := range [][2]string{
		{"Board", r.Board},
		{"Transport", r.Transport},
		{"Firmware", r.Firmware},
		{"Crypto tier", r.CryptoTier},
		{"Attest level", r.AttestLevel},
		{"Device identity (ueid)", r.Identity},
		{"ACME server", r.ACME},
		{"Evidence verdict", r.Verdict},
		{"Time (UTC)", r.Time.Format(time.RFC3339)},
	} {
		fmt.Fprintf(&b, "| %s | %s |\n", row[0], row[1])
	}
	b.WriteString("\n## Issued certificate chain\n\nLeaf first.\n\n```pem\n")
	for _, cert := range r.Chain {
		b.Write(pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: cert.Raw}))
	}
	b.WriteString("```\n")
	return []byte(b.String())
}

// FileName is unique per board, transport and second.
func (r *runRecord) FileName() string {
	return fmt.Sprintf("%s-%s-%s.md", r.Board, r.Transport, r.Time.Format("20060102T150405Z"))
}

// TestHardwareEnrollment is the run `make hw-enroll` makes. With no board
// named it enrolls the simulator, and reads back the record it wrote.
func TestHardwareEnrollment(t *testing.T) {
	for _, target := range enrollTargets(t) {
		t.Run(target.name, func(t *testing.T) {
			record := enrollOnce(t, target)

			dir := os.Getenv(envRecordDir)
			if dir == "" {
				dir = t.TempDir()
			}
			path := filepath.Join(dir, record.FileName())
			require.NoError(t, os.WriteFile(path, record.Markdown(), 0o644))
			t.Logf("run record: %s", path)

			written, err := os.ReadFile(path)
			require.NoError(t, err)
			var chain [][]byte
			for rest := written; ; {
				var block *pem.Block
				block, rest = pem.Decode(rest)
				if block == nil {
					break
				}
				chain = append(chain, block.Bytes)
			}
			require.Len(t, chain, len(record.Chain), "the record carries the whole chain")
			for i, cert := range record.Chain {
				assert.Equal(t, cert.Raw, chain[i])
			}
			assert.Contains(t, string(written), "| Board | "+record.Board+" |")
			assert.Contains(t, string(written), "| Evidence verdict | accepted ")
		})
	}
}

// TestEnrollTargetsTransportFromDevice pins the rule `make hw-enroll` documents:
// the form of DEVICE picks the transport, and an unset DEVICE runs the
// simulator over both.
func TestEnrollTargetsTransportFromDevice(t *testing.T) {
	for _, tc := range []struct {
		device string
		want   []string
	}{
		{"", []string{"tfwsim-serial/serial", "tfwsim-tcp/tcp"}},
		{"/dev/ttyACM0", []string{"operator-serial/serial"}},
		{"192.0.2.1:7000", []string{"operator-tcp/tcp"}},
		{"0.0.0.0:7000", []string{"operator-tcp/tcp"}},
		{"[2001:db8::1]:7000", []string{"operator-tcp/tcp"}},
	} {
		t.Run(tc.device, func(t *testing.T) {
			t.Setenv(envDevice, tc.device)
			var got []string
			for _, target := range enrollTargets(t) {
				got = append(got, target.name+"/"+target.transport)
			}
			assert.Equal(t, tc.want, got)
		})
	}
}
