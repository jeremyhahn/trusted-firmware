### Fixed

- **An i.MX 95 tone moved to a new pin starts that pin on a fresh half
  period** (#627), as the mcxn94x tone does. The retarget drove the old pin
  low but kept its toggle phase, so the new pin's first match wrote low to a
  pin already low and its first half period was lost.
