#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# The public-releasability gate (docs/development/releasability.md).
#
# Scans every file that would be published -- the tracked files minus those
# .gitattributes marks export-ignore -- for the generic leak shapes listed in
# scripts/releasability.patterns, and prints `file:line: class` for each. Exits 1
# on any. Export-ignored files are reported as `file: strip-at-publish`, which
# is information and not a finding. A tracked symlink's target text, which is
# what git archive publishes for it, is checked against each pattern and
# reported as `file: class (symlink target)`. Needs no network.
#
#   RELEASABILITY_ROOT      the git work tree to scan (default: the current directory)
#   RELEASABILITY_PATTERNS  the pattern list (default: releasability.patterns beside this script)
#
# A list line the gate cannot use (an unknown class, an empty pattern, a regex
# grep -E refuses) exits 2, as do a list with no rules (only blank and `#`
# lines), a list the gate cannot read, and a published file it cannot read.
#
# A line is waived by `lab-string-ok:` followed by a reason, in the file's own
# comment syntax, on the line itself or the line above. The marker with no
# reason is not a waiver.
set -euo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
patterns=${RELEASABILITY_PATTERNS:-$here/releasability.patterns}
cd "${RELEASABILITY_ROOT:-.}"

classes=' lab-address lab-host owner-path owner-account '
waiver='lab-string-ok:[[:space:]]*[A-Za-z0-9]'

published=()
links=()
stripped=()
# check-attr -z prints path, attribute and value, each NUL-terminated.
while IFS= read -r -d '' path && IFS= read -r -d '' _ && IFS= read -r -d '' value; do
  if [ "$value" = set ]; then stripped+=("$path"); continue; fi
  # A deleted file or a submodule is not published content; a symlink's target is.
  if [ -L "$path" ]; then links+=("$path")
  elif [ -f "$path" ]; then published+=("$path"); fi
done < <(git ls-files -z | git check-attr -z --stdin export-ignore)

[ -f "$patterns" ] && [ -r "$patterns" ] || { echo "releasability: cannot read pattern list $patterns" >&2; exit 2; }
# grep's status 2 on an unreadable file is lost in the scan below, so refuse it here.
for path in ${published[@]+"${published[@]}"}; do
  [ -r "$path" ] || { echo "releasability: cannot read $path" >&2; exit 2; }
done

rules=0
hits=$(mktemp)
trap 'rm -f "$hits"' EXIT

# `|| [ -n "$class" ]`: read fails on a last line with no newline, which is still a rule.
while IFS=$'\t' read -r class re _ || [ -n "$class" ]; do
  case "$class" in ''|'#'*) continue ;; esac
  case "$classes" in *" $class "*) ;; *) echo "releasability: unknown class '$class' in $patterns" >&2; exit 2 ;; esac
  [ -n "$re" ] || { echo "releasability: empty pattern for $class in $patterns" >&2; exit 2; }
  # The scan below ignores grep's status, so a regex grep -E refuses would match nothing.
  rerc=0; grep -E -e "$re" /dev/null 2> /dev/null || rerc=$?
  [ "$rerc" -lt 2 ] || { echo "releasability: pattern grep -E refuses for $class in $patterns" >&2; exit 2; }
  rules=$((rules + 1))
  # git archive publishes a symlink as its target text, which has no comment
  # to carry a waiver. A here-string, not a pipe: see the SIGPIPE note below.
  for path in ${links[@]+"${links[@]}"}; do
    target=$(readlink -- "$path")
    if grep -qEi -e "$re" <<< "$target"; then
      printf '%s: %s (symlink target)\n' "$path" "$class" >> "$hits"
    fi
  done
  [ ${#published[@]} -gt 0 ] || continue
  # -Z ends the file name with NUL, so the line number and text follow it.
  printf '%s\0' "${published[@]}" \
    | { xargs -0 -r grep -HInEiZ -e "$re" -- || true; } \
    | while IFS= read -r -d '' path && IFS= read -r rest; do
        n=${rest%%:*}
        text=${rest#*:}
        prev=
        if [ "$n" -gt 1 ]; then prev=$(sed -n "$((n - 1))p" "$path"); fi
        # Not `printf | grep -q` under pipefail: grep -q exits on a waived $prev
        # before printf writes $text, and the SIGPIPE read as "not waived".
        if [[ $prev =~ $waiver || $text =~ $waiver ]]; then continue; fi
        printf '%s:%s: %s\n' "$path" "$n" "$class"
      done >> "$hits"
done < "$patterns"
[ "$rules" -gt 0 ] || { echo "releasability: no rules in $patterns" >&2; exit 2; }

for path in ${stripped[@]+"${stripped[@]}"}; do
  echo "$path: strip-at-publish"
done

if [ -s "$hits" ]; then
  sort -u "$hits"
  echo "releasability: $(sort -u "$hits" | wc -l) finding(s) in the published tree"
  echo "use a role, an RFC 5737 address or a neutral fixture; see docs/development/releasability.md"
  exit 1
fi
echo "releasability: ${#published[@]} published files, no findings"
