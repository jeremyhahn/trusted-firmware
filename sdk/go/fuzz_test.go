// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package aggeneric

import (
	"context"
	"testing"
)

// Fuzzing the host side, which is the side that parses bytes it did not write.
//
// WHY THIS EXISTS, CONCRETELY. Device.ProvInfo panicked on every legal reply --
// it bounds-checked at 36 bytes and then indexed 90 and 91 -- and shipped in a
// tagged release because provision.go had no test. A fuzz target over the reply
// parsers is precisely the thing that finds that class: an index computed from
// one constant while the bound was computed from another.
//
// The device is not the only thing that can put bytes on this link. Anything
// that can reach the serial port can, and the host must not panic on any of
// them. "It crashed" is a denial of service in a process that may be a
// gateway attesting a fleet.
//
// These run as ordinary tests over their seed corpus under `go test`, so they
// cost nothing in CI and still catch a regression against the seeds. A real
// campaign is `go test -fuzz=FuzzDecode -fuzztime=5m`.

// FuzzDecode drives the framer with arbitrary bytes.
//
// The C framer has an equivalent -- test_random_bytes_never_overrun_or_wedge_the_framer
// -- but that one uses a fixed seed and a fixed iteration count, so it explores
// the same path every run. Coverage-guided fuzzing explores the paths the seed
// never reaches.
func FuzzDecode(f *testing.F) {
	// Seeds: a well-formed frame, a truncated one, one claiming a length it
	// does not have, and the start byte alone.
	good, err := EncodeRequest(1, CmdPing, []byte("hi"))
	if err != nil {
		f.Fatal(err)
	}
	f.Add(good)
	f.Add(good[:len(good)-1])
	f.Add([]byte{0xA5, 0xFF, 0x01, 0x01})
	f.Add([]byte{0xA5})
	f.Add([]byte{})

	f.Fuzz(func(t *testing.T, data []byte) {
		frame, n, err := Decode(data)
		if err != nil {
			return
		}
		// A successful decode must consume bytes that exist, and must not
		// report a payload longer than what it was given. Either would mean a
		// length field was trusted over the buffer -- the FieldTooLongError
		// hazard this protocol's length bounds exist to exclude.
		if n < 0 || n > len(data) {
			t.Fatalf("decode consumed %d of %d bytes", n, len(data))
		}
		if len(frame.Payload) > len(data) {
			t.Fatalf("payload is %d bytes from a %d-byte input", len(frame.Payload), len(data))
		}
	})
}

func fuzzDevice(payload []byte, status Status) *Device {
	return New(&cannedLink{status: status, payload: payload})
}

// FuzzReplyParsers is the target that would have caught the ProvInfo panic.
//
// Every one of these takes a reply it did not construct and indexes into it. A
// panic in any of them is a host a malicious or malfunctioning device can
// crash, so the assertion is simply that none of them panics -- an error is a
// correct outcome and the expected one for almost every input.
func FuzzReplyParsers(f *testing.F) {
	f.Add([]byte{})
	f.Add(make([]byte, 14)) // a legacy CAPS board header
	f.Add(make([]byte, 16)) // a current one
	f.Add(make([]byte, 36)) // the PROV_INFO reply that used to be read past
	f.Add(make([]byte, 38)) // the one that is correct now
	f.Add(make([]byte, 5))  // a CAPS pin page
	f.Add(make([]byte, 64)) // a full payload

	f.Fuzz(func(t *testing.T, payload []byte) {
		if len(payload) > int(MaxPayload) {
			// Longer than the protocol can carry; the framer refuses it before
			// a parser sees it, so fuzzing past this tests nothing real.
			return
		}
		ctx := context.Background()

		// Each on its own device, because these parsers are reached one reply
		// at a time and sharing a link would let one call's leftovers feed the
		// next -- which would be testing the harness.
		_, _ = fuzzDevice(payload, StatusOK).Caps(ctx)
		_, _ = fuzzDevice(payload, StatusOK).PinInfo(ctx, 13)
		_, _ = fuzzDevice(payload, StatusOK).SysInfo(ctx)
		_, _ = fuzzDevice(payload, StatusOK).ProvInfo(ctx)
		_, _ = fuzzDevice(payload, StatusOK).ProvPubKey(ctx, SlotDeviceID)
		_, _ = fuzzDevice(payload, StatusOK).AttestCert(ctx, make([]byte, AliasNonceLen))
		_, _ = fuzzDevice(payload, StatusOK).Attest(ctx, make([]byte, AttestNonceLen))
	})
}
