### Fixed

- **The lwIP import audit matches the make targets** (#629).
  `docs/third_party/lwip-import.md` no longer says `make lwip-check` runs
  offline at every build: it runs `make lwip-upstream-check` first and no
  build depends on it. The page names the upstream check's archive mode
  (`LWIP_ARCHIVE_SHA256`) and git mode, and counts the 145 compared files.
