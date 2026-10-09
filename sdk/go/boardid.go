// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import "fmt"

// BoardID is the one-byte board identifier CAPS reports. It mirrors
// TFW_BOARD_ID in boards/*/board_profile.h, and the numbering is the registry in
// docs/boards/board-id-registry.md: a range per architecture family, ids that
// extend and never renumber.
type BoardID uint8

// The ids allocated so far. A board added to boards/ takes the next free id in
// its family's range and is added here and in the registry in the same change.
const (
	BoardATmega328P  BoardID = 0x01
	BoardATmega2560  BoardID = 0x02
	BoardATmega32U4  BoardID = 0x03
	BoardATtiny85    BoardID = 0x04
	boardAVRFirst    BoardID = 0x01
	boardAVRLast     BoardID = 0x3F
	boardCortexFirst BoardID = 0x40
	boardCortexLast  BoardID = 0x7F
	boardOtherFirst  BoardID = 0x80
	boardOtherLast   BoardID = 0xBF
)

var boardNames = map[BoardID]string{
	BoardATmega328P: "atmega328p",
	BoardATmega2560: "atmega2560",
	BoardATmega32U4: "atmega32u4",
	BoardATtiny85:   "attiny85",
}

// Family names the architecture family whose range the id falls in, or
// "unallocated" for 0x00 and 0xC0-0xFF.
func (b BoardID) Family() string {
	switch {
	case b >= boardAVRFirst && b <= boardAVRLast:
		return "avr"
	case b >= boardCortexFirst && b <= boardCortexLast:
		return "cortex-m"
	case b >= boardOtherFirst && b <= boardOtherLast:
		return "esp32/other"
	}
	return "unallocated"
}

// String is the board's name when this SDK knows it. An id it does not know is
// reported with its family rather than as an error: a newer device on an older
// host is the normal case, and the id is still worth showing.
func (b BoardID) String() string {
	if n, ok := boardNames[b]; ok {
		return n
	}
	return fmt.Sprintf("unknown %s board 0x%02x", b.Family(), uint8(b))
}
