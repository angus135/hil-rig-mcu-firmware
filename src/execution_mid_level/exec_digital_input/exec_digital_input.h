/******************************************************************************
 *  File:       exec_digital_input.h
 *  Author:     Coen Pasitchnyj
 *  Created:    6-April-2026
 *
 *  Description:
 *      Aggregate lifecycle and voltage-mode configuration for the ten
 *      execution-time digital-input channels.
 *
 *  Notes:
 *      Silkscreen channels are numbered 1 to 10 while enum values remain
 *      zero-based. Sampling intentionally performs no lifecycle checks.
 ******************************************************************************/

#ifndef EXEC_DIGITAL_INPUT_H
#define EXEC_DIGITAL_INPUT_H

#ifdef __cplusplus
extern "C"
{
#endif

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */
#include <stdbool.h>
#include <stdint.h>

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef enum
{
    EXEC_DIGITAL_INPUT_CHANNEL_1 = 0,
    EXEC_DIGITAL_INPUT_CHANNEL_2,
    EXEC_DIGITAL_INPUT_CHANNEL_3,
    EXEC_DIGITAL_INPUT_CHANNEL_4,
    EXEC_DIGITAL_INPUT_CHANNEL_5,
    EXEC_DIGITAL_INPUT_CHANNEL_6,
    EXEC_DIGITAL_INPUT_CHANNEL_7,
    EXEC_DIGITAL_INPUT_CHANNEL_8,
    EXEC_DIGITAL_INPUT_CHANNEL_9,
    EXEC_DIGITAL_INPUT_CHANNEL_10,
    EXEC_DIGITAL_INPUT_CHANNEL_COUNT
} ExecDigitalInputChannel_T;

typedef enum
{
    EXEC_DIGITAL_INPUT_MODE_DISABLED = 0,
    EXEC_DIGITAL_INPUT_MODE_3V3,
    EXEC_DIGITAL_INPUT_MODE_5V,
    EXEC_DIGITAL_INPUT_MODE_12V,
    EXEC_DIGITAL_INPUT_MODE_24V,
    EXEC_DIGITAL_INPUT_MODE_COUNT
} ExecDigitalInputMode_T;

typedef struct
{
    ExecDigitalInputMode_T channels[EXEC_DIGITAL_INPUT_CHANNEL_COUNT];
} ExecDigitalInputConfig_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Configure all ten channel modes and leave sampling stopped.
 * @return true when all selector states were staged and submitted.
 */
bool EXEC_DIGITAL_INPUT_Configure( const ExecDigitalInputConfig_T* config );

/** @brief Activate the configured physical input mask. */
bool EXEC_DIGITAL_INPUT_Start( void );

/** @brief Clear the active mask while retaining configuration. */
bool EXEC_DIGITAL_INPUT_Stop( void );

/** @brief Return true while configured or started. */
bool EXEC_DIGITAL_INPUT_Is_Configured( void );

/** @brief Return true only while sampling is started. */
bool EXEC_DIGITAL_INPUT_Is_Started( void );

/**
 * @brief Sample the active physical GPIOD input mask.
 *
 * The returned uint32_t retains physical GPIOD bit positions and raw GPIO
 * polarity. Only bits 0 to 15 are currently used.
 *
 * TODO: Consider changing the public sample and buffered storage type to
 * uint16_t to reduce capture-buffer and transfer size.
 */
void EXEC_DIGITAL_INPUT_Sample_All( uint32_t* destination );

#ifdef __cplusplus
}
#endif

#endif /* EXEC_DIGITAL_INPUT_H */
