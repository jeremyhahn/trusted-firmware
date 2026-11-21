### Changed

- **The OPTIGA Trust M datasheet is pinned and the element's supply range
  settled** (#511). `docs/reference/README.md` lists the *OPTIGA™ Trust M
  Datasheet*, Revision 3.70, by SHA-256, and the onboarding ADR's §2 Supply
  item cites its pages: VCC and VCC_I2C 1.62 V to 5.5 V, the bus at most
  VCC + 0.5 V or 5.5 V. The shield's own supply path stays to confirm.
