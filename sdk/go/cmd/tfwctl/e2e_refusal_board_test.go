// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build tfwsim && hwtest

package main

import (
	"testing"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
)

// The refusal rows for the peripheral families every simulator has, so only
// `make hw-test` (the hwtest tag) builds them: on a simulator each would fail
// for its peer reporting the family. A board image without the family, as the
// QEMU soft image lacks most of them, answers each command of it
// TFW_ERR_UNKNOWN_CMD (refusesAbsentFamily).

func TestFirmwareRefusesAnalogItsCapsLacks(t *testing.T) {
	refusesAbsentFamily(t, startFirmware(t), tfw.FeatureAnalog, tfw.CmdAnalogRead)
}

func TestFirmwareRefusesPWMItsCapsLacks(t *testing.T) {
	refusesAbsentFamily(t, startFirmware(t), tfw.FeaturePWM, tfw.CmdPWMWrite)
}

func TestFirmwareRefusesPulseItsCapsLacks(t *testing.T) {
	refusesAbsentFamily(t, startFirmware(t), tfw.FeaturePulse, tfw.CmdPulseIn)
}

func TestFirmwareRefusesEEPROMItsCapsLacks(t *testing.T) {
	refusesAbsentFamily(t, startFirmware(t), tfw.FeatureEEPROM, tfw.CmdEEPROMRead)
}

func TestFirmwareRefusesI2CItsCapsLacks(t *testing.T) {
	refusesAbsentFamily(t, startFirmware(t), tfw.FeatureI2C, tfw.CmdI2CXfer)
}

func TestFirmwareRefusesSPIItsCapsLacks(t *testing.T) {
	refusesAbsentFamily(t, startFirmware(t), tfw.FeatureSPI, tfw.CmdSPIXfer)
}

func TestFirmwareRefusesOneWireItsCapsLacks(t *testing.T) {
	refusesAbsentFamily(t, startFirmware(t), tfw.FeatureOneWire, tfw.CmdOneWireReset)
}
