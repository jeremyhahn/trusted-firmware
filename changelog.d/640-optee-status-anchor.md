### Fixed

- **`make optee-run` reads a guest marker only once its line has ended** (#640): a status split across two console reads (`TFW-GUEST-STATUS=1`, then `27`) exits 127, not 1, and a split refusal names the whole reason.
