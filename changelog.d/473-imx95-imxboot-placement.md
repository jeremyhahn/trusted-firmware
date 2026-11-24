### Added

- **A host test pins where the i.MX 95 M7 application is linked** (#473).
  scripts/test_provenance.py reads the link flags make gives
  frdm-imx95-m7: the default build links the application at the 0x00008000
  slot, and IMX_BOOT=1 links it at ITCM 0, where the System Manager starts
  the boot container's M7 image. Before, the IMX_BOOT tests checked only the
  measurement's base, so an image measured from ITCM 0 but linked at the slot
  passed.
