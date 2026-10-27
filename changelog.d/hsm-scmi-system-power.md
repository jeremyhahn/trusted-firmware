### Added

- **SYSTEM_POWER_STATE_SET in the i.MX 95 M7's SCMI client** (#50 slice 6,
  action A5). `tfw_scmi_system_power_state_set` encodes the System power
  management protocol's command from Arm DEN0056E §4.4: protocol 0x12,
  message 0x3, then flags (bit 0 graceful, the rest zero) and system_state.
  `tfw_scmi_system_power_status` maps the statuses the command lists to this
  firmware's: SUCCESS, INVALID_PARAMETERS, NOT_SUPPORTED and DENIED become
  `TFW_OK`, `TFW_ERR_RANGE`, `TFW_ERR_UNSUPPORTED` and
  `TFW_ERR_PROTECTED_REGION`. Any other status reads as unrecognised,
  `TFW_ERR_INTERNAL`. The host tests pin the command's bytes as literals and
  check its layout in the register model's shared memory. Its values still need
  checking against the text of DEN0056E.
