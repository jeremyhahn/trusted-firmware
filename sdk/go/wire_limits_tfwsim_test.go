// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build tfwsim

package tfw

import (
	"bytes"
	"context"
	"encoding/binary"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/simproc"
)

// The host SDK and the firmware agree on every limit the row's docs state:
// CAPS's max payload and protocol version are this SDK's MaxPayload and
// ProtoVersion; a PING of exactly MaxPayload bytes, the largest frame either
// side may send (LEN = TFW_MAX_LEN), echoes whole; one byte more is refused by
// the encoder before the wire, and the same frame built by hand is refused by
// the firmware's framer as over-length rather than answered.
func TestSimHostAndFirmwareAgreeOnEveryLimit(t *testing.T) {
	for _, b := range []simproc.Binary{simproc.Software, simproc.Element} {
		t.Run(b.Name, func(t *testing.T) {
			d, p, caps := startRaw(t, b)
			ctx := context.Background()

			assert.Equal(t, uint8(MaxPayload), caps.MaxPayload)
			assert.Equal(t, ProtoVersionMajor, caps.ProtoMajor)
			assert.Equal(t, ProtoVersionMinor, caps.ProtoMinor)

			full := bytes.Repeat([]byte{0x5A}, MaxPayload)
			echo, err := d.Ping(ctx, full)
			require.NoError(t, err)
			assert.Equal(t, full, echo)

			_, err = EncodeRequest(0x41, CmdPing, append(full, 0x5A))
			var pse *PayloadSizeError
			require.ErrorAs(t, err, &pse)

			// [SOF][LEN = MaxPayload+4][SEQ][OP][payload MaxPayload+1][CRC]
			over := []byte{SOF, byte(MaxPayload + 4), 0x42, byte(CmdPing)}
			over = append(over, bytes.Repeat([]byte{0x00}, MaxPayload+1)...)
			over = binary.LittleEndian.AppendUint16(over, CRC16(over[1:]))
			p.send(append(over, rawRequest(t, 0x43, CmdPing, []byte("after"))...))
			f := p.reply()
			require.Equal(t, Seq(0x43), f.Seq, "the over-length frame was answered")
			require.Equal(t, StatusOK, f.Status)
			assert.Equal(t, []byte("after"), f.Payload)

			si, err := d.SysInfo(ctx)
			require.NoError(t, err)
			assert.Equal(t, uint16(1), si.ErrFormat)
			assert.Zero(t, si.ErrCRC)
		})
	}
}
