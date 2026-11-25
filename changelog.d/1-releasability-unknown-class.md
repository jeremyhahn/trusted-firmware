### Fixed

- **The releasability gate's refusal of an unknown class is tested to name the class and to fail the whole list, even after a rule that matched** (#1): `make releasability-test` now checks the refusal message names the unknown class, not only exit 2, which the gate's other list refusals share; a second case puts the unknown class after a rule that matches a planted line, so a bad line anywhere in the list must stop the gate before it scans.
