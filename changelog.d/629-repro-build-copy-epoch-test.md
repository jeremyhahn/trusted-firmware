### Changed

- **scripts/repro_build.py's clean copy and commit time have a test of their own** (#629): the epoch is HEAD's committer time as a digit string and an empty repository fails; the copy holds the committed bytes only, in an emptied or newly made target.
