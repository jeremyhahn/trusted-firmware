### Added

- **The i.MX 95 M7's tamper-response port for A1 and A2** (#50 slice 5).
  `src/hal/cortex-m/imx95/hsm_port.c` implements `tfw_hsm_port_act` with SCMI
  messages to the System Manager. A1 (PHYs off) holds the PHYs' reset domain
  asserted with RESET and switches their power domain off with
  POWER_STATE_SET, each where its id is set. A2 (A55 off) switches the A55's
  power domain off. A reply's status maps as `tfw_scmi_system_power_status`
  maps it. The channel and the ids in `boards/frdm-imx95-m7/board_profile.h`
  are zero, meaning unsupported, until imx95 question 21 names them. So on the
  board today every action answers `TFW_ERR_UNSUPPORTED` and sends nothing.
  `test_imx95_hsm_port` pins each action's bytes in the register model's shared
  memory, using test ids.
