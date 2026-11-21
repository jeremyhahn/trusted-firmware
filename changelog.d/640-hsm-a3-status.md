### Fixed

- **The HSM self-defence audit no longer says A3 is A2** (row `hsm-self-defence`). The i.MX 95 port answers `TFW_ERR_UNSUPPORTED` for A3 (`TFW_HSM_ACT_KEYS_LOCK`), so a policy naming A3 alone does not power the A55 off; `docs/hsm-self-defence/engineering.md` now names O5's fallback as proposed and A3's implementation and test as outstanding.
