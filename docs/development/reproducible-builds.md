# Reproducible builds

> **Status: NORMATIVE** · Scope: `mk/firmware.mk`, `mk/arch/`, `make reproducible-check`
> · Last verified: 2026-09-22
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
| `-ffile-prefix-map` | strips the build directory, so its absolute path does not end up in the image |
| `-frandom-seed=$(notdir $<)` | GCC's internal symbol naming is otherwise seeded per invocation |
| `$(sort …)` over the object list | link order is deterministic, not filesystem order |
| no `__DATE__`, no `__TIME__` | greped for in `make lint`; either one makes the image unreproducible by construction |
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
make reproducible-check BOARD=atmega328p
make reproducible-check BOARD=atmega328p CRYPTO=atecc608a
```

It builds the same source twice **in two different directories** and fails if
the `.hex` files differ. Two different directories specifically, because the
most common reproducibility defect is a path leaking into the image, and a check
that builds twice in the same place cannot see it.

The crypto tier is part of the build path, so the soft and element images are
separate artifacts with separate measurements. They are different firmware.

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
