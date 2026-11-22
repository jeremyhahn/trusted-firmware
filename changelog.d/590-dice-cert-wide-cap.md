### Fixed

- **A host test covers the page-cap clamp of `tfw_dice_alias_cert`** (#590).
  A caller's cap of 256, 300 or 1024 bytes gets one full page of
  TFW_DICE_PAGE_MAX (255) bytes and nothing past it; a cap cut to its low
  byte would have returned an empty or a 44-byte page with TFW_OK.
