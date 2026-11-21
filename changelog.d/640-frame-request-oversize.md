### Fixed

- **The framer refuses a request whose payload exceeds `TFW_MAX_PAYLOAD`**
  (#640). `TFW_MAX_LEN` leaves room for a response's STATUS byte, so a
  hand-built request of that LEN with a good CRC was READY with a 65-byte
  payload no encoder produces; the dispatcher then refused it per command. It
  is now `TFW_FRAME_BAD_FORMAT` and counted in `err_format`. Responses of the
  same LEN are unchanged. No wire format, status code or vector changed.
