### Fixed

- **The OTA user guide's activate and confirm steps carry the row key's
  signature** (#640). docs/ota/user.md shows `--sig-file` on `ota-activate`
  and `ota-stage --activate`, lists `TFW_ERR_AUTH_REQUIRED` among the
  refusals, and confirms with `tfwctl ota-confirm` instead of saying tfwctl
  has no verb for it.
