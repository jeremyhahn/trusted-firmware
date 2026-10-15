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
// are both parameters. `make hw-enroll` sets these; `make xkms-test` sets none
// of them and the run is the simulator's, so the code a bench run takes is the
// code CI takes. Procedure: docs/development/hardware-enrollment.md.
const (
	// envDevice is the board: a serial port path. Unset, the simulator.
	envDevice = "TFW_HW_DEVICE"
	// envBootWait is how long to wait for the board's BOOT event after the
	// port opens (Go duration). Unset, tfw.DefaultBootWait.
	envBootWait = "TFW_HW_BOOT_WAIT"
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

// enrollTarget is a device to enroll and what a registry knows about it.
type enrollTarget struct {
	name      string
	transport string
	open      func(t *testing.T) *tfw.Device
	// deviceID is the provisioned DeviceID key the in-process registry holds.
	deviceID func(t *testing.T) *ecdsa.PublicKey
}

// enrollTargets is the operator's board when one is named, else the
// simulator.
//
// The simulator over TCP (tfwsim.StartElementEthernet) is not here yet: the
// Ethernet transport (#59) is in the working tree's sdk/go and not in the tag
// xkms/go.mod requires, and this module never builds against the working tree
// (docs/xkms-adapter/engineering.md). It goes in with the go.mod bump to the
// first sdk/go tag that carries it, and so does a board over TCP.
func enrollTargets(t *testing.T) []enrollTarget {
	t.Helper()
	spec := os.Getenv(envDevice)
	if spec == "" {
		return []enrollTarget{{
			name:      "tfwsim-serial",
			transport: "serial",
			open:      func(t *testing.T) *tfw.Device { return tfwsim.StartElement(t) },
			deviceID:  func(*testing.T) *ecdsa.PublicKey { return tfwsim.TestDeviceIDPublicKey() },
		}}
	}
	if _, _, err := net.SplitHostPort(spec); err == nil {
		t.Fatalf("%s=%s is a TCP address: a board over Ethernet needs tfw.Listen (#59), which is not "+
			"in the sdk/go tag xkms/go.mod requires. Tag sdk/go and bump xkms/go.mod first, or use the "+
			"board's serial port", envDevice, spec)
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
