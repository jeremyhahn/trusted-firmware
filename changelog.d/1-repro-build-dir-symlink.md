### Fixed

- **`scripts/repro_build.py` refuses two `--dir` that resolve to one directory
  through a symlink, and its refusals of either `--dir` are tested by
  message** (#1). Both builds used to run at one path, so a path-dependent
  image could be reported reproducible; the two directories are now compared
  after resolving symlinks, and it exits 2 before anything is built.
