// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

// Package vectors mirrors testdata/vectors.h.
//
// THESE BYTES ARE THE CONTRACT between the C firmware and this SDK. Both assert
// against them and the two co-merge only when both are green.
//
// They are LITERALS ON PURPOSE. A vector computed from the code it is meant to
// pin moves whenever the code moves, and would agree with a regression as
// readily as with the contract.
//
// A FAILURE HERE IS A WIRE BREAK, NOT A TEST TO UPDATE. Changing any of these
// makes every already-deployed device fail to talk to an updated host.
// Changing the contract means a new protocol major version, not new literals.
//
// `make verify-vectors` asserts this file and testdata/vectors.h carry
// identical values, so the two cannot drift silently.
package vectors

// CRC-16/CCITT-FALSE.
const (
	CRCCheckInput  = "123456789"
	CRCCheckResult = 0x29B1 // the published check value for this polynomial
	CRCEmptyResult = 0xFFFF // folding nothing returns the seed untouched
	CRCZeroResult  = 0xE1F0 // one zero byte still shifts the register
	CRCZeroZero    = 0x1D0F
	CRCFFResult    = 0xFF00
)

// Order sensitivity. These inputs are byte-permutations of each other, so an
// implementation that sums or XORs its bytes returns the same value for both
// and passes every other case. The real-path tests cannot catch that -- both
// ends call the same function and agree -- so the discriminator is published.
const (
	CRCOrderAResult = 0x0E7C // {0x01, 0x02}
	CRCOrderBResult = 0x6B4C // {0x02, 0x01}
)

// SYS_INFO reply payloads, #5. err_crc and err_format are appended after
// reset_cause, never inserted, so SysInfoOldReply is what a host built before
// they existed still receives correctly and SysInfoNewReply is what this host
// receives. Literal, not built from the encoder under test, for the same
// reason the CRC vectors above are.
const (
	SysInfoOldReply = "\x78\x56\x34\x12\x21\x03\x04"
	SysInfoNewReply = "\x78\x56\x34\x12\x21\x03\x04\x05\x00\x07\x00"
)

// SYS_INFO with free_ram widened, #12. The u32 is appended after err_format and
// the u16 stays where it was, saturating at 0xFFFF. SysInfoWideReply is what the
// firmware sends today and begins with SysInfoNewReply byte for byte;
// SysInfoBigReply carries a free_ram of 0x00012345, which the u16 cannot hold.
const (
	SysInfoWideReply = "\x78\x56\x34\x12\x21\x03\x04\x05\x00\x07\x00\x21\x03\x00\x00"
	SysInfoBigReply  = "\x78\x56\x34\x12\xff\xff\x04\x05\x00\x07\x00\x45\x23\x01\x00"
)

// CAPS, #12. The pin page grew from five bytes to six, which shifted the ADC and
// PWM fields, so the protocol major went to 2: a 1.x host must refuse the device
// rather than misread it. CapsProtoReply is the board header's first two bytes,
// major then minor. CapsPin13Reply is the 328P's LED pin: pin 13, flags
// DIGITAL|LED, port 0, bit 5, no ADC channel, no compare unit.
const (
	CapsProtoReply = "\x02\x00"
	CapsPin13Reply = "\x0d\x81\x00\x05\xff\x00"
)

// Wire frames. Each is one full encoded request -- SOF through CRC16 -- for
// I2C_CONFIG or SPI_CONFIG (issue #21) at a fixed SEQ and payload, produced
// once by the C encoder and frozen here. A change to either command's id,
// payload layout or byte order breaks this instead of shipping silently.
const (
	// FrameI2CConfigReq is TFW_CMD_I2C_CONFIG, seq 1, hz=100000 (LE).
	FrameI2CConfigReq = "\xa5\x06\x01\x55\xa0\x86\x01\x00\x93\x73"
	// FrameSPIConfigReq is TFW_CMD_SPI_CONFIG, seq 2, mode=3 clkdiv=2 lsb=1.
	FrameSPIConfigReq = "\xa5\x05\x02\x56\x03\x02\x01\x23\x74"
)
