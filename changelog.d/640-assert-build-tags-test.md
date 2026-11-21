### Added

- **`scripts/assert-build-tags.sh` has a test** (#640): `scripts/test_assert_build_tags.py`, over a stub `go`, pins that an empty tag set passes, a tag set that changes nothing fails, each tag is checked by removal (a conjunction passes, a masked sibling fails) and files are compared, not counts. `make scripts-test` runs it.
