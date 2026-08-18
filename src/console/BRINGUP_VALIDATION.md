# Hardware and Branch Bring-up Validation

## Purpose

Use the diagnostic console to validate a new board, board revision, or firmware
branch one peripheral at a time. The objective is to confirm that firmware
configuration, board-control signals, peripheral operation, physical signals,
and safe stopped states agree with the schematic.

This procedure complements automated tests. LEDs are useful indicators, but a
correct LED alone does not prove that the downstream electrical signal is
correct.

## Safety and prerequisites

Before applying power:

- Record the firmware commit, branch, board revision, board serial number, and
  test equipment used.
- Review the schematic signal names, expected voltage levels, logic polarity,
  relevant Logic Expander bits, connector pins, and testpoints.
- Inspect the board and check resistance between each supply rail and ground.
- Use a current-limited bench supply with conservative voltage and current
  limits for initial power-up.
- Establish a common measurement ground and use suitably rated probes.
- Disconnect or protect external DUTs until board-generated voltages and
  signal directions have been verified.
- Define the expected safe state before exercising each peripheral.

Stop testing immediately if current consumption, temperature, rail voltage, or
signal level is outside the expected range.

## Initial baseline

1. Power the board without starting any execution-level peripheral.
2. Verify all supply rails and record total current consumption.
3. Record the initial LED states.
4. Confirm that externally driven outputs, enables, chip selects, and power
   switches are in their documented safe states.
5. Initialize the Logic Expanders and verify that the expected devices respond.
6. Check that the console is responsive and record the console startup output.

If the baseline is not safe and repeatable, do not proceed to peripheral
testing.

## Per-peripheral validation

Test only one peripheral, and preferably one channel, at a time. Use the normal
execution-layer lifecycle exposed by its console command.

### 1. Configure

1. Choose a conservative, deterministic configuration.
2. Record the exact console command and expected board-control state.
3. Run the peripheral's configuration command without starting it.
4. Confirm that the command succeeds and that lifecycle status reports
   configured but stopped where status is available.
5. Record the LEDs associated with the selected channel, voltage, mode, or
   routing configuration.
6. Measure static configuration signals at their testpoints where possible,
   including enables, voltage-select lines, mux selections, chip selects, and
   Logic Expander outputs.
7. Verify that no runtime waveform or unintended output activity is present.

Record the correct LED pattern and measured static levels as the reference for
that configuration. Do not rely on an LED when its downstream signal can also
be measured.

### 2. Start

1. Run the peripheral's Start command.
2. Confirm that lifecycle status reports started where status is available.
3. Verify that the expected enable and power-control signals transition.
4. Confirm that unrelated channels and shared-port signals do not change.
5. Check supply current and component temperature again.

### 3. Exercise the data path

Use a simple, bounded stimulus that is easy to identify with test equipment:

- Digital or PWM output: use a slow repeating pattern or distinctive duty
  cycle and verify it at the output pin.
- Analogue output: begin at zero, then use one low, known voltage and measure
  it with a meter before testing additional levels.
- Digital or analogue input: apply known safe levels and compare console reads
  against physical measurements.
- UART, SPI, I2C, or CAN: begin at a conservative rate using loopback or a known
  responding device. Verify protocol timing and signal levels with an
  oscilloscope or logic analyser.
- PWM capture: apply a known waveform and compare reported frequency and duty
  cycle with the source and oscilloscope.

For write paths, confirm the signal at the final accessible output pin rather
than only at the MCU. For read paths, confirm the applied signal at the board
input or comparator output as appropriate.

### 4. Stop and safe state

1. Stop traffic and allow any queued operation to complete.
2. Run the peripheral's Stop command.
3. Verify that runtime activity ceases and that configuration is retained when
   that is the documented lifecycle contract.
4. Confirm the physical output, enables, power switches, and Logic Expander
   controls return to the documented safe stopped state.
5. Verify that unrelated channels remain unchanged.
6. Start the channel again and repeat one simple transaction to validate the
   stopped-to-started transition without reconfiguration.
7. Where supported, apply a disabled configuration and confirm the stronger
   disabled safe state and cleared configured status.

## Coverage and fault checks

After the basic path passes:

- Repeat for every physical channel and supported mode or voltage selection.
- Check boundary configurations without exceeding board or DUT ratings.
- Verify channel isolation by monitoring neighbouring and shared signals.
- Confirm invalid console arguments are rejected without changing hardware.
- Confirm Start before Configure and repeated Start/Stop calls return the
  documented result.
- Exercise recovery only with a controlled, understood fault and confirm the
  post-recovery lifecycle state.
- Power-cycle and repeat a representative configuration to check that results
  are deterministic.

## Evidence record

Use one row per configuration or meaningful lifecycle transition.

| Field | Record |
|---|---|
| Date and operator | |
| Firmware branch and commit | |
| Board revision and serial number | |
| Peripheral and channel | |
| Exact console command | |
| Expected lifecycle state | |
| Expected LED pattern | |
| Observed LED pattern | |
| Testpoint or connector pin | |
| Expected voltage, timing, or waveform | |
| Measured result | |
| Supply current | |
| Stop/disabled safe-state result | |
| Pass/fail and issue reference | |

Attach scope captures, logic-analyser traces, photographs, and console logs to
the corresponding record when they materially support the result.

## Completion criteria

A peripheral is ready for broader system testing when:

- Configure, Start, Stop, and restart behavior matches its documented
  lifecycle.
- Static board-control signals match the schematic and recorded LED patterns.
- Data-path behavior is verified at a downstream physical point where
  accessible.
- Stopped and disabled states are electrically safe.
- Other channels and shared signals are not disturbed.
- Results are repeatable after a power cycle.
- Any unavailable measurement or provisional safe-state assumption is recorded
  as an explicit follow-up item.

