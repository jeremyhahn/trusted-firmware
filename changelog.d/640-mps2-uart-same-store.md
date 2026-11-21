### Fixed

- **The MPS2 UART host test sees the ordinary interrupt acknowledgement**
  (Refs #640). Its register model logged a store only when it changed the cell,
  so INTCLEAR written with exactly INTSTATUS's bits was invisible; it now counts
  INTCLEAR handed out straight after the driver read INTSTATUS as a store, and
  two cases assert the plain TX and RX acknowledgements.
