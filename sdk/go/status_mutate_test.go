// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"fmt"
	"testing"

	"github.com/stretchr/testify/assert"
)

// Mutation tests for status.go: each test kills a fault the other tests let
// through. Faults applied (killed/total in the commit): invert the name
// lookup, read an unknown status as a known one, misnumber the unknown
// status, invert or drop AsError's OK check, empty Error, renumber a status,
// swap two names, drop the last name, drop the first error name.

// Every known status reads as its own name, and only as that name. The ids
// are literals: status codes extend, they never renumber.
func TestMutateEveryKnownStatusReadsAsItsOwnName(t *testing.T) {
	want := map[uint8]string{
		0:  "ok",
		1:  "frame CRC mismatch",
		2:  "framing error",
		3:  "message too long",
		4:  "unknown command",
		5:  "wrong argument count",
		6:  "pin out of range",
		7:  "unsupported on this build or pin",
		8:  "peripheral busy",
		9:  "timed out",
		10: "bus NACK",
		11: "value out of range",
		12: "no free slot",
		13: "pin is reserved",
		14: "crypto provider failed",
		15: "device has no provisioned identity",
		16: "target is platform-reserved",
		17: "command requires an authenticated session",
		18: "internal error",
		19: "command not valid in the current OTA state",
		20: "external store did not answer",
	}
	for id, name := range want {
		assert.Equal(t, name, Status(id).String(), "status %d", id)
		assert.Equal(t, name, Status(id).Error(), "status %d as an error", id)
	}
}

// Every status this build does not know reads as unrecognised, with its own
// number, never as another known status.
func TestMutateEveryUnknownStatusReadsAsUnrecognised(t *testing.T) {
	for id := 21; id <= 255; id++ {
		s := Status(id)
		assert.Equal(t, fmt.Sprintf("unrecognised status %d", id), s.String())
		assert.Equal(t, s, s.AsError(), "status %d", id)
	}
}
