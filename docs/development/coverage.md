# Coverage

> **Status: NORMATIVE** · Scope: `make coverage`, `mk/host.mk`
> · Last verified: 2026-09-22

```sh
make coverage          # runs the host and boot suites instrumented, enforces the floor
```

Current: **96.3% of lines, 95.7% of functions** over `src/proto`, `src/core`,
`src/caps` and `boards/`. The floor is 90% and it fails the build.

## What is measured, and what is deliberately not

Measured, as the `--filter`s of the gcovr call in `mk/host.mk` (lines
2099-2113) give it, with the reason for each in the comment above it (lines
2056-2098):

- **`src/` layers:** `src/proto/`, `src/core/`, `src/caps/`; and `boards/`.
- **`boot/`:** `boot_image`, `boot_measure`, `boot_ota`, `boot_platform`,
  `boot_policy`, `boot_session`, `boot_request`, `boot_entry`, `boot_stage`,
  `boot_stage_none`, `boot_swap`, `boot_swap_none`, `boot_trial`, `boot_store`,
  `boot_handoff`, `boot_cose` and `boot_prev`; `boot/recovery/recovery_ota.c`;
  `boot/measure/measure_soft.c` and `measure_atecc608a.c`; `boot/tier/cdi_soft.c`
  and `cdi_atecc.c`; `boot/verify/verify_ed25519_donna.c`, `verify_atecc608a.c`,
  `donna_shim.c`, `verify_p256_soft.c` and `p256_key_slot.c`.
- **`src/crypto/`:** `sha256.c`, `sha256_small.c`, `deviceid_p256.c`; the soft
  provider with `soft/uds_flash.c`, `soft/id_p256.c` and `soft/id_hmac.c`; the
  none, ATECC608A (`atecc608a/atecc.c`, `atecc608a/provider.c`) and auto
  providers; the OPTIGA Trust M driver, `optiga/optiga_i2c.c`, `optiga_cmd.c`
  and `optiga_crypto.c`.
- **`src/net/`:** `netcfg.c`, `w5100.c`, `w5500.c`, `w5x00.c`, `dhcp.c`,
  `dhcp_none.c`.
- **`src/store/`:** `spi_nor.c`.
- **`tools/`:** `tools/suit-emit/emit.c` and `tools/platform-bake/bake.c`.
- **`optee/`:** `optee/ta/tfw_ta.c` and `optee/check/check.c`.

These are the layers that run on x86 against the mock HAL, the software
element and the software W5100, W5500, NOR and OPTIGA parts, which means every
branch in them is reachable from a test. The `main()`s are out: `src/main.c`
and `boot/boot_main.c` are superloops with no return, linked only into the AVR
images, and no host test links the tools' or the OP-TEE check's `main.c`.

**`src/hal/avr/` and `boot/hal/avr` are excluded on purpose.** They poke registers that do not exist
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
