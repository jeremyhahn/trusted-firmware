// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"bytes"
	"context"
	"crypto"
	"crypto/sha256"
	"encoding/binary"
	"errors"
	"io"
	"testing"
)

// authChallengeFrame is the AUTH_CHALLENGE reply a device sends to the first
// request of a session -- seq 1, status OK, nonce 10 11 .. 1f -- its CRC
// worked by hand (CRC-16/CCITT-FALSE over len..body, binascii.crc_hqx with
// 0xffff), not by this package's encoder.
var authChallengeFrame = []byte{
	0xa5, 0x13, 0x01, 0xe1, 0x00,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
	0x23, 0x10,
}

// authResponseFrame is the AUTH_RESPONSE reply granting the session: seq 1,
// status OK, no payload, CRC worked the same way.
var authResponseFrame = []byte{0xa5, 0x03, 0x01, 0xe2, 0x00, 0xfc, 0x5e}

// authSigDER is a P-256 signature as a crypto.Signer hands it back,
// SEQUENCE{r, s}: r = 7f 01 02 .. 1f, s = 3f 21 22 .. 3f, neither needing a
// leading zero.
var authSigDER = func() []byte {
	b := []byte{0x30, 0x44, 0x02, 0x20}
	b = append(b, 0x7f)
	for i := 1; i < 32; i++ {
		b = append(b, byte(i))
	}
	b = append(b, 0x02, 0x20, 0x3f)
	for i := 0x21; i < 0x40; i++ {
		b = append(b, byte(i))
	}
	return b
}()

// frameLink answers every request with the bytes it holds, whatever they
// are, and reads io.EOF once they are consumed: a reply that never completes
// ends in an error, not a wait. sent records the last request written.
type frameLink struct {
	reply []byte
	rx    bytes.Buffer
	sent  []byte
}

func (l *frameLink) Write(p []byte) (int, error) {
	l.sent = bytes.Clone(p)
	l.rx.Write(l.reply)
	return len(p), nil
}

func (l *frameLink) Read(p []byte) (int, error) { return l.rx.Read(p) }

// crcCollides reports whether a mutated frame still carries a good CRC: a
// 16-bit CRC passes one random multi-byte corruption in 65536 by design, so
// such a frame is not a parser fault.
func crcCollides(b []byte) bool {
	if len(b) < 2 || int(b[1])+4 > len(b) {
		return false
	}
	end := int(b[1]) + 2
	return CRC16(b[1:end]) == binary.LittleEndian.Uint16(b[end:])
}

// TestAuthChallengeMutations: every mutation of the nonce payload answers a
// *ShortReplyError for a wrong length, and exactly the bytes the device sent
// otherwise -- every byte of the nonce is read, so no change answers the
// original's nonce.
func TestAuthChallengeMutations(t *testing.T) {
	ctx := context.Background()
	valid := authChallengeFrame[5:21]
	got, err := New(&cannedLink{payload: valid}).AuthChallenge(ctx)
	if err != nil || !bytes.Equal(got, valid) {
		t.Fatalf("the valid nonce answers % x, %v", got, err)
	}
	eachMutation(valid, 800, func(m mutation) {
		var nonce []byte
		var err error
		noPanic(t, m, func() { nonce, err = New(&cannedLink{payload: m.b}).AuthChallenge(ctx) })
		if len(m.b) != AuthNonceLen {
			var e *ShortReplyError
			if !errors.As(err, &e) || e.Cmd != CmdAuthChallenge || e.Got != len(m.b) {
				t.Fatalf("seed %#x %v: answered % x, %v", mutateSeed, m, nonce, err)
			}
			return
		}
		if err != nil || !bytes.Equal(nonce, m.b) || bytes.Equal(nonce, valid) {
			t.Fatalf("seed %#x %v: answered % x, %v", mutateSeed, m, nonce, err)
		}
	})
}

// TestAuthChallengeFrameMutations: every mutation of the whole reply frame
// is an error, never a nonce, unless only bytes past the frame changed (an
// append, read as the start of the next frame) or a random corruption kept
// the CRC good.
func TestAuthChallengeFrameMutations(t *testing.T) {
	ctx := context.Background()
	want := authChallengeFrame[5:21]
	got, err := New(&frameLink{reply: authChallengeFrame}).AuthChallenge(ctx)
	if err != nil || !bytes.Equal(got, want) {
		t.Fatalf("the valid frame answers % x, %v", got, err)
	}
	eachMutation(authChallengeFrame, 801, func(m mutation) {
		var nonce []byte
		var err error
		noPanic(t, m, func() { nonce, err = New(&frameLink{reply: m.b}).AuthChallenge(ctx) })
		if err != nil {
			if m.kind == "append" {
				t.Fatalf("seed %#x %v: refused: %v", mutateSeed, m, err)
			}
			return
		}
		switch {
		case m.kind == "append":
			if !bytes.Equal(nonce, want) {
				t.Fatalf("seed %#x %v: answered % x", mutateSeed, m, nonce)
			}
		case m.kind == "random" && crcCollides(m.b):
		default:
			t.Fatalf("seed %#x %v: a mutated frame answered nonce % x", mutateSeed, m, nonce)
		}
	})
}

// TestAuthResponseFrameMutations: every mutation of the grant is an error --
// a refusal turned into OK, or a reply to another command, is never taken as
// the session granted -- unless only bytes past the frame changed or a
// random corruption kept the CRC good.
func TestAuthResponseFrameMutations(t *testing.T) {
	ctx := context.Background()
	nonce, serial := authChallengeFrame[5:21], make([]byte, SerialLen)
	signer := failingSigner{der: authSigDER}
	if err := New(&frameLink{reply: authResponseFrame}).AuthResponse(ctx, signer, nonce, serial); err != nil {
		t.Fatalf("the valid grant answers %v", err)
	}
	eachMutation(authResponseFrame, 802, func(m mutation) {
		var err error
		noPanic(t, m, func() { err = New(&frameLink{reply: m.b}).AuthResponse(ctx, signer, nonce, serial) })
		if err != nil {
			if m.kind == "append" {
				t.Fatalf("seed %#x %v: refused: %v", mutateSeed, m, err)
			}
			return
		}
		if m.kind != "append" && (m.kind != "random" || !crcCollides(m.b)) {
			t.Fatalf("seed %#x %v: a mutated grant was accepted", mutateSeed, m)
		}
	})
}

// TestAuthResponseSignatureMutations: every mutation of the signer's DER is
// an *AuthSignError, sent nowhere, or a raw R||S that differs from the
// original's -- DER is canonical, so no other encoding of the same signature
// gets through -- and what is sent is exactly that R||S.
func TestAuthResponseSignatureMutations(t *testing.T) {
	ctx := context.Background()
	nonce, serial := authChallengeFrame[5:21], make([]byte, SerialLen)
	raw := append(bytes.Clone(authSigDER[4:36]), authSigDER[38:70]...)
	link := &frameLink{reply: authResponseFrame}
	if err := New(link).AuthResponse(ctx, failingSigner{der: authSigDER}, nonce, serial); err != nil {
		t.Fatalf("the valid signature answers %v", err)
	}
	if f, _, err := Decode(link.sent); err != nil || !bytes.Equal(f.Payload, raw) {
		t.Fatalf("the valid signature sent % x, %v", link.sent, err)
	}
	eachMutation(authSigDER, 803, func(m mutation) {
		link := &frameLink{reply: authResponseFrame}
		var err error
		noPanic(t, m, func() { err = New(link).AuthResponse(ctx, failingSigner{der: m.b}, nonce, serial) })
		if err != nil {
			var e *AuthSignError
			if !errors.As(err, &e) || link.sent != nil {
				t.Fatalf("seed %#x %v: %v, sent % x", mutateSeed, m, err, link.sent)
			}
			return
		}
		f, _, derr := Decode(link.sent)
		if derr != nil || len(f.Payload) != ecdsaRawSigLen || bytes.Equal(f.Payload, raw) {
			t.Fatalf("seed %#x %v: sent % x", mutateSeed, m, link.sent)
		}
	})
}

// digestSigner records the digest it is asked to sign and answers
// authSigDER.
type digestSigner struct{ digest []byte }

func (s *digestSigner) Public() crypto.PublicKey { return nil }

func (s *digestSigner) Sign(_ io.Reader, digest []byte, _ crypto.SignerOpts) ([]byte, error) {
	s.digest = bytes.Clone(digest)
	return authSigDER, nil
}

// TestAuthResponseBindsNonceAndSerial: the digest signed is SHA-256 of
// "tfw-auth-v1" || serial || nonce, and every one-bit flip of the nonce or
// the serial signs a different digest, so a signature for one challenge or
// one device never answers another. Every other length of either is refused
// before anything is signed or sent.
func TestAuthResponseBindsNonceAndSerial(t *testing.T) {
	ctx := context.Background()
	nonce := bytes.Clone(authChallengeFrame[5:21])
	serial := []byte("TFW-SERIAL-0001!")
	s := &digestSigner{}
	if err := New(&frameLink{reply: authResponseFrame}).AuthResponse(ctx, s, nonce, serial); err != nil {
		t.Fatal(err)
	}
	want := sha256.Sum256([]byte("tfw-auth-v1TFW-SERIAL-0001!\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f"))
	if !bytes.Equal(s.digest, want[:]) {
		t.Fatalf("signed % x, want % x", s.digest, want)
	}
	for _, v := range []struct {
		name string
		b    []byte
	}{{"nonce", nonce}, {"serial", serial}} {
		for bit := range len(v.b) * 8 {
			v.b[bit/8] ^= 1 << (bit % 8)
			s := &digestSigner{}
			err := New(&frameLink{reply: authResponseFrame}).AuthResponse(ctx, s, nonce, serial)
			v.b[bit/8] ^= 1 << (bit % 8)
			if err != nil || bytes.Equal(s.digest, want[:]) {
				t.Fatalf("%s bit %d flipped: signed % x, %v", v.name, bit, s.digest, err)
			}
		}
	}
	for n := range AuthNonceLen + 9 {
		for _, c := range []struct {
			arg           string
			nonce, serial []byte
		}{
			{"nonce", make([]byte, n), serial},
			{"serial", nonce, make([]byte, n)},
		} {
			if c.arg == "nonce" && n == AuthNonceLen || c.arg == "serial" && n == SerialLen {
				continue
			}
			link := &frameLink{reply: authResponseFrame}
			s := &digestSigner{}
			err := New(link).AuthResponse(ctx, s, c.nonce, c.serial)
			var e *ArgRangeError
			if !errors.As(err, &e) || e.Arg != c.arg || s.digest != nil || link.sent != nil {
				t.Fatalf("%s of %d bytes: %v, signed %v, sent % x", c.arg, n, err, s.digest != nil, link.sent)
			}
		}
	}
}
