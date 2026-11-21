### Fixed

- **`docs/wire-protocol/user.md` activation examples carry a signature** (#629): `ota-activate` and `ota-stage --activate=now` now pass `--sig-file=`, and the page names `OTA_AUTH` and the `TFW_ERR_AUTH_REQUIRED` refusal (#137); as written before, tfwctl refused every activation and sent nothing.
