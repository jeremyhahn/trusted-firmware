#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# The latest-version check (handbook ci-standard.md §1, devops.md §9): fail when
# a library or a toolchain pin is behind its latest release without a written
# reason.
#
#   deps-check.py toolchain       the pins in scripts/tool-pins.sh, and the AVR
#                                 toolchain the image actually installed
#   deps-check.py go <module>...  every require in each module's go.mod
#
# Where the reason lives, as devops.md §9 says it must:
#   a toolchain pin   HELD_<NAME>="..." in scripts/tool-pins.sh
#   a Go module       a `held: <reason>` comment on its line in go.mod
#
# It needs the network: the question is what upstream has released. A lookup
# that fails is a failure, never a pass -- an offline run cannot say the pins
# are current.

import json
import os
import re
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.request

PINS = "scripts/tool-pins.sh"
# The Dockerfiles that repeat a pin as an ARG default, and which pins.
DOCKERFILE_PINS = [
    ("build/docker/devcontainer/Dockerfile", ["BASE_IMAGE", "BASE_DIGEST"]),
]
TIMEOUT = 60
# An upstream lookup is tried ATTEMPTS times when it fails transiently, with
# BACKOFF seconds before each retry. BOUND is the whole lookup, every attempt
# and pause included: the 600 s mk/stages.mk relies on, so a real hang still
# fails and a retry never makes a lookup slower than one attempt was allowed.
ATTEMPTS = 3
BACKOFF = (2, 8)
BOUND = 600
# What an upstream that is down for a moment says: a 5xx or 429 (git over
# https, the Go proxy), and a name, connection, TLS or time failure.
TRANSIENT = re.compile(
    r"returned error: (?:5\d\d|429)\b|\b(?:5\d\d|429) [A-Z][a-z]"
    r"|(?i:could not resolve|no such host|name resolution|connection (?:reset|refused|timed out)"
    r"|timed out|i/o timeout|tls handshake|ssl(?:_| connect)|gnutls|unexpected eof|early eof)")


class LookupFailed(Exception):
    pass


class Transient(LookupFailed):
    """A failure the next attempt may not have."""


def attempt(once):
    """once(budget) up to ATTEMPTS times while it fails Transient, inside BOUND.

    The last failure is raised as it came: after the last attempt a lookup
    fails exactly as a single attempt would."""
    deadline = time.monotonic() + BOUND
    for n in range(ATTEMPTS):
        try:
            return once(deadline - time.monotonic())
        except Transient as e:
            if n + 1 == ATTEMPTS or time.monotonic() + BACKOFF[n] >= deadline:
                raise
        time.sleep(BACKOFF[n])


def load_pins(path=PINS):
    pins = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            m = re.match(r'^([A-Z][A-Z0-9_]*)=(.*)$', line.rstrip("\n"))
            if not m:
                continue
            value = m.group(2)
            if len(value) >= 2 and value[0] == value[-1] == '"':
                value = value[1:-1]
            pins[m.group(1)] = value
    return pins


def fetch(url, headers=None, method="GET"):
    def once(budget):
        req = urllib.request.Request(url, headers=headers or {}, method=method)
        try:
            with urllib.request.urlopen(req, timeout=min(TIMEOUT, budget)) as r:
                return r.read().decode(), r.headers
        except urllib.error.HTTPError as e:
            kind = Transient if e.code >= 500 or e.code == 429 else LookupFailed
            raise kind(f"{url}: {e}") from e
        except OSError as e:
            raise Transient(f"{url}: {e}") from e
    return attempt(once)


def run(cmd, cwd=None, upstream=False):
    """cmd's stdout. upstream: cmd asks the network, so a TRANSIENT failure is retried."""
    def once(budget):
        try:
            out = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True,
                                 timeout=budget, check=False)
        except FileNotFoundError as e:
            raise LookupFailed(f"{cmd[0]} is missing: run in the devcontainer image") from e
        except subprocess.TimeoutExpired as e:
            kind = Transient if upstream else LookupFailed
            raise kind(f"{' '.join(cmd)}: no answer in {budget:.0f} s") from e
        if out.returncode != 0:
            err = out.stderr.strip()
            kind = Transient if upstream and TRANSIENT.search(err) else LookupFailed
            raise kind(f"{' '.join(cmd)}: {err[-600:]}")
        return out.stdout
    return attempt(once)


def numbers(version):
    return tuple(int(n) for n in re.findall(r"\d+", version))


def latest_tag(repo, pattern):
    """The highest release tag in a git repository whose name matches pattern.

    The pattern's groups are the version numbers, so a pre-release tag (-rc,
    -beta, -dev) does not match and is never taken for a release."""
    out = run(["git", "ls-remote", "--tags", "--refs", repo], upstream=True)
    best = None
    for line in out.splitlines():
        name = line.split("refs/tags/", 1)[-1]
        m = re.fullmatch(pattern, name)
        if m:
            v = tuple(int(g) for g in m.groups() if g is not None)
            if best is None or v > best:
                best = v
    if best is None:
        raise LookupFailed(f"{repo}: no tag matches {pattern}")
    return best


def dpkg_version(package):
    v = run(["dpkg-query", "-W", "-f", "${Version}", package]).strip()
    if not v:
        raise LookupFailed(f"{package} is not installed: run in the devcontainer image")
    # Debian's epoch and revision are not the upstream release.
    v = v.split(":", 1)[-1].rsplit("-", 1)[0]
    return numbers(v.split("+", 1)[0].split("~", 1)[0])


def show(v):
    return ".".join(str(n) for n in v)


class Report:
    def __init__(self):
        self.behind = 0
        self.failed = 0

    def check(self, name, pinned, latest, reason):
        if pinned >= latest:
            print(f"  ok      {name} {show(pinned)}")
        elif reason:
            print(f"  held    {name} {show(pinned)} (latest {show(latest)}): {reason}")
        else:
            print(f"  BEHIND  {name} {show(pinned)} (latest {show(latest)}), no reason recorded")
            self.behind += 1

    def fail(self, name, err):
        print(f"  FAILED  {name}: {err}")
        self.failed += 1


def base_digest(image):
    repo, tag = image.split(":", 1)
    if "/" not in repo:
        repo = "library/" + repo
    body, _ = fetch("https://auth.docker.io/token?service=registry.docker.io"
                    f"&scope=repository:{repo}:pull")
    token = json.loads(body)["token"]
    _, headers = fetch(f"https://registry-1.docker.io/v2/{repo}/manifests/{tag}",
                       method="HEAD", headers={
                           "Authorization": "Bearer " + token,
                           "Accept": "application/vnd.oci.image.index.v1+json, "
                                     "application/vnd.docker.distribution.manifest.list.v2+json"})
    return headers["Docker-Content-Digest"]


def toolchain():
    pins = load_pins()
    rep = Report()
    held = lambda key: pins.get("HELD_" + key, "")

    def guarded(name, fn):
        try:
            fn()
        except LookupFailed as e:
            rep.fail(name, e)

    def go():
        body, _ = fetch("https://go.dev/VERSION?m=text")
        rep.check("go", numbers(pins["GO_VERSION"]), numbers(body.splitlines()[0]), held("GO"))

    def base():
        image = pins["BASE_IMAGE"]
        codename = image.split(":", 1)[1].split("-", 1)[0]
        body, _ = fetch("https://deb.debian.org/debian/dists/stable/Release")
        stable = re.search(r"^Codename: (\S+)$", body, re.M).group(1)
        if codename != stable:
            reason = held("BASE_IMAGE")
            tag = "held   " if reason else "BEHIND "
            print(f"  {tag} {image}: Debian stable is {stable}{': ' + reason if reason else ''}")
            rep.behind += 0 if reason else 1
        current = base_digest(image)
        if current == pins["BASE_DIGEST"]:
            print(f"  ok      {image}@{current}")
        elif held("BASE_DIGEST"):
            print(f"  held    {image}@{pins['BASE_DIGEST']} (tag now {current}): {held('BASE_DIGEST')}")
        else:
            print(f"  BEHIND  {image}@{pins['BASE_DIGEST']}: the tag now names {current}")
            rep.behind += 1

    # The AVR toolchain is what the image installed, read back from dpkg.
    def avr_gcc():
        rep.check("avr-gcc", dpkg_version("gcc-avr"),
                  latest_tag("https://gcc.gnu.org/git/gcc.git", r"releases/gcc-(\d+)\.(\d+)\.(\d+)"),
                  held("AVR_GCC"))

    def avr_libc():
        rep.check("avr-libc", dpkg_version("avr-libc"),
                  latest_tag("https://github.com/avrdudes/avr-libc", r"avr-libc-(\d+)_(\d+)_(\d+)-release"),
                  held("AVR_LIBC"))

    def simavr():
        rep.check("simavr", dpkg_version("simavr"),
                  latest_tag("https://github.com/buserror/simavr", r"v(\d+)\.(\d+)(?:\.(\d+))?"),
                  held("SIMAVR"))

    def esp_idf():
        rep.check("esp-idf", numbers(pins["ESP_IDF_VERSION"]),
                  latest_tag("https://github.com/espressif/esp-idf", r"v(\d+)\.(\d+)(?:\.(\d+))?"),
                  held("ESP_IDF"))

    def ci_tool(name, key, repo):
        def f():
            rep.check(name, numbers(pins[key + "_VERSION"]),
                      latest_tag(repo, r"v(\d+)\.(\d+)\.(\d+)"), held(key))
        return f

    # A Dockerfile ARG default is a second copy of a pin; it must say the same.
    def drift():
        for path, keys in DOCKERFILE_PINS:
            with open(path, encoding="utf-8") as f:
                args = dict(re.findall(r"^ARG ([A-Z][A-Z0-9_]*)=(\S+)$", f.read(), re.M))
            for key in keys:
                if args.get(key) != pins[key]:
                    raise LookupFailed(f"{path}: ARG {key}={args.get(key)} but {PINS} says {pins[key]}")
        print(f"  ok      the Dockerfiles' ARG defaults match {PINS}")

    print("toolchain pins (scripts/tool-pins.sh):")
    for name, fn in [("dockerfile pins", drift), ("go", go), ("base image", base), ("avr-gcc", avr_gcc),
                     ("avr-libc", avr_libc), ("simavr", simavr), ("esp-idf", esp_idf),
                     ("gosec", ci_tool("gosec", "GOSEC", "https://github.com/securego/gosec")),
                     ("govulncheck", ci_tool("govulncheck", "GOVULNCHECK", "https://go.googlesource.com/vuln")),
                     ("actionlint", ci_tool("actionlint", "ACTIONLINT", "https://github.com/rhysd/actionlint")),
                     ("golangci-lint", ci_tool("golangci-lint", "GOLANGCI",
                                               "https://github.com/golangci/golangci-lint")),
                     ("trivy", ci_tool("trivy", "TRIVY", "https://github.com/aquasecurity/trivy"))]:
        guarded(name, fn)
    return rep


def decode_stream(text):
    dec = json.JSONDecoder()
    i, out = 0, []
    while True:
        while i < len(text) and text[i].isspace():
            i += 1
        if i >= len(text):
            return out
        obj, i = dec.raw_decode(text, i)
        out.append(obj)


def held_in_gomod(gomod_lines, path):
    for line in gomod_lines:
        code, _, comment = line.partition("//")
        fields = code.split()
        if path in fields and "held:" in comment:
            return comment.split("held:", 1)[1].strip()
    return ""


def seed_modcache():
    """Copy the go.mod and .info files of an already-fetched module cache
    (DEPS_SEED_GOMODCACHE: the image's pre-fetched one) into this run's own.

    `go list -m -u` loads the whole module graph before it asks for any latest
    version. A GOPRIVATE module has no proxy, so each go.mod in the graph is a
    `git fetch --depth=1` of that version's whole tree: dozens of them for
    xkms/, and the cold run outlasts the timeout. Seeded, the graph loads from
    disk. Only the metadata is copied, never a list or a zip: every latest
    version is still asked of upstream, so a seed cannot hide an update, and
    the files are the checksum-verified ones go.sum names."""
    seed = os.environ.get("DEPS_SEED_GOMODCACHE", "")
    if not seed:
        return
    src = os.path.join(seed, "cache", "download")
    if not os.path.isdir(src):
        print(f"deps-check: DEPS_SEED_GOMODCACHE={seed} has no cache/download; not seeding")
        return
    dst = os.path.join(run(["go", "env", "GOMODCACHE"]).strip(), "cache", "download")
    n = 0
    for root, _, files in os.walk(src):
        if os.path.basename(root) != "@v":
            continue
        for f in files:
            if not f.endswith((".mod", ".info")):
                continue
            out = os.path.join(dst, os.path.relpath(root, src), f)
            if os.path.exists(out):
                continue
            os.makedirs(os.path.dirname(out), exist_ok=True)
            shutil.copyfile(os.path.join(root, f), out)
            n += 1
    print(f"deps-check: seeded {n} go.mod/.info file(s) from {seed}")


def go_modules(dirs):
    rep = Report()
    try:
        seed_modcache()
    except (LookupFailed, OSError) as e:
        rep.fail("module cache seed", e)
    for d in dirs:
        print(f"Go module {d}:")
        try:
            edit = json.loads(run(["go", "mod", "edit", "-json"], cwd=d))
            # A require replaced by a directory is this tree, not a release,
            # and asking upstream for its latest would only ask for a credential.
            local = {r["Old"]["Path"] for r in edit.get("Replace") or [] if not r["New"].get("Version")}
            listed = sorted({r["Path"] for r in edit.get("Require") or []} - local)
            mods = decode_stream(run(["go", "list", "-m", "-u", "-json"] + listed, cwd=d, upstream=True)) if listed else []
        except LookupFailed as e:
            rep.fail(d, e)
            continue
        with open(os.path.join(d, "go.mod"), encoding="utf-8") as f:
            gomod = f.readlines()
        if not listed:
            print("  ok      no require outside this tree")
        for m in mods:
            upd = m.get("Update")
            if not upd:
                print(f"  ok      {m['Path']} {m['Version']}")
                continue
            reason = held_in_gomod(gomod, m["Path"])
            if reason:
                print(f"  held    {m['Path']} {m['Version']} (latest {upd['Version']}): {reason}")
            else:
                print(f"  BEHIND  {m['Path']} {m['Version']} (latest {upd['Version']}), no held: reason in go.mod")
                rep.behind += 1
    return rep


def main(argv):
    if len(argv) < 2 or argv[1] not in ("toolchain", "go") or (argv[1] == "go" and len(argv) < 3):
        print("usage: deps-check.py toolchain | go <module dir>...", file=sys.stderr)
        return 2
    if shutil.which("git") is None:
        print("deps-check: git is missing: run in the devcontainer image", file=sys.stderr)
        return 1
    rep = toolchain() if argv[1] == "toolchain" else go_modules(argv[2:])
    if rep.failed:
        print(f"deps-check: {rep.failed} lookup(s) failed; an unanswered check is not a pass")
    if rep.behind:
        print(f"deps-check: {rep.behind} behind without a recorded reason "
              "(devops.md §9: record it in place, or update)")
    return 1 if rep.behind or rep.failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
