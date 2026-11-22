### Fixed

- **The bootloader's PING refuses a payload longer than its response holds**
  (#640). `tfw_boot_session_handle` echoed up to 255 bytes into a response
  buffer of TFW_MAX_PAYLOAD; it now returns `TFW_ERR_TOO_LONG` and writes
  nothing.
