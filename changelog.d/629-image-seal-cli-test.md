### Added

- **A host test pins the image-seal command line** (#629). `test_image_seal_cli`
  runs `tools/image-seal/main.c` as `make image-tbs` and `make image-seal` do:
  the usage on any other argument count or verb, the image_version and
  image_flags bounds, the file-size refusals, and the span, stream and region
  it writes, byte for byte seal.c's. `make host-test` runs it.
