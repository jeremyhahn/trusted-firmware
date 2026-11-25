### Added

- **The 2560 store image's update slots are pinned** (#635). `test_sys_store`
  asserts that update_request_store.c serves TFW_OTA_SLOTS, that the runner
  starts an application declaring TFW_APP_OTA_SINGLE on that image, and that
  one asking for more slots than the image serves is refused as unsupported.
