### Fixed

- `scripts/ci-mask-lan.sh` classifies a host name case-insensitively and ignores one terminal dot, as the handbook template's v3 does: an internal host written in upper case or fully qualified (`cache.INTERNAL`, `registry.lan.`, `buildcache.`) is now masked, and an upper-case keyword (`DIRECT`) no longer is; the masks keep the entry's own spelling (#611).
