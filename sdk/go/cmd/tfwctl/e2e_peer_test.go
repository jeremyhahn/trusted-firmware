// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build tfwsim

package main

import (
	"bytes"
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
// reason.

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
	target, err := peer.FromEnv()
	require.NoError(t, err)
	var conn io.ReadWriter = peer.Start(t, sim, args...)
	if wrap != nil {
		conn = wrap(conn)
	}
	p := serialPeer{serial: tfw.New(conn, tfw.WithTimeout(10*time.Second)), board: target.Hardware()}
	want := tfw.CryptoSoftware
	if sim == simproc.Element {
		want = tfw.CryptoSecureElement
	}
	require.Equal(t, want, cliCaps(t, p.serial).CryptoTier, "this row needs a %s-tier peer", want)
	return p
}

// startProvisioned is the element tier holding the test identity, with no
// operator key; on a board, the element-tier board as tfwprov provisioned it.
func startProvisioned(t *testing.T) serialPeer {
	t.Helper()
	return startPeer(t, simproc.Element, elementArgs(t, nil, nil)...)
}

// startOperator is startProvisioned with an operator key the row can sign
// with, and the PEM file of its private half. The simulator's is a fresh key
// baked into its platform block, as tfwprov bakes one; a board's is the file
// TFW_HW_OPERATOR_KEY names.
func startOperator(t *testing.T) (serialPeer, string) {
	t.Helper()
	target, err := peer.FromEnv()
	require.NoError(t, err)
	if target.Hardware() {
		key := os.Getenv(operatorKeyEnv)
		require.NotEmpty(t, key, "this row needs the operator key the board trusts: set %s to its PEM file", operatorKeyEnv)
		return startPeer(t, simproc.Element), key
	}
	pub, key := writeOperatorKey(t)
	return startPeer(t, simproc.Element, elementArgs(t, pub, nil)...), key
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

// claims are the ueid and FWHash of the board's alias certificate.
func (p serialPeer) claims(t *testing.T) (ueid, fwHash []byte) {
	t.Helper()
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
