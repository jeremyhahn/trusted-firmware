### Fixed

- A timed revert the HAL keeps refusing is still retried after 2^31 ms, and TIMER_QUERY keeps answering 0 ms left.
