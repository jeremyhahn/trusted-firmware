package tfw

import (
	"context"
	"testing"
)

// TestCapabilityRequestsMatchTheDocumentedLayout pins the request the SDK
// writes for each device-capabilities command against
// docs/device-capabilities/api.md: the id as a literal from the document and
// the payload byte for byte, little-endian. The firmware side of the same
// table is test/unit/test_caps_wire.c.
func TestCapabilityRequestsMatchTheDocumentedLayout(t *testing.T) {
	ctx := context.Background()
	for _, c := range []struct {
		name  string
		reply []byte
		cmd   Cmd
		want  []byte
		call  func(d *Device) error
	}{
		{"SetPinMode input", nil, 0x10, []byte{7, 0}, func(d *Device) error { return d.SetPinMode(ctx, 7, PinInput) }},
		{"SetPinMode output", nil, 0x10, []byte{7, 1}, func(d *Device) error { return d.SetPinMode(ctx, 7, PinOutput) }},
		{"SetPinMode pullup", nil, 0x10, []byte{7, 2}, func(d *Device) error { return d.SetPinMode(ctx, 7, PinInputPullup) }},
		{"DigitalRead", []byte{0}, 0x11, []byte{7}, func(d *Device) error { _, err := d.DigitalRead(ctx, 7); return err }},
		{"DigitalWrite low", nil, 0x12, []byte{7, 0}, func(d *Device) error { return d.DigitalWrite(ctx, 7, false) }},
		{"DigitalWrite high", nil, 0x12, []byte{7, 1}, func(d *Device) error { return d.DigitalWrite(ctx, 7, true) }},
		{"TimerCancel leave", nil, 0x15, []byte{7, 0}, func(d *Device) error { return d.TimerCancel(ctx, 7, false) }},
		{"TimerCancel apply", nil, 0x15, []byte{7, 1}, func(d *Device) error { return d.TimerCancel(ctx, 7, true) }},
		{"TimerQuery", make([]byte, 6), 0x16, []byte{7}, func(d *Device) error { _, err := d.TimerQuery(ctx, 7); return err }},
		{"AnalogRead", []byte{0, 0}, 0x20, []byte{14}, func(d *Device) error { _, err := d.AnalogRead(ctx, 14); return err }},
		{"SetADCReference vcc", nil, 0x21, []byte{0}, func(d *Device) error { return d.SetADCReference(ctx, ADCRefVCC) }},
		{"SetADCReference external", nil, 0x21, []byte{1}, func(d *Device) error { return d.SetADCReference(ctx, ADCRefExternal) }},
		{"SetADCReference internal", nil, 0x21, []byte{2}, func(d *Device) error { return d.SetADCReference(ctx, ADCRefInternal) }},
		{"PWMWrite", nil, 0x22, []byte{9, 0xc8}, func(d *Device) error { return d.PWMWrite(ctx, 9, 200) }},
		{"PWMWrite zero", nil, 0x22, []byte{9, 0}, func(d *Device) error { return d.PWMWrite(ctx, 9, 0) }},
		{"Tone", nil, 0x23, []byte{11, 0x10, 0x27}, func(d *Device) error { return d.Tone(ctx, 11, 10000) }},
		{"NoTone", nil, 0x24, []byte{11}, func(d *Device) error { return d.NoTone(ctx, 11) }},
		{"I2CTransfer", []byte{0xaa, 0xbb}, 0x50, []byte{0x50, 2, 0x01, 0x02}, func(d *Device) error {
			_, err := d.I2CTransfer(ctx, 0x50, []byte{0x01, 0x02}, 2)
			return err
		}},
		{"I2CTransfer read only", []byte{0xaa}, 0x50, []byte{0x50, 1}, func(d *Device) error {
			_, err := d.I2CTransfer(ctx, 0x50, nil, 1)
			return err
		}},
		{"SPITransfer", []byte{0xaa, 0xbb, 0xcc}, 0x51, []byte{10, 0x9f, 0, 0}, func(d *Device) error {
			_, err := d.SPITransfer(ctx, 10, []byte{0x9f, 0, 0})
			return err
		}},
		{"OneWireReset", []byte{1}, 0x52, []byte{4}, func(d *Device) error { _, err := d.OneWireReset(ctx, 4); return err }},
		{"OneWireWrite", nil, 0x53, []byte{4, 0xcc, 0x44}, func(d *Device) error { return d.OneWireWrite(ctx, 4, []byte{0xcc, 0x44}) }},
		{"OneWireRead", make([]byte, 9), 0x54, []byte{4, 9}, func(d *Device) error { _, err := d.OneWireRead(ctx, 4, 9); return err }},
	} {
		sent, err := sentBy(1, c.reply, c.call)
		requireSent(t, c.name, c.want, sent, err, c.cmd, c.want)
	}
}

// TestCapabilityRepliesDecodeTheDocumentedLayout decodes the replies api.md
// gives a body for and the earlier peripheral tests leave out: a low digital
// level, an idle timer, a present 1-Wire device, and the bus reads, which
// come back as the bytes the device sent.
func TestCapabilityRepliesDecodeTheDocumentedLayout(t *testing.T) {
	ctx := context.Background()

	low, err := New(&cannedLink{payload: []byte{0}}).DigitalRead(ctx, 7)
	if err != nil || low {
		t.Errorf("DigitalRead of level 0 = %v, %v; want false", low, err)
	}

	// active 0, held_level 0, remaining_ms 0.
	ts, err := New(&cannedLink{payload: make([]byte, 6)}).TimerQuery(ctx, 7)
	if err != nil || ts.Active || ts.HeldLevel || ts.Remaining != 0 {
		t.Errorf("TimerQuery of an idle pin = %+v, %v; want inactive", ts, err)
	}

	// remaining_ms is u32: the top byte must reach the duration.
	ts, err = New(&cannedLink{payload: []byte{1, 0, 0x00, 0x00, 0x00, 0x01}}).TimerQuery(ctx, 7)
	if err != nil || !ts.Active || ts.HeldLevel || ts.Remaining.Milliseconds() != 1<<24 {
		t.Errorf("TimerQuery with remaining 0x01000000 ms = %+v, %v", ts, err)
	}

	present, err := New(&cannedLink{payload: []byte{1}}).OneWireReset(ctx, 4)
	if err != nil || !present {
		t.Errorf("OneWireReset with presence 1 = %v, %v; want true", present, err)
	}

	for _, c := range []struct {
		name string
		want []byte
		call func(d *Device) ([]byte, error)
	}{
		{"I2CTransfer", []byte{0xde, 0xad}, func(d *Device) ([]byte, error) { return d.I2CTransfer(ctx, 0x50, []byte{0}, 2) }},
		{"SPITransfer", []byte{0xbe, 0xef}, func(d *Device) ([]byte, error) { return d.SPITransfer(ctx, 10, []byte{1, 2}) }},
		{"OneWireRead", []byte{0x50, 0x05}, func(d *Device) ([]byte, error) { return d.OneWireRead(ctx, 4, 2) }},
		{"EEPROMRead", []byte{0x12, 0x34}, func(d *Device) ([]byte, error) { return d.EEPROMRead(ctx, EEPROMReservedEnd, 2) }},
	} {
		got, err := c.call(New(&cannedLink{payload: c.want}))
		if err != nil || string(got) != string(c.want) {
			t.Errorf("%s = % x, %v; want % x", c.name, got, err, c.want)
		}
	}
}
