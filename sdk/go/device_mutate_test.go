// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"bytes"
	"context"
	"errors"
	"fmt"
	"io"
	"net"
	"os"
	"reflect"
	"sort"
	"testing"
	"time"
)

// responseVectors are the pinned response frames, in a fixed order.
func responseVectors() []string {
	var names []string
	for name, s := range frameVectors {
		if s[3]&0x80 != 0 {
			names = append(names, name)
		}
	}
	sort.Strings(names)
	return names
}

// doFrame sends one request on a fresh Device whose next seq is the frame's,
// so the pinned frame is the reply it waits for, and returns what Do answers.
func doFrame(conn io.ReadWriter, valid []byte, opts ...Option) (Frame, error) {
	d := New(conn, append([]Option{WithBootWait(0)}, opts...)...)
	d.seq = Seq(valid[2] - 1)
	return d.Do(context.Background(), Cmd(valid[3]&^0x80), nil)
}

// sameAnswer reports whether two answers from Do are the same frame and the
// same status error.
func sameAnswer(f Frame, err error, wantF Frame, wantErr error) bool {
	return reflect.DeepEqual(f, wantF) && errors.Is(err, wantErr) && errors.Is(wantErr, err)
}

// isLinkEOF reports whether err is the closed link that ends a read with
// nothing left to decode.
func isLinkEOF(err error) bool {
	var e *LinkError
	return errors.As(err, &e) && errors.Is(err, io.EOF)
}

// TestDoReadsMutatedFrames: every mutation of every pinned response, as the
// only bytes on a link that then closes, answers a closed-link error -- never
// a panic, never a wait -- or, for bytes appended past the frame, the
// original's answer. A random corruption that keeps the CRC good is the one
// frame a 16-bit CRC cannot refuse.
func TestDoReadsMutatedFrames(t *testing.T) {
	for i, name := range responseVectors() {
		valid := []byte(frameVectors[name])
		wantF, wantErr := doFrame(&frameLink{reply: valid}, valid)
		if isLinkEOF(wantErr) || wantF.Seq != Seq(valid[2]) {
			t.Fatalf("%s: the valid frame answers %+v, %v", name, wantF, wantErr)
		}
		eachMutation(valid, 910+uint64(i), func(m mutation) {
			var f Frame
			var err error
			noPanic(t, m, func() { f, err = doFrame(&frameLink{reply: m.b}, valid) })
			switch {
			case isLinkEOF(err):
				if m.kind == "append" {
					t.Fatalf("seed %#x %s %v: refused: %v", mutateSeed, name, m, err)
				}
			case m.kind == "append":
				if !sameAnswer(f, err, wantF, wantErr) {
					t.Fatalf("seed %#x %s %v: answered %+v, %v", mutateSeed, name, m, f, err)
				}
			case m.kind == "random" && crcCollides(m.b):
			default:
				t.Fatalf("seed %#x %s %v: a mutated frame answered %+v, %v", mutateSeed, name, m, f, err)
			}
		})
	}
}

// TestDoResyncsPastAMutatedFrame: a mutated response followed by the valid
// one answers the valid one's answer or a closed-link error -- a corrupt
// length can claim the bytes after it -- and never anything else.
func TestDoResyncsPastAMutatedFrame(t *testing.T) {
	resynced := 0
	for i, name := range responseVectors() {
		valid := []byte(frameVectors[name])
		wantF, wantErr := doFrame(&frameLink{reply: valid}, valid)
		eachMutation(valid, 920+uint64(i), func(m mutation) {
			reply := append(bytes.Clone(m.b), valid...)
			var f Frame
			var err error
			noPanic(t, m, func() { f, err = doFrame(&frameLink{reply: reply}, valid) })
			switch {
			case isLinkEOF(err):
			case sameAnswer(f, err, wantF, wantErr):
				resynced++
			case m.kind == "random" && crcCollides(m.b):
			default:
				t.Fatalf("seed %#x %s %v then the valid frame: answered %+v, %v", mutateSeed, name, m, f, err)
			}
		})
	}
	if resynced == 0 {
		t.Fatal("no mutated frame was followed by the valid one's answer")
	}
}

// byteLink delivers its reply one byte per Read.
type byteLink struct{ frameLink }

func (l *byteLink) Read(p []byte) (int, error) {
	if len(p) > 1 {
		p = p[:1]
	}
	return l.rx.Read(p)
}

// TestDoReadsAFrameByteByByte: a pinned response arriving one byte per read
// answers as it does whole, behind any run of noise holding no start byte;
// every truncation of it answers a closed-link error.
func TestDoReadsAFrameByteByByte(t *testing.T) {
	for _, name := range responseVectors() {
		valid := []byte(frameVectors[name])
		wantF, wantErr := doFrame(&frameLink{reply: valid}, valid)
		for _, noise := range [][]byte{nil, {0x00}, {0xff, 0x5a, 0x00}, bytes.Repeat([]byte{0x11}, 300)} {
			reply := append(bytes.Clone(noise), valid...)
			f, err := doFrame(&byteLink{frameLink{reply: reply}}, valid)
			if !sameAnswer(f, err, wantF, wantErr) {
				t.Fatalf("%s behind % x: answered %+v, %v", name, noise, f, err)
			}
		}
		for n := range len(valid) {
			f, err := doFrame(&byteLink{frameLink{reply: valid[:n]}}, valid)
			if !isLinkEOF(err) {
				t.Fatalf("%s cut to %d bytes: answered %+v, %v", name, n, f, err)
			}
		}
	}
}

// stallConn is a net.Conn over memory, as a TCP stream looks to a Device: it
// never closes, and once its bytes are consumed a read answers the deadline
// passed. deadline records the last read deadline set.
type stallConn struct {
	frameLink
	deadline time.Time
}

type stallAddr struct{}

func (stallAddr) Network() string { return "mem" }
func (stallAddr) String() string  { return "mem" }

func (c *stallConn) Read(p []byte) (int, error) {
	if c.rx.Len() == 0 {
		return 0, os.ErrDeadlineExceeded
	}
	return c.rx.Read(p)
}
func (c *stallConn) Close() error                       { return nil }
func (c *stallConn) LocalAddr() net.Addr                { return stallAddr{} }
func (c *stallConn) RemoteAddr() net.Addr               { return stallAddr{} }
func (c *stallConn) SetDeadline(t time.Time) error      { return c.SetReadDeadline(t) }
func (c *stallConn) SetReadDeadline(t time.Time) error  { c.deadline = t; return nil }
func (c *stallConn) SetWriteDeadline(_ time.Time) error { return nil }

// TestTCPDeviceStalledFrame: over a stream that stays open, a pinned
// response answers as it does on a serial link, and every truncation of it,
// a run of noise or a frame claiming more bytes than ever arrive answers a
// *TimeoutError at the Device's timeout, with a read deadline set -- a short
// frame is never a hang.
func TestTCPDeviceStalledFrame(t *testing.T) {
	const timeout = 2 * time.Millisecond
	valid := []byte(frameVectors["OTA_INFO resp"])
	wantF, wantErr := doFrame(&frameLink{reply: valid}, valid)
	tcp := func(reply []byte) (*stallConn, Frame, error) {
		c := &stallConn{frameLink: frameLink{reply: reply}}
		d := &TCPDevice{Device: New(c, WithBootWait(0), WithTimeout(timeout)), conn: c}
		d.seq = Seq(valid[2] - 1)
		f, err := d.Do(context.Background(), Cmd(valid[3]&^0x80), nil)
		return c, f, err
	}
	if _, f, err := tcp(valid); !sameAnswer(f, err, wantF, wantErr) {
		t.Fatalf("the valid frame answers %+v, %v", f, err)
	}
	cases := map[string][]byte{
		"noise":                 {0x00, 0xff, 0x5a},
		"a length past the end": {0xa5, 0x43, valid[2], valid[3], 0x00},
	}
	for n := range len(valid) {
		cases[fmt.Sprintf("cut to %d bytes", n)] = valid[:n]
	}
	for name, reply := range cases {
		c, f, err := tcp(reply)
		var e *TimeoutError
		if !errors.As(err, &e) || e.Elapsed < timeout || c.deadline.IsZero() {
			t.Fatalf("%s (% x): answered %+v, %v, deadline %v", name, reply, f, err, c.deadline)
		}
	}
}
