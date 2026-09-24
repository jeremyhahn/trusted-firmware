# Host SDK — using it

> **Status: NORMATIVE** · Scope: arduino-generic · Last verified: 2026-09-23
> Conflicts with this document are defects. Fix them here first.

Task-oriented. Full flag reference: `cli.md`. Design rationale: `architecture.md`.

## Build the tools

```sh
make sdk-build     # go build ./... in sdk/go: agctl, agprov, irrigation
```

## Talk to a board over serial

```sh
agctl /dev/ttyUSB0 ping hello
agctl /dev/ttyUSB0 caps        # what the board is and what it can do
agctl /dev/ttyUSB0 pin 13
```

No board handy? List what the host can see:

```sh
agctl ports
```

## Try it with no hardware at all

`agsim` is the real firmware, compiled to run on this machine. Build it once,
then point `agctl` at its pty:

```sh
make agsim
build/host/agsim --pty &   # prints the pty path to stderr, e.g. /dev/pts/4
agctl /dev/pts/4 caps
```

This is also how the SDK's own cross-implementation tests run — see
`engineering.md#test-strategy`.

## Drive an output for a fixed time, safely

```sh
agctl /dev/ttyUSB0 mode 13 out
agctl /dev/ttyUSB0 timer 13 1 5s
```

The device — not this command, not your shell — reverts pin 13 after 5
seconds. You can unplug the host immediately after the command returns and the
pin still reverts on time; that's the property `TimerSet` exists to provide.
Check on it, cancel it early, or apply the final level:

```sh
agctl /dev/ttyUSB0 timer-query 13
agctl /dev/ttyUSB0 timer-cancel 13 apply
```

## Provision a secure element

`agprov` writes the config zone, locks it, generates the DeviceID key and UDS
inside the element, and locks the data zone — irreversibly, once you pass
`-commit -yes-i-mean-it`. Always in this order:

```sh
agprov -dry-run -config templates/atecc608a-dice.conf /dev/ttyUSB0
agprov -commit  -config templates/atecc608a-dice.conf /dev/ttyUSB0 -yes-i-mean-it   # spare part first
agprov -verify  -config templates/atecc608a-dice.conf /dev/ttyUSB0
```

The device must be running a provisioning-jig image; a production build
answers "unknown command" for every provisioning verb by design. Full
procedure, what's reversible and what isn't, and how to read the template:
`docs/device-provisioning/user.md`.

## Run the irrigation example

Read the rig once:

```sh
irrigation /dev/ttyUSB0
```

Open a valve for a fixed duration — again, the device holds the deadline:

```sh
irrigation -water 3 -for 2m /dev/ttyUSB0
```

What moved from the old CropDroid firmware to this host program, and what
stayed on the device and why: `docs/migration/from-cropdroid-irrigation.md`.

## Write your own host program

Import `aggeneric` and `serialport` directly rather than shelling out to
`agctl`; see `api.md` for the connect/call/error-match pattern and
`engineering.md` for every method's invariants.

## Flashing a board

Not this SDK's job. `avrdude` is the tool, called from the top-level
Makefile: `docs/development/flashing.md`.
