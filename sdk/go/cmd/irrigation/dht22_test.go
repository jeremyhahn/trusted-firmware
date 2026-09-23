// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package main

import (
	"math"
	"testing"
)

// Building a capture the way the device would deliver it: two acknowledgement
// intervals, then forty pulses whose widths encode the bits.
func capture(data [5]byte) []uint16 {
	out := []uint16{80, 80}
	for _, b := range data {
		for i := 7; i >= 0; i-- {
			if b&(1<<uint(i)) != 0 {
				out = append(out, 70)
			} else {
				out = append(out, 26)
			}
		}
	}
	return out
}

func sum(b [4]byte) byte { return b[0] + b[1] + b[2] + b[3] }

func TestDecodeDHT22(t *testing.T) {
	// 65.2% RH, 23.5 C. The checksum is computed rather than written, because a
	// hand-computed one that is wrong makes a correct decoder look broken.
	d := [4]byte{0x02, 0x8C, 0x00, 0xEB}
	full := [5]byte{d[0], d[1], d[2], d[3], sum(d)}

	r, err := DecodeDHT22(capture(full))
	if err != nil {
		t.Fatalf("decode: %v", err)
	}
	if math.Abs(r.RelativeHumidity-65.2) > 0.001 {
		t.Errorf("humidity = %v, want 65.2", r.RelativeHumidity)
	}
	if math.Abs(r.TemperatureC-23.5) > 0.001 {
		t.Errorf("temperature = %v, want 23.5", r.TemperatureC)
	}
}

// THE SIGN BIT. The DHT22's temperature word is sign-and-magnitude, not two's
// complement. Reading it as two's complement turns -1.0 C into +3276.7 C -- a
// value so obviously wrong that it gets mistaken for a sensor fault rather than
// a decoder bug, which is why it survives.
func TestDecodeDHT22HandlesNegativeTemperature(t *testing.T) {
	d := [4]byte{0x02, 0x8C, 0x80, 0x0A} // -1.0 C
	full := [5]byte{d[0], d[1], d[2], d[3], sum(d)}

	r, err := DecodeDHT22(capture(full))
	if err != nil {
		t.Fatalf("decode: %v", err)
	}
	if math.Abs(r.TemperatureC-(-1.0)) > 0.001 {
		t.Errorf("temperature = %v, want -1.0", r.TemperatureC)
	}
}

// A marginal pulse flips exactly one bit and yields a plausible humidity. The
// checksum is the only thing between that and a logged reading nobody
// questions.
func TestDecodeDHT22RejectsABadChecksum(t *testing.T) {
	full := [5]byte{0x02, 0x8C, 0x00, 0xEB, 0x00}
	if _, err := DecodeDHT22(capture(full)); err == nil {
		t.Fatal("a wrong checksum was accepted")
	}
}

func TestDecodeDHT22RejectsAShortCapture(t *testing.T) {
	if _, err := DecodeDHT22([]uint16{80, 80, 26, 70}); err == nil {
		t.Fatal("a truncated capture was accepted")
	}
	if _, err := DecodeDHT22(nil); err == nil {
		t.Fatal("an empty capture was accepted")
	}
}

// The arithmetic that used to run on the MCU, and the reason it no longer does:
// it is float64 here and was float there, which is why the old image carried
// the AVR soft-float library.
func TestVaporPressureDeficit(t *testing.T) {
	// Saturated air has no deficit, whatever the temperature.
	if vpd := VaporPressureDeficit(Reading{RelativeHumidity: 100, TemperatureC: 25}); math.Abs(vpd) > 1e-9 {
		t.Errorf("saturated air has VPD %v, want 0", vpd)
	}
	// Bone-dry air's deficit is the full saturation vapour pressure, which at
	// 25 C is about 3.17 kPa.
	vpd := VaporPressureDeficit(Reading{RelativeHumidity: 0, TemperatureC: 25})
	if vpd < 3.1 || vpd > 3.25 {
		t.Errorf("dry air at 25 C has VPD %v, want about 3.17", vpd)
	}
	// And it must rise with temperature at fixed humidity, which is the whole
	// reason anyone measures it.
	warm := VaporPressureDeficit(Reading{RelativeHumidity: 50, TemperatureC: 30})
	cool := VaporPressureDeficit(Reading{RelativeHumidity: 50, TemperatureC: 20})
	if warm <= cool {
		t.Errorf("VPD did not rise with temperature: %v at 30 C vs %v at 20 C", warm, cool)
	}
}

func TestHeatIndexUsesTheSimpleFormBelowItsDomain(t *testing.T) {
	// Below about 27 C the Rothfusz regression is not meaningful, so the simple
	// average is used. Returning the regression's answer outside its domain
	// would be a number with no basis, which is worse than a coarser one that
	// has one.
	cool := HeatIndex(Reading{RelativeHumidity: 50, TemperatureC: 20})
	if math.Abs(cool-20) > 3 {
		t.Errorf("heat index at 20 C is %v, which is not close to the temperature", cool)
	}
	// And in its domain, humidity must make it worse.
	dry := HeatIndex(Reading{RelativeHumidity: 20, TemperatureC: 32})
	humid := HeatIndex(Reading{RelativeHumidity: 80, TemperatureC: 32})
	if humid <= dry {
		t.Errorf("humidity did not raise the heat index: %v at 80%% vs %v at 20%%", humid, dry)
	}
}
