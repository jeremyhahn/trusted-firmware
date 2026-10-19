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
	"os"
	"path/filepath"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/tfwsim"
)

// Issue #55, end to end: the firmware's own link and allow-list, compiled for
// the host, dial the address tfwctl listens on (tcp://host:port), and tfwctl
// accepts it exactly as it does a W5100 or W5500 on the bench.
//
// What the CLI cannot send is pinned in C instead: a raw AUTH_RESPONSE and
// OTA_CONFIRM over Ethernet by test/unit/test_link_eth.c
// test_ethernet_admits_the_enrolment_attestation_and_staging_commands_and_nothing_else
// and test_an_update_command_past_0x6b_is_answered_unknown_over_ethernet, and an
// END before the declared length by test/unit/test_ota_stage.c
// test_write_past_the_end_is_refused.

// deviceWait bounds how long tfwctl waits for the simulator to dial, as
// --timeout does on the command line.
const deviceWait = 30 * time.Second

// startEthernet runs the element-tier simulator with the test identity and a
// network block whose endpoint is a loopback port tfwctl listens on, and
// returns both of the device's links: serial, which carries everything, and
// the TCP connection tfwctl accepted, which carries only the allow-list.
func startEthernet(t *testing.T) (serial, eth *tfw.Device) {
	t.Helper()
	addr, isTCP, err := tcpAddr("tcp://127.0.0.1:0")
	require.NoError(t, err)
	require.True(t, isTCP)
	a, err := net.ResolveTCPAddr("tcp", addr)
	require.NoError(t, err)
	l, err := net.ListenTCP("tcp", a)
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
	platform := tfw.BuildPlatformBlock(tfwsim.TestSerial, [tfw.PubkeyHashLen]byte{}, [tfw.OperatorPubkeyLen]byte{})
	eep := filepath.Join(t.TempDir(), "eeprom.bin")
	require.NoError(t, os.WriteFile(eep, append(platform[:], block...), 0o600))

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

func TestE2EEthernetDeviceAnswersCapsOverTCP(t *testing.T) {
	_, eth := startEthernet(t)
	c := cliCaps(t, eth)
	assert.Equal(t, tfw.CryptoSecureElement, c.CryptoTier)
	assert.True(t, c.Has(tfw.FeatureAttest) && c.Has(tfw.FeatureAliasCert) && c.Has(tfw.FeatureAttestCSR),
		"features %#x lack the attestation commands", c.Features)
}

func TestE2EEthernetDeviceAttestsOverTCP(t *testing.T) {
	_, eth := startEthernet(t)
	nonce := attestNonce(0x5a)
	ev := cliAttest(t, eth, nonce)
	assert.Equal(t, nonce, ev[offNonce:offNonce+tfw.AttestNonceLen], "frame nonce")
	assert.Equal(t, tfwsim.TestSerial[:], ev[offDevice:offDevice+tfw.SerialLen], "DeviceID")
	assert.True(t, verifyFrame(tfwsim.TestDeviceIDPublicKey(), ev), "the evidence signature does not verify under the DeviceID key")
}

func TestE2EEthernetDeviceIssuesItsAliasCertificateOverTCP(t *testing.T) {
	_, eth := startEthernet(t)
	nonce := aliasNonce(0x40)
	cert := parseCert(t, cliAttestCert(t, eth, nonce))
	require.NoError(t, cert.CheckSignature(cert.SignatureAlgorithm, cert.RawTBSCertificate, cert.Signature))
	got, _, _ := claimsOf(t, uccsOf(t, cert))
	assert.Equal(t, nonce, got, "eat_nonce")
}

func TestE2EEthernetDeviceSignsTheCSRDigestOverTCP(t *testing.T) {
	_, eth := startEthernet(t)
	b := make([]byte, tfw.AliasNonceLen)
	for i := range b {
		b[i] = byte(i)
	}
	cliAttestCert(t, eth, b)
	sig, err := cliCSR(t, eth, b)
	require.NoError(t, err)
	assert.True(t, csrVerifies(t, sig, csrDigestForTestIdentity),
		"the signature does not verify under the DeviceID key over the pinned digest")
}

// Off the allow-list, a command is answered UNKNOWN_CMD over TCP and does
// nothing; the same command over serial, on the same device, still works.
func TestE2EEthernetDeviceRefusesWhatIsNotOnTheAllowList(t *testing.T) {
	serial, eth := startEthernet(t)

	assert.Equal(t, tfw.StatusErrUnknownCmd, cliStatus(t, eth, "ping", "hello"), "ping over TCP")
	assert.Equal(t, tfw.StatusErrUnknownCmd, cliStatus(t, eth, "eeprom-write", "0x100", "01020304"), "eeprom-write over TCP")

	var got struct {
		Hex string `json:"hex"`
	}
	cliJSON(t, serial, &got, "eeprom-read", "0x100", "4")
	assert.NotEqual(t, "01020304", got.Hex, "a write refused over TCP reached the EEPROM")

	var echo struct {
		Echo string `json:"echo"`
	}
	cliJSON(t, serial, &echo, "ping", "hello")
	assert.Equal(t, "hello", echo.Echo, "ping over serial")
}

// The OTA staging ids are on the Ethernet allow-list (src/proto/link_eth.c,
// tfw_link_admits). The link refuses anything else with StatusErrUnknownCmd, so
// each one here is driven to an answer that is not that: tfwsim links the
// stager, and every answer below is the stager's.
func TestE2EEthernetDeviceAdmitsTheOTAIdsOverTCP(t *testing.T) {
	_, eth := startEthernet(t)

	assert.Equal(t, otaStatus{tfw.OTAIdle.String(), 0}, cliOTAStatus(t, eth), "ota-status over TCP")

	assert.Equal(t, tfw.StatusErrRange, cliStatus(t, eth, "ota-stage",
		writeFile(t, "odd.bin", make([]byte, 1000))), "OTA_BEGIN 1000 over TCP")

	var s otaStatus
	cliJSON(t, eth, &s, "ota-stage", writeFile(t, "img.bin", stageImage(512, 200)))
	assert.Equal(t, otaStatus{tfw.OTAComplete.String(), 512}, s, "BEGIN, DATA and END over TCP")
}

// The OTA ADR (docs/ota/adr-ota-image-format-and-trust.md, decision 8, :516-517)
// names AUTH_CHALLENGE, AUTH_RESPONSE and OTA_INFO, OTA_ACTIVATE and
// OTA_CONFIRM for the Ethernet allow-list too; the code admits none of them
// today, and the owner has not ruled which is right. This pins today's code.
func TestE2EEthernetDeviceRefusesAuthAndOTAActivateOverTCP(t *testing.T) {
	_, eth := startEthernet(t)
	_, keyPath := writeOperatorKey(t)

	for _, tc := range []struct {
		verb string
		args []string
	}{
		{"auth", []string{hex.EncodeToString(tfwsim.TestSerial[:]), keyPath}}, // AUTH_CHALLENGE
		{"ota-info", []string{"0"}},
		{"ota-activate", []string{heldSig}},
		{"ota-confirm", []string{heldSig}},
	} {
		t.Run(tc.verb, func(t *testing.T) {
			assert.Equal(t, tfw.StatusErrUnknownCmd, cliStatus(t, eth, tc.verb, tc.args...))
		})
	}
}

// Both links at once: a host on serial is not shut out while the Ethernet
// link is up, and the two answers do not cross.
func TestE2ESerialAndEthernetAreAnsweredSideBySide(t *testing.T) {
	serial, eth := startEthernet(t)
	a := cliAttest(t, serial, attestNonce(0x01))
	b := cliAttest(t, eth, attestNonce(0x02))
	assert.Equal(t, attestNonce(0x01), a[offNonce:offNonce+tfw.AttestNonceLen], "serial answer")
	assert.Equal(t, attestNonce(0x02), b[offNonce:offNonce+tfw.AttestNonceLen], "TCP answer")
}

// The network block the simulator booted with is the one the SDK encodes,
// read back through the device over serial.
func TestE2ETheNetworkBlockReadsBackOverSerial(t *testing.T) {
	serial, eth := startEthernet(t)

	var got []byte
	for off := 0; off < tfw.NetConfigLen; off += tfw.EEPROMChunk {
		var r struct {
			Hex string `json:"hex"`
		}
		cliJSON(t, serial, &r, "eeprom-read", fmt.Sprint(tfw.NetConfigAddr+off), fmt.Sprint(tfw.EEPROMChunk))
		p, err := hex.DecodeString(r.Hex)
		require.NoError(t, err)
		got = append(got, p...)
	}
	require.Len(t, got, tfw.NetConfigLen)
	assert.Equal(t, []byte("TFNC"), got[:4], "block magic")
	assert.NotZero(t, int(got[28])<<8|int(got[29]), "block carries no endpoint port")
	assert.Equal(t, tfw.CRC16(got[:30]), uint16(got[30])|uint16(got[31])<<8, "block CRC")
	cliCaps(t, eth)
}
