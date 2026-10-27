#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# The public-releasability gate (docs/development/releasability.md).
#
# Scans every file that would be published -- the tracked files minus those
# .gitattributes marks export-ignore -- for the private-fleet values listed in
# scripts/releasability.patterns, and prints `file:line: class` for each. Exits 1
# on any. Export-ignored files are reported as `file: strip-at-publish`, which
# is information and not a finding. Needs no network.
#
#   RELEASABILITY_ROOT      the git work tree to scan (default: the current directory)
#   RELEASABILITY_PATTERNS  the pattern list (default: releasability.patterns beside this script)
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
stripped=()
# check-attr -z prints path, attribute and value, each NUL-terminated.
while IFS= read -r -d '' path && IFS= read -r -d '' _ && IFS= read -r -d '' value; do
  if [ "$value" = set ]; then stripped+=("$path"); continue; fi
  # A deleted file, a symlink or a submodule is not published content.
  if [ -f "$path" ] && [ ! -L "$path" ]; then published+=("$path"); fi
done < <(git ls-files -z | git check-attr -z --stdin export-ignore)

hits=$(mktemp)
trap 'rm -f "$hits"' EXIT

while IFS=$'\t' read -r class re _; do
  case "$class" in ''|'#'*) continue ;; esac
  case "$classes" in *" $class "*) ;; *) echo "releasability: unknown class '$class' in $patterns" >&2; exit 2 ;; esac
  [ -n "$re" ] || { echo "releasability: empty pattern for $class in $patterns" >&2; exit 2; }
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
