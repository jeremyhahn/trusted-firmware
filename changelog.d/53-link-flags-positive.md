### Fixed

- tfwprov `--dry-run`, `--verify`, `--commit` and `--write-pubkey` refuse a zero or negative `--timeout` or `--baud` before the template is read or the port opens: a zero timeout failed the first PROV_INFO as a timeout and reported a working device as not running a PROVISION=1 image, and a zero baud ran at 115200 without a word (#53).
