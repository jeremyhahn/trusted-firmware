### Added

- **`scripts/test_qmp_regs_edges.py`** (#640): pins the paths of `test/integration/cortex-m/qmp_regs.py` `scripts/test_qmp_regs.py` does not reach: an `elf=` that cannot be read, an `elf=` read only on a `sym=`, a second `elf=` replacing the first, a size-0 object, a QMP error with no `desc`, a gdbstub with no `target.xml` or that hangs up, and an annex included twice.
