// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"bytes"
	"context"
	"errors"
	"testing"

	"github.com/stretchr/testify/require"
)

// testPubKey is the key the PROV_WRITE_PUBKEY vectors carry: X = 10 11 .. 2F,
// Y = 80 81 .. 9F.
func testPubKey() []byte {
	k := make([]byte, 64)
	for i := 0; i < 32; i++ {
		k[i] = byte(0x10 + i)
		k[32+i] = byte(0x80 + i)
	}
	return k
}

// slotLink answers PROV_WRITE_PUBKEY as the element's slot would: each
// request writes its half, each reply is the slot's X||Y. A non-nil corrupt
// changes what it answers, as a slot that did not take the write would.
type slotLink struct {
	slot    [64]byte
	reqs    [][]byte
	corrupt func(back []byte)
	rx      bytes.Buffer
}

func (l *slotLink) Write(p []byte) (int, error) {
	f, _, err := Decode(p)
	if err != nil {
		return 0, err
	}
	l.reqs = append(l.reqs, append([]byte(nil), f.Payload...))
	copy(l.slot[int(f.Payload[1])*32:], f.Payload[2:])
	back := append([]byte(nil), l.slot[:]...)
	if l.corrupt != nil {
		l.corrupt(back)
	}
	resp, err := EncodeResponse(f.Seq, f.Cmd(), StatusOK, back)
	if err != nil {
		return 0, err
	}
	l.rx.Write(resp)
	return len(p), nil
}

func (l *slotLink) Read(p []byte) (int, error) { return l.rx.Read(p) }

func TestProvWritePubKeySendsXThenYAndChecksTheReadBack(t *testing.T) {
	key := testPubKey()
	l := &slotLink{}
	require.NoError(t, New(l).ProvWritePubKey(context.Background(), SlotSecureBootKey, key))
	require.Equal(t, [][]byte{
		append([]byte{SlotSecureBootKey, 0}, key[:32]...),
		append([]byte{SlotSecureBootKey, 1}, key[32:]...),
	}, l.reqs)
	require.Equal(t, key, l.slot[:])
}

func TestProvWritePubKeyReportsASlotThatReadsBackOtherwise(t *testing.T) {
	key := testPubKey()
	l := &slotLink{corrupt: func(b []byte) { b[63] ^= 0x01 }}
	err := New(l).ProvWritePubKey(context.Background(), SlotImageSigning, key)
	var rb *ReadBackError
	require.True(t, errors.As(err, &rb), "got %v", err)
	require.Equal(t, SlotImageSigning, rb.Slot)
	require.Equal(t, key, rb.Want)
	require.Equal(t, byte(0x9f^0x01), rb.Got[63])
	require.Contains(t, rb.Error(), "slot 2 reads back")
}

func TestProvWritePubKeyRefusesAKeyNotSixtyFourBytes(t *testing.T) {
	for _, n := range []int{0, 32, 63, 65} {
		l := &slotLink{}
		err := New(l).ProvWritePubKey(context.Background(), SlotSecureBootKey, make([]byte, n))
		var ar *ArgRangeError
		require.True(t, errors.As(err, &ar), "len %d: got %v", n, err)
		require.Equal(t, int64(n), ar.Value)
		require.Empty(t, l.reqs, "len %d: a request was sent", n)
	}
}

func TestProvWritePubKeyReturnsTheFirmwaresRefusal(t *testing.T) {
	for _, st := range []Status{StatusErrRange, StatusErrBadState, StatusErrProtectedRegion} {
		err := New(&cannedLink{status: st}).ProvWritePubKey(context.Background(), SlotSecureBootKey, testPubKey())
		require.Equal(t, st, asStatus(t, err))
	}
}

func TestProvWritePubKeyReportsAShortReadBack(t *testing.T) {
	err := New(&cannedLink{status: StatusOK, payload: make([]byte, 63)}).
		ProvWritePubKey(context.Background(), SlotSecureBootKey, testPubKey())
	var sr *ShortReplyError
	require.True(t, errors.As(err, &sr), "got %v", err)
	require.Equal(t, CmdProvWritePubKey, sr.Cmd)
	require.Equal(t, 63, sr.Got)
}
