#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# Tests of the public-releasability gate (scripts/releasability.sh) and of the
# export-ignore list in .gitattributes. Run by `make releasability-test`, which
# `make ci` includes. There is no skip guard: a test that cannot run fails.
#
# Only generic shapes are planted here: private-network DNS names, home
# directories and the agent identity directory, each with a reference name
# (RFC 2606, RFC 8375). No internal host, device, address or account is named,
# not even as a value to refuse: those are checked in CI from the
# RELEASABILITY_PATTERNS secret (docs/development/releasability.md). A plant
# carries an inline waiver, so the gate passes over this file.
set -euo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo=$(cd "$here/.." && pwd)
gate=$here/releasability.sh

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

failures=0
checks=0
n=0

fail() { echo "FAIL: $*"; failures=$((failures + 1)); }

# fresh: a new throwaway git work tree, named by $t.
fresh() {
  n=$((n + 1))
  t=$tmp/w$n
  mkdir -p "$t"
  git -C "$t" init -q
}

# run_gate gives the gate a fixture list: the public list it reads by default,
# plus one stand-in rule per private class plant, the shape the CI secret
# carries. The stand-ins are made-up names and an RFC 5737 address, not the
# fleet values. The address rule names the one planted address, so the
# RFC 5737 addresses the clean cases use stay clean.
fixture=$tmp/fixture.patterns
{
  cat "$here/releasability.patterns"
  printf '%s\t%s\n' lab-address '(^|[^0-9.])192\.0\.2\.22([^0-9]|$)'
  printf '%s\t%s\n' lab-host 'farm-host-[0-9]+'
  printf '%s\t%s\n' lab-host '(^|[^A-Za-z0-9-])lab-node-[0-9]+'
  printf '%s\t%s\n' lab-host '(^|[^A-Za-z0-9-])ci-node-[0-9]+'
  printf '%s\t%s\n' lab-host '<host>'
  printf '%s\t%s\n' lab-host 'desk-station-[0-9]+'
  printf '%s\t%s\n' owner-account '(^|[^A-Za-z0-9_./-])exampleowner($|[^A-Za-z0-9_/-])'
  printf '%s\t%s\n' owner-account '(^|[^A-Za-z0-9_./-])exowner($|[^A-Za-z0-9_/-])'
} > "$fixture"

# run_gate: the gate on $t with the fixture list; output in $out, status in $rc.
run_gate() {
  git -C "$t" add -A
  rc=0
  out=$(RELEASABILITY_ROOT=$t RELEASABILITY_PATTERNS=$fixture "$gate" 2>&1) || rc=$?
}

# expect_hit CLASS CONTENT: a file holding CONTENT on its first line fails the
# gate with `doc.md:1: CLASS`.
expect_hit() {
  checks=$((checks + 1))
  fresh
  printf '%s\n' "$2" > "$t/doc.md"
  run_gate
  if [ "$rc" -ne 1 ]; then fail "$1: gate exited $rc, want 1, for: $2"; return; fi
  case "$out" in *"doc.md:1: $1"*) ;; *) fail "$1: no 'doc.md:1: $1' in output for: $2" ;; esac
}

# expect_clean CONTENT: the same file passes.
expect_clean() {
  checks=$((checks + 1))
  fresh
  printf '%s\n' "$1" > "$t/doc.md"
  run_gate
  if [ "$rc" -ne 0 ]; then fail "gate rejected clean content (exit $rc): $1 -> $out"; fi
}

# ---------------------------------------------------- a hit for each pattern

expect_hit lab-host    'forge at http://git.example.internal/o/r'   # lab-string-ok: plant
expect_hit lab-host    'printer at printer.home.arpa'               # lab-string-ok: plant
expect_hit lab-host    'share at nas.lan:445'                       # lab-string-ok: plant
expect_hit owner-path  'cd /home/example/sources'                   # lab-string-ok: plant
expect_hit owner-path  'built in /Users/someone/src'                # lab-string-ok: plant
expect_hit owner-path  'key at ~/.agent-identity/keys/agent.key'    # lab-string-ok: plant
expect_hit lab-address 'endpoint 192.0.2.22:7020'                   # lab-string-ok: plant
expect_hit lab-host    'runs on farm-host-3'                        # lab-string-ok: plant
expect_hit lab-host    'built on lab-node-1'                        # lab-string-ok: plant
expect_hit lab-host    'built on ci-node-2'                         # lab-string-ok: plant
expect_hit lab-host    'forge <host>'                               # lab-string-ok: plant
expect_hit lab-host    'ssh desk-station-12'                        # lab-string-ok: plant
expect_hit owner-account 'ask exampleowner about it'                # lab-string-ok: plant
expect_hit owner-account 'owner `exampleowner`.'                    # lab-string-ok: plant
expect_hit owner-account 'user exowner'                             # lab-string-ok: plant

# A hit on the second line is reported on the second line.
checks=$((checks + 1)); fresh
printf 'clean\nhost build.example.internal\n' > "$t/doc.md"     # lab-string-ok: plant
run_gate
case "$out" in *"doc.md:2: lab-host"*) ;; *) fail "second-line hit not reported as doc.md:2: $out" ;; esac

# ---------------------------------------------------------- what must pass

expect_clean 'documentation addresses 198.51.100.7 and 203.0.113.9'
expect_clean 'opaque RFC 1918 fixtures 192.168.1.20 and 10.0.0.5'
expect_clean 'names example.com, host.example, a.test and b.invalid'
expect_clean 'a word internalize, an internal.go file and lane.local'
expect_clean 'build host, CI runner, bench host'
expect_clean 'a relative path build/host, and $HOME/src'
# Benchmark provenance and the other inline waivers: same line, or the line above.
expect_clean 'built in /home/example/src <!-- lab-string-ok: an example path -->'   # lab-string-ok: plant
expect_clean "$(printf '<!-- lab-string-ok: an example path -->\nbuilt in /home/example/src')"   # lab-string-ok: plant

# The marker with no reason is not a waiver.
expect_hit owner-path 'built in /home/example/src <!-- lab-string-ok: -->'   # lab-string-ok: plant
expect_hit owner-path 'built in /home/example/src # lab-string-ok:'          # lab-string-ok: plant

# A binary file is not scanned.
checks=$((checks + 1)); fresh
printf 'build.example.internal\0\001\002' > "$t/blob.bin"           # lab-string-ok: plant
run_gate
if [ "$rc" -ne 0 ]; then fail "binary file was scanned: $out"; fi

# ------------------------------------------------- export-ignored, not scanned

checks=$((checks + 1)); fresh
printf 'ignored.md export-ignore\n' > "$t/.gitattributes"
printf 'host build.example.internal\n' > "$t/ignored.md"            # lab-string-ok: plant
printf 'neutral text\n' > "$t/kept.md"
run_gate
if [ "$rc" -ne 0 ]; then fail "an export-ignored file was scanned: $out"; fi
case "$out" in *"ignored.md: strip-at-publish"*) ;; *) fail "export-ignored file not reported as strip-at-publish: $out" ;; esac

# The same file, not ignored, fails: the test above is not passing vacuously.
checks=$((checks + 1)); fresh
printf 'ignored.md export-ignore\n' > "$t/.gitattributes"
printf 'host build.example.internal\n' > "$t/other.md"              # lab-string-ok: plant
run_gate
if [ "$rc" -ne 1 ]; then fail "a published file with a private host name passed: $out"; fi

# --------------------------------------------------------- the pattern list

checks=$((checks + 1)); fresh
printf 'neutral text\n' > "$t/doc.md"
printf 'no-such-class\tfoo\t# note\n' > "$tmp/bad.patterns"
git -C "$t" add -A
rc=0
RELEASABILITY_ROOT=$t RELEASABILITY_PATTERNS=$tmp/bad.patterns "$gate" > /dev/null 2>&1 || rc=$?
if [ "$rc" -ne 2 ]; then fail "an unknown class exited $rc, want 2"; fi

# This tree passes its own list.
checks=$((checks + 1))
rc=0
out=$(RELEASABILITY_ROOT=$repo "$gate" 2>&1) || rc=$?
if [ "$rc" -ne 0 ]; then fail "this tree fails the pattern list: $out"; fi

# ------------------------------------------------- the export-ignore list

# Paths the published tree must not hold: development tooling at any depth, and
# the forge-only workflow files. One definition, used by both checks below.
is_stripped() {
  case "$1" in
    CLAUDE.md|AGENTS.md|*/CLAUDE.md|*/AGENTS.md|.claude/*|.serena/*) return 0 ;;
    .github/workflows/automerge.yml|.github/workflows/qwen-review.yml) return 0 ;;
    .github/workflows/code-review.yml|.github/actionlint.yaml) return 0 ;;
  esac
  return 1
}

# git archive of a tree carrying this repository's .gitattributes holds none of
# them and keeps the public workflow beside them.
checks=$((checks + 1)); fresh
cp "$repo/.gitattributes" "$t/.gitattributes"
mkdir -p "$t/pkg" "$t/.claude/sub" "$t/.serena" "$t/.github/workflows"
for f in CLAUDE.md AGENTS.md pkg/CLAUDE.md pkg/AGENTS.md .claude/settings.json .claude/sub/x \
         .serena/project.yml .github/actionlint.yaml .github/workflows/automerge.yml \
         .github/workflows/qwen-review.yml .github/workflows/code-review.yml; do
  printf 'x\n' > "$t/$f"
done
for f in README.md pkg/main.c .github/workflows/ci.yml; do printf 'x\n' > "$t/$f"; done
git -C "$t" add -A
listing=$(git -C "$t" archive "$(git -C "$t" write-tree)" | tar -t)
while IFS= read -r f; do
  if is_stripped "$f"; then fail "git archive holds $f"; fi
done <<< "$listing"
for f in README.md pkg/main.c .github/workflows/ci.yml; do
  case $'\n'"$listing"$'\n' in *$'\n'"$f"$'\n'*) ;; *) fail "git archive lost the published file $f" ;; esac
done

# In this repository's own index: every stripped path is export-ignored, and
# nothing else is.
checks=$((checks + 1))
while IFS= read -r -d '' path && IFS= read -r -d '' _ && IFS= read -r -d '' value; do
  if is_stripped "$path"; then want=set; else want=unspecified; fi
  if [ "$value" != "$want" ]; then fail "$path: export-ignore is '$value', want '$want'"; fi
done < <(git -C "$repo" ls-files -z | git -C "$repo" check-attr -z --stdin export-ignore)

if [ "$failures" -ne 0 ]; then
  echo "releasability tests: $failures failure(s) in $checks checks"
  exit 1
fi
echo "releasability tests: $checks checks passed"
