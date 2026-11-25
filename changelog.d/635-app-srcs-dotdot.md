### Fixed

- **An APP_SRCS entry with `..` anywhere is refused** (#635). `mk/app.mk` refused only an entry that started with `/` or `../`, so `sub/../../x.c` passed and its object left the application's object directory.
