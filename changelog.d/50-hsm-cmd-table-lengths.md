### Added

- **Every HSM channel command's payload length is pinned in the table**
  (#50). A unit test checks the commands.def rows of HSM_HEARTBEAT,
  HSM_REPORT, HSM_VERDICT, HSM_SELECT and HSM_LOAD, so an empty verdict or
  selection stays refused by the dispatcher before its handler would read the
  missing byte as clear or as policy 0.
