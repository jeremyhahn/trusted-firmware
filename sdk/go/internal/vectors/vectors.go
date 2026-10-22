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

// SYS_INFO with a reset_cause appended after TFW_RESET_WATCHDOG, #48. Each is
// SysInfoWideReply with only byte 6 changed: TFW_RESET_SOFTWARE (5) and
// TFW_RESET_PANIC (6).
const (
	SysInfoSoftwareReply = "\x78\x56\x34\x12\x21\x03\x05\x05\x00\x07\x00\x21\x03\x00\x00"
	SysInfoPanicReply    = "\x78\x56\x34\x12\x21\x03\x06\x05\x00\x07\x00\x21\x03\x00\x00"
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

// APP_INFO, #51: state, overruns (u16), abi, ota_slots, version major, minor,
// patch (u16), config_len (u16), required_caps (u32), then a 16-byte name and a
// 32-byte profile, NUL-padded. AppInfoHeartbeatReply is test/app/example
// started: running, ABI 1, no OTA, version 1.0.0, needing GPIO (bit 0).
// AppInfoOverrunReply is the unit-test mock after 0x0102 overrunning polls,
// with config_len 16. AppInfoRefusedReply is a descriptor the runner refused,
// reported as written: A/B slots, 2.3.1029, config_len 0x0102, caps bits 31
// and 0, a name filling all but its last byte, and profile "bmc/esp32-p4".
const (
	AppInfoHeartbeatReply = "\x01\x00\x00\x01\x00\x01\x00\x00\x00\x00\x00\x01\x00\x00\x00\x68\x65\x61\x72\x74\x62\x65\x61\x74\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
	AppInfoOverrunReply   = "\x01\x02\x01\x01\x00\x00\x00\x00\x00\x10\x00\x00\x00\x00\x00\x6d\x6f\x63\x6b\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
	AppInfoRefusedReply   = "\x03\x00\x00\x01\x02\x02\x03\x05\x04\x02\x01\x01\x00\x00\x80\x61\x62\x63\x64\x65\x66\x67\x68\x69\x6a\x6b\x6c\x6d\x6e\x6f\x00\x62\x6d\x63\x2f\x65\x73\x70\x33\x32\x2d\x70\x34\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
)

// Application evidence (#51), mirroring TFWV_APPEV_* in testdata/vectors.h.
// AppEvWithUccs is the 151-byte claims set the alias certificate carries when
// a running application supplies AppEvExample, test/app/example's 32 bytes, as
// 273's single hash value; AppEvWithoutUccs is the same set when no
// application supplies any, 273 holding the firmware measurement as before.
const (
	AppEvWithUccs    = "\xa3\x0a\x58\x20\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f\x19\x01\x00\x51\x01\xa1\xb2\xc3\xd4\xe5\xf6\x07\x18\x29\x3a\x4b\x5c\x6d\x7e\x8f\x90\x19\x01\x11\x81\x82\x19\x01\x02\x58\x54\xa5\x00\x50\xbc\xc7\x10\x07\x1a\x66\x53\xc1\x96\x7e\xf0\xd3\xd5\xed\xe4\x21\x01\x63\x74\x66\x77\x02\xa2\x18\x1f\x63\x74\x66\x77\x18\x21\x01\x03\xa1\x11\xa2\x07\x82\x01\x58\x20\xe0\xe1\xe2\xe3\xe4\xe5\xe6\xe7\xe8\xe9\xea\xeb\xec\xed\xee\xef\xf0\xf1\xf2\xf3\xf4\xf5\xf6\xf7\xf8\xf9\xfa\xfb\xfc\xfd\xfe\xff\x18\x18\x63\x61\x70\x70\x0c\x00"
	AppEvWithoutUccs = "\xa3\x0a\x58\x20\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f\x19\x01\x00\x51\x01\xa1\xb2\xc3\xd4\xe5\xf6\x07\x18\x29\x3a\x4b\x5c\x6d\x7e\x8f\x90\x19\x01\x11\x81\x82\x19\x01\x02\x58\x54\xa5\x00\x50\xbc\xc7\x10\x07\x1a\x66\x53\xc1\x96\x7e\xf0\xd3\xd5\xed\xe4\x21\x01\x63\x74\x66\x77\x02\xa2\x18\x1f\x63\x74\x66\x77\x18\x21\x01\x03\xa1\x11\xa2\x07\x82\x01\x58\x20\xba\x78\x16\xbf\x8f\x01\xcf\xea\x41\x41\x40\xde\x5d\xae\x22\x23\xb0\x03\x61\xa3\x96\x17\x7a\x9c\xb4\x10\xff\x61\xf2\x00\x15\xad\x18\x18\x63\x61\x70\x70\x0c\x00"
	AppEvExample     = "\xe0\xe1\xe2\xe3\xe4\xe5\xe6\xe7\xe8\xe9\xea\xeb\xec\xed\xee\xef\xf0\xf1\xf2\xf3\xf4\xf5\xf6\xf7\xf8\xf9\xfa\xfb\xfc\xfd\xfe\xff"
)

// Image trailer v1 signed spans (#76), bytes [0, 48) of the boot image
// trailer: magic "TFIM", trailer_version 1, board_id, image_flags,
// image_version 3, image_len 0x1000 and, as the measurement, SHA-256("abc").
// One well-formed span per board id with a profile, flags 0.
// TrailerMismatchSpan is a 2560 image (0x02) offered to a 328P (0x01);
// TrailerBoardZeroSpan names no board; TrailerReservedFlagSpan sets
// image_flags bit 1; all three are refused. TrailerDevImageSpan sets bit 0,
// a development image, which the binding check permits.
const (
	TrailerSpanLen = 48

	TrailerATmega328PSpan   = "\x54\x46\x49\x4d\x01\x00\x01\x00\x03\x00\x00\x00\x00\x10\x00\x00\xba\x78\x16\xbf\x8f\x01\xcf\xea\x41\x41\x40\xde\x5d\xae\x22\x23\xb0\x03\x61\xa3\x96\x17\x7a\x9c\xb4\x10\xff\x61\xf2\x00\x15\xad"
	TrailerATmega2560Span   = "\x54\x46\x49\x4d\x01\x00\x02\x00\x03\x00\x00\x00\x00\x10\x00\x00\xba\x78\x16\xbf\x8f\x01\xcf\xea\x41\x41\x40\xde\x5d\xae\x22\x23\xb0\x03\x61\xa3\x96\x17\x7a\x9c\xb4\x10\xff\x61\xf2\x00\x15\xad"
	TrailerATmega32U4Span   = "\x54\x46\x49\x4d\x01\x00\x03\x00\x03\x00\x00\x00\x00\x10\x00\x00\xba\x78\x16\xbf\x8f\x01\xcf\xea\x41\x41\x40\xde\x5d\xae\x22\x23\xb0\x03\x61\xa3\x96\x17\x7a\x9c\xb4\x10\xff\x61\xf2\x00\x15\xad"
	TrailerATtiny85Span     = "\x54\x46\x49\x4d\x01\x00\x04\x00\x03\x00\x00\x00\x00\x10\x00\x00\xba\x78\x16\xbf\x8f\x01\xcf\xea\x41\x41\x40\xde\x5d\xae\x22\x23\xb0\x03\x61\xa3\x96\x17\x7a\x9c\xb4\x10\xff\x61\xf2\x00\x15\xad"
	TrailerESP32P4PoESpan   = "\x54\x46\x49\x4d\x01\x00\x80\x00\x03\x00\x00\x00\x00\x10\x00\x00\xba\x78\x16\xbf\x8f\x01\xcf\xea\x41\x41\x40\xde\x5d\xae\x22\x23\xb0\x03\x61\xa3\x96\x17\x7a\x9c\xb4\x10\xff\x61\xf2\x00\x15\xad"
	TrailerESP32QEMUSpan    = "\x54\x46\x49\x4d\x01\x00\x81\x00\x03\x00\x00\x00\x00\x10\x00\x00\xba\x78\x16\xbf\x8f\x01\xcf\xea\x41\x41\x40\xde\x5d\xae\x22\x23\xb0\x03\x61\xa3\x96\x17\x7a\x9c\xb4\x10\xff\x61\xf2\x00\x15\xad"
	TrailerMismatchSpan     = "\x54\x46\x49\x4d\x01\x00\x02\x00\x03\x00\x00\x00\x00\x10\x00\x00\xba\x78\x16\xbf\x8f\x01\xcf\xea\x41\x41\x40\xde\x5d\xae\x22\x23\xb0\x03\x61\xa3\x96\x17\x7a\x9c\xb4\x10\xff\x61\xf2\x00\x15\xad"
	TrailerBoardZeroSpan    = "\x54\x46\x49\x4d\x01\x00\x00\x00\x03\x00\x00\x00\x00\x10\x00\x00\xba\x78\x16\xbf\x8f\x01\xcf\xea\x41\x41\x40\xde\x5d\xae\x22\x23\xb0\x03\x61\xa3\x96\x17\x7a\x9c\xb4\x10\xff\x61\xf2\x00\x15\xad"
	TrailerReservedFlagSpan = "\x54\x46\x49\x4d\x01\x00\x01\x02\x03\x00\x00\x00\x00\x10\x00\x00\xba\x78\x16\xbf\x8f\x01\xcf\xea\x41\x41\x40\xde\x5d\xae\x22\x23\xb0\x03\x61\xa3\x96\x17\x7a\x9c\xb4\x10\xff\x61\xf2\x00\x15\xad"
	TrailerDevImageSpan     = "\x54\x46\x49\x4d\x01\x00\x01\x01\x03\x00\x00\x00\x00\x10\x00\x00\xba\x78\x16\xbf\x8f\x01\xcf\xea\x41\x41\x40\xde\x5d\xae\x22\x23\xb0\x03\x61\xa3\x96\x17\x7a\x9c\xb4\x10\xff\x61\xf2\x00\x15\xad"
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
	// FrameOTABeginBadStateResp is the TFW_CMD_OTA_BEGIN response, seq 3,
	// status TFW_ERR_BAD_STATE (19), no payload.
	FrameOTABeginBadStateResp = "\xa5\x03\x03\xe8\x13\x05\xfd"
	// FrameOTABeginNoStoreResp is the TFW_CMD_OTA_BEGIN response, seq 3,
	// status TFW_ERR_NO_STORE (20), no payload (#167).
	FrameOTABeginNoStoreResp = "\xa5\x03\x03\xe8\x14\xe2\x8d"
	// FrameOTAInfoReq is TFW_CMD_OTA_INFO (0x6C), seq 4, slot 1 (#77).
	FrameOTAInfoReq = "\xa5\x03\x04\x6c\x01\xba\x9d"
	// FrameOTAInfoResp is its response, status OK: slots 2, active 0, flags
	// 0x05, floor 7, state 4 (PENDING), image_version 42, measurement 0xAB x 32.
	FrameOTAInfoResp = "\xa5\x2f\x04\xec\x00\x02\x00\x05\x07\x00\x00\x00\x04\x2a\x00\x00\x00\xab\xab\xab\xab\xab\xab\xab\xab\xab\xab\xab\xab\xab\xab\xab\xab\xab\xab\xab\xab\xab\xab\xab\xab\xab\xab\xab\xab\xab\xab\xab\xab\xd8\x23"
	// FrameOTAActivateReq is TFW_CMD_OTA_ACTIVATE (0x6D), seq 5, mode 1.
	FrameOTAActivateReq = "\xa5\x03\x05\x6d\x01\xbb\x99"
	// FrameOTAActivateResp is its response, status OK, no payload.
	FrameOTAActivateResp = "\xa5\x03\x05\xed\x00\x02\x92"
	// FrameOTAActivateBadStateResp is its fail-closed refusal, status
	// TFW_ERR_BAD_STATE (19), no payload.
	FrameOTAActivateBadStateResp = "\xa5\x03\x05\xed\x13\x50\xb0"
	// FrameOTAConfirmReq is TFW_CMD_OTA_CONFIRM (0x6E), seq 6, no payload.
	FrameOTAConfirmReq = "\xa5\x02\x06\x6e\x32\x85"
	// FrameOTAConfirmResp is its response, status OK, no payload.
	FrameOTAConfirmResp = "\xa5\x03\x06\xee\x00\x01\x9e"

	// The HSM self-defence channel. Each reply that is not a refusal is the
	// engine's state, [flags:1][selected:1][refused:4 LE].

	// FrameHSMHeartbeatReq is TFW_CMD_HSM_HEARTBEAT (0x76), seq 1, no payload.
	FrameHSMHeartbeatReq = "\xa5\x02\x01\x76\x9c\x8f"
	// FrameHSMHeartbeatResp is its response, status OK: flags 0x01 (a set is
	// held), policy 0, 258 envelopes refused (0x00000102, little-endian).
	FrameHSMHeartbeatResp = "\xa5\x09\x01\xf6\x00\x01\x00\x02\x01\x00\x00\xaa\x7d"
	// FrameHSMReportReq is TFW_CMD_HSM_REPORT (0x77), seq 2, signal 4 (S4).
	FrameHSMReportReq = "\xa5\x03\x02\x77\x04\x36\xa0"
	// FrameHSMReportResp is its response, status OK: flags 0x03 (set held,
	// locked), policy 0, none refused.
	FrameHSMReportResp = "\xa5\x09\x02\xf7\x00\x03\x00\x00\x00\x00\x00\x25\x46"
	// FrameHSMReportRangeResp is its refusal of a signal the A55 may not
	// report, status TFW_ERR_RANGE (11), no payload.
	FrameHSMReportRangeResp = "\xa5\x03\x02\xf7\x0b\x41\x4a"
	// FrameHSMVerdictReq is TFW_CMD_HSM_VERDICT (0x78), seq 3, verdict 1.
	FrameHSMVerdictReq = "\xa5\x03\x03\x78\x01\x9d\xd7"
	// FrameHSMSelectReq is TFW_CMD_HSM_SELECT (0x79), seq 4, policy 0.
	FrameHSMSelectReq = "\xa5\x03\x04\x79\x00\x1d\x71"
	// FrameHSMSelectResp is its response, status OK: flags 0x01, policy 0,
	// none refused.
	FrameHSMSelectResp = "\xa5\x09\x04\xf9\x00\x01\x00\x00\x00\x00\x00\xfc\x09"
	// FrameHSMLoadReq is TFW_CMD_HSM_LOAD (0x7A), seq 5: total 270, offset 0,
	// then the envelope's first four bytes, d8 6b a3 02.
	FrameHSMLoadReq = "\xa5\x0a\x05\x7a\x0e\x01\x00\x00\xd8\x6b\xa3\x02\x44\x58"
	// FrameHSMLoadRangeResp is its refusal of an envelope larger than the
	// buffer, status TFW_ERR_RANGE (11), no payload.
	FrameHSMLoadRangeResp = "\xa5\x03\x05\xfa\x0b\x8d\xb9"
)
