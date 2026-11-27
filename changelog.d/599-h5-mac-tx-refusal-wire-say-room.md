### Added

- **Host tests pin what an STM32H5 MAC send refusal and a full diagnostic queue leave alone** (#599): a refused STM32H5 MAC send leaves the DMA's frame and descriptor alone; a diagnostic with no room behind a queued one is dropped whole.
