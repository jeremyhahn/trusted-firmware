### Added

- **`scripts/test_ccdeps.py` pins `mk/ccdeps.sh`, the wrapper every host binary and tfwsim is compiled through** (#629): headers stay out of the compile, `-include` and `-imacros` keep their file, and the .d is written only after a good compile.
