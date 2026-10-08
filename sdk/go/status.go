// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import "fmt"

// Status mirrors tfw_status_t in include/tfw/status.h.
//
// THESE ARE WIRE VALUES. They extend and never renumber: a host built before a
// status existed reads the new number as unrecognised and falls through to its
// generic failure path, degraded to exactly the behaviour it had before --
// never a misclassification.
type Status uint8

const (
	StatusOK                 Status = 0
	StatusErrCRC             Status = 1
	StatusErrFraming         Status = 2
	StatusErrTooLong         Status = 3
	StatusErrUnknownCmd      Status = 4
	StatusErrBadArgc         Status = 5
	StatusErrBadPin          Status = 6
	StatusErrUnsupported     Status = 7
	StatusErrBusy            Status = 8
	StatusErrTimeout         Status = 9
	StatusErrNACK            Status = 10
	StatusErrRange           Status = 11
	StatusErrNoSlot          Status = 12
	StatusErrReservedPin     Status = 13
	StatusErrCrypto          Status = 14
	StatusErrNotProvisioned  Status = 15
	StatusErrProtectedRegion Status = 16
	StatusErrAuthRequired    Status = 17
	StatusErrInternal        Status = 18
)

var statusNames = map[Status]string{
	StatusOK:                 "ok",
	StatusErrCRC:             "frame CRC mismatch",
	StatusErrFraming:         "framing error",
	StatusErrTooLong:         "message too long",
	StatusErrUnknownCmd:      "unknown command",
	StatusErrBadArgc:         "wrong argument count",
	StatusErrBadPin:          "pin out of range",
	StatusErrUnsupported:     "unsupported on this build or pin",
	StatusErrBusy:            "peripheral busy",
	StatusErrTimeout:         "timed out",
	StatusErrNACK:            "bus NACK",
	StatusErrRange:           "value out of range",
	StatusErrNoSlot:          "no free slot",
	StatusErrReservedPin:     "pin is reserved",
	StatusErrCrypto:          "crypto provider failed",
	StatusErrNotProvisioned:  "device has no provisioned identity",
	StatusErrProtectedRegion: "target is platform-reserved",
	StatusErrAuthRequired:    "command requires an authenticated session",
	StatusErrInternal:        "internal error",
}

func (s Status) String() string {
	if n, ok := statusNames[s]; ok {
		return n
	}
	// Deliberately not an error. A status this build does not know is what
	// forward compatibility looks like from the old side of a version skew.
	return fmt.Sprintf("unrecognised status %d", uint8(s))
}

// Error lets a Status be returned directly as an error. StatusOK returns nil
// from AsError rather than an error that says "ok".
func (s Status) Error() string { return s.String() }

// AsError returns nil for StatusOK and the status itself otherwise.
func (s Status) AsError() error {
	if s == StatusOK {
		return nil
	}
	return s
}
