# Copilot Instructions — Logan STM32 Lab

Target: STM32 NUCLEO-F334R8 controlling Logan.

## Core rules
- No polling loops. No `while(flag == 0)`, no busy-wait, no `HAL_Delay()` for control logic.
- Prefer interrupt-based design: TIM interrupts, EXTI/input-capture for PWM input, UART RX interrupt, ADC interrupt/DMA.
- Keep code simple and readable. Prefer explicit state machines and small functions over clever abstractions.
- Make incremental changes only. Do not rewrite working modules unless necessary.
- Keep modules separated:
  - `logan_spi`
  - `logan_control`
  - `step_timer`
  - `position_pwm`
  - `adc_speed`
  - `uart_cli`
  - `app_state`

## Logan constraints
- Step pin PB4, max 5 kHz.
- SPI: master, full-duplex, 8 bit, MSB first, 1 MBaud, CPOL=0, sample on rising edge, NSS low during transfer.
- Control byte must not be sent more often than every 4 ms.
- Position PWM inputs are asynchronous 125 Hz signals; measure each independently.
- ADC on PA7 controls speed.
- UART is for debug/control, not for blocking the main loop.

## Coding style
- ISR handlers must be short: capture data, set flags, update counters. Heavy logic stays in main state machine.
- Main loop should be event-driven: consume flags/events and advance state.
- Use named constants for pins, timings, bit masks, and limits.
- Add comments only where hardware behavior is non-obvious.
- Prefer clear C over complicated HAL call chains when direct code is easier to understand.

## Change discipline
- Before editing, identify the smallest module affected.
- Preserve existing behavior.
- Add one feature at a time and keep it testable with UART/log output.
- Never introduce blocking code to “quickly fix” timing problems.


