"""scripts/trim-ci-caches.sh: argument checks, and the largest idle cache
volumes removed until the total fits the bound (Refs #640).

Each case runs the real script with bash, a scrubbed environment and a fake
`docker` first on PATH. The fake reads a table of volumes (name, size in KiB,
flags) and logs every argv it is called with; no real docker is called.
"""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(
    os.environ.get(
        "TRIM_CI_CACHES_SCRIPT",
        Path(__file__).resolve().parent / "trim-ci-caches.sh",
    )
)

GIB = 1024 * 1024  # KiB
PREFIX = "tf-ci-"
IMAGE = "toolchain:test"

# Table lines: "<name> <kib> <flags>", flags a comma list of unreadable,
# inuse and refuse, or "-". Log lines: one call per line, argv joined by \x1f.
FAKE_DOCKER = r"""#!/usr/bin/env bash
printf '%s\037' "$@" >> "$FAKE_LOG"
printf '\n' >> "$FAKE_LOG"
row() {
  while read -r n k f; do
    if [ "$n" = "$1" ]; then echo "$k ,$f,"; return 0; fi
  done < "$FAKE_TABLE"
  return 1
}
case $1 in
  volume)
    case $2 in
      ls) while read -r n k f; do echo "$n"; done < "$FAKE_TABLE"; exit 0 ;;
      rm) r=$(row "$3") || exit 1
          case $r in *,refuse,*) exit 1 ;; esac
          exit 0 ;;
    esac ;;
  run)
    vol=
    for a in "$@"; do case $a in *:/c:ro) vol=${a%:/c:ro} ;; esac; done
    r=$(row "$vol") || exit 1
    case $r in *,unreadable,*) exit 1 ;; esac
    printf '%s\t/c\n' "${r%% *}"
    exit 0 ;;
  ps)
    vol=${4#volume=}
    r=$(row "$vol") || exit 0
    case $r in *,inuse,*) echo 0123456789ab ;; esac
    exit 0 ;;
esac
exit 1
"""


class TrimCiCachesTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.dir = Path(self._tmp.name)
        bindir = self.dir / "bin"
        bindir.mkdir()
        fake = bindir / "docker"
        fake.write_text(FAKE_DOCKER)
        fake.chmod(0o755)
        self.log = self.dir / "docker.log"
        self.table = self.dir / "volumes.txt"
        self.table.write_text("")
        self.env = {
            "PATH": f"{bindir}:/usr/bin:/bin",
            "HOME": str(self.dir),
            "FAKE_LOG": str(self.log),
            "FAKE_TABLE": str(self.table),
        }

    def volumes(self, *rows):
        self.table.write_text(
            "".join(f"{n} {k} {f or '-'}\n" for n, k, f in rows)
        )

    def run_trim(self, *args, **env):
        e = dict(self.env)
        e.update(env)
        return subprocess.run(
            ["bash", str(SCRIPT), *args],
            env=e,
            capture_output=True,
            text=True,
            timeout=30,
        )

    def calls(self):
        if not self.log.exists():
            return []
        return [
            line.split("\x1f")[:-1]
            for line in self.log.read_text().splitlines()
        ]

    def runs(self):
        return [c for c in self.calls() if c[:1] == ["run"]]

    def removed(self):
        return [c[2] for c in self.calls() if c[:2] == ["volume", "rm"]]

    def three(self, f6="", f5="", f2=""):
        # 0.6, 0.5 and 0.2 GiB: 1.3 GiB, over a 1 GiB bound by 0.3.
        self.volumes(
            (PREFIX + "cache-a-gomod", int(0.6 * GIB), f6),
            (PREFIX + "cache-a-gobuild", int(0.5 * GIB), f5),
            (PREFIX + "cache-b-golangci", int(0.2 * GIB), f2),
        )

    # (1) argument checks
    def test_bad_arguments_exit_2_and_run_nothing(self):
        cases = [
            ((), {}),
            (("", IMAGE), {}),
            (("tf/ci-", IMAGE), {}),
            (("tf ci-", IMAGE), {}),
            ((PREFIX, ""), {}),
            ((PREFIX, IMAGE, "x"), {}),
            ((PREFIX, IMAGE), {"IN_IMAGE_UID": "0"}),
            ((PREFIX, IMAGE), {"IN_IMAGE_UID": "abc"}),
            ((PREFIX, IMAGE), {"IN_IMAGE_UID": "-1"}),
        ]
        self.three()
        for args, env in cases:
            with self.subTest(args=args, env=env):
                self.log.unlink(missing_ok=True)
                p = self.run_trim(*args, **env)
                self.assertEqual(p.returncode, 2, p.stderr)
                self.assertEqual(self.calls(), [])

    # (2) the du container is locked down. An empty IN_IMAGE_UID is unset to
    # ${IN_IMAGE_UID:-10001}, so it runs as 10001, never as root.
    def test_every_run_is_confined(self):
        self.three()
        for uid in (None, "", "4242"):
            with self.subTest(uid=uid):
                self.log.unlink(missing_ok=True)
                env = {} if uid is None else {"IN_IMAGE_UID": uid}
                p = self.run_trim(PREFIX, IMAGE, "1", **env)
                self.assertEqual(p.returncode, 0, p.stderr)
                want_uid = uid or "10001"
                runs = self.runs()
                self.assertEqual(len(runs), 3)
                for r in runs:
                    joined = " ".join(r)
                    self.assertIn("--network none", joined)
                    self.assertIn("--cap-drop ALL", joined)
                    self.assertIn(
                        "--security-opt no-new-privileges:true", joined
                    )
                    self.assertIn(f"--user {want_uid}:{want_uid}", joined)
                    self.assertTrue(
                        any(a.endswith(":/c:ro") for a in r), r
                    )
                    self.assertIn(IMAGE, r)

    # (3) under the bound
    def test_under_bound_removes_nothing(self):
        self.three()
        p = self.run_trim(PREFIX, IMAGE, "2")
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertEqual(self.removed(), [])

    # (4) largest first, stop as soon as the total fits
    def test_over_bound_removes_largest_until_it_fits(self):
        self.three()
        p = self.run_trim(PREFIX, IMAGE, "1")
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertEqual(self.removed(), [PREFIX + "cache-a-gomod"])
        self.assertIn(f"trim  {PREFIX}cache-a-gomod (", p.stdout)

    def test_prefix_elsewhere_in_name_is_dropped(self):
        other = "old-" + PREFIX + "cache-a-gomod"
        self.volumes(
            (other, 5 * GIB, ""),
            (PREFIX + "cache-a-gomod", int(0.6 * GIB), ""),
        )
        p = self.run_trim(PREFIX, IMAGE, "1")
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertEqual(self.removed(), [])
        for r in self.runs():
            self.assertNotIn(f"{other}:/c:ro", r)
        self.assertNotIn(other, p.stdout)

    # (5) a volume in use is kept
    def test_in_use_largest_is_kept(self):
        self.three(f6="inuse")
        p = self.run_trim(PREFIX, IMAGE, "1")
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertIn(f"keep  {PREFIX}cache-a-gomod (", p.stdout)
        self.assertIn("in use)", p.stdout)
        self.assertEqual(self.removed(), [PREFIX + "cache-a-gobuild"])

    # (6) an unreadable size is neither counted nor removed
    def test_unreadable_size_is_warned_and_skipped(self):
        bad = PREFIX + "cache-a-bad"
        self.volumes(
            (bad, 9 * GIB, "unreadable"),
            (PREFIX + "cache-a-gomod", int(0.6 * GIB), ""),
            (PREFIX + "cache-a-gobuild", int(0.3 * GIB), ""),
        )
        p = self.run_trim(PREFIX, IMAGE, "1")
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertIn(f"warn  {bad}: size not read", p.stdout)
        self.assertEqual(self.removed(), [])

    # (7) a refused rm is warned and the next volume tried
    def test_refused_rm_is_warned_and_next_tried(self):
        self.three(f6="refuse")
        p = self.run_trim(PREFIX, IMAGE, "1")
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertIn(f"warn  {PREFIX}cache-a-gomod not removed", p.stdout)
        self.assertEqual(
            self.removed(),
            [PREFIX + "cache-a-gomod", PREFIX + "cache-a-gobuild"],
        )

    # (8) dry run removes nothing
    def test_dry_run_removes_nothing(self):
        self.three()
        p = self.run_trim(PREFIX, IMAGE, "1", REAP_DRY_RUN="1")
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertEqual(self.removed(), [])
        self.assertIn(
            f"dry   docker volume rm {PREFIX}cache-a-gomod (", p.stdout
        )


if __name__ == "__main__":
    unittest.main()
