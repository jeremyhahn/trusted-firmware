### Changed

- **TIMER_SET's duration bound is pinned at both edges** (#627): an `ms` of exactly `0x7FFFFFFF` is accepted and reverts on time across the millisecond wrap, and `0x80000000` and above are refused with `TFW_ERR_RANGE`, arming nothing.
