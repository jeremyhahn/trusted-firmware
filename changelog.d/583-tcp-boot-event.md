- hw-test: the BOOT-event row (`TestFirmwarePingDeliversTheBootEventWithNoBootWait`)
  runs on a board whose wire is TCP: `HW_RESET=cmd:<command>` names the command
  that resets it, run before the row dials and waits for BOOT; with none the row
  is skipped, "transport does not reset on open". Serial boards are unchanged
  (#583).
