/******************************************************************************
 *  File:       dut_driver_lifecycle.c
 *  Author:     Callum Rafferty
 *  Created:    26-Aug-2026
 *
 *  Description:
 *      Coordinates DUT-facing driver lifecycle operations for a test run.
 *
 *  Notes:
 *      This module applies one complete test configuration, retains only the
 *      derived enable plan and actual started state, and performs no execution
 *      tick reads or writes.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "dut_driver_lifecycle.h"
#include "logic_expander.h"
#include "rtos_config.h"
#include <stdint.h>
#include <string.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

#define DUT_DRIVER_LIFECYCLE_CHANNEL_BIT( channel ) ( 1UL << ( channel ) )

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef struct
{
    bool     analogue_input;
    bool     analogue_output;
    bool     digital_inputs;
    bool     digital_outputs;
    uint32_t can_channels;
    uint32_t pwm_capture_channels;
    uint32_t pwm_generation_channels;
    uint32_t spi_channels;
    uint32_t uart_channels;
} DutDriverLifecycleSelection_T;

typedef struct
{
    bool                          configuration_valid;
    bool                          configuration_pending;
    DutDriverConfiguration_T      configuration;
    bool                          start_accepted;
    LogicExpanderControlBatchId_T configuration_batch_id;
    LogicExpanderControlBatchId_T start_batch_id;
    LogicExpanderControlBatchId_T shutdown_batch_id;
    bool                          shutdown_active;
    bool                          shutdown_forced;
    bool                          shutdown_clear_configuration;
    bool                          shutdown_batch_sealed;
    bool                          shutdown_disabled_applied;
    DutDriverLifecycleSelection_T enabled;
    DutDriverLifecycleSelection_T started;
} DutDriverLifecycleContext_T;

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

static DutDriverLifecycleContext_T lifecycle_context;

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

static bool
DUT_DRIVER_LIFECYCLE_AnyDigitalInputEnabled( const ExecDigitalInputConfig_T* configuration );
static bool
DUT_DRIVER_LIFECYCLE_AnyDigitalOutputEnabled( const ExecDigitalOutputConfig_T* configuration );
static void DUT_DRIVER_LIFECYCLE_BuildEnablePlan( const DutDriverConfiguration_T* configuration );
static bool DUT_DRIVER_LIFECYCLE_ConfigureAll( const DutDriverConfiguration_T* configuration );
static bool DUT_DRIVER_LIFECYCLE_ApplyDisabledConfiguration( void );
static bool DUT_DRIVER_LIFECYCLE_AnyDriverStarted( void );
static bool DUT_DRIVER_LIFECYCLE_StopAttempt( bool force_abort, bool* busy );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

static bool
DUT_DRIVER_LIFECYCLE_AnyDigitalInputEnabled( const ExecDigitalInputConfig_T* configuration )
{
    for ( uint32_t channel = 0U; channel < EXEC_DIGITAL_INPUT_CHANNEL_COUNT; channel++ )
    {
        if ( configuration->channels[channel] != EXEC_DIGITAL_INPUT_MODE_DISABLED )
        {
            return true;
        }
    }

    return false;
}

static bool
DUT_DRIVER_LIFECYCLE_AnyDigitalOutputEnabled( const ExecDigitalOutputConfig_T* configuration )
{
    for ( uint32_t channel = 0U; channel < EXEC_DIGITAL_OUTPUT_CHANNEL_COUNT; channel++ )
    {
        if ( configuration->channels[channel].is_enabled )
        {
            return true;
        }
    }

    return false;
}

static void DUT_DRIVER_LIFECYCLE_BuildEnablePlan( const DutDriverConfiguration_T* configuration )
{
    DutDriverLifecycleSelection_T enabled = { 0 };

    enabled.analogue_input  = configuration->analogue_input.is_enabled;
    enabled.analogue_output = configuration->analogue_output.is_enabled;
    enabled.digital_inputs =
        DUT_DRIVER_LIFECYCLE_AnyDigitalInputEnabled( &configuration->digital_inputs );
    enabled.digital_outputs =
        DUT_DRIVER_LIFECYCLE_AnyDigitalOutputEnabled( &configuration->digital_outputs );

    for ( uint32_t channel = 0U; channel < EXEC_CAN_CHANNEL_COUNT; channel++ )
    {
        if ( configuration->can_channels[channel].is_enabled )
        {
            enabled.can_channels |= DUT_DRIVER_LIFECYCLE_CHANNEL_BIT( channel );
        }
    }

    for ( uint32_t channel = 0U; channel < TEST_CONFIGURATION_PWM_CAPTURE_CHANNEL_COUNT; channel++ )
    {
        if ( configuration->pwm_capture_channels[channel].is_enabled )
        {
            enabled.pwm_capture_channels |= DUT_DRIVER_LIFECYCLE_CHANNEL_BIT( channel );
        }
    }

    for ( uint32_t channel = 0U; channel < EXEC_PWM_GEN_CHANNEL_COUNT; channel++ )
    {
        if ( configuration->pwm_generation_channels[channel].is_enabled )
        {
            enabled.pwm_generation_channels |= DUT_DRIVER_LIFECYCLE_CHANNEL_BIT( channel );
        }
    }

    for ( uint32_t channel = 0U; channel < TEST_CONFIGURATION_SPI_CHANNEL_COUNT; channel++ )
    {
        if ( configuration->spi_channels[channel].is_enabled )
        {
            enabled.spi_channels |= DUT_DRIVER_LIFECYCLE_CHANNEL_BIT( channel );
        }
    }

    for ( uint32_t channel = 0U; channel < EXEC_UART_CHANNEL_COUNT; channel++ )
    {
        if ( configuration->uart_channels[channel].is_enabled )
        {
            enabled.uart_channels |= DUT_DRIVER_LIFECYCLE_CHANNEL_BIT( channel );
        }
    }

    lifecycle_context.enabled = enabled;
}

static bool DUT_DRIVER_LIFECYCLE_ConfigureAll( const DutDriverConfiguration_T* configuration )
{
    if ( !EXEC_ANALOGUE_INPUT_Configure( &configuration->analogue_input )
         || !EXEC_ANALOGUE_OUTPUT_Configure( &configuration->analogue_output )
         || !EXEC_DIGITAL_INPUT_Configure( &configuration->digital_inputs )
         || !EXEC_DIGITAL_OUTPUT_Configure( &configuration->digital_outputs ) )
    {
        return false;
    }

    for ( uint32_t channel = 0U; channel < EXEC_CAN_CHANNEL_COUNT; channel++ )
    {
        if ( EXEC_CAN_Configure_Channel( ( EXEC_CAN_Channel_T )channel,
                                         &configuration->can_channels[channel] )
             != EXEC_CAN_RESULT_OK )
        {
            return false;
        }
    }

    /* The known I2C hardware fault requires both channels to remain disabled. */
    const EXECI2CChannelConfig_T disabled_i2c = { 0 };
    for ( uint32_t channel = 0U; channel < EXEC_I2C_CHANNEL_COUNT; channel++ )
    {
        if ( EXEC_I2C_Configure_Channel( ( ExecI2CChannel_T )channel, &disabled_i2c )
             != EXEC_I2C_STATUS_OK )
        {
            return false;
        }
    }

    for ( uint32_t channel = 0U; channel < TEST_CONFIGURATION_PWM_CAPTURE_CHANNEL_COUNT; channel++ )
    {
        if ( !EXEC_PWM_Capture_Configure_Channel( ( ExecPwmCaptureChannel_T )channel,
                                                  &configuration->pwm_capture_channels[channel] ) )
        {
            return false;
        }
    }

    for ( uint32_t channel = 0U; channel < EXEC_PWM_GEN_CHANNEL_COUNT; channel++ )
    {
        if ( !EXEC_PWM_GEN_Configure_Channel( ( ExecPwmGenChannel_T )channel,
                                              &configuration->pwm_generation_channels[channel] ) )
        {
            return false;
        }
    }

    for ( uint32_t channel = 0U; channel < TEST_CONFIGURATION_SPI_CHANNEL_COUNT; channel++ )
    {
        if ( !EXEC_SPI_Configure_Channel( ( ExecSPIChannel_T )channel,
                                          &configuration->spi_channels[channel] ) )
        {
            return false;
        }
    }

    for ( uint32_t channel = 0U; channel < EXEC_UART_CHANNEL_COUNT; channel++ )
    {
        if ( !EXEC_UART_Configure_Channel( ( ExecUartChannel_T )channel,
                                           &configuration->uart_channels[channel] ) )
        {
            return false;
        }
    }

    return true;
}

static bool DUT_DRIVER_LIFECYCLE_ApplyDisabledConfiguration( void )
{
    const DutDriverConfiguration_T disabled = { 0 };
    return DUT_DRIVER_LIFECYCLE_ConfigureAll( &disabled );
}

static bool DUT_DRIVER_LIFECYCLE_AnyDriverStarted( void )
{
    const DutDriverLifecycleSelection_T* started = &lifecycle_context.started;

    return started->analogue_input || started->analogue_output || started->digital_inputs
           || started->digital_outputs || started->can_channels != 0U
           || started->pwm_capture_channels != 0U || started->pwm_generation_channels != 0U
           || started->spi_channels != 0U || started->uart_channels != 0U;
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

bool DUT_DRIVER_LIFECYCLE_Configure( const DutDriverConfiguration_T* configuration )
{
    LogicExpanderControlBatchId_T batch_id = 0U;

    if ( configuration == NULL )
    {
        return false;
    }

    if ( LOGIC_EXPANDER_Begin_Control_Batch( &batch_id ) != LOGIC_EXPANDER_STATUS_OK )
    {
        return false;
    }

    if ( !DUT_DRIVER_LIFECYCLE_Stop() )
    {
        LOGIC_EXPANDER_Cancel_Control_Batch( batch_id );
        return false;
    }

    ( void )memset( &lifecycle_context, 0, sizeof( lifecycle_context ) );
    lifecycle_context.configuration          = *configuration;
    lifecycle_context.configuration_batch_id = batch_id;

    if ( !DUT_DRIVER_LIFECYCLE_ConfigureAll( &lifecycle_context.configuration ) )
    {
        /* A full expander queue is backpressure, not configuration failure. */
        lifecycle_context.configuration_pending = true;
        lifecycle_context.configuration_valid   = true;
        DUT_DRIVER_LIFECYCLE_BuildEnablePlan( configuration );
        return true;
    }

    if ( LOGIC_EXPANDER_End_Control_Batch( batch_id ) != LOGIC_EXPANDER_STATUS_OK )
    {
        LOGIC_EXPANDER_Cancel_Control_Batch( batch_id );
        return false;
    }

    DUT_DRIVER_LIFECYCLE_BuildEnablePlan( configuration );
    lifecycle_context.configuration_valid = true;
    return true;
}

DutDriverConfigurationStatus_T DUT_DRIVER_LIFECYCLE_GetConfigurationStatus( void )
{
    if ( !lifecycle_context.configuration_valid )
    {
        return DUT_DRIVER_CONFIGURATION_FAILED;
    }

    if ( lifecycle_context.configuration_pending )
    {
        if ( !DUT_DRIVER_LIFECYCLE_ConfigureAll( &lifecycle_context.configuration ) )
        {
            return DUT_DRIVER_CONFIGURATION_PENDING;
        }

        if ( LOGIC_EXPANDER_End_Control_Batch( lifecycle_context.configuration_batch_id )
             != LOGIC_EXPANDER_STATUS_OK )
        {
            return DUT_DRIVER_CONFIGURATION_PENDING;
        }
        lifecycle_context.configuration_pending = false;
    }

    const LogicExpanderControlBatchStatus_T batch_status =
        LOGIC_EXPANDER_Get_Control_Batch_Status( lifecycle_context.configuration_batch_id );
    switch ( batch_status )
    {
        case LOGIC_EXPANDER_CONTROL_BATCH_COMPLETE:
            break;

        case LOGIC_EXPANDER_CONTROL_BATCH_OPEN:
        case LOGIC_EXPANDER_CONTROL_BATCH_PENDING:
            return DUT_DRIVER_CONFIGURATION_PENDING;

        case LOGIC_EXPANDER_CONTROL_BATCH_FAILED:
        case LOGIC_EXPANDER_CONTROL_BATCH_UNKNOWN:
        default:
            return DUT_DRIVER_CONFIGURATION_FAILED;
    }

    if ( lifecycle_context.enabled.analogue_output )
    {
        switch ( EXEC_ANALOGUE_OUTPUT_Get_State() )
        {
            case EXEC_ANALOGUE_OUTPUT_STATE_CONFIGURING:
                return DUT_DRIVER_CONFIGURATION_PENDING;

            case EXEC_ANALOGUE_OUTPUT_STATE_CONFIGURED:
                break;

            case EXEC_ANALOGUE_OUTPUT_STATE_FAULTED:
            case EXEC_ANALOGUE_OUTPUT_STATE_DISABLED:
            case EXEC_ANALOGUE_OUTPUT_STATE_STARTED:
            default:
                return DUT_DRIVER_CONFIGURATION_FAILED;
        }
    }

    return DUT_DRIVER_CONFIGURATION_READY;
}

bool DUT_DRIVER_LIFECYCLE_Start( void )
{
    LogicExpanderControlBatchId_T batch_id = 0U;

    if ( !lifecycle_context.configuration_valid || DUT_DRIVER_LIFECYCLE_AnyDriverStarted() )
    {
        return false;
    }

    if ( LOGIC_EXPANDER_Begin_Control_Batch( &batch_id ) != LOGIC_EXPANDER_STATUS_OK )
    {
        return false;
    }

    const DutDriverLifecycleSelection_T* enabled = &lifecycle_context.enabled;
    DutDriverLifecycleSelection_T*       started = &lifecycle_context.started;

    if ( enabled->analogue_input )
    {
        if ( !EXEC_ANALOGUE_INPUT_Start() )
        {
            goto start_failed;
        }
        started->analogue_input = true;
    }

    if ( enabled->digital_inputs )
    {
        if ( !EXEC_DIGITAL_INPUT_Start() )
        {
            goto start_failed;
        }
        started->digital_inputs = true;
    }

    for ( uint32_t channel = 0U; channel < TEST_CONFIGURATION_PWM_CAPTURE_CHANNEL_COUNT; channel++ )
    {
        const uint32_t channel_bit = DUT_DRIVER_LIFECYCLE_CHANNEL_BIT( channel );
        if ( ( enabled->pwm_capture_channels & channel_bit ) != 0U )
        {
            if ( !EXEC_PWM_Capture_Start_Channel( ( ExecPwmCaptureChannel_T )channel ) )
            {
                goto start_failed;
            }
            started->pwm_capture_channels |= channel_bit;
        }
    }

    for ( uint32_t channel = 0U; channel < EXEC_CAN_CHANNEL_COUNT; channel++ )
    {
        const uint32_t channel_bit = DUT_DRIVER_LIFECYCLE_CHANNEL_BIT( channel );
        if ( ( enabled->can_channels & channel_bit ) != 0U )
        {
            if ( EXEC_CAN_Start_Channel( ( EXEC_CAN_Channel_T )channel ) != EXEC_CAN_RESULT_OK )
            {
                goto start_failed;
            }
            started->can_channels |= channel_bit;
        }
    }

    for ( uint32_t channel = 0U; channel < TEST_CONFIGURATION_SPI_CHANNEL_COUNT; channel++ )
    {
        const uint32_t channel_bit = DUT_DRIVER_LIFECYCLE_CHANNEL_BIT( channel );
        if ( ( enabled->spi_channels & channel_bit ) != 0U )
        {
            if ( !EXEC_SPI_Start_Channel( ( ExecSPIChannel_T )channel ) )
            {
                goto start_failed;
            }
            started->spi_channels |= channel_bit;
        }
    }

    for ( uint32_t channel = 0U; channel < EXEC_UART_CHANNEL_COUNT; channel++ )
    {
        const uint32_t channel_bit = DUT_DRIVER_LIFECYCLE_CHANNEL_BIT( channel );
        if ( ( enabled->uart_channels & channel_bit ) != 0U )
        {
            if ( !EXEC_UART_Start_Channel( ( ExecUartChannel_T )channel ) )
            {
                goto start_failed;
            }
            started->uart_channels |= channel_bit;
        }
    }

    if ( enabled->digital_outputs )
    {
        if ( !EXEC_DIGITAL_OUTPUT_Start() )
        {
            goto start_failed;
        }
        started->digital_outputs = true;
    }

    for ( uint32_t channel = 0U; channel < EXEC_PWM_GEN_CHANNEL_COUNT; channel++ )
    {
        const uint32_t channel_bit = DUT_DRIVER_LIFECYCLE_CHANNEL_BIT( channel );
        if ( ( enabled->pwm_generation_channels & channel_bit ) != 0U )
        {
            if ( !EXEC_PWM_GEN_Start_Channel( ( ExecPwmGenChannel_T )channel ) )
            {
                goto start_failed;
            }
            started->pwm_generation_channels |= channel_bit;
        }
    }

    if ( enabled->analogue_output )
    {
        if ( !EXEC_ANALOGUE_OUTPUT_Start() )
        {
            goto start_failed;
        }
        started->analogue_output = true;
    }

    if ( LOGIC_EXPANDER_End_Control_Batch( batch_id ) != LOGIC_EXPANDER_STATUS_OK )
    {
        goto start_failed;
    }

    lifecycle_context.start_batch_id = batch_id;
    lifecycle_context.start_accepted = true;
    return true;

start_failed:
    ( void )DUT_DRIVER_LIFECYCLE_Stop();
    LOGIC_EXPANDER_Cancel_Control_Batch( batch_id );
    return false;
}

DutDriverStartStatus_T DUT_DRIVER_LIFECYCLE_GetStartStatus( void )
{
    if ( !lifecycle_context.start_accepted )
    {
        return DUT_DRIVER_START_FAILED;
    }

    switch ( LOGIC_EXPANDER_Get_Control_Batch_Status( lifecycle_context.start_batch_id ) )
    {
        case LOGIC_EXPANDER_CONTROL_BATCH_COMPLETE:
            return DUT_DRIVER_START_READY;

        case LOGIC_EXPANDER_CONTROL_BATCH_FAILED:
        case LOGIC_EXPANDER_CONTROL_BATCH_UNKNOWN:
            return DUT_DRIVER_START_FAILED;

        case LOGIC_EXPANDER_CONTROL_BATCH_OPEN:
        case LOGIC_EXPANDER_CONTROL_BATCH_PENDING:
        default:
            return DUT_DRIVER_START_PENDING;
    }
}

static bool DUT_DRIVER_LIFECYCLE_StopAttempt( bool force_abort, bool* busy )
{
    DutDriverLifecycleSelection_T* started = &lifecycle_context.started;
    bool                           failed  = false;
    *busy                                  = false;

    if ( started->analogue_output )
    {
        if ( !force_abort
             && EXEC_ANALOGUE_OUTPUT_Get_State() == EXEC_ANALOGUE_OUTPUT_STATE_FAULTED )
        {
            failed = true;
        }
        else if ( !force_abort && !EXEC_ANALOGUE_OUTPUT_Is_Transmission_Complete() )
        {
            *busy = true;
        }
        else if ( force_abort ? EXEC_ANALOGUE_OUTPUT_Abort() : EXEC_ANALOGUE_OUTPUT_Stop() )
        {
            started->analogue_output = false;
        }
        else
        {
            failed = true;
        }
    }

    for ( uint32_t channel = EXEC_PWM_GEN_CHANNEL_COUNT; channel > 0U; channel-- )
    {
        const uint32_t index       = channel - 1U;
        const uint32_t channel_bit = DUT_DRIVER_LIFECYCLE_CHANNEL_BIT( index );
        if ( ( started->pwm_generation_channels & channel_bit ) != 0U
             && EXEC_PWM_GEN_Stop_Channel( ( ExecPwmGenChannel_T )index ) )
        {
            started->pwm_generation_channels &= ~channel_bit;
        }
        else if ( ( started->pwm_generation_channels & channel_bit ) != 0U )
        {
            failed = true;
        }
    }

    if ( started->digital_outputs && EXEC_DIGITAL_OUTPUT_Stop() )
    {
        started->digital_outputs = false;
    }
    else if ( started->digital_outputs )
    {
        failed = true;
    }

    for ( uint32_t channel = EXEC_UART_CHANNEL_COUNT; channel > 0U; channel-- )
    {
        const uint32_t index       = channel - 1U;
        const uint32_t channel_bit = DUT_DRIVER_LIFECYCLE_CHANNEL_BIT( index );
        if ( ( started->uart_channels & channel_bit ) == 0U )
        {
            continue;
        }
        if ( !force_abort && !EXEC_UART_Is_Tx_Complete( ( ExecUartChannel_T )index ) )
        {
            *busy = true;
        }
        else if ( force_abort ? EXEC_UART_Abort_Channel( ( ExecUartChannel_T )index )
                              : EXEC_UART_Stop_Channel( ( ExecUartChannel_T )index ) )
        {
            started->uart_channels &= ~channel_bit;
        }
        else
        {
            failed = true;
        }
    }

    for ( uint32_t channel = TEST_CONFIGURATION_SPI_CHANNEL_COUNT; channel > 0U; channel-- )
    {
        const uint32_t index       = channel - 1U;
        const uint32_t channel_bit = DUT_DRIVER_LIFECYCLE_CHANNEL_BIT( index );
        if ( ( started->spi_channels & channel_bit ) == 0U )
        {
            continue;
        }
        if ( !force_abort && EXEC_SPI_Is_Transmission_Faulted( ( ExecSPIChannel_T )index ) )
        {
            failed = true;
        }
        else if ( !force_abort && !EXEC_SPI_Is_Transmission_Complete( ( ExecSPIChannel_T )index ) )
        {
            *busy = true;
        }
        else if ( force_abort ? EXEC_SPI_Abort_Channel( ( ExecSPIChannel_T )index )
                              : EXEC_SPI_Stop_Channel( ( ExecSPIChannel_T )index ) )
        {
            started->spi_channels &= ~channel_bit;
        }
        else
        {
            failed = true;
        }
    }

    for ( uint32_t channel = EXEC_CAN_CHANNEL_COUNT; channel > 0U; channel-- )
    {
        const uint32_t index       = channel - 1U;
        const uint32_t channel_bit = DUT_DRIVER_LIFECYCLE_CHANNEL_BIT( index );
        if ( ( started->can_channels & channel_bit ) == 0U )
        {
            continue;
        }
        const EXEC_CAN_Tx_Status_T tx_status =
            EXEC_CAN_Get_Tx_Status( ( EXEC_CAN_Channel_T )index );
        if ( !force_abort && tx_status == EXEC_CAN_TX_STATUS_ACTIVE )
        {
            *busy = true;
        }
        else if ( !force_abort
                  && ( tx_status == EXEC_CAN_TX_STATUS_ERROR
                       || tx_status == EXEC_CAN_TX_STATUS_INVALID_CHANNEL ) )
        {
            failed = true;
        }
        else if ( ( force_abort ? EXEC_CAN_Abort_Channel( ( EXEC_CAN_Channel_T )index )
                                : EXEC_CAN_Stop_Channel( ( EXEC_CAN_Channel_T )index ) )
                  == EXEC_CAN_RESULT_OK )
        {
            started->can_channels &= ~channel_bit;
        }
        else
        {
            failed = true;
        }
    }

    for ( uint32_t channel = TEST_CONFIGURATION_PWM_CAPTURE_CHANNEL_COUNT; channel > 0U; channel-- )
    {
        const uint32_t index       = channel - 1U;
        const uint32_t channel_bit = DUT_DRIVER_LIFECYCLE_CHANNEL_BIT( index );
        if ( ( started->pwm_capture_channels & channel_bit ) != 0U
             && EXEC_PWM_Capture_Stop_Channel( ( ExecPwmCaptureChannel_T )index ) )
        {
            started->pwm_capture_channels &= ~channel_bit;
        }
        else if ( ( started->pwm_capture_channels & channel_bit ) != 0U )
        {
            failed = true;
        }
    }

    if ( started->digital_inputs && EXEC_DIGITAL_INPUT_Stop() )
    {
        started->digital_inputs = false;
    }
    else if ( started->digital_inputs )
    {
        failed = true;
    }

    if ( started->analogue_input && EXEC_ANALOGUE_INPUT_Stop() )
    {
        started->analogue_input = false;
    }
    else if ( started->analogue_input )
    {
        failed = true;
    }

    lifecycle_context.start_accepted = false;
    return !failed;
}

bool DUT_DRIVER_LIFECYCLE_Stop( void )
{
    bool busy = false;
    return DUT_DRIVER_LIFECYCLE_StopAttempt( false, &busy ) && !busy
           && !DUT_DRIVER_LIFECYCLE_AnyDriverStarted();
}

bool DUT_DRIVER_LIFECYCLE_BeginShutdown( bool force_abort, bool clear_configuration )
{
    if ( lifecycle_context.shutdown_active )
    {
        lifecycle_context.shutdown_forced |= force_abort;
        lifecycle_context.shutdown_clear_configuration |= clear_configuration;
        return true;
    }

    LogicExpanderControlBatchId_T batch_id = 0U;
    if ( LOGIC_EXPANDER_Begin_Control_Batch( &batch_id ) != LOGIC_EXPANDER_STATUS_OK )
    {
        return false;
    }

    lifecycle_context.shutdown_active              = true;
    lifecycle_context.shutdown_forced              = force_abort;
    lifecycle_context.shutdown_clear_configuration = clear_configuration;
    lifecycle_context.shutdown_batch_sealed        = false;
    lifecycle_context.shutdown_disabled_applied    = false;
    lifecycle_context.shutdown_batch_id            = batch_id;
    return true;
}

DutDriverShutdownStatus_T DUT_DRIVER_LIFECYCLE_GetShutdownStatus( void )
{
    if ( !lifecycle_context.shutdown_active )
    {
        return DUT_DRIVER_SHUTDOWN_FAILED;
    }

    if ( !lifecycle_context.shutdown_batch_sealed )
    {
        bool busy = false;
        if ( !DUT_DRIVER_LIFECYCLE_StopAttempt( lifecycle_context.shutdown_forced, &busy ) )
        {
            return DUT_DRIVER_SHUTDOWN_FAILED;
        }
        if ( busy || DUT_DRIVER_LIFECYCLE_AnyDriverStarted() )
        {
            return DUT_DRIVER_SHUTDOWN_PENDING;
        }

        if ( lifecycle_context.shutdown_clear_configuration
             && !lifecycle_context.shutdown_disabled_applied )
        {
            if ( !DUT_DRIVER_LIFECYCLE_ApplyDisabledConfiguration() )
            {
                return DUT_DRIVER_SHUTDOWN_FAILED;
            }
            lifecycle_context.shutdown_disabled_applied = true;
        }

        if ( LOGIC_EXPANDER_End_Control_Batch( lifecycle_context.shutdown_batch_id )
             != LOGIC_EXPANDER_STATUS_OK )
        {
            return DUT_DRIVER_SHUTDOWN_FAILED;
        }
        lifecycle_context.shutdown_batch_sealed = true;
    }

    const LogicExpanderControlBatchStatus_T status =
        LOGIC_EXPANDER_Get_Control_Batch_Status( lifecycle_context.shutdown_batch_id );
    if ( status == LOGIC_EXPANDER_CONTROL_BATCH_COMPLETE )
    {
        const bool clear_configuration    = lifecycle_context.shutdown_clear_configuration;
        lifecycle_context.shutdown_active = false;
        if ( clear_configuration )
        {
            ( void )memset( &lifecycle_context, 0, sizeof( lifecycle_context ) );
        }
        return DUT_DRIVER_SHUTDOWN_COMPLETE;
    }
    if ( status == LOGIC_EXPANDER_CONTROL_BATCH_FAILED
         || status == LOGIC_EXPANDER_CONTROL_BATCH_UNKNOWN )
    {
        return DUT_DRIVER_SHUTDOWN_FAILED;
    }
    return DUT_DRIVER_SHUTDOWN_PENDING;
}

void DUT_DRIVER_LIFECYCLE_GetStatus( DutDriverLifecycleStatus_T* status )
{
    if ( status == NULL )
    {
        return;
    }

    taskENTER_CRITICAL();
    const DutDriverLifecycleContext_T context = lifecycle_context;
    taskEXIT_CRITICAL();

    const DutDriverLifecycleSelection_T* enabled = &context.enabled;
    const DutDriverLifecycleSelection_T* started = &context.started;

    *status = ( DutDriverLifecycleStatus_T ){
        .configuration_valid         = context.configuration_valid,
        .analogue_input_enabled      = enabled->analogue_input,
        .analogue_input_started      = started->analogue_input,
        .analogue_output_enabled     = enabled->analogue_output,
        .analogue_output_started     = started->analogue_output,
        .digital_inputs_enabled      = enabled->digital_inputs,
        .digital_inputs_started      = started->digital_inputs,
        .digital_outputs_enabled     = enabled->digital_outputs,
        .digital_outputs_started     = started->digital_outputs,
        .can_enabled_mask            = enabled->can_channels,
        .can_started_mask            = started->can_channels,
        .pwm_capture_enabled_mask    = enabled->pwm_capture_channels,
        .pwm_capture_started_mask    = started->pwm_capture_channels,
        .pwm_generation_enabled_mask = enabled->pwm_generation_channels,
        .pwm_generation_started_mask = started->pwm_generation_channels,
        .spi_enabled_mask            = enabled->spi_channels,
        .spi_started_mask            = started->spi_channels,
        .uart_enabled_mask           = enabled->uart_channels,
        .uart_started_mask           = started->uart_channels,
    };
}

void DUT_DRIVER_LIFECYCLE_EnterIdle( void )
{
    const bool stopped = DUT_DRIVER_LIFECYCLE_Stop();
    DUT_DRIVER_LIFECYCLE_ApplyDisabledConfiguration();
    if ( stopped )
    {
        ( void )memset( &lifecycle_context, 0, sizeof( lifecycle_context ) );
    }
}

void DUT_DRIVER_LIFECYCLE_EnterFault( void )
{
    const bool stopped = DUT_DRIVER_LIFECYCLE_Stop();
    DUT_DRIVER_LIFECYCLE_ApplyDisabledConfiguration();
    if ( stopped )
    {
        ( void )memset( &lifecycle_context, 0, sizeof( lifecycle_context ) );
    }

    /* DUT power shutdown remains pending the board-level power-control API. */
}
