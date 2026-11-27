### Fixed

- Boot session margin drift (#640): `docs/secure-boot/api.md` and the `include/tfw/config.h` comment on the session ceiling now use the ATmega2560's 64 KB image: a ceiling of about 92 KB (45 KB on the 28 KB parts), about 1 100 frames and 10 s for a full update, and 55 ms of host latency per frame within the 60 s limit.
