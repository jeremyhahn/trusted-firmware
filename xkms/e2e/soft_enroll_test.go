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
	"bytes"
	"context"
	"crypto/ecdsa"
	"crypto/x509"
	"encoding/hex"
	"io"
	"net"
	"net/netip"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
	"time"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/tfwsim"
	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	deviceattest01 "github.com/jeremyhahn/go-xkms/pkg/acme/challenges/device-attest-01"
	"github.com/jeremyhahn/go-xkms/pkg/attestation/dice"
	"github.com/jeremyhahn/go-xkms/pkg/types"
)

// The software tier's identity through device-attest-01 (#130 item 2): the
// CRYPTO=auto image with no element answering runs the soft root, whose P-256
// DeviceID tfw-boot seeds from the UDS and hands over. That image enrols in
// the same in-process go-xkms server TestHardwareEnrollment uses, over the
// serial link and over Ethernet, and what the wire says about the tier is
// read back where it is carried today.

// autoSim is the CRYPTO=auto simulator (mk/host.mk). The pinned sdk/go has no
// starter for it -- this module builds against a tagged SDK, not this tree --
// so it is started here, from the directory TFWSIM_DIR names as the SDK's
// starters find theirs, and a missing binary fails as theirs does.
const (
	autoSim     = "tfwsim-auto"
	autoSimMake = "make tfwsim-auto"
)

// softUDS is the soft tfw-boot's UDS for these runs: one secret, so the
// factory boot and the enrolment boot derive one DeviceID.
var softUDS = strings.Repeat("c3", 32)

// evidenceAlgOffset and evidenceAlgES256Soft are the ATTEST frame's AlgID byte
// and TFW_ALG_ECDSA_P256_SHA256_SOFT, per include/tfw/protocol.h: ES256 signed
// by the soft root, which the element's 0x01 does not share (#130).
const (
	evidenceAlgOffset    = 73
	evidenceAlgES256Soft = 0x11
)

// simConn is a running simulator: its stdin and stdout, with the read
// deadline tfw.Device applies its timeout through.
type simConn struct {
	cmd *exec.Cmd
	in  io.WriteCloser
	out *os.File
}

func (c *simConn) Read(p []byte) (int, error)        { return c.out.Read(p) }
func (c *simConn) Write(p []byte) (int, error)       { return c.in.Write(p) }
func (c *simConn) SetReadDeadline(t time.Time) error { return c.out.SetReadDeadline(t) }
func (c *simConn) close() {
	_ = c.in.Close()
	_ = c.cmd.Process.Kill()
	_, _ = c.cmd.Process.Wait()
	_ = c.out.Close()
}

func autoSimPath(t *testing.T) string {
	t.Helper()
	dir := os.Getenv(tfwsim.DirEnv)
	if dir == "" {
		_, file, _, _ := runtime.Caller(0)
		// <root>/xkms/e2e/soft_enroll_test.go
		dir = filepath.Join(filepath.Dir(file), "..", "..", "build", "host")
	}
	path := filepath.Join(dir, autoSim)
	if _, err := os.Stat(path); err != nil {
		t.Fatalf("%s not built at %s -- run `%s` first (or set %s to the directory holding it): %v",
			autoSim, path, autoSimMake, tfwsim.DirEnv, err)
	}
	return path
}

// startAutoNoElement boots the CRYPTO=auto image with no element on the bus
// and a soft tfw-boot holding softUDS. eeprom is the platform block, and the
// network block after it for an Ethernet run.
func startAutoNoElement(t *testing.T, eeprom []byte, args ...string) io.ReadWriter {
	t.Helper()
	eep := filepath.Join(t.TempDir(), "eeprom.bin")
	require.NoError(t, os.WriteFile(eep, eeprom, 0o600))
	args = append([]string{
		"--no-element",
		"--uds", softUDS,
		"--eeprom-bin", eep,
		"--image-hash", hex.EncodeToString(tfwsim.TestFWHash[:]),
	}, args...)
	cmd := exec.Command(autoSimPath(t), args...) // #nosec G204 -- the simulator this repo's make target built, with the test's literal flags; no shell
	cmd.Stderr = os.Stderr
	in, err := cmd.StdinPipe()
	require.NoError(t, err)
	out, w, err := os.Pipe()
	require.NoError(t, err)
	cmd.Stdout = w
	err = cmd.Start()
	_ = w.Close()
	if err != nil {
		_ = out.Close()
		t.Fatal(err)
	}
	c := &simConn{cmd: cmd, in: in, out: out}
	t.Cleanup(c.close)
	return c
}

func platformBlock() []byte {
	block := tfw.BuildPlatformBlock(tfwsim.TestSerial, [tfw.PubkeyHashLen]byte{}, [tfw.OperatorPubkeyLen]byte{})
	return block[:]
}

func startSoftSerial(t *testing.T) *tfw.Device {
	t.Helper()
	return tfw.New(startAutoNoElement(t, platformBlock()), tfw.WithTimeout(5*time.Second))
}

// startSoftEthernet is the same image with its network block naming a
// loopback listener: the device dials it at boot and the enrolment runs over
// that link, the serial link left idle.
func startSoftEthernet(t *testing.T) *tfw.Device {
	t.Helper()
	raw, err := net.Listen("tcp", "127.0.0.1:0")
	require.NoError(t, err)
	ln := tfw.NewListener(raw, tfw.WithTimeout(5*time.Second))
	t.Cleanup(func() { _ = ln.Close() })

	cfg := tfw.NetConfig{
		MAC:      net.HardwareAddr{0x02, 0x00, 0x00, 0x00, 0x00, 0x01},
		IP:       netip.MustParseAddr("192.0.2.10"),
		Mask:     netip.MustParseAddr("255.255.255.0"),
		Gateway:  netip.MustParseAddr("192.0.2.1"),
		Endpoint: netip.AddrPortFrom(netip.MustParseAddr("127.0.0.1"), raw.Addr().(*net.TCPAddr).AddrPort().Port()),
	}
	block, err := cfg.MarshalBinary()
	require.NoError(t, err)
	startAutoNoElement(t, append(platformBlock(), block...), "--eth")

	require.NoError(t, raw.(*net.TCPListener).SetDeadline(time.Now().Add(5*time.Second)))
	eth, err := ln.Accept()
	require.NoError(t, err, "the auto image never dialled the listener")
	t.Cleanup(func() { _ = eth.Close() })
	return eth.Device
}

// softFactoryDeviceID is the provisioning step: a separate boot of the same
// part reads the DeviceID public key off the alias certificate the device
// mints, which verifies under that key (src/caps/alias.c). Only the public
// key crosses into the registry.
func softFactoryDeviceID(t *testing.T) *ecdsa.PublicKey {
	t.Helper()
	att, err := newDiceElementOn(t, startSoftSerial(t), softRootPermitted).attest(deviceattest01.DICENonce("factory"))
	require.NoError(t, err)
	pub, ok := att.Certificate.PublicKey.(*ecdsa.PublicKey)
	require.True(t, ok, "the soft root certified a %T", att.Certificate.PublicKey)
	require.NoError(t, att.Certificate.CheckSignature(x509.ECDSAWithSHA256, att.Certificate.RawTBSCertificate, att.Certificate.Signature),
		"the soft root's alias certificate does not verify under its DeviceID")
	return pub
}

// softRootPermitted is the relying party's policy that accepts a software
// root: the device reports notSecure truthfully, and the default refuses it.
var softRootPermitted = dice.OperationalFlags{NotSecure: true}

func TestSoftRootEnrollsThroughDeviceAttest01(t *testing.T) {
	deviceID := softFactoryDeviceID(t)
	require.False(t, deviceID.Equal(tfwsim.TestDeviceIDPublicKey()), "the soft root answered with the element's DeviceID")
	spki, err := x509.MarshalPKIXPublicKey(deviceID)
	require.NoError(t, err)

	for _, link := range []struct {
		name  string
		start func(*testing.T) *tfw.Device
	}{
		{"serial", startSoftSerial},
		{"tcp", startSoftEthernet},
	} {
		t.Run(link.name, func(t *testing.T) {
			device := link.start(t)
			element := newDiceElementOn(t, device, softRootPermitted)
			local := newDiceEnrollment(t, provisioningRecord(t, element.identity, deviceID), softRootPermitted)

			// (a) The enrolment completes and the production verifier accepted it.
			cert, err := deviceAttest(local.client, element)
			require.NoError(t, err)
			require.NoError(t, local.verifier.Refusal())
			select {
			case issued := <-local.server.issued:
				assert.Equal(t, issued.Raw, cert.Raw)
			default:
				t.Fatal("the server issued no certificate")
			}
			assert.Equal(t, element.identity, permanentIdentifier(t, cert),
				"the certificate must be bound to the device's ueid")

			// (b) The attested key is the soft P-256 DeviceID the factory read.
			issuedKey, ok := cert.PublicKey.(*ecdsa.PublicKey)
			require.True(t, ok, "the server issued for a %T", cert.PublicKey)
			assert.True(t, issuedKey.Equal(deviceID), "the certificate is not for the soft DeviceID")
			verified := local.verifier.Verified()
			require.NotNil(t, verified, "the verifier recorded no accepted evidence")
			assert.Equal(t, spki, verified.AttestedSPKI, "go-xkms vouched for a key other than the provisioned DeviceID")

			// (c) What the wire says about the tier. CAPS names the software
			// tier and ES256 alone; the ATTEST frame's AlgID names the soft root; the
			// adapter stamps the software attest level from CAPS.
			caps := element.backend.Caps()
			assert.Equal(t, tfw.CryptoSoftware, caps.CryptoTier)
			assert.Equal(t, tfw.AlgECDSAP256SHA256, caps.Algorithms)
			att, err := element.attest(deviceattest01.DICENonce("tier"))
			require.NoError(t, err)
			assert.Equal(t, tfw.CryptoSoftware, att.CryptoTier)
			assert.Equal(t, types.AttestLevelSoftware, att.AttestLevel)
			ev, err := device.Attest(context.Background(), bytes.Repeat([]byte{0x5a}, tfw.AttestNonceLen))
			require.NoError(t, err)
			require.Len(t, ev, tfw.EvidenceLen)
			assert.EqualValues(t, evidenceAlgES256Soft, ev[evidenceAlgOffset], "the evidence AlgID")

			// The alias certificate's DiceTcbInfo sets notSecure, and the chain
			// enrolled only because this relying party permits it; it declares
			// no root type, so the verdict records none.
			assert.Equal(t, dice.RootTypeUnreported, verified.RootType,
				"the chain now declares a root type: update docs/dice-attestation/engineering.md")
		})
	}
}
