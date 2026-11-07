# Public releasability

> **Status: NORMATIVE** · Scope: `make releasability`, `make releasability-test`,
> `.gitattributes` · Last verified: 2026-11-07

```sh
make releasability        # the gate, on this tree; no network
make releasability-test   # the gate's own tests; both are part of `make ci`
```

## The rule

No published file names the private development fleet, its owner or its
internal identities. Anything that must not appear publicly is scrubbed in the
forge's `main` before a tag, never at publication. "Published" means every
tracked file except those `.gitattributes` marks `export-ignore`.

| Class | What it covers | Use instead |
|---|---|---|
| `lab-address` | An address on the fleet's subnets (private list) | An RFC 5737 documentation address (`192.0.2.0/24`, `198.51.100.0/24`, `203.0.113.0/24`) |
| `lab-host` | A fleet hostname (private list), or any `.internal`, `.lan` or `home.arpa` name | The role: `bench host`, `CI runner` |
| `owner-path` | A path under anyone's home directory (`/home/...`, `/Users/...`), or an agent's `.agent-identity` | A repository-relative path <!-- lab-string-ok: names the class, not a path --> |
| `owner-account` | The owner's account name on its own (private list) | A role, or the module path |

The module path `github.com/jeremyhahn/...` and the organisation path
`jeremyhahn/<repository>` are not `owner-account`: it is the name standing alone
that is.

Not violations:

- **Opaque RFC 1918 literals** that are not the fleet's subnets. Tests use
  `192.168.1.x` and `10.0.0.x` as addresses that are never dialled. The gate does
  not look at `192.168.0.0/16` at large.
- **Benchmark provenance.** A measurement is a property of the machine that made
  it, so "measured on `core-01`, 2026-09-14" is the result's meaning and not a
  leak. Waive the line (below).

Issue links in docs are relative (`../../../../issues/N` from `docs/<topic>/`,
as [ROADMAP.md](../../ROADMAP.md) writes them from the root), never an absolute
forge URL.

## Where the values live

The gate reads two pattern lists of the same format, one line each as `class`,
`regex` and a waiver:

- **Public**, `scripts/releasability.patterns`: generic shapes of a leak that
  hold for any checkout, such as a path under a home directory or a
  private-network DNS name. It names no fleet.
- **Private**, outside the tree: the fleet's subnets, hostnames, path prefixes
  and the owner's account name. It is `$RELEASABILITY_PRIVATE_PATTERNS` when that
  is set, else `${XDG_CONFIG_HOME:-$HOME/.config}/trusted-firmware/releasability.patterns`,
  mode `600`. Set to a path that is not a file, the gate exits 2; set empty, or
  unset with no file at the default path, only the public list runs, and the
  gate says so in its last line. Release cuts run where the private list is.

No tracked file writes a fleet value: docs and fixtures use reference values,
RFC 5737 addresses, RFC 2606 names (`example.com`, `*.test`, `*.invalid`) and
devices named like `core-01`. A new fleet value is one line in the private list.

## The gate

`scripts/releasability.sh` lists the tracked files, drops the `export-ignore`
ones, skips binary files, and prints `file:line: class` for each line that
matches a pattern. It exits 1 on any. The export-ignored files are listed as
`file: strip-at-publish`, which is information and not a finding.

`scripts/releasability-test.sh` runs it on throwaway repositories and has no skip
guard. It writes a stand-in private list of reference values and never reads
the real one. It proves that a hit from each list fails, that RFC 5737 addresses,
`192.168.1.x`, module paths and waived lines pass, that an export-ignored file is
not scanned, that the private list is found at its default path, that a named
but missing one is an error, and that `git archive` of a tree carrying this repository's
`.gitattributes` holds none of the stripped files. It also checks the real
index: every file below is export-ignored, and no other is.

### Waiving a line

In the file's own comment syntax, on the offending line or the line above it:

```markdown
<!-- lab-string-ok: benchmark provenance -->
```

A reason is what makes it a waiver: the marker `lab-string-ok:` with nothing
after it is not one. There is no path allow-list; a whole file is either
published and clean, or export-ignored.

## What is export-ignored

`.gitattributes` marks these `export-ignore`, and the test pins the list against
the tracked tree:

- `CLAUDE.md` and `AGENTS.md`, at any depth, `.claude/` and `.serena/`: the agent
  development workflow.
- The forge-only workflows `.github/workflows/automerge.yml`,
  `.github/workflows/code-review.yml` and `.github/workflows/qwen-review.yml`,
  and the lint configuration for them, `.github/actionlint.yaml`. Their content
  names the private forge's runners and principals and is not scrubbed.
  `.github/workflows/ci.yml` is published.

A new agent file or forge-only workflow is added to `.gitattributes` and to
`is_stripped` in `scripts/releasability-test.sh` together.

## Not covered: a history push

`export-ignore` is honoured by `git archive`, which is what a forge's source
tarball and a public tree are cut with. It is **not** honoured by pushing this
repository's history to another remote: every commit there still carries the
agent files, the forge workflows and any value a past commit held. For the
exclusion to hold, the public repository must be produced as an export (`git
archive` of the release commit, committed fresh), not as a push of this
history. The Go module zip served from a tag does not honour `export-ignore`
either.
