### Fixed

- **The i.MX 95 HSM intake no longer drops a back-to-back S1 or S5 event**
  (#50). A tamper notification or hotplug pin interrupt that arrived in the
  pass right after another was reported high two passes running, and the loop,
  which raises only a rising line, raised one signal for two events.
  `tfw_imx95_hsm_intake_sense` now holds such an edge for the pass after its
  bit drops low, so it is raised late but never lost.
