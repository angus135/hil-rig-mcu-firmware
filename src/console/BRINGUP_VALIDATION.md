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

## Flash Manager instruction-path validation

The following sequence is destructive to previously stored diagnostic
instructions and results. Run it only when overwriting those partitions is
acceptable:

```text
flash init
flash upload_test
flash prepare
flash execute_echo 100
```

`flash upload_test` writes a deterministic framing-compatible stream containing
one 20-byte instruction per tick: an eight-byte little-endian header followed by
12 opaque, word-aligned diagnostic operation bytes. The opaque bytes test Flash
Manager transport and ISR echo framing; they are not dispatched through
Execution Manager operation adapters. Upload transport chunks may split headers,
operations, and NAND pages. `flash prepare` must preload the instruction buffer
successfully, and `flash execute_echo 100` must consume the stream without an
alignment, corruption, or underrun fault before reporting its result summary.

Use `flash status` between stages if a state transition does not complete. The
separate `flash external_test` command exercises External Flash directly and
does not validate the canonical Execution Manager instruction format.

## Execution Manager digital-output path

Choose a digital-output silkscreen channel from 1 through 10. The following
example holds LOW for one second, HIGH for three seconds, then returns LOW at
100 Hz on channel 1:

```text
flash init
flash status
run_state status
run_state receive
run_state status
test_config inert
test_config digital_output 1 3v3 low
test_config status
flash upload_do_test 1 100 300
run_state configure
run_state status
run_state frequency 100
run_state execute 400 0
run_state status
run_state discard
run_state status
```

Replace `1` with the intended output channel. The command uses
`EXEC_DIGITAL_OUTPUT_Combine_Port_Pin_Masks()` to resolve the channel to its
physical GPIO mask before creating the instruction. Probe the downstream output
rather than relying only on an indicator LED.

`upload_do_test` uses the public Flash Manager upload API to store two canonical
instructions. Tick zero is the configured LOW initial condition. Tick 100
requests the mask high and tick 400 requests it low.
The RSM execution request prepares Flash Manager and the Execution Manager,
starts configured DUT drivers, and owns TIM4. The 400 ticks run through
`EXECUTION_MANAGER_ProcessTickFromISR()`, the zero-copy operation walker, the
digital-output adapter, and the production digital-output driver. Terminal ISR
notification inhibits further dispatch; RSM task context then stops TIM4 and
the drivers and finalises the empty result stream.

Expected lifecycle outcome is `RESULTS_READY` with TIM4 and the DUT lifecycle
stopped. Expected electrical behavior is LOW for one second, HIGH for three
seconds, then LOW again. The execution path currently
produces no measurement records, so `run_state discard` releases the empty
result session and returns the system to IDLE.

Repeat at 1 kHz with delay/high values 1000/3000, then at 10 kHz with
10000/30000 to retain the same waveform while increasing ISR frequency.

## Execution Manager LV PWM-output path

Perform this first PWM test on the LV channel only, with no DUT attached. Probe
the channel-1 LV PWM output and use 3.3 V selection. From a fresh power-on, the
following sequence starts at 1 Hz and 50% duty, then changes to 2 Hz and 75%
duty for the second four-second interval:

```text
flash init
flash status
run_state status
run_state receive
test_config inert
test_config pwm_generation 1 3v3 1 500
test_config status
flash upload_pwm_test 1 2 750 390 800
run_state configure
run_state status
run_state frequency 100
run_state execute 800 0
run_state status
run_state discard
run_state status
```

The two console preparation commands accept physical frequency and duty. They
call `HW_PWM_GEN_compute_psc()`, `HW_PWM_GEN_compute_arr()`, and
`HW_PWM_GEN_compute_ccr()` outside execution, then place the resulting timer
values in the initial configuration and canonical PWM instruction. The ISR
does not calculate them.

Tick 390 requests the update at 3.90 seconds. The timer's ARR, CCR, and PSC
preloads become active together at the next natural 1 Hz update event, expected
near 4.00 seconds. Scheduling the register write slightly before four seconds
avoids forcing a timer update or resetting PWM phase. The run ends at tick 800,
eight seconds after execution starts.

On an oscilloscope, expect approximately four 1 Hz cycles with 0.5 seconds HIGH
and 0.5 seconds LOW, followed by approximately eight 2 Hz cycles with 0.375
seconds HIGH and 0.125 seconds LOW. A DC multimeter cannot verify frequency or
duty precisely. It may average the 3.3 V waveform near 1.65 V initially and
near 2.48 V after the update, or visibly fluctuate because these frequencies
are slow relative to its sampling/filtering. Use the meter only as a coarse
change indicator and use an oscilloscope or logic analyser for sign-off.

The expected terminal state is `RESULTS_READY`, with TIM4 and the PWM channel
stopped. `run_state discard` releases the empty result session and returns the
RSM and Flash Manager to IDLE for another run.

## Execution Manager analogue-output path

This sequence exercises one prepared DAC frame through the host-side voltage
conversion, Flash Manager instruction storage, Execution Manager adapter, and
the production analogue-output SPI/DAC path. Select `internal` or `external`
to match the fitted DAC reference and probe board-labelled analogue-output
channel 1 (driver channel 0) with respect to board ground:

```text
flash init
run_state reset
run_state receive
test_config inert
test_config analogue_output internal
flash upload_ao_test 0 10.0 100 300
run_state configure
run_state frequency 100
run_state execute 300 0
```

The DAC is initialized to zero during configuration. At tick 100 (one second
at 100 Hz), the instruction applies the prepared 10 V request to driver
channel 0 (board-labelled channel 1);
the output remains at that value until the run completes at tick 300. The
measured voltage is approximately half of the selected DAC full-scale range,
because the execution input range is 0-20 V. The exact voltage depends on the
configured reference and analogue-output scaling. The expected terminal state
is `RESULTS_READY`; use `run_state discard` to return to IDLE for another run.

`upload_ao_test` calls `EXEC_ANALOGUE_OUTPUT_Prepare_Frame()` before storing the
canonical operation. The ISR receives only the three-byte prepared DAC frame;
it performs no voltage conversion or validation.

## Execution Manager CAN-output path

This sequence exercises one classical CAN data frame through canonical flash
storage, the Execution Manager adapter, the existing CAN transmit validation,
and the hardware transmit queue. Begin at 100 Hz with a conservative bitrate
and connect a correctly terminated CAN receiver before enabling the channel:

```text
flash init
run_state reset
run_state receive
test_config inert
test_config can 1 500000 0 0 0
flash upload_can_test 1 0x123 0xAA 8 100 300
run_state configure
run_state frequency 100
run_state execute 300 0
```

At tick 100 (one second at 100 Hz), channel 1 transmits one standard frame
with identifier `0x123`, eight `0xAA` data bytes, and DLC 8. The current CAN
driver performs its normal packet validation and copies the frame into its
transmit queue. A driver or queue rejection terminates the execution as a
fault; no retry is performed in the ISR. The expected terminal state is
`RESULTS_READY`; use `run_state discard` after inspection.

The optional `repeat_count interval_ticks` arguments create additional
single-frame instructions at strictly increasing ticks. Keep the interval
large enough for the configured CAN bitrate and bus arbitration. This command
does not yet perform CAN schedule-feasibility admission, so do not use it as a
10 kHz validation sequence.

## Execution Manager peak output-ISR timing

`flash upload_output_stress [sample_count] [interval_ticks]` commits a predefined
configuration and uploads repeated peak-load instructions. The command enables
only digital output 1, both PWM outputs, both UART channels, and SPI channel 2.
CAN, SPI channel 1, analogue output, and every measurement input remain
disabled. Each scheduled instruction contains six operations on the same tick:
one digital update, two PWM updates, one 128-byte SPI transmit, and one 16-byte
transmit for each UART channel. The stress-only configuration runs SPI2 at the
driver's 45 Mbit/s setting and both UART channels at 2 Mbit/s.

The default interval is one tick and the profiling sequence runs at 10 kHz.
The SPI and UART payloads are sized below their nominal per-tick wire capacity,
allowing every execution ISR to exercise every configured output path without
deliberately filling the DMA-backed transmit queues. Use the maximum rather than
latest cycle count reported by `run_state status`.

This upload also enables one-shot per-opcode cycle profiling for its next run.
`run_state status` reports sample count, average cycles, and maximum cycles for
each exercised opcode. These values include the profiler's two cycle-counter
reads and accumulation. The overall ISR maximum from a profiled run likewise
includes profiling overhead; use an unprofiled run for the final deadline figure.

From an IDLE run state and IDLE Flash Manager:

```text
flash upload_output_stress
run_state receive
run_state configure
run_state frequency 10000
run_state execute 110 0
run_state status
```

The default upload contains 100 sustained-load instructions at ticks 1 through 100,
followed by ten drain ticks before execution ends at tick 110. After result
finalisation completes, status should report 110 ISR samples and
per-opcode timing for all exercised output paths. The measurement includes
higher-priority interrupt preemption but excludes the optional FreeRTOS yield
at the end of TIM4.

## Execution Manager UART-output path

This is a coarse multimeter-visible test of the real UART instruction and DMA
path. It does not verify individual UART bits. Probe the UART channel-1 TX pin
with respect to board ground and configure its external interface for 3.3 V:

```text
flash init
flash status
run_state status
run_state receive
test_config inert
test_config uart 1 3v3 1500 tx
test_config status
flash upload_uart_test 1 0 256 200 800 3 172
run_state configure
run_state status
run_state frequency 100
run_state execute 800 0
run_state status
run_state discard
run_state status
```

Wait for `run_state status` to report `ARMED` before requesting execution. Tick
200 queues the first 256-byte `0x00` payload at two seconds. Two more ordinary
UART instructions follow at 172-tick intervals. At 1500 baud using the
configured 8-N-1 framing, each burst lasts approximately
`256 * 10 / 1500 = 1.71 seconds`. The 1.72-second start interval leaves roughly
13 ms between bursts, which a multimeter should average out.

UART idles HIGH, so the meter should initially indicate approximately 3.3 V.
Every `0x00` frame contains one LOW start bit, eight LOW data bits, and one HIGH
stop bit. During the sustained burst the ideal DC average is therefore about
0.33 V. A real meter will respond slowly, but it should show a conspicuous
drop for just over five seconds and then rise toward 3.3 V again. This confirms
scheduled dispatch and sustained TX activity only; use a logic analyser or
oscilloscope to verify baud rate, framing, byte values, and exact timing.

`upload_uart_test` accepts a variable payload length and fills the complete raw
UART payload with the selected byte. Its optional repeat count and interval
create separate, strictly increasing instructions; they do not bypass the UART
driver's normal queue. The command limits construction only to
what fits safely in its canonical instruction staging buffer. It does not
perform UART ring-capacity or schedule-feasibility analysis. If the driver
cannot atomically accept the complete payload at runtime, the adapter reports
operation rejection and the Execution Manager terminates the run.

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
