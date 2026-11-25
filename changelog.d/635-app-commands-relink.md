### Fixed

- Changing APP_COMMANDS now relinks the firmware and simulator images, so a hook missing or defined twice fails the build instead of the old binary being reused (#635).
