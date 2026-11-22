### Fixed

- The bench bake image refuses a carried platform block whose serial is the all-zero unprovisioned sentinel, as tfwprov does, instead of writing it and reporting `result=baked` (#53).
