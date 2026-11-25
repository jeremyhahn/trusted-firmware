### Fixed

- **`image-seal tbs` and `image-seal seal` name an empty image file** (#1):
  an image that opens but holds no bytes exited 1 with nothing on stderr, so
  `make image-tbs` / `make image-seal` failed silently; they now say
  `image-seal: <path> is empty`.
