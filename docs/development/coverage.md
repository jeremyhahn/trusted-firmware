# Coverage

> **Status: NORMATIVE** · Scope: `make coverage`, `mk/host.mk`
> · Last verified: 2026-09-22

```sh
make coverage          # runs the host suite instrumented, enforces the floor
```

Current: **96.3% of lines, 95.7% of functions** over `src/proto`, `src/core`,
`src/caps` and `boards/`. The floor is 90% and it fails the build.

## What is measured, and what is deliberately not

Measured: `src/proto`, `src/core`, `src/caps`, `boards/*/board.c`. These are the
layers that run on x86 against the mock HAL, which means every branch in them is
reachable from a test.

**`src/hal/avr/` is excluded on purpose.** It pokes registers that do not exist
on the host, so a number counting code x86 cannot execute would not mean
anything — it would go up when someone added an `#ifdef`, which is the opposite
of what a coverage figure is for. What covers that layer is `make
integration-test`: the real image on an emulated ATmega328P.

`src/third_party/` is excluded for the same reason it is excluded from cppcheck:
it is not ours, and its coverage is not a fact about this project.

## The floor is a floor, not a target

`COVERAGE_FLOOR ?= 90`. Raising it as the number climbs would make every commit
that legitimately adds an untestable branch a failure, and the response to that
is always to add a test that executes a line without asserting anything about
it — which lowers the real coverage while raising the reported figure.

The project rule is explicit: **do not write tests for the sake of coverage**.
A branch that is genuinely unreachable should be unreachable in the source, not
papered over with a test that reaches it.

## Two things this figure does not tell you

**Branch coverage is 80.4%, and that is the more honest number.** Line coverage
counts a line with a condition on it as covered when either arm runs. The gap
between the two is mostly error paths in the bus drivers, where a peripheral
would have to fail at a particular moment — and `tfw_mock_fail_next()` reaches
many of those, but not all.

**Coverage says nothing about the wire.** The golden vectors are what hold the C
and Go codecs to the same bytes, and a codec can be at 100% coverage while
emitting the wrong frame. `make verify-vectors` is the check that matters there,
and a failure in it is a wire break rather than a test to update.

## If it fails

The failure mode that is not about your code: `gcov` names its data file after
the source's **basename**, so two files with the same basename in one link write
to the same `.gcda`. The error is "could not infer a working directory that
resolved it", which names neither file. Two `board.c` files did this once; the
lookups are `board_lookup.c` now.
