### Fixed

- **OTA_ACTIVATE's manifest check re-checks the staged trailer's image_len
  against the stream** (#640): a trailer whose image_len is raised past the
  bytes before it after OTA_END is refused with TFW_ERR_RANGE, as OTA_END
  refuses it, instead of reading the envelope from inside the trailer.
