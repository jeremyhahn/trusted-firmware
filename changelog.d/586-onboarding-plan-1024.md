### Fixed

- **The STM32H573I-DK onboarding ADR and the #586 row plan name TF-M with our
  DICE partition** (#586). The onboarding ADR's ADR #15 summary still read
  open question 2 (Secure Manager or upstream TF-M) as open, its DICE map
  offered the Secure Manager without a marker, and the row plan's
  implementation slices read as our own NSC split. The ADR now reads the
  question closed by ruling 935 and ruling 1024, marks the Secure Manager
  candidates not taken, and the plan notes that under ruling 1024 slices (a)
  and (b) are TF-M's split and our DICE layer as a TF-M Application RoT
  partition, carried by row #608. Documentation only.
