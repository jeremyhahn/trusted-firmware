# Wire protocol — user guide

> **Status: NORMATIVE** · Scope: arduino-generic · Last verified: 2026-09-23
> Conflicts with this document are defects. Fix them here first.

Task-oriented usage of the wire protocol from a host. For the reference
client's flags, see `cli.md`; for the full command inventory, `api.md`.

## Connect and discover

Every session starts the same way: open the port, issue `CAPS` once, and
cache the answer. Everything else — which pins do PWM, whether attestation
is available, how large a payload the device accepts — follows from it.

```go
conn, _ := serialport.Open("/dev/ttyUSB0", serialport.DefaultBaud)
dev := aggeneric.New(conn)
caps, err := dev.Caps(context.Background())
```

A `*ProtocolVersionError` here means the device's protocol major differs
from this host's — the frames themselves may not parse the same way, and
nothing beyond `CAPS` should be trusted. `caps` is still returned alongside
the error so the mismatch can be logged.

Equivalently from the shell: `agctl /dev/ttyUSB0 caps`.

## Prove the path works

`PING` echoes a payload through framing, dispatch and back, touching no
hardware — the first thing to run against an unfamiliar device or a new
transport.

```sh
agctl /dev/ttyUSB0 ping hello
```

## Read a pin's descriptor before using it

`CAPS` with a one-byte pin index returns that pin's flags, port/bit, ADC
channel and PWM state, so a host discovers a board instead of hardcoding
one. Check `PWM` before assuming a pin can carry a duty cycle: a pin whose
compare unit was spent on the millisecond tick reports only the digital flag
and a distinct `PWMSystemTick` value — not the same as "no compare unit at
all," and not something a different pin can fix.

```sh
agctl /dev/ttyUSB0 pin 13
```

## Drive a pin and let the device revert it

`TIMER_SET` drives a pin to a level and has the **device** revert it after a
duration — the deadline is owned by the device so a host that crashes with a
load energised does not leave it energised.

```sh
agctl /dev/ttyUSB0 timer 13 1 5s     # drive pin 13 high for 5s, then release
agctl /dev/ttyUSB0 timer-query 13    # active? held level? time remaining?
agctl /dev/ttyUSB0 timer-cancel 13   # stop it early
```

## Handle events

A device may send an unsolicited event — `EVENT_BOOT` means it restarted
underneath the host and every cached assumption (including `Caps`) needs
re-fetching; `EVENT_DEADMAN` means the link went quiet and scheduled pins
were driven to safety; `EVENT_COUNTER_WRAP` means a pulse counter wrapped.
Events accumulate on the `Device` and are read with `Events()`:

```go
for _, ev := range dev.Events() {
    switch ev.Payload[0] {
    case aggeneric.EventBoot:
        caps, _ = dev.Caps(ctx) // re-establish everything
    }
}
```

Events never arrive mid-reply: the device only emits one when no response is
in flight, so a host never has to reassemble a reply with an event spliced
into it.

## Recognise what a status means before acting on it

Every response carries a `Status`. A caller checking a specific failure uses
`errors.Is` against the returned error (which is the `Status` itself, or a
wrapping error type — see `../host-sdk/architecture.md`); a caller logging an
unrecognised value gets a readable "unrecognised status N" rather than a
crash, which is what lets an old host talk to a device with a newer status
vocabulary. See `api.md` for the full status table.

## Provisioning is a separate, guarded tool

`agprov`, not `agctl`, and only against a device built with
`AG_FEATURE_PROVISION` — a fielded device has no provisioning handler at
all and answers `AG_ERR_UNKNOWN_CMD`. See `../device-provisioning/user.md`.
