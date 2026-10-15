// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfwsim_test

import (
	"bytes"
	"context"
	"errors"
	"testing"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/tfwsim"
)

// Issue #55, end to end: the firmware's own link and allow-list, compiled for
// the host, dial a real TCP socket, and this package's Listener accepts it.

func TestEthernetDeviceAnswersCapsOverTCP(t *testing.T) {
	_, eth := tfwsim.StartElementEthernet(t)

	c, err := eth.Caps(context.Background())
	if err != nil {
		t.Fatalf("CAPS over TCP: %v", err)
	}
	if c.CryptoTier != tfw.CryptoSecureElement {
		t.Errorf("tier = %v, want %v", c.CryptoTier, tfw.CryptoSecureElement)
	}
	if !c.Has(tfw.FeatureAttest) || !c.Has(tfw.FeatureAliasCert) || !c.Has(tfw.FeatureAttestCSR) {
		t.Errorf("features %#x lack the attestation commands", c.Features)
	}
}

func TestEthernetDeviceAttestsOverTCP(t *testing.T) {
	_, eth := tfwsim.StartElementEthernet(t)
	nonce := attestNonce(0x5a)

	ev, err := eth.Attest(context.Background(), nonce)
	if err != nil {
		t.Fatalf("ATTEST over TCP: %v", err)
	}
	if !bytes.Equal(ev[offNonce:offNonce+tfw.AttestNonceLen], nonce) {
		t.Errorf("frame nonce = %x, want %x", ev[offNonce:offNonce+tfw.AttestNonceLen], nonce)
	}
	if !bytes.Equal(ev[offDevice:offDevice+tfw.SerialLen], tfwsim.TestSerial[:]) {
		t.Errorf("DeviceID = %x, want %x", ev[offDevice:offDevice+tfw.SerialLen], tfwsim.TestSerial)
	}
	if !verifyFrame(tfwsim.TestDeviceIDPublicKey(), ev) {
		t.Error("the evidence signature does not verify under the DeviceID key")
	}
}

func TestEthernetDeviceIssuesItsAliasCertificateOverTCP(t *testing.T) {
	_, eth := tfwsim.StartElementEthernet(t)
	nonce := aliasNonce(0x40)

	der, err := eth.AttestCert(context.Background(), nonce)
	if err != nil {
		t.Fatalf("ATTEST_CERT over TCP: %v", err)
	}
	cert := parseCert(t, der)
	if err := cert.CheckSignature(cert.SignatureAlgorithm, cert.RawTBSCertificate, cert.Signature); err != nil {
		t.Fatalf("x509 CheckSignature: %v", err)
	}
	if got, _, _ := claimsOf(t, uccsOf(t, cert)); !bytes.Equal(got, nonce) {
		t.Errorf("eat_nonce = %x, want the caller's %x", got, nonce)
	}
}

func TestEthernetDeviceSignsTheCSRDigestOverTCP(t *testing.T) {
	_, eth := tfwsim.StartElementEthernet(t)
	var b [tfw.AliasNonceLen]byte
	for i := range b {
		b[i] = byte(i)
	}

	sig, err := eth.AttestCSR(context.Background(), b)
	if err != nil {
		t.Fatalf("ATTEST_CSR over TCP: %v", err)
	}
	if !verifies(t, sig, csrDigestForTestIdentity) {
		t.Error("the signature does not verify under the DeviceID key over the pinned digest")
	}
}

// Off the allow-list, a command is answered UNKNOWN_CMD over TCP and does
// nothing; the same command over serial, on the same device, still works.
func TestEthernetDeviceRefusesWhatIsNotOnTheAllowList(t *testing.T) {
	ctx := context.Background()
	serial, eth := tfwsim.StartElementEthernet(t)
	const addr = 0x100

	if _, err := eth.Ping(ctx, []byte("hello")); !errors.Is(err, tfw.StatusErrUnknownCmd) {
		t.Errorf("PING over TCP: %v, want %v", err, tfw.StatusErrUnknownCmd)
	}
	err := eth.EEPROMWrite(ctx, addr, []byte{1, 2, 3, 4})
	if !errors.Is(err, tfw.StatusErrUnknownCmd) {
		t.Errorf("EEPROM_WRITE over TCP: %v, want %v", err, tfw.StatusErrUnknownCmd)
	}

	got, err := serial.EEPROMRead(ctx, addr, 4)
	if err != nil {
		t.Fatalf("EEPROM_READ over serial: %v", err)
	}
	if bytes.Equal(got, []byte{1, 2, 3, 4}) {
		t.Error("a write refused over TCP reached the EEPROM")
	}
	if p, err := serial.Ping(ctx, []byte("hello")); err != nil || string(p) != "hello" {
		t.Errorf("PING over serial = %q, %v; want the echo", p, err)
	}
}

// Both links at once: a host on serial is not shut out while the Ethernet
// link is up, and the two answers do not cross.
func TestSerialAndEthernetAreAnsweredSideBySide(t *testing.T) {
	ctx := context.Background()
	serial, eth := tfwsim.StartElementEthernet(t)

	a, err := serial.Attest(ctx, attestNonce(0x01))
	if err != nil {
		t.Fatalf("ATTEST over serial: %v", err)
	}
	b, err := eth.Attest(ctx, attestNonce(0x02))
	if err != nil {
		t.Fatalf("ATTEST over TCP: %v", err)
	}
	if !bytes.Equal(a[offNonce:offNonce+tfw.AttestNonceLen], attestNonce(0x01)) ||
		!bytes.Equal(b[offNonce:offNonce+tfw.AttestNonceLen], attestNonce(0x02)) {
		t.Error("an answer came back on the other link")
	}
}

// The network block the simulator booted with is the one this package
// encodes, read back through the device over serial.
func TestTheNetworkBlockReadsBackOverSerial(t *testing.T) {
	ctx := context.Background()
	serial, eth := tfwsim.StartElementEthernet(t)

	var got []byte
	for off := 0; off < tfw.NetConfigLen; off += tfw.EEPROMChunk {
		p, err := serial.EEPROMRead(ctx, uint16(tfw.NetConfigAddr+off), tfw.EEPROMChunk)
		if err != nil {
			t.Fatalf("EEPROM_READ over serial: %v", err)
		}
		got = append(got, p...)
	}
	if !bytes.Equal(got[:4], []byte("TFNC")) {
		t.Fatalf("block magic = %q, want TFNC", got[:4])
	}
	if port := int(got[28])<<8 | int(got[29]); port == 0 {
		t.Fatal("block carries no endpoint port")
	}
	if crc := tfw.CRC16(got[:30]); uint16(got[30])|uint16(got[31])<<8 != crc {
		t.Fatalf("block CRC does not match its contents")
	}
	if _, err := eth.Caps(ctx); err != nil {
		t.Fatalf("CAPS over TCP after the readback: %v", err)
	}
}
