# Host SDK — using it

> **Status: NORMATIVE** · Scope: arduino-generic · Last verified: 2026-09-23
> Conflicts with this document are defects. Fix them here first.

Task-oriented. Full flag reference: `cli.md`. Design rationale: `architecture.md`.

## Build the tools

```sh
make sdk-build     # go build ./... in sdk/go: agctl, agprov
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

### Boards that auto-reset on open

Every classic Arduino resets when a serial port raises DTR, which is the
default `stty` behaviour every OS uses when a program opens the port. Opening
`/dev/ttyUSB0` therefore restarts the board and drops it into its bootloader
for roughly a second before the firmware you flashed even starts running.

Nothing to do about this: the SDK's `Device` waits up to ~2 seconds after
connecting for the firmware's own BOOT event before sending the first
command, so `agctl /dev/ttyUSB0 ping hello` succeeds on the very first
invocation rather than only the second. If the adapter you use does *not* reset the
board — DTR wired off, or a board that was already running — no BOOT event
ever comes and the wait simply falls through once the bound elapses; nothing
fails, the exchange just pays that bound once, at connect.

The ~2 second default is sized for the stock Arduino bootloader, which runs
briefly and jumps. **`ag-boot` built with `BOOT_MEASURE=atecc608a` is
different**: it measures the whole application through the element at up to
36 ms per 64-byte block, so anything but a small image can legitimately take
several seconds before the application starts and sends BOOT — longer than the
default bound. Pass a larger bound sized to the image you flashed when talking
to such a build: `agctl -boot-wait 10s ...`, or `ag.WithBootWait` when using
the SDK directly.

**A BOOT event from a PREVIOUS session does not fool this.** Closing a port
does not stop the device transmitting — `agctl reset soft` followed by the
port closing, as one invocation does, leaves that BOOT event sitting wherever
the OS queues unread bytes, and the very next open would otherwise hand it
straight back as if it were fresh. `serialport.Open` purges that backlog
before returning the port (`ResetInputBuffer`, `TCIFLUSH`), and `Device`
independently asks the conn to purge itself too, on top of that — belt and
suspenders, because the second one works for any conn that offers the same
purge method, not only one this package opened. Neither is a wall-clock
guess: a purge discards whatever is ALREADY there at the moment it runs and
nothing else, so it cannot be tuned into discarding a genuine event no
matter how fast a board answers — which several boards this project supports
do, the ATtiny85 (no bootloader at all) among them.

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

For DICE evidence — `ATTEST` and `ATTEST_CERT` — build the element-tier
simulator instead, `make agsim-atecc608a`, and start it with an identity. From a
Go test, `agsim.StartElement(t)` does both; see `api.md` and
`docs/dice-attestation/user.md`. Its evidence is a TEST identity and no
signature in it verifies.

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

## Write your own host program

Import `aggeneric` and `serialport` directly rather than shelling out to
`agctl`; see `api.md` for the connect/call/error-match pattern and
`engineering.md` for every method's invariants. `ag.New` waits for the BOOT
event as described above by default; pass `ag.WithBootWait(0)` if you know
your adapter never resets the board and would rather not pay any bound at
connect at all.

## Flashing a board

Not this SDK's job. `avrdude` is the tool, called from the top-level
Makefile: `docs/development/flashing.md`.
