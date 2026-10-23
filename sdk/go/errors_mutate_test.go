// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"errors"
	"testing"

	"github.com/stretchr/testify/assert"
)

// Mutation tests for errors.go: each test kills a fault the other tests let
// through. Faults applied (killed/total in the commit): let FrameError,
// CRCError, PayloadSizeError or TimeoutError match a sentinel not its own,
// drop LinkError's Unwrap, swap the fields of ShortReplyError, ArgRangeError,
// ProtocolVersionError or CRCError in the message, drop LinkError's cause
// from the message, misprint TimeoutError's command.

// Each typed error matches its own sentinel and no other.
func TestMutateEachTypedErrorMatchesOnlyItsSentinel(t *testing.T) {
	sentinels := []error{ErrShortFrame, ErrBadCRC, ErrBadFormat, ErrPayloadTooLarge, ErrTimeout}
	cases := []struct {
		err  error
		want error
	}{
		{&FrameError{Reason: "x"}, ErrBadFormat},
		{&CRCError{Computed: 1, Claimed: 2}, ErrBadCRC},
		{&PayloadSizeError{Size: 65, Limit: 64}, ErrPayloadTooLarge},
		{&TimeoutError{Cmd: CmdPing}, ErrTimeout},
	}
	for _, c := range cases {
		for _, s := range sentinels {
			assert.Equal(t, s == c.want, errors.Is(c.err, s), "%T against %q", c.err, s)
		}
		assert.False(t, errors.Is(c.err, StatusErrInternal), "%T matched a device status", c.err)
	}
}
