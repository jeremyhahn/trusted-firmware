#!/usr/bin/env python3
"""lwip_upstream_check's archive_blobs reads one top directory only: an
archive whose entries sit under more than one top directory fails."""

import contextlib
import hashlib
import importlib.util
import io
import os
import tarfile
import tempfile
import unittest
import zipfile
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("lwip_upstream_check", os.path.join(HERE, "lwip_upstream_check.py"))
luc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(luc)

TWO_TOPS = [("lwip-REL/README", b"real"), ("other/COPYING", b"stray"), ("other/README", b"stray")]


def tar_gz(entries):
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:gz") as t:
        for name, body in entries:
            info = tarfile.TarInfo(name)
            info.size = len(body)
            t.addfile(info, io.BytesIO(body))
    return buf.getvalue()


def zip_of(entries):
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as z:
        for name, body in entries:
            z.writestr(name, body)
    return buf.getvalue()


class ArchiveOneTopTest(unittest.TestCase):
    def test_tar_two_tops_raises(self):
        with self.assertRaises(ValueError) as cm:
            luc.archive_blobs(tar_gz(TWO_TOPS), ["README", "COPYING"])
        self.assertIn("lwip-REL", str(cm.exception))
        self.assertIn("other", str(cm.exception))

    def test_zip_two_tops_raises(self):
        with self.assertRaises(ValueError) as cm:
            luc.archive_blobs(zip_of(TWO_TOPS), ["README", "COPYING"])
        self.assertIn("lwip-REL", str(cm.exception))
        self.assertIn("other", str(cm.exception))

    def test_one_top_and_bare_file(self):
        data = tar_gz([("lwip-REL/README", b"r"), ("lwip-REL/src/a.c", b"a"), ("COPYING", b"bare")])
        got = luc.archive_blobs(data, ["README", "src/a.c", "COPYING"])
        self.assertEqual(got, {"README": b"r", "src/a.c": b"a", "COPYING": None})

    def test_main_archive_two_tops_fails(self):
        data = tar_gz(TWO_TOPS)
        want = hashlib.sha256(data).hexdigest()
        with tempfile.TemporaryDirectory() as root:
            for name in ("README", "COPYING"):
                with open(os.path.join(root, name), "wb") as f:
                    f.write(b"stray")
            os.makedirs(os.path.join(root, "src"))
            with open(os.path.join(root, "src", "a.c"), "wb") as f:
                f.write(b"stray")
            out = io.StringIO()
            with mock.patch.object(luc, "fetch_archive", return_value=data), contextlib.redirect_stdout(out):
                rc = luc.main_archive(root, "https://example.invalid/lwip.tar.gz", want)
        self.assertEqual(rc, 1)
        self.assertIn("lwip-upstream-check: FAIL: https://example.invalid/lwip.tar.gz:", out.getvalue())


if __name__ == "__main__":
    unittest.main()
