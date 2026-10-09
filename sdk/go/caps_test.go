// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"bytes"
	"context"
	"errors"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/vectors"
)

// CAPS is the one reply whose LENGTH is part of the contract rather than an
// implementation detail: fields are appended to it, so this host must read a
// reply from firmware older than itself and a reply from firmware newer than
// itself without failing either. tfwsim proves the SDK agrees with the firmware
// as it is TODAY; these canned replies are how the versions the simulator
// cannot be get tested at all.

// cannedLink answers every request with one prepared response, echoing the
// sequence number so Device accepts it. Enough to exercise a reply layout, and
// deliberately not a device: anything that needed device behaviour belongs in
// the cross-implementation suite against tfwsim.
type cannedLink struct {
	status  Status
	payload []byte
	rx      bytes.Buffer
}

func (l *cannedLink) Write(p []byte) (int, error) {
	f, _, err := Decode(p)
	if err != nil {
		return 0, err
	}
	resp, err := EncodeResponse(f.Seq, f.Cmd(), l.status, l.payload)
	if err != nil {
		return 0, err
	}
	l.rx.Write(resp)
	return len(p), nil
}

func (l *cannedLink) Read(p []byte) (int, error) { return l.rx.Read(p) }

// capsHeader builds the board header a current firmware sends.
func capsHeader(algorithms uint16) []byte {
	return []byte{
		2, 0, // protocol 2.0
		0, 1, 0, // firmware 0.1.0
		0x01,                   // board id
		22,                     // pin count
		8,                      // scheduler slots
		MaxPayload,             // max payload
		byte(CryptoSoftware),   // crypto tier
		0x0F, 0x00, 0x00, 0x00, // features
		byte(algorithms), byte(algorithms >> 8),
	}
}

func TestCapsParsesTheAppendedAlgorithmBitmap(t *testing.T) {
	link := &cannedLink{payload: capsHeader(uint16(AlgECDSAP256SHA256 | AlgHMACSHA256))}
	c, err := New(link).Caps(context.Background())
	require.NoError(t, err)

	assert.Equal(t, AlgECDSAP256SHA256|AlgHMACSHA256, c.Algorithms)
	assert.True(t, c.Algorithms.Can(AlgECDSAP256SHA256))
	assert.True(t, c.Algorithms.Can(AlgECDSAP256SHA256|AlgHMACSHA256), "Can is every bit, not any bit")
	// The defect this guards: TFW_ALG_HMAC_SHA256 is 0x10 and the BIT that says
	// a device can perform it is 0x0004. A parser that mixed the two would
	// read this device as offering nothing it actually offers.
	assert.False(t, c.Algorithms.Can(AlgEd25519))
	assert.Equal(t, AlgorithmSet(0x0005), c.Algorithms)
}

// Firmware built before the bitmap existed sends fourteen bytes and is
// CORRECT: the reply is complete by the contract it was built against. A host
// that rejected it would take a fleet down on an upgrade of the host alone.
func TestCapsAcceptsTheFourteenByteReplyOfOlderFirmware(t *testing.T) {
	link := &cannedLink{payload: capsHeader(0)[:capsHeaderLen]}
	c, err := New(link).Caps(context.Background())
	require.NoError(t, err)

	assert.Equal(t, uint8(22), c.PinCount)
	assert.True(t, c.Has(FeatureGPIO))
	// No claim, rather than a wrong one: the empty set is what this host knew
	// about such a device before the field existed.
	assert.Equal(t, AlgorithmSet(0), c.Algorithms)
	assert.False(t, c.Algorithms.Can(AlgHMACSHA256))
}

func TestCapsRejectsAReplyShorterThanTheBoardHeader(t *testing.T) {
	link := &cannedLink{payload: capsHeader(0)[:9]}
	_, err := New(link).Caps(context.Background())
	require.Error(t, err)

	var se *ShortReplyError
	require.True(t, errors.As(err, &se))
	assert.Equal(t, CmdCaps, se.Cmd)
	assert.Equal(t, 9, se.Got)
	assert.Equal(t, capsHeaderLen, se.Need)
}

func TestCapsRejectsAProtocolMajorItCannotParse(t *testing.T) {
	for _, tc := range []struct {
		name         string
		major, minor uint8
	}{
		// #12: firmware from before the six-byte pin page reports 1.x. This
		// host would read its five-byte page with the bit, ADC and PWM fields
		// shifted, so it must refuse the device rather than misread it.
		{name: "an older major, 1 against 2", major: 1, minor: 0},
		{name: "a newer major", major: 3, minor: 7},
	} {
		t.Run(tc.name, func(t *testing.T) {
			p := capsHeader(0)
			p[0], p[1] = tc.major, tc.minor
			link := &cannedLink{payload: p}

			c, err := New(link).Caps(context.Background())
			require.Error(t, err)
			// Returned alongside the error on purpose: what the device said about
			// itself is the only useful thing to log when a host refuses to talk to it.
			require.NotNil(t, c)
			assert.Equal(t, tc.major, c.ProtoMajor)

			var pe *ProtocolVersionError
			require.True(t, errors.As(err, &pe))
			assert.Equal(t, tc.major, pe.DeviceMajor)
			assert.Equal(t, tc.minor, pe.DeviceMinor)
			assert.Equal(t, ProtoVersionMajor, pe.HostMajor)
		})
	}
}

// The version this host speaks is the literal pair the firmware's vector pins,
// so a bump on one side without the other fails here and in the C suite.
func TestTheHostSpeaksTheProtocolVersionTheVectorsPin(t *testing.T) {
	require.Len(t, vectors.CapsProtoReply, 2)
	assert.Equal(t, vectors.CapsProtoReply[0], ProtoVersionMajor)
	assert.Equal(t, vectors.CapsProtoReply[1], ProtoVersionMinor)
	assert.Equal(t, []byte(vectors.CapsProtoReply), capsHeader(0)[:2])
}

// A minor ahead of this host's is not an error: tolerating it in both
// directions is what lets a fleet run mixed firmware without a flag day.
func TestCapsAcceptsANewerProtocolMinor(t *testing.T) {
	p := capsHeader(uint16(AlgHMACSHA256))
	p[1] = 9
	link := &cannedLink{payload: p}

	c, err := New(link).Caps(context.Background())
	require.NoError(t, err)
	assert.Equal(t, uint8(9), c.ProtoMinor)
}

func TestPinInfoParsesADescriptor(t *testing.T) {
	for _, tc := range []struct {
		name string
		wire []byte
		want PinInfo
	}{
		{
			name: "a digital pin whose compare unit is usable",
			wire: []byte{9, byte(PinDigital | PinPWM), 0x00, 0x01, 0xFF, 1},
			want: PinInfo{Pin: 9, Flags: PinDigital | PinPWM, HasPort: true, Port: 0, Bit: 1, PWM: PWMAvailable},
		},
		{
			// The state a boolean would lose. The pin HAS a compare unit; this
			// build spent it on the millisecond tick, so the fix is a different
			// firmware rather than a different pin.
			name: "a pin whose compare unit drives the tick",
			wire: []byte{5, byte(PinDigital | PinPWM), 0x02, 0x05, 0xFF, 2},
			want: PinInfo{Pin: 5, Flags: PinDigital | PinPWM, HasPort: true, Port: 2, Bit: 5, PWM: PWMSystemTick},
		},
		{
			// Port 0 bit 5 on the Uno's LED: the wire vector the firmware's
			// own suite asserts it sends.
			name: "the onboard led",
			wire: []byte(vectors.CapsPin13Reply),
			want: PinInfo{Pin: 13, Flags: PinDigital | PinLED, HasPort: true, Port: 0, Bit: 5, PWM: PWMNone},
		},
		{
			// #12: "no digital port" is 0xFF in the port byte and the bit byte.
			// Every ADC-only pin on a 328P arrives exactly this way.
			name: "an adc-only pin with no digital port at all",
			wire: []byte{20, byte(PinADC), 0xFF, 0xFF, 6, 0},
			want: PinInfo{Pin: 20, Flags: PinADC, HasADC: true, ADCChannel: 6, PWM: PWMNone},
		},
		{
			name: "a pin this build has claimed for itself",
			wire: []byte{1, byte(PinDigital | PinInUse), 0x02, 0x01, 0xFF, 0},
			want: PinInfo{Pin: 1, Flags: PinDigital | PinInUse, HasPort: true, Port: 2, Bit: 1, PWM: PWMNone},
		},
		{
			// What the nibble packing could not say: a Cortex-M port 3 with a
			// bit past 15, and a port index past 15.
			name: "a wide port and bit",
			wire: []byte{40, byte(PinDigital), 0x03, 0x1F, 0xFF, 0},
			want: PinInfo{Pin: 40, Flags: PinDigital, HasPort: true, Port: 3, Bit: 31, PWM: PWMNone},
		},
		{
			name: "a port index above 15",
			wire: []byte{41, byte(PinDigital), 0x12, 0x00, 0xFF, 0},
			want: PinInfo{Pin: 41, Flags: PinDigital, HasPort: true, Port: 18, Bit: 0, PWM: PWMNone},
		},
	} {
		t.Run(tc.name, func(t *testing.T) {
			link := &cannedLink{payload: tc.wire}
			got, err := New(link).PinInfo(context.Background(), tc.wire[0])
			require.NoError(t, err)
			assert.Equal(t, tc.want, *got)
		})
	}
}

func TestPinInfoRejectsATruncatedDescriptor(t *testing.T) {
	link := &cannedLink{payload: []byte{9, byte(PinDigital), 0x01, 0x02, 0xFF}}
	_, err := New(link).PinInfo(context.Background(), 9)
	require.Error(t, err)

	var se *ShortReplyError
	require.True(t, errors.As(err, &se))
	assert.Equal(t, capsPinPageLen, se.Need)
	assert.Equal(t, 5, se.Got)
}

// A pin the board does not have is refused by the DEVICE, and that refusal
// must reach the caller as the status the device sent rather than as a parse
// failure of the empty payload behind it.
func TestPinInfoSurfacesTheDeviceRefusal(t *testing.T) {
	link := &cannedLink{status: StatusErrBadPin}
	_, err := New(link).PinInfo(context.Background(), 200)
	require.Error(t, err)
	assert.Equal(t, StatusErrBadPin, err)
}

func TestAlgorithmSetStringShowsBitsItCannotName(t *testing.T) {
	assert.Equal(t, "(none)", AlgorithmSet(0).String())
	assert.Equal(t, "ecdsa-p256-sha256 hmac-sha256", (AlgECDSAP256SHA256 | AlgHMACSHA256).String())
	// A device offering something newer than this host is the ordinary state
	// of a fleet mid-upgrade; showing fewer algorithms than were reported
	// would hide it.
	assert.Equal(t, "ed25519 0x0100", (AlgEd25519 | AlgorithmSet(0x0100)).String())
}

func TestPinFlagsAndPWMSupportDescribeThemselves(t *testing.T) {
	assert.True(t, (PinDigital | PinLED).Has(PinLED))
	assert.False(t, (PinDigital | PinLED).Has(PinDigital|PinPWM), "Has is every flag, not any flag")
	assert.Equal(t, "(none)", PinFlags(0).String())
	assert.Equal(t, "digital led 0x08", (PinDigital | PinLED | PinFlags(0x08)).String())

	assert.Equal(t, "none", PWMNone.String())
	assert.Equal(t, "available", PWMAvailable.String())
	assert.Equal(t, "spent on the millisecond tick", PWMSystemTick.String())
	// A state this build has no name for is reported as unrecognised rather
	// than as one of the three it knows -- the same rule the status codes
	// follow, and for the same reason.
	assert.Equal(t, "unrecognised pwm state 7", PWMSupport(7).String())
}
