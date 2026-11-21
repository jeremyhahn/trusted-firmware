# Reproducible builds

> **Status: NORMATIVE** · Scope: `mk/firmware.mk`, `mk/boot.mk`, `mk/arch/`, `scripts/repro_build.py`,
> `make reproducible-check`, `make bootloader-reproducible-check`
> · Last verified: 2026-11-21
>
> This is a requirement, not a property we happen to have. It is checked on
> every commit and a regression fails the build.

## Why

`FWHash` is **signed evidence**. A device attests to a SHA-256 of its own
application flash, and a verifier decides whether to trust the device partly on
whether that hash matches a known-good image.

For that to mean anything, the verifier has to be able to compute the expected
hash **from source**. If the same source produces different bytes on two
machines — or on the same machine on two days — then the golden measurement is
whatever one particular build happened to emit, and "this device is running the
audited firmware" degrades to "this device is running a firmware someone once
built".

So reproducibility is the difference between attestation and ceremony.

## What makes it hold

| | why |
|---|---|
| `SOURCE_DATE_EPOCH` | pins anything time-derived in the toolchain |
| `-ffile-prefix-map` | strips the build directory, so its absolute path does not end up in the image (`$(CURDIR)=.`, `mk/firmware.mk`) |
| `APP_PREFIX_MAP` | an application's own `-ffile-prefix-map=$(APP_ABS)=app`, after the build directory's, so the image does not record where the application was built (`mk/app.mk`) |
| `-frandom-seed=$(notdir $<)` | GCC's internal symbol naming is otherwise seeded per invocation |
| `$(sort …)` over the source list | link order is deterministic, not filesystem order (`FW_SRCS`, `mk/firmware.mk`) |
| no `__DATE__`, no `__TIME__` | grepped for in `make lint` (`lint-timestamps`); either one makes the image unreproducible by construction |
| **a pinned toolchain** | the biggest one — see below |

## The toolchain is the part that bites

The CI runner's own `avr-gcc` is 7.3.0. It rejects `-ffile-prefix-map`, which
did not exist then, and it generates different code.

Installing the toolchain per job — the obvious arrangement — means a build that
silently uses a different compiler than the one anyone tested with, which breaks
reproducibility *and* moves every size-budget number at the same time. The
toolchain is therefore pinned in the devcontainer
(`build/docker/devcontainer/Dockerfile`, every version in `scripts/tool-pins.sh`),
the image records what it contains in `/etc/ci-toolchain.txt`, and
`make docker-build` prints it.

A reproducible build is reproducible **with a stated toolchain**. Without naming
the compiler, the claim is not falsifiable.

## The check

```sh
# the firmware image, one command per tier of the board (BOARD_TIERS)
make reproducible-check BOARD=atmega2560 CRYPTO=soft
make reproducible-check BOARD=atmega2560 CRYPTO=atecc608a
# the application's image: APP_DIR names it, as the build stage does
make reproducible-check APP_DIR=test/app/example BOARD=atmega2560
# tfw-boot, on the ATmega2560 soft base row
make bootloader-reproducible-check
```

Each builds the image twice, each time from a clean copy of the committed
source in its own directory, and fails unless the two `.hex` files are
byte-identical. Both targets run `scripts/repro_build.py` (`mk/firmware.mk`
`reproducible-check`, `mk/boot.mk` `bootloader-reproducible`). A clean copy is
`git archive HEAD`, so no `build/`, no untracked file and no uncommitted edit
reaches it, and an image a previous build left behind cannot satisfy the check:
what is checked is the commit, not the working tree. Each build runs with
`SOURCE_DATE_EPOCH` set to the commit time (`git log -1 --format=%ct HEAD`).

Two different directories specifically, because the most common
reproducibility defect is a path leaking into the image, and a check that
builds twice in the same place cannot see it.

With `APP_DIR`, the image checked is the application's (`$(TARGET)` carries its
name). An application inside this tree is built from each clean copy, so it
must be committed; one outside it is the same directory for both builds,
passed by absolute path (`repro_build.py --app-dir`). `make build` runs
`reproducible-check` with `APP_DIR=test/app/example BOARD=atmega2560` and
`bootloader-reproducible-check` (`mk/stages.mk`). The bootloader check builds
the ATmega2560 row `BOOT_TIER=soft BOOT_MEASURE=soft BOOT_SIG=p256-soft
BOOT_OTA=0 BOOT_STORE=0`; `make bootloader-reproducible` checks whatever row
its `BOOT_*` variables name.

The crypto tier is part of the build path, so on a board with more than one
tier (`BOARD_TIERS` in its `boards/<board>/profile.mk`) each tier's image is a
separate artifact with its own measurement: they are different firmware. The
ATmega328P has one tier, `atecc608a`, and refuses `CRYPTO=soft`
(`BOARD_REFUSED_TIERS`).

## What a verifier does with this

1. Check out the tagged source.
2. Build in the pinned container: `make firmware BOARD=<board> CRYPTO=<tier>`.
3. SHA-256 the application region of the resulting image.
4. Compare with the `FWHash` in the device's signed evidence.

A build-provenance manifest ships with each release carrying the expected hash,
so step 4 does not require a verifier to build anything — but the point is that
it *can*.

## What this deliberately does not do

- **No timestamp, anywhere, for any reason.** Not a build date in a banner, not
  a version string with a date in it. If you want to know when an image was
  built, ask the provenance manifest, which is signed and is not the image.
- **No `-march=native` or any host-derived flag.** The image must not depend on
  the machine that produced it.
- **No claim of bit-identical output across toolchain versions.** Reproducible
  means *same source, same toolchain, same bytes*. Upgrading avr-gcc changes the
  image and therefore the measurement, and that is a release event with a new
  provenance manifest, not a regression.
