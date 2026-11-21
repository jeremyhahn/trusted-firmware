# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
"""Unit tests for scripts/lwip_upstream_check.py's archive_blobs (make scripts-test).

archive_blobs reads the lwIP release archive in memory, a gzipped tar or a zip
(told apart by the zip's PK\\x03\\x04 magic), and keys each file by its path
under the archive's top directory, split at the first slash only. Each case
here builds its archive with tarfile or zipfile in io.BytesIO. No network.

LWIP_UPSTREAM_CHECK names another copy of the script (a mutation run).
"""

import importlib.util
import io
import os
import tarfile
import unittest
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.environ.get("LWIP_UPSTREAM_CHECK", os.path.join(HERE, "lwip_upstream_check.py"))
spec = importlib.util.spec_from_file_location("lwip_upstream_check", SCRIPT)
luc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(luc)

PATHS = ["README", "src/core/tcp.c", "src/api/api.c", "COPYING"]
WANT = {"README": b"r", "src/core/tcp.c": b"tcp", "src/api/api.c": None, "COPYING": None}


def tar_gz():
    """top/, top/README, top/src/core/tcp.c, a bare COPYING, a directory
    top/src/api and a symlink top/COPYING -> README."""
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:gz") as t:
        def entry(name, kind=tarfile.REGTYPE, data=b"", link=""):
            info = tarfile.TarInfo(name)
            info.type = kind
            info.linkname = link
            info.size = len(data)
            t.addfile(info, io.BytesIO(data) if data else None)

        entry("top/", tarfile.DIRTYPE)
        entry("top/README", data=b"r")
        entry("top/src/core/tcp.c", data=b"tcp")
        entry("COPYING", data=b"bare")
        entry("top/src/api", tarfile.DIRTYPE)
        entry("top/COPYING", tarfile.SYMTYPE, link="README")
    return buf.getvalue()


def zip_bytes():
    """top/, top/README, top/src/core/tcp.c and a bare COPYING."""
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as z:
        z.writestr("top/", b"")
        z.writestr("top/README", b"r")
        z.writestr("top/src/core/tcp.c", b"tcp")
        z.writestr("COPYING", b"bare")
    return buf.getvalue()


class TestArchiveBlobs(unittest.TestCase):
    def test_tar_gz(self):
        self.assertEqual(luc.archive_blobs(tar_gz(), PATHS), WANT)

    def test_zip(self):
        data = zip_bytes()
        self.assertEqual(data[:4], b"PK\x03\x04")
        self.assertEqual(luc.archive_blobs(data, PATHS), WANT)

    def test_no_paths(self):
        for name, data in (("tar.gz", tar_gz()), ("zip", zip_bytes())):
            with self.subTest(archive=name):
                self.assertEqual(luc.archive_blobs(data, []), {})


if __name__ == "__main__":
    unittest.main()
