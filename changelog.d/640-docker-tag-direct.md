### Added

- **`scripts/test_docker_tag_direct.py`, scripts/docker-tag.sh run directly**
  (#640). `make scripts-test` now pins the paths the make-driven tests never
  reach: the usage refusal, a clean tag, docker's literal `<no value>` for an
  unlabelled image (two unlabelled images of other ids are not the same
  inputs), a target that resolves to no image, and the refusal line's text.
