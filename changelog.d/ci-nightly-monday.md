### Changed

- **The scheduled CI run fires on Monday night only.** The nightly cron is
  `0 4 * * 2` (early Tuesday UTC, the end of Monday night) instead of every
  night, per this project's slot in the nightly rotation (#322).
