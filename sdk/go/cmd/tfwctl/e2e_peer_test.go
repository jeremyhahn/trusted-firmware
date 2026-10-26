// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build tfwsim

package main

import (
	"bytes"
	"context"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/sha256"
	"encoding/hex"
	"io"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/peer"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/simproc"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/tfwsim"
)

// The serial rows' one way to their peer, sdk/go/internal/peer: tfwsim by
// default, the board on TFW_HW_PORT when one is selected. A row names the
// simulator and the flags that give it the state the row needs; a board runs
// what was flashed and provisioned, and ignores them. The tier is checked on
// both, so a board flashed with the other tier's image fails the row up front
// and says which it needs, rather than failing an assertion for the wrong
// reason. A row about one feature instead (startServing) asks a board's CAPS,
// so one image runs every such row and the bits it lacks end theirs with the
// board's refusal checked.

const (
	// deviceIDEnv is the board's DeviceID key, the hex X||Y tfwprov --commit
	// printed: the name and form `make hw-enroll` takes it in.
	deviceIDEnv = "TFW_HW_DEVICEID"
	// criDigestEnv is the hex SHA-256 of the CertificationRequestInfo the board
	// signs for the CSR row's b. That is certificate-profile work, go-xkms's
	// (go-xkms#1441); tfwctl holds none of it.
	criDigestEnv = "TFW_HW_CRI_DIGEST"
	// operatorKeyEnv is the PEM file of the operator key the board was
	// provisioned to trust, the form `tfwctl auth` reads. The simulator's is
	// generated per row and baked into its platform block.
	operatorKeyEnv = "TFW_HW_OPERATOR_KEY"
)

// serialPeer is one device on its serial link, and whether it is a board.
type serialPeer struct {
	serial *tfw.Device
	board  bool
}

// startPeer starts sim with args, or the selected board, through the seam.
func startPeer(t *testing.T, sim simproc.Binary, args ...string) serialPeer {
	t.Helper()
	return startPeerVia(t, nil, sim, args...)
}

// startPeerVia is startPeer with the link passed through wrap, when it is not
// nil, before tfwctl's device sits on it.
func startPeerVia(t *testing.T, wrap func(io.ReadWriter) io.ReadWriter, sim simproc.Binary, args ...string) serialPeer {
	t.Helper()
	p := openPeer(t, wrap, sim, args...)
	requireTier(t, p, sim)
	return p
}

// openPeer is startPeerVia without the tier check.
func openPeer(t *testing.T, wrap func(io.ReadWriter) io.ReadWriter, sim simproc.Binary, args ...string) serialPeer {
	t.Helper()
	target, err := peer.FromEnv()
	require.NoError(t, err)
	var conn io.ReadWriter = peer.Start(t, sim, args...)
	if wrap != nil {
		conn = wrap(conn)
	}
	return serialPeer{serial: tfw.New(conn, tfw.WithTimeout(10*time.Second)), board: target.Hardware()}
}

// requireTier fails the row unless the peer reports sim's tier.
func requireTier(t *testing.T, p serialPeer, sim simproc.Binary) {
	t.Helper()
	want := tfw.CryptoSoftware
	if sim == simproc.Element {
		want = tfw.CryptoSecureElement
	}
	require.Equal(t, want, cliCaps(t, p.serial).CryptoTier, "this row needs a %s-tier peer", want)
}

// startServing starts the peer a row about one CAPS feature needs, and reports
// whether the device serves it. The simulator is sim with args, on sim's tier
// and with the feature, or the row fails. A board runs the one image it was
// given, whatever its tier, so its CAPS decides: with the feature's bit clear
// the device must not know cmd -- sent on the wire with payload, past anything
// tfwctl checks first -- and the row ends there. docs/cortex-m/README.md tables
// the rows each QEMU board's CAPS leaves out.
func startServing(t *testing.T, feature uint32, cmd tfw.Cmd, payload []byte, sim simproc.Binary, args ...string) (serialPeer, bool) {
	t.Helper()
	p := openPeer(t, nil, sim, args...)
	has := cliCaps(t, p.serial).Has(feature)
	if !p.board {
		requireTier(t, p, sim)
		require.True(t, has, "the simulator's CAPS lacks feature %#x", feature)
	}
	if has {
		return p, true
	}
	_, err := p.serial.Do(context.Background(), cmd, payload)
	var st tfw.Status
	require.True(t, errorsAsStatus(err, &st), "command 0x%02x: %v, want a device status", byte(cmd), err)
	assert.Equal(t, tfw.StatusErrUnknownCmd, st,
		"command 0x%02x: CAPS does not report feature %#x, so the board must not know the command", byte(cmd), feature)
	return p, false
}

// certServing, evidenceServing and csrServing are startServing for the
// provisioned element (startProvisioned) and the row's feature: the alias
// certificate (bit 13), the evidence frame (bit 10) and ATTEST_CSR (bit 15).
func certServing(t *testing.T) (serialPeer, bool) {
	t.Helper()
	return startServing(t, tfw.FeatureAliasCert, tfw.CmdAttestCert, make([]byte, tfw.AliasNonceLen),
		simproc.Element, elementArgs(t, nil, nil)...)
}

func evidenceServing(t *testing.T) (serialPeer, bool) {
	t.Helper()
	return startServing(t, tfw.FeatureAttest, tfw.CmdAttest, make([]byte, tfw.AttestNonceLen),
		simproc.Element, elementArgs(t, nil, nil)...)
}

func csrServing(t *testing.T) (serialPeer, bool) {
	t.Helper()
	return startServing(t, tfw.FeatureAttestCSR, tfw.CmdAttestCSR, make([]byte, tfw.AliasNonceLen),
		simproc.Element, elementArgs(t, nil, nil)...)
}

// operatorServing is the element tier holding the test identity with an
// operator key the row can sign with, and the PEM file of its private half.
// The simulator's is a fresh key baked into its platform block, as tfwprov
// bakes one; a board's is the file TFW_HW_OPERATOR_KEY names. As startServing,
// it reports whether the board has the session (bit 14): on a board the key is
// asked for only once CAPS says the session is there, and a board without it
// has been checked to refuse AUTH_CHALLENGE.
func operatorServing(t *testing.T) (serialPeer, string, bool) {
	t.Helper()
	target, err := peer.FromEnv()
	require.NoError(t, err)
	if !target.Hardware() {
		pub, key := writeOperatorKey(t)
		p, ok := startServing(t, tfw.FeatureAuthSession, tfw.CmdAuthChallenge, nil, simproc.Element, elementArgs(t, pub, nil)...)
		return p, key, ok
	}
	p, ok := startServing(t, tfw.FeatureAuthSession, tfw.CmdAuthChallenge, nil, simproc.Element)
	if !ok {
		return p, "", false
	}
	key := os.Getenv(operatorKeyEnv)
	require.NotEmpty(t, key, "this row needs the operator key the board trusts: set %s to its PEM file", operatorKeyEnv)
	return p, key, true
}

// elementArgs are the element simulator's flags for the test identity: a
// platform block with the test serial and operator (none when nil) and then
// extra, the element provisioned, and the test FWHash handed off.
func elementArgs(t *testing.T, operator *ecdsa.PublicKey, netblock []byte) []string {
	t.Helper()
	var point [tfw.OperatorPubkeyLen]byte
	if operator != nil {
		// Bytes is the uncompressed SEC 1 point, 0x04 || X || Y.
		sec1, err := operator.Bytes()
		require.NoError(t, err)
		copy(point[:], sec1[1:])
	}
	platform := tfw.BuildPlatformBlock(tfwsim.TestSerial, [tfw.PubkeyHashLen]byte{}, point)
	eep := filepath.Join(t.TempDir(), "eeprom.bin")
	require.NoError(t, os.WriteFile(eep, eepromImage(platform[:], netblock), 0o600))
	return []string{"--eeprom-bin", eep, "--provision-element", "--image-hash", hex.EncodeToString(tfwsim.TestFWHash[:])}
}

// eepromImage is the simulator's EEPROM file: the platform block at 0x000 and,
// when there is one, the network block at tfw.NetConfigAddr, the bootloader's
// span between them left erased (#125).
func eepromImage(platform, netblock []byte) []byte {
	if netblock == nil {
		return append([]byte(nil), platform...)
	}
	image := bytes.Repeat([]byte{0xFF}, tfw.NetConfigAddr)
	copy(image, platform)
	return append(image, netblock...)
}

// The identity the rows expect the device to answer as. The simulator's is the
// documented test identity it was provisioned with. A board's is its own: the
// tier, serial number and FWHash it reports over its serial link, so its other
// answers are checked against the same device's; the DeviceID key the operator
// recorded at provisioning; and the CertificationRequestInfo digest go-xkms
// reproduces. A board run that names neither fails the one row that needs it.

func (p serialPeer) tier(t *testing.T) tfw.CryptoTier {
	t.Helper()
	if !p.board {
		return tfw.CryptoSecureElement
	}
	return cliCaps(t, p.serial).CryptoTier
}

// claims are the ueid and FWHash the board reports: its alias certificate's,
// or, on a board whose CAPS has no certificate (bit 13 clear: a soft-tier
// image), its evidence frame's. The rows that compare a frame with them also
// verify its signature under the DeviceID key the operator recorded.
func (p serialPeer) claims(t *testing.T) (ueid, fwHash []byte) {
	t.Helper()
	if !cliCaps(t, p.serial).Has(tfw.FeatureAliasCert) {
		ev := cliAttest(t, p.serial, attestNonce(0x01))
		require.Len(t, ev, tfw.EvidenceLen, "the evidence frame the board reports over serial")
		ueid = append([]byte{0x01}, ev[offDevice:offDevice+tfw.SerialLen]...)
		return ueid, ev[offFWHash : offFWHash+sha256.Size]
	}
	_, ueid, fwHash = claimsOf(t, uccsOf(t, parseCert(t, cliAttestCert(t, p.serial, aliasNonce(0x01)))))
	return ueid, fwHash
}

func (p serialPeer) serialNumber(t *testing.T) []byte {
	t.Helper()
	if !p.board {
		return tfwsim.TestSerial[:]
	}
	// RFC 9711 section 4.2.1.1: the ueid is a type byte and the serial number.
	ueid, _ := p.claims(t)
	require.Len(t, ueid, 1+tfw.SerialLen, "the ueid the board reports over serial")
	return ueid[1:]
}

func (p serialPeer) serialHex(t *testing.T) string {
	t.Helper()
	return hex.EncodeToString(p.serialNumber(t))
}

func (p serialPeer) fwHash(t *testing.T) []byte {
	t.Helper()
	if !p.board {
		return tfwsim.TestFWHash[:]
	}
	_, h := p.claims(t)
	require.Len(t, h, sha256.Size, "the FWHash the board reports over serial")
	return h
}

func (p serialPeer) deviceID(t *testing.T) *ecdsa.PublicKey {
	t.Helper()
	if !p.board {
		return tfwsim.TestDeviceIDPublicKey()
	}
	s := os.Getenv(deviceIDEnv)
	require.NotEmpty(t, s, "this row needs the board's DeviceID key: set %s, as hw-enroll takes it", deviceIDEnv)
	point, err := hex.DecodeString(strings.TrimSpace(s))
	require.NoError(t, err, "%s", deviceIDEnv)
	require.Len(t, point, 64, "%s is the 64-byte X||Y point", deviceIDEnv)
	key, err := ecdsa.ParseUncompressedPublicKey(elliptic.P256(), append([]byte{0x04}, point...))
	require.NoError(t, err, "%s is not a P-256 point", deviceIDEnv)
	return key
}

func (p serialPeer) criDigest(t *testing.T) string {
	t.Helper()
	if !p.board {
		return csrDigestForTestIdentity
	}
	s := strings.TrimSpace(os.Getenv(criDigestEnv))
	require.Len(t, s, 2*sha256.Size, "this row needs the board's CertificationRequestInfo digest: set %s to its hex SHA-256", criDigestEnv)
	return s
}
