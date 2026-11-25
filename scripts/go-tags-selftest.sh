#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# go-tags-selftest: every Go build, test, vet and scanner the CI stages reach takes the template's GO_BUILD_TAGS.
set -u

: "${GO_TAGS_BRIDGE:?run it through make go-tags-selftest}"
: "${GO_TAGS_BRIDGE_PREREQS:?run it through make go-tags-selftest}"
: "${TFWPROV_BIN:?run it through make go-tags-selftest}"

# The calling make's flags and variables would reach every dry run below
# through MAKEFLAGS, a GO_BUILD_TAGS among them: each case sets its own.
unset MAKEFLAGS MFLAGS MAKELEVEL GO_BUILD_TAGS

# stage: target, one make target per entry; boot-bridge-run's arguments come
# from GO_TAGS_BRIDGE and GO_TAGS_BRIDGE_PREREQS.
targets=(
  "build:sdk-build"
  "build:$TFWPROV_BIN"
  "test:sdk-coverage"
  "test:xkms-coverage"
  "integration-test:sdk-test"
  "integration-test:xkms-test"
  "integration-test:boot-bridge-run"
  "security:sdk-lint"
  "security:go-vet"
  "security:go-gosec"
  "security:go-govulncheck"
  "security:go-golangci"
  "security:xkms-security"
  "security:xkms-vuln"
)

# The Go invocations that take build tags. go fmt, go mod, go list -m and go
# tool cover read no build constraint, so they are not here.
go_line='(^|[ ;&(])go (build|test|vet)( |$)|gosec -quiet|govulncheck( -tags| \./)|golangci-lint run'

# dryrun TARGET [MAKE ARGS...]: print "target<TAB>go line" for each Go invocation.
dryrun() {
  local t=$1; shift
  local extra=()
  if [ "$t" = boot-bridge-run ]; then
    read -r -a extra <<< "$GO_TAGS_BRIDGE"
    local p; for p in $GO_TAGS_BRIDGE_PREREQS; do extra+=(-o "$p"); done
  fi
  local out
  if ! out=$(make --no-print-directory -n "$t" "${extra[@]}" "$@" 2>&1); then
    echo "go-tags-selftest: make -n $t failed:" >&2
    printf '%s\n' "$out" | tail -n 5 >&2
    return 1
  fi
  printf '%s\n' "$out" | grep -E -- "$go_line" | sed -e "s|$PWD/||g" -e 's/^[[:space:]]*//' \
    | while IFS= read -r l; do printf '%s\t%s\n' "$t" "$l"; done
}

# tagarg LINE: the tag argument of one Go invocation, or nothing.
tagarg() { printf '%s\n' "$1" | grep -oE -- '(^| )--?(build-)?tags [^ ]+' | sed 's/.* //'; }

fail=0
bad() { echo "go-tags-selftest: $*"; fail=1; }

# 1. The probe reaches every invocation.
for e in "${targets[@]}"; do
  stage=${e%%:*}; t=${e#*:}
  lines=$(dryrun "$t" GO_BUILD_TAGS=tfwtagprobe) || { fail=1; continue; }
  [ -n "$lines" ] || { bad "$stage: $t runs no Go invocation"; continue; }
  while IFS=$'\t' read -r _ l; do
    case ",$(tagarg "$l")," in
      *,tfwtagprobe,*) ;;
      *) bad "$stage: $t: GO_BUILD_TAGS missing from: $l" ;;
    esac
  done <<< "$lines"
done

# 2. A tag given twice, and one the module has of its own, appear once.
for e in "${targets[@]}"; do
  t=${e#*:}
  lines=$(dryrun "$t" GO_BUILD_TAGS='codec_json tfwtagprobe codec_json simavr') || { fail=1; continue; }
  while IFS=$'\t' read -r _ l; do
    a=$(tagarg "$l")
    dup=$(printf '%s\n' "$a" | tr ',' '\n' | sort | uniq -d)
    [ -z "$dup" ] || bad "$t: tag $dup given twice in: $l"
  done <<< "$lines"
done

# 3. Empty and unset: exactly the command lines from before.
expected=$(cat <<'EOF'
sdk-build	cd sdk/go && go build ./...
build/tools/tfwprov	cd sdk/go && go build -o build/tools/tfwprov ./cmd/tfwprov
sdk-coverage	cd sdk/go && { go test -count=1 -coverprofile=../../coverage/sdk.out ./... \
xkms-coverage	{ go test -count=1 -tags codec_json -coverprofile=../coverage/xkms.out $pkgs \
sdk-test	cd sdk/go && go test -count=1 -tags tfwsim ./...
xkms-test	cd xkms && TFWSIM_DIR=build/host go test -count=1 -tags codec_json,tfwsim \
boot-bridge-run	go test -count=1 -tags simavr -run '^TestOTAStageThroughTfwBoot$' ./cmd/tfwctl/
sdk-lint	cd sdk/go && go vet ./...
sdk-lint	cd sdk/go && go vet -tags tfwsim ./...
go-vet	(cd $d && go vet ./...); echo "$d: gofmt and vet clean"; done
go-gosec	(cd $d && gosec -quiet ./...) || fail=1; done; exit $fail
go-govulncheck	(cd $d && govulncheck ./...) || fail=1; done; exit $fail
go-golangci	(cd $d && golangci-lint run --config .golangci.yml ./...) || fail=1; done; \
go-golangci	(cd sdk/go && golangci-lint run --config .golangci.yml --build-tags tfwsim ./...) || fail=1; \
xkms-security	cd xkms && gosec -quiet -tags codec_json ./...
xkms-security	cd xkms && golangci-lint run --config .golangci.yml --build-tags codec_json ./...
xkms-vuln	cd xkms && out=$(govulncheck -tags codec_json -format json ./...) && \
EOF
)
for how in empty unset; do
  got=$(for e in "${targets[@]}"; do
          t=${e#*:}
          if [ $how = empty ]; then dryrun "$t" GO_BUILD_TAGS=; else dryrun "$t"; fi
        done) || fail=1
  if [ "$got" != "$expected" ]; then
    bad "GO_BUILD_TAGS $how changed a command line:"
    diff <(printf '%s\n' "$expected") <(printf '%s\n' "$got") | tail -n 20
  fi
done

[ $fail -eq 0 ] && echo "go-tags-selftest: GO_BUILD_TAGS reaches every Go invocation; empty changes none"
exit $fail
