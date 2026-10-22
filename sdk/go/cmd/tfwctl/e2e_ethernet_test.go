// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build tfwsim

package main

import (
	"bytes"
	"crypto/sha256"
	"encoding/binary"
	"encoding/hex"
	"fmt"
	"net"
	"net/netip"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/peer"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/simproc"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/tfwsim"
)

// Issue #55, end to end: the firmware's own link and allow-list dial the
// address tfwctl listens on, and tfwctl accepts it exactly as it does a W5100
// or W5500 on the bench.
//
// The TestEthernet* rows reach the device through the transport seam,
// internal/peer, as the TestFirmware* rows do: tfwsim -- the firmware compiled
// for the host -- when no port is set, and under `make hw-test HW_ETH=` the board
// on TFW_HW_PORT, whose network block dials TFW_HW_ETH. One row is the
// cross-implementation check in CI and the bench check on a board.
//
// What the CLI cannot send is pinned in C instead: a raw AUTH_RESPONSE and
// OTA_CONFIRM over Ethernet by test/unit/test_link_eth.c
// test_ethernet_admits_the_enrolment_attestation_and_staging_commands_and_nothing_else
// and test_an_update_command_past_0x6b_is_answered_unknown_over_ethernet, and an
// END before the declared length by test/unit/test_ota_stage.c
// test_write_past_the_end_is_refused.

// deviceWait bounds how long tfwctl waits for the device to dial, as
// --timeout does on the command line. A board dials once its bootloader has
// handed over and its link is up.
const deviceWait = 30 * time.Second

// ethPeer is one device's two links: serial, which carries everything, and
// the TCP connection tfwctl accepted, which carries only the allow-list.
type ethPeer struct {
	serialPeer
	eth      *tfw.Device
	endpoint netip.AddrPort // what the device's network block dials
}

// startEthernet listens where the seam says, starts the device through the
// seam, and accepts the device's dial. The simulator is the element tier with
// the test identity and a network block naming the loopback port listened on;
// a board runs what was flashed, with the network block it was provisioned
// with, and ignores the simulator's flags.
func startEthernet(t *testing.T) ethPeer {
	t.Helper()
	target, err := peer.FromEnv()
	require.NoError(t, err)
	addr, err := target.Listen()
	require.NoError(t, err)
	a, err := net.ResolveTCPAddr("tcp", addr)
	require.NoError(t, err)
	l, err := net.ListenTCP("tcp", a)
	require.NoError(t, err)
	t.Cleanup(func() { _ = l.Close() })
	// The endpoint, not the listener: behind a published port the board
	// dials an address this host does not hold.
	endpoint, err := target.Endpoint(l.Addr().(*net.TCPAddr).AddrPort())
	require.NoError(t, err)

	// Listening comes first: a board dials as soon as Start has reset it.
	conn := peer.Start(t, simproc.Element, simEthernetArgs(t, endpoint)...)
	serial := tfw.New(conn, tfw.WithTimeout(10*time.Second))
	td, err := acceptDevice(l, deviceWait, tfw.WithTimeout(10*time.Second))
	require.NoError(t, err)
	t.Cleanup(func() { _ = td.Close() })
	return ethPeer{serialPeer: serialPeer{serial: serial, board: target.Hardware()}, eth: td.Device, endpoint: endpoint}
}

// simEthernetArgs are the simulator's flags: the element tier, provisioned,
// booting from an EEPROM holding the test platform block and a network block
// whose endpoint is the one listened on.
func simEthernetArgs(t *testing.T, endpoint netip.AddrPort) []string {
	t.Helper()
	cfg := tfw.NetConfig{
		MAC:      net.HardwareAddr{0x02, 0x00, 0x00, 0x00, 0x00, 0x01},
		IP:       netip.MustParseAddr("192.0.2.10"),
		Mask:     netip.MustParseAddr("255.255.255.0"),
		Gateway:  netip.MustParseAddr("192.0.2.1"),
		Endpoint: endpoint,
	}
	block, err := cfg.MarshalBinary()
	require.NoError(t, err)
	return append([]string{"--eth"}, elementArgs(t, nil, block)...)
}

// startEthernetWith is the simulator over Ethernet with the platform block
// given, the network block appended after it, and both of its links: serial
// and the TCP connection it dialled. The block's state bytes are the
// simulator's to boot from, so it runs only against tfwsim.
func startEthernetWith(t *testing.T, platform []byte) (serial, eth *tfw.Device) {
	t.Helper()
	l, err := net.ListenTCP("tcp", &net.TCPAddr{IP: net.IPv4(127, 0, 0, 1)})
	require.NoError(t, err)
	t.Cleanup(func() { _ = l.Close() })

	cfg := tfw.NetConfig{
		MAC:      net.HardwareAddr{0x02, 0x00, 0x00, 0x00, 0x00, 0x01},
		IP:       netip.MustParseAddr("192.0.2.10"),
		Mask:     netip.MustParseAddr("255.255.255.0"),
		Gateway:  netip.MustParseAddr("192.0.2.1"),
		Endpoint: l.Addr().(*net.TCPAddr).AddrPort(),
	}
	block, err := cfg.MarshalBinary()
	require.NoError(t, err)
	eep := writeFile(t, "eeprom.bin", append(platform[:len(platform):len(platform)], block...))

	serial = tfwsim.StartElementWith(t, []string{"--eth", "--eeprom-bin", eep,
		"--provision-element", "--image-hash", hex.EncodeToString(tfwsim.TestFWHash[:])})
	td, err := acceptDevice(l, deviceWait, tfw.WithTimeout(10*time.Second))
	require.NoError(t, err)
	t.Cleanup(func() { _ = td.Close() })
	return serial, td.Device
}

// stageImage builds a total-byte image whose last 128 bytes are a trailer
// declaring a body-byte image, version 3, with its SHA-256, per
// docs/ota/adr-ota-image-format-and-trust.md; between them is erased flash.
func stageImage(total, body int) []byte {
	img := bytes.Repeat([]byte{0xFF}, total)
	for i := 0; i < body; i++ {
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

type otaStatus struct {
	State    string `json:"state"`
	Received int    `json:"received"`
}

func cliOTAStatus(t *testing.T, dev *tfw.Device) otaStatus {
	t.Helper()
	var s otaStatus
	cliJSON(t, dev, &s, "ota-status")
	return s
}

func TestEthernetDeviceAnswersCapsOverTCP(t *testing.T) {
	p := startEthernet(t)
	c := cliCaps(t, p.eth)
	assert.Equal(t, p.tier(t), c.CryptoTier)
	assert.True(t, c.Has(tfw.FeatureAttest) && c.Has(tfw.FeatureAliasCert) && c.Has(tfw.FeatureAttestCSR),
		"features %#x lack the attestation commands", c.Features)
}

func TestEthernetDeviceAttestsOverTCP(t *testing.T) {
	p := startEthernet(t)
	nonce := attestNonce(0x5a)
	ev := cliAttest(t, p.eth, nonce)
	assert.Equal(t, nonce, ev[offNonce:offNonce+tfw.AttestNonceLen], "frame nonce")
	assert.Equal(t, p.serialNumber(t), ev[offDevice:offDevice+tfw.SerialLen], "DeviceID")
	assert.True(t, verifyFrame(p.deviceID(t), ev), "the evidence signature does not verify under the DeviceID key")
}

func TestEthernetDeviceIssuesItsAliasCertificateOverTCP(t *testing.T) {
	p := startEthernet(t)
	nonce := aliasNonce(0x40)
	cert := parseCert(t, cliAttestCert(t, p.eth, nonce))
	require.NoError(t, cert.CheckSignature(cert.SignatureAlgorithm, cert.RawTBSCertificate, cert.Signature))
	got, _, _ := claimsOf(t, uccsOf(t, cert))
	assert.Equal(t, nonce, got, "eat_nonce")
}

func TestEthernetDeviceSignsTheCSRDigestOverTCP(t *testing.T) {
	p := startEthernet(t)
	b := make([]byte, tfw.AliasNonceLen)
	for i := range b {
		b[i] = byte(i)
	}
	cliAttestCert(t, p.eth, b)
	sig, err := cliCSR(t, p.eth, b)
	require.NoError(t, err)
	assert.True(t, csrVerifiesUnder(t, p.deviceID(t), sig, p.criDigest(t)),
		"the signature does not verify under the DeviceID key over the pinned digest")
}

// Off the allow-list, a command is answered UNKNOWN_CMD over TCP and does
// nothing; the same command over serial, on the same device, still works.
func TestEthernetDeviceRefusesWhatIsNotOnTheAllowList(t *testing.T) {
	p := startEthernet(t)

	assert.Equal(t, tfw.StatusErrUnknownCmd, cliStatus(t, p.eth, "ping", "hello"), "ping over TCP")
	assert.Equal(t, tfw.StatusErrUnknownCmd, cliStatus(t, p.eth, "eeprom-write", "0x100", "01020304"), "eeprom-write over TCP")

	var got struct {
		Hex string `json:"hex"`
	}
	cliJSON(t, p.serial, &got, "eeprom-read", "0x100", "4")
	assert.NotEqual(t, "01020304", got.Hex, "a write refused over TCP reached the EEPROM")

	var echo struct {
		Echo string `json:"echo"`
	}
	cliJSON(t, p.serial, &echo, "ping", "hello")
	assert.Equal(t, "hello", echo.Echo, "ping over serial")
}

// The OTA staging ids are on the Ethernet allow-list (src/proto/link_eth.c,
// tfw_link_admits). The link refuses anything else with StatusErrUnknownCmd, so
// each one here is driven to an answer that is not that: tfwsim links the
// stager, and every answer below is the stager's.
func TestEthernetDeviceAdmitsTheOTAIdsOverTCP(t *testing.T) {
	p := startEthernet(t)

	assert.Equal(t, otaStatus{tfw.OTAIdle.String(), 0}, cliOTAStatus(t, p.eth), "ota-status over TCP")

	assert.Equal(t, tfw.StatusErrRange, cliStatus(t, p.eth, "ota-stage",
		writeFile(t, "odd.bin", make([]byte, 1000))), "OTA_BEGIN 1000 over TCP")

	var s otaStatus
	cliJSON(t, p.eth, &s, "ota-stage", writeFile(t, "img.bin", stageImage(512, 200)))
	assert.Equal(t, otaStatus{tfw.OTAComplete.String(), 512}, s, "BEGIN, DATA and END over TCP")
}

// The OTA ADR (docs/ota/adr-ota-image-format-and-trust.md, decision 8, :516-517)
// names AUTH_CHALLENGE, AUTH_RESPONSE and OTA_INFO, OTA_ACTIVATE and
// OTA_CONFIRM for the Ethernet allow-list too; the code admits none of them
// today, and the owner has not ruled which is right. This pins today's code.
func TestEthernetDeviceRefusesAuthAndOTAActivateOverTCP(t *testing.T) {
	p := startEthernet(t)
	_, keyPath := writeOperatorKey(t)

	for _, tc := range []struct {
		verb string
		args []string
	}{
		{"auth", []string{hex.EncodeToString(p.serialNumber(t)), keyPath}}, // AUTH_CHALLENGE
		{"ota-info", []string{"0"}},
		{"ota-activate", []string{heldSig}},
		{"ota-confirm", []string{heldSig}},
	} {
		t.Run(tc.verb, func(t *testing.T) {
			assert.Equal(t, tfw.StatusErrUnknownCmd, cliStatus(t, p.eth, tc.verb, tc.args...))
		})
	}
}

// Both links at once: a host on serial is not shut out while the Ethernet
// link is up, and the two answers do not cross.
func TestEthernetAndSerialAreAnsweredSideBySide(t *testing.T) {
	p := startEthernet(t)
	a := cliAttest(t, p.serial, attestNonce(0x01))
	b := cliAttest(t, p.eth, attestNonce(0x02))
	assert.Equal(t, attestNonce(0x01), a[offNonce:offNonce+tfw.AttestNonceLen], "serial answer")
	assert.Equal(t, attestNonce(0x02), b[offNonce:offNonce+tfw.AttestNonceLen], "TCP answer")
}

// The network block the simulator booted with is the one the SDK encodes,
// read back through the device over serial.
func TestEthernetNetworkBlockReadsBackOverSerial(t *testing.T) {
	p := startEthernet(t)

	var got []byte
	for off := 0; off < tfw.NetConfigLen; off += tfw.EEPROMChunk {
		var r struct {
			Hex string `json:"hex"`
		}
		cliJSON(t, p.serial, &r, "eeprom-read", fmt.Sprint(tfw.NetConfigAddr+off), fmt.Sprint(tfw.EEPROMChunk))
		h, err := hex.DecodeString(r.Hex)
		require.NoError(t, err)
		got = append(got, h...)
	}
	require.Len(t, got, tfw.NetConfigLen)
	assert.Equal(t, []byte("TFNC"), got[:4], "block magic")
	assert.NotZero(t, int(got[28])<<8|int(got[29]), "block carries no endpoint port")
	assert.Equal(t, tfw.CRC16(got[:30]), uint16(got[30])|uint16(got[31])<<8, "block CRC")
	var cfg tfw.NetConfig
	require.NoError(t, cfg.UnmarshalBinary(got))
	assert.Equal(t, p.endpoint, cfg.Endpoint, "the block dials somewhere other than the endpoint the row names")
	cliCaps(t, p.eth)
}
