// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package main

import "errors"

// DHT22 decoding, on the HOST.
//
// This is the migration in one file. The old firmware linked a DHT library and
// returned a temperature; this firmware returns edge intervals and knows
// nothing about humidity. The forty bits below are decoded here, in Go, where
// they can be unit-tested with no hardware and changed without a re-flash.
//
// Why the device cannot do it: a DHT22 distinguishes a bit by pulse width, 26
// microseconds for a zero against 70 for a one. No protocol with a round trip
// per bit can read that -- at 115200 baud a single byte is 87 microseconds, so
// the whole bit is over before a request could be answered. PULSE_CAPTURE
// exists for exactly this shape of sensor, and the same primitive reads
// DHT11/22, HC-SR04, IR and RC PPM.

// The threshold between a zero and a one. Halfway between the datasheet's 26
// and 70 with room on both sides for the sensor's tolerance and for the
// microsecond counter's own granularity.
const dhtBitThresholdUS = 48

var (
	errDHTShort    = errors.New("irrigation: fewer than 40 data bits captured")
	errDHTChecksum = errors.New("irrigation: DHT22 checksum mismatch")
)

// Reading is one sample from a DHT22.
type Reading struct {
	// RelativeHumidity is a percentage; TemperatureC is degrees Celsius.
	// Both are computed here from the sensor's fixed-point wire format, which
	// is why the MCU needs no floating point at all -- and therefore no
	// soft-float library, which is most of a kilobyte.
	RelativeHumidity float64
	TemperatureC     float64
}

// DecodeDHT22 turns captured edge intervals into a reading.
//
// intervals are the widths returned by PULSE_CAPTURE. The sensor's response
// preamble is skipped: the first two intervals are its 80 microsecond low and
// 80 microsecond high acknowledgement, not data.
func DecodeDHT22(intervals []uint16) (*Reading, error) {
	if len(intervals) < 42 {
		return nil, errDHTShort
	}
	bits := intervals[2:42]

	var b [5]byte
	for i, w := range bits {
		if w > dhtBitThresholdUS {
			b[i/8] |= 1 << (7 - uint(i%8))
		}
	}

	// The checksum is the low byte of the sum of the first four, and checking
	// it is not optional: a marginal pulse near the threshold flips exactly one
	// bit, which produces a plausible humidity rather than an obvious error.
	if byte(b[0]+b[1]+b[2]+b[3]) != b[4] {
		return nil, errDHTChecksum
	}

	rh := float64(uint16(b[0])<<8|uint16(b[1])) / 10
	raw := uint16(b[2])<<8 | uint16(b[3])
	// Sign-and-magnitude, not two's complement: the top bit of the temperature
	// word is the sign and the rest is the magnitude. Treating it as two's
	// complement reads -1.0 C as +3276.7 C, which is a value nobody sanity
	// checks because it is so obviously wrong that it looks like a fault.
	t := float64(raw&0x7FFF) / 10
	if raw&0x8000 != 0 {
		t = -t
	}
	return &Reading{RelativeHumidity: rh, TemperatureC: t}, nil
}

// VaporPressureDeficit, in kilopascals.
//
// The arithmetic that used to run on the MCU. It is float64 here and was float
// there, which is the whole reason the old image carried the AVR soft-float
// library -- roughly a kilobyte of flash to compute a number the host was going
// to receive anyway.
func VaporPressureDeficit(r Reading) float64 {
	// Tetens' equation for saturation vapour pressure over water.
	svp := 0.61078 * exp((17.27*r.TemperatureC)/(r.TemperatureC+237.3))
	return svp * (1 - r.RelativeHumidity/100)
}

// HeatIndex in degrees Celsius, by the Rothfusz regression.
//
// Below about 27 C the regression is not meaningful and the National Weather
// Service's simple average is used instead -- returning the regression's answer
// outside its domain would be a number with no basis, which is worse than a
// coarser one that has one.
func HeatIndex(r Reading) float64 {
	t := r.TemperatureC*9/5 + 32
	h := r.RelativeHumidity
	if t < 80 {
		hi := 0.5 * (t + 61.0 + ((t - 68.0) * 1.2) + (h * 0.094))
		return (hi - 32) * 5 / 9
	}
	hi := -42.379 + 2.04901523*t + 10.14333127*h -
		0.22475541*t*h - 0.00683783*t*t - 0.05481717*h*h +
		0.00122874*t*t*h + 0.00085282*t*h*h - 0.00000199*t*t*h*h
	return (hi - 32) * 5 / 9
}
