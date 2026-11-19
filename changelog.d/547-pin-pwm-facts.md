### Fixed

- **The Pin and PWM hw-test rows drive the board's own PWM pin** (#547): they
  took pin 9, the AVR's, which on the ESP32-P4 PoE is an ADC input, so LEDC
  PWM was never measured there. Each board's pin facts name its PWM pin (9 on
  the AVR boards, LEDC0 on the ESP32-P4), and the tick-pin refusal runs only
  on a board that spends a PWM pin on its tick
  (`sdk/go/cmd/tfwctl/e2e_pins_test.go`).
