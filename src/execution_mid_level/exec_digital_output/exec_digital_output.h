/******************************************************************************
 *  File:       exec_digital_output.h
 *  Author:     Angus Corr
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Aggregate lifecycle and voltage configuration for the ten digital
 *      output channels.
 *
 *  Notes:
 *      Silkscreen channels are numbered 1 to 10. Their enum values remain
 *      zero-based for safe array indexing. Runtime port writes intentionally
 *      perform no lifecycle checks.
 ******************************************************************************/

#ifndef EXEC_DIGITAL_OUTPUT_H
#define EXEC_DIGITAL_OUTPUT_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "hw_gpio.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    EXEC_DIGITAL_OUTPUT_CHANNEL_1 = 0,
    EXEC_DIGITAL_OUTPUT_CHANNEL_2,
    EXEC_DIGITAL_OUTPUT_CHANNEL_3,
    EXEC_DIGITAL_OUTPUT_CHANNEL_4,
    EXEC_DIGITAL_OUTPUT_CHANNEL_5,
    EXEC_DIGITAL_OUTPUT_CHANNEL_6,
    EXEC_DIGITAL_OUTPUT_CHANNEL_7,
    EXEC_DIGITAL_OUTPUT_CHANNEL_8,
    EXEC_DIGITAL_OUTPUT_CHANNEL_9,
    EXEC_DIGITAL_OUTPUT_CHANNEL_10,
    EXEC_DIGITAL_OUTPUT_CHANNEL_COUNT
} ExecDigitalOutputChannel_T;

typedef enum
{
    EXEC_DIGITAL_OUTPUT_MODE_3V3 = 0,
    EXEC_DIGITAL_OUTPUT_MODE_5V,
    EXEC_DIGITAL_OUTPUT_MODE_12V,
    EXEC_DIGITAL_OUTPUT_MODE_24V,
    EXEC_DIGITAL_OUTPUT_MODE_COUNT
} ExecDigitalOutputMode_T;

typedef struct
{
    bool                    is_enabled;
    ExecDigitalOutputMode_T mode;
    bool                    initial_high;
} ExecDigitalOutputChannelConfig_T;

typedef struct
{
    ExecDigitalOutputChannelConfig_T channels[EXEC_DIGITAL_OUTPUT_CHANNEL_COUNT];
} ExecDigitalOutputConfig_T;

/**
 * @brief Configure all ten channels without starting the subsystem.
 *
 * Enabled channels retain their requested voltage and initial signal level.
 * Disabled channels ignore their other fields and use the safe 3.3 V
 * selection. All MCU output signals remain low after successful configuration.
 * Reconfiguration is rejected while started.
 *
 * @param config Complete ten-channel configuration.
 * @return true when configuration was accepted and applied.
 */
bool EXEC_DIGITAL_OUTPUT_Configure( const ExecDigitalOutputConfig_T* config );

/**
 * @brief Start the configured subsystem using the retained initial states.
 * @return true when transitioning from configured to started.
 */
bool EXEC_DIGITAL_OUTPUT_Start( void );

/**
 * @brief Drive all ten output signals low while retaining configuration.
 * @return true when transitioning from started to configured.
 */
bool EXEC_DIGITAL_OUTPUT_Stop( void );

/** @brief Return true when the subsystem is configured or started. */
bool EXEC_DIGITAL_OUTPUT_Is_Configured( void );

/** @brief Return true only while the subsystem is started. */
bool EXEC_DIGITAL_OUTPUT_Is_Started( void );

/**
 * @brief Translate logical GPIO output identities on one port into a physical pin mask.
 *
 * Callers must pass a non-empty array whose pins share one physical GPIO port.
 */
DigitalOutputPinmask_T EXEC_DIGITAL_OUTPUT_Combine_Port_Pin_Masks( GPIOOutput_T* gpio_names,
                                                                   uint8_t       length );

/**
 * @brief Directly set pins identified by a physical digital-output port mask.
 *
 * The mask must come from the HW GPIO mapping; its bit positions are physical
 * GPIO pins, not zero-based digital-output channel numbers. This execution-time
 * path intentionally performs no lifecycle checks.
 */
void EXEC_DIGITAL_OUTPUT_Set_Output( uint32_t pin_mask );

/**
 * @brief Directly reset pins identified by a physical digital-output port mask.
 *
 * The mask must come from the HW GPIO mapping; its bit positions are physical
 * GPIO pins, not zero-based digital-output channel numbers. This execution-time
 * path intentionally performs no lifecycle checks.
 */
void EXEC_DIGITAL_OUTPUT_Reset_Output( uint32_t pin_mask );

#ifdef __cplusplus
}
#endif

#endif /* EXEC_DIGITAL_OUTPUT_H */
