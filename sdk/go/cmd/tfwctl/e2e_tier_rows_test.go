// SPDX-License-Identifier: AGPL-3.0-or-later

//go:build tfwsim

package main

import (
	"encoding/binary"
	"testing"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
)

// tierPeer answers the four commands the tier-branched rows send the way a
// firmware of the given crypto tier does: the none tier has no algorithm and
// answers ATTEST UNSUPPORTED (src/crypto/none/provider.c), the software tier
// with no identity answers it NOT_PROVISIONED; both read the reserved
// EEPROM region and apply an I2C clock.
func tierPeer(t *testing.T, tier tfw.CryptoTier) *tfw.Device {
	t.Helper()
	algorithms, attest := uint16(0), tfw.StatusErrUnsupported
	if tier == tfw.CryptoSoftware {
		algorithms, attest = uint16(tfw.AlgECDSAP256SHA256), tfw.StatusErrNotProvisioned
	}
	return startFakeDevice(t, func(f tfw.Frame) (tfw.Status, []byte) {
		p := f.Payload
		switch f.Cmd() {
		case tfw.CmdCaps:
			c := []byte{tfw.ProtoVersionMajor, 0, 0, 1, 0, 0x01, 22, 8, tfw.MaxPayload, byte(tier)}
			c = binary.LittleEndian.AppendUint32(c, 0xFFFFFFFF)
			return tfw.StatusOK, binary.LittleEndian.AppendUint16(c, algorithms)
		case tfw.CmdAttest:
			return attest, nil
		case tfw.CmdEEPROMRead:
			return tfw.StatusOK, make([]byte, p[2])
		case tfw.CmdI2CConfig:
			return tfw.StatusOK, nil
		}
		return tfw.StatusErrUnknownCmd, nil
	})
}

// TestTierRowsHoldOnTheNoneAndSoftTiers runs each row that branches on the
// crypto tier against a none-tier and a soft-tier peer, so a none-tier board
// (the ESP32-P4's HIL-0 image) fails a row only on what it answers (#549).
func TestTierRowsHoldOnTheNoneAndSoftTiers(t *testing.T) {
	for _, tier := range []tfw.CryptoTier{tfw.CryptoNone, tfw.CryptoSoftware} {
		t.Run(tier.String(), func(t *testing.T) {
			t.Run("Caps", func(t *testing.T) {
				capsTierRow(t, cliCaps(t, tierPeer(t, tier)), tfw.AlgECDSAP256SHA256)
			})
			t.Run("AttestRefusedUnprovisioned", func(t *testing.T) {
				attestRefusedUnprovisionedRow(t, tierPeer(t, tier))
			})
			t.Run("EEPROMReservedRead", func(t *testing.T) {
				eepromReservedReadRow(t, tierPeer(t, tier))
			})
			t.Run("I2CConfig", func(t *testing.T) {
				i2cConfigRow(t, tierPeer(t, tier))
			})
		})
	}
}
