### Added

- **`test_rearming_with_the_other_level_flips_the_revert_level` and `test_rearming_to_a_shorter_duration_shortens_the_deadline` in `test/unit/test_sched.c`** (#627): a TIMER_SET re-arm of a held pin with the other level drives the new level, TIMER_QUERY answers the new held level, and the sweep reverts to the opposite of the new level (both directions); a re-arm to a shorter duration moves the deadline earlier, so a re-arm in `tfw_sched_cmd_set` in `src/caps/sched.c` that kept the old revert level or the later deadline now fails the host suite.
