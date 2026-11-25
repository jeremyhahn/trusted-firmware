### Fixed

- **`scripts/app_symbols.py` exits 2 on an ELF it cannot read or parse**
  (#635). A missing or unreadable path, and a symbol table whose string-table
  link names no section, escaped `main` as an uncaught exception and exited 1,
  the code esp32-build's no-application check accepts as "this image holds no
  application symbol", so the check passed on an image it never read. Both
  now print the error and exit 2, as a file that is not ELF already did.
