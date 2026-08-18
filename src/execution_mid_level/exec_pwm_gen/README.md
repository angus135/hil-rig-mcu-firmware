# exec_pwm_gen

## Overview

`exec_pwm_gen` owns the execution-layer lifecycle and board-level voltage selection for the
independent low-voltage (LV) and high-voltage (HV) PWM outputs. Timer peripheral control is
delegated to `hw_pwm_gen`.

The LV and HV channels may be configured and started simultaneously. Their valid voltage ranges
are independent:

| Channel | Supported voltages |
|---|---|
| LV | 3.3 V, 5 V |
| HV | 12 V, 24 V |

## Lifecycle

Each channel has its own strict lifecycle:

1. Configure validates and preloads the initial ARR, CCR, and PSC values, selects the channel
   voltage, and configures the HW timer without starting it.
2. Start enables the fully prepared timer output.
3. Stop disables the timer output while retaining configuration and voltage selection.
4. Configuring with `is_enabled` false stops an active channel, selects its lowest voltage, and
   enters the disabled state.

Invalid lifecycle transitions and dependency failures return false without advancing state.

## Logic Expander Mapping

PWM voltage selection uses `LOGIC_EXPANDER_PWM_SPI`, port A:

| Bit | Signal | Behavior |
|---:|---|---|
| 4 | `PWM_GEN_HV_12V` | High selects the HV 12 V path |
| 5 | `PWM_GEN_HV_24V` | High selects the HV 24 V path |
| 6 | `PWM_GEN_LV_VSEL` | Low selects 3.3 V; high selects 5 V |

The HV bits are driven mutually exclusively. The disabled safe selections are 3.3 V for LV and
12 V for HV. Configuring one channel does not change the other channel's selection.

The module currently sends staged Logic Expander changes directly. This direct send should be
removed when global configuration commits all staged subsystem changes.

## Execution Path

`EXEC_PWM_GEN_Set_PWM_LV()` and `EXEC_PWM_GEN_Set_PWM_HV()` directly forward precomputed timer
values to the HW layer. They intentionally perform no lifecycle or parameter checks so the
execution-time path remains lean. Callers must configure and start the relevant channel before
execution.
