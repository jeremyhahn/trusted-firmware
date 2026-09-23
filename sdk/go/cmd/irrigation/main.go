// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

// Command irrigation reproduces the old CropDroid controller against the new
// firmware. It is the migration, made checkable.
//
// WHAT MOVED AND WHAT DID NOT. Almost everything moved to this host program:
// the DHT22 decode, the moisture-sensor names and calibration, the VPD and
// heat-index arithmetic. The device no longer knows what a moisture sensor is;
// it returns an ADC count for a pin number and edge intervals for a pin, and
// the meaning lives here where it can be changed without a re-flash and tested
// without hardware.
//
// The auto-off timer did NOT move, and that is the one decision in this program
// worth arguing about. A valve told to run for ten minutes must close after ten
// minutes whether or not this process is still running -- so the deadline is
// armed on the DEVICE with TIMER_SET, and a crashed host, a pulled cable or a
// rebooted gateway cannot leave the valve open. Everything that is convenience
// is here; everything that is safety is where it cannot be disconnected from.
package main

import (
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"os"
	"time"

	ag "github.com/jeremyhahn/arduino-generic/sdk/go"
	"github.com/jeremyhahn/arduino-generic/sdk/go/serialport"
)

// The rig, as configuration rather than as firmware. The old controller had
// these as #defines and a re-flash was the way to move a sensor.
type rig struct {
	DHTPin      uint8
	MoisturePin []uint8
	ValvePin    []uint8
}

var defaultRig = rig{
	DHTPin:      9,
	MoisturePin: []uint8{14, 15, 16, 17, 18, 19}, // A0..A5
	ValvePin:    []uint8{2, 3, 4, 5, 6, 7, 8},
}

type sample struct {
	Humidity     float64   `json:"relative_humidity"`
	TemperatureC float64   `json:"temperature_c"`
	VPD          float64   `json:"vpd_kpa"`
	HeatIndexC   float64   `json:"heat_index_c"`
	Moisture     []uint16  `json:"moisture_raw"`
	At           time.Time `json:"at"`
}

func main() {
	var (
		baud    = flag.Int("baud", serialport.DefaultBaud, "serial baud rate")
		water   = flag.Uint("water", 0, "open valve N (1-based) and let the DEVICE close it")
		forDur  = flag.Duration("for", 30*time.Second, "how long the valve stays open")
		asJSON  = flag.Bool("json", false, "emit JSON")
		timeout = flag.Duration("timeout", 10*time.Second, "how long to wait for a reply")
	)
	flag.Usage = func() {
		fmt.Fprint(os.Stderr, `irrigation -- the CropDroid rig, host-side

  irrigation [flags] <port>

With no --water it reads the rig once and prints a sample.

flags
`)
		flag.PrintDefaults()
	}
	flag.Parse()
	if flag.NArg() != 1 {
		flag.Usage()
		os.Exit(2)
	}

	link, err := serialport.Open(flag.Arg(0), *baud)
	if err != nil {
		fail(err)
	}
	defer link.Close()

	dev := ag.New(link, ag.WithTimeout(*timeout))
	ctx := context.Background()

	if *water > 0 {
		if err := openValve(ctx, dev, defaultRig, int(*water), *forDur); err != nil {
			fail(err)
		}
		return
	}

	s, err := read(ctx, dev, defaultRig)
	if err != nil {
		fail(err)
	}
	if *asJSON {
		_ = json.NewEncoder(os.Stdout).Encode(s)
		return
	}
	fmt.Printf("%.1f%% RH  %.1f C  VPD %.2f kPa  heat index %.1f C\n",
		s.Humidity, s.TemperatureC, s.VPD, s.HeatIndexC)
	for i, m := range s.Moisture {
		fmt.Printf("  moisture %d  %d\n", i+1, m)
	}
}

func read(ctx context.Context, dev *ag.Device, r rig) (*sample, error) {
	// 42 intervals: the sensor's two-interval acknowledgement plus forty bits.
	// The device captures them with interrupts and hands back microseconds; it
	// has no idea what they mean.
	iv, err := dev.PulseCapture(ctx, r.DHTPin, true, 42, 50*time.Millisecond)
	if err != nil {
		return nil, err
	}
	reading, err := DecodeDHT22(iv)
	if err != nil {
		return nil, err
	}

	s := &sample{
		Humidity:     reading.RelativeHumidity,
		TemperatureC: reading.TemperatureC,
		VPD:          VaporPressureDeficit(*reading),
		HeatIndexC:   HeatIndex(*reading),
		At:           time.Now(),
	}
	for _, pin := range r.MoisturePin {
		raw, err := dev.AnalogRead(ctx, pin)
		if err != nil {
			return nil, fmt.Errorf("moisture pin %d: %w", pin, err)
		}
		// RAW COUNTS, deliberately. Calibrating an ADC count into a percentage
		// needs the sensor's wet and dry references, which are per-sensor and
		// per-installation; putting a guess here would produce a number that
		// looks calibrated and is not. The old firmware did exactly that.
		s.Moisture = append(s.Moisture, raw)
	}
	return s, nil
}

// openValve arms the deadline ON THE DEVICE and returns.
//
// It does not sleep, and it does not close the valve itself. That is the
// point: after this function returns, this process can be killed, the cable
// pulled, the host rebooted -- and the valve still closes on time, because the
// deadline is held by the millisecond tick on the other end of the wire.
func openValve(ctx context.Context, dev *ag.Device, r rig, n int, d time.Duration) error {
	if n < 1 || n > len(r.ValvePin) {
		return fmt.Errorf("irrigation: valve %d does not exist; the rig has %d", n, len(r.ValvePin))
	}
	pin := r.ValvePin[n-1]
	if err := dev.SetPinMode(ctx, pin, ag.PinOutput); err != nil {
		return err
	}
	slot, err := dev.TimerSet(ctx, pin, true, d)
	if err != nil {
		return err
	}
	fmt.Printf("valve %d (pin %d) open for %s, held by the device in slot %d\n", n, pin, d, slot)
	fmt.Println("you may now unplug this host; the device closes it on time")
	return nil
}

func fail(err error) {
	fmt.Fprintln(os.Stderr, "irrigation:", err)
	os.Exit(1)
}
