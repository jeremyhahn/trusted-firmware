# Releasing

> **Status: NORMATIVE** · Scope: `make release-alpha`, `scripts/release_alpha.py`
> · Last verified: 2026-10-25

```sh
make release-alpha DRY_RUN=1   # print the tags it would cut; cuts and pushes nothing
make release-alpha             # cut the next alpha and push it to dev
```

The release kinds, their gates and who cuts them are the handbook's
(`software/conventions/release-versioning.md` §1a to §3). This page says only
how this repository carries them out.

## Alpha: `make release-alpha`

An internal alpha is cut by the project agent whenever CI is green on `main`.
`make release-alpha` runs `scripts/release_alpha.py`, which:

1. refuses unless HEAD is on `main`, the working tree is clean (an untracked
   file counts), and, after fetching `dev`'s `main` and tags, HEAD equals
   `dev/main`;
2. takes the newest `vX.Y.Z-alpha` tag and increments PATCH only; a MINOR is
   still a hand-cut tag (§2);
3. refuses unless the new version sorts, in semver precedence, above every tag
   of its series: `v…` for the parent module and `sdk/go/v…` for the SDK, so an
   existing `-rc.N` or release of that PATCH, or an SDK tag ahead of the parent,
   stops it (§3);
4. cuts the annotated tags `vX.Y.Z-alpha` and `sdk/go/vX.Y.Z-alpha` on HEAD,
   signed with git's configured key (`git tag --sign`; no key path is named);
5. pushes both to `dev` in one atomic push, never forced and never to `github`;
6. prints the post-tag step, which the coordinator runs (runbook step 12):
   `consumer-check github.com/jeremyhahn/trusted-firmware/sdk/go sdk/go/vX.Y.Z-alpha`.

The parent and `sdk/go` move in lockstep, as `v0.4.1-alpha` and
`sdk/go/v0.4.1-alpha` did. The `xkms/` module keeps its own `xkms/v…` series
and is tagged by hand. `VERSION` is not read or written: the handbook keeps it
and the tag as two sources of truth (§2).

`DRY_RUN=1` runs every check, including the fetch, and prints the tags and the
post-tag step without cutting or pushing anything.

`scripts/test_release_alpha.py` (`make scripts-test`, part of `make ci`) runs
the script against a scratch repository whose `dev` is a bare repository beside
it, so no network is touched: a tag series, a dirty tree, a branch that is not
`main`, a non-monotonic series, a HEAD behind `dev/main`, no alpha to
increment, and a real cut, signed by a stand-in signer configured in the
scratch repository alone.

## Publication

Publishing to GitHub is a human step (handbook `version-control.md` §5a);
what must hold before it is [releasability.md](releasability.md).
