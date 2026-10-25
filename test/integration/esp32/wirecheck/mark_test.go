package main

import (
	"testing"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
)

// The mark must land where EEPROM_WRITE accepts it and where nothing else the
// boot reads lives: past the bootloader's reserved end (EEPROMReservedEnd,
// 0x140 since #125) and past the network block that starts there, which the
// Ethernet boot needs intact. 0x100 sat inside the reserved span once the end
// moved, so boot 1's write was refused (#231).
func TestMarkAddrIsWritable(t *testing.T) {
	if markAddr < tfw.EEPROMReservedEnd {
		t.Fatalf("markAddr 0x%03x is below EEPROMReservedEnd 0x%03x: EEPROM_WRITE refuses it",
			markAddr, tfw.EEPROMReservedEnd)
	}
	if markAddr < tfw.NetConfigAddr+tfw.NetConfigLen && markAddr+tfw.EEPROMChunk > tfw.NetConfigAddr {
		t.Fatalf("markAddr 0x%03x overlaps the network block [0x%03x, 0x%03x)",
			markAddr, tfw.NetConfigAddr, tfw.NetConfigAddr+tfw.NetConfigLen)
	}
}
