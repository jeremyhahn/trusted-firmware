### Fixed

- Four make comments now name what main builds and links: mk/release.mk's SOURCE_COMMIT note (no image built from this tree writes /etc/ci-source-commit) and provenance-one's NOLOAD note (mk/handoff.ld on AVR, the Cortex-M linker scripts' .handoff section), mk/board-restore.mk's bench page (mega2560.md, esp32-p4.md), and mk/platform-bake.mk's BOARD_ROWS mix (ETH rows on the 2560, peripheral rows on Cortex-M) (#629).
