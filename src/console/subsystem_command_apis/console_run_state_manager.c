/******************************************************************************
 *  File:       console_run_state_manager.c
 *  Author:     Callum Rafferty
 *  Created:    26-Aug-2026
 *
 *  Description:
 *      Console command interface for manual Run State Manager control.
 *
 *  Notes:
 *      State requests are asynchronous. An accepted request means it was
 *      delivered to the Run State Manager task, not that processing completed.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */
#include "console_run_state_manager.h"
#include "console.h"
#include "dut_driver_lifecycle.h"
#include "run_state_manager.h"
#include <stdbool.h>
#include <string.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */
static const char* CONSOLE_RunStateManager_StateName( RunState_T state );
static const char* CONSOLE_RunStateManager_FrequencyName( RunStateFrequencyMode_T frequency );
static const char* CONSOLE_RunStateManager_FaultName( RunStateFaultReason_T reason );
static const char* CONSOLE_RunStateManager_RequestName( RunStateRequest_T request );
static const char* CONSOLE_RunStateManager_RequestResultName( RunStateRequestResult_T result );
static void        CONSOLE_RunStateManager_PrintUsage( void );
static void        CONSOLE_RunStateManager_PrintRequestResult( bool accepted );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Converts a Run State Manager state to console text.
 *
 * @param state State to convert.
 *
 * @returns Constant state-name string.
 */
static const char* CONSOLE_RunStateManager_StateName( RunState_T state )
{
    switch ( state )
    {
        case RUN_STATE_IDLE:
            return "IDLE";
        case RUN_STATE_TEST_PACKAGE_RECEIVE:
            return "TEST_PACKAGE_RECEIVE";
        case RUN_STATE_CONFIGURATION:
            return "CONFIGURATION";
        case RUN_STATE_ARMED:
            return "ARMED";
        case RUN_STATE_EXECUTION:
            return "EXECUTION";
        case RUN_STATE_RESULT_FINALISATION:
            return "RESULT_FINALISATION";
        case RUN_STATE_RESULTS_READY:
            return "RESULTS_READY";
        case RUN_STATE_RESULT_TRANSFER:
            return "RESULT_TRANSFER";
        case RUN_STATE_FAULT:
            return "FAULT";
        default:
            return "UNKNOWN";
    }
}

static const char* CONSOLE_RunStateManager_FaultName( RunStateFaultReason_T reason )
{
    switch ( reason )
    {
        case RUN_STATE_FAULT_NONE:
            return "none";
        case RUN_STATE_FAULT_EXTERNAL_REQUEST:
            return "external request";
        case RUN_STATE_FAULT_INVALID_TRANSITION:
            return "invalid internal transition";
        case RUN_STATE_FAULT_LOGIC_EXPANDER_NOT_READY:
            return "logic expander not ready";
        case RUN_STATE_FAULT_CONFIGURATION_UNAVAILABLE:
            return "configuration unavailable";
        case RUN_STATE_FAULT_DRIVER_CONFIGURATION:
            return "DUT driver configuration";
        case RUN_STATE_FAULT_DRIVER_CONFIGURATION_TIMEOUT:
            return "DUT driver configuration timeout";
        case RUN_STATE_FAULT_DRIVER_START:
            return "DUT driver start";
        case RUN_STATE_FAULT_DRIVER_STOP:
            return "DUT driver stop";
        case RUN_STATE_FAULT_EXECUTION_TIMER:
            return "execution timer";
        case RUN_STATE_FAULT_FLASH_EXECUTION_PREPARATION:
            return "Flash execution preparation";
        case RUN_STATE_FAULT_FLASH_EXECUTION_PREPARATION_TIMEOUT:
            return "Flash execution preparation timeout";
        case RUN_STATE_FAULT_FLASH_RESULT_FINALISATION:
            return "Flash result finalisation";
        case RUN_STATE_FAULT_FLASH_RESULT_FINALISATION_TIMEOUT:
            return "Flash result finalisation timeout";
        case RUN_STATE_FAULT_FLASH_RESULT_TRANSFER:
            return "Flash result transfer";
        case RUN_STATE_FAULT_FLASH_RESULT_DISPOSITION:
            return "Flash result disposition";
        case RUN_STATE_FAULT_FLASH_MANAGER:
            return "Flash Manager";
        case RUN_STATE_FAULT_INTERNAL:
            return "internal RSM error";
        default:
            return "unknown";
    }
}

static const char* CONSOLE_RunStateManager_FrequencyName( RunStateFrequencyMode_T frequency )
{
    switch ( frequency )
    {
        case RUN_STATE_FREQUENCY_100HZ:
            return "100 Hz";
        case RUN_STATE_FREQUENCY_1KHZ:
            return "1 kHz";
        case RUN_STATE_FREQUENCY_10KHZ:
            return "10 kHz";
        default:
            return "UNKNOWN";
    }
}

static const char* CONSOLE_RunStateManager_RequestName( RunStateRequest_T request )
{
    switch ( request )
    {
        case RUN_STATE_REQUEST_NONE:
            return "none";
        case RUN_STATE_REQUEST_PACKAGE_RECEIVE:
            return "receive";
        case RUN_STATE_REQUEST_CONFIGURATION_READY:
            return "configure";
        case RUN_STATE_REQUEST_EXECUTION:
            return "execute";
        case RUN_STATE_REQUEST_EXECUTION_COMPLETE:
            return "execution_complete";
        case RUN_STATE_REQUEST_RESULT_TRANSFER:
            return "transfer";
        case RUN_STATE_REQUEST_RESULT_TRANSFER_COMPLETE:
            return "transfer_complete";
        case RUN_STATE_REQUEST_REPEAT:
            return "repeat";
        case RUN_STATE_REQUEST_DISCARD_RESULTS:
            return "discard";
        case RUN_STATE_REQUEST_FAULT:
            return "fault";
        case RUN_STATE_REQUEST_RESET:
            return "reset";
        case RUN_STATE_REQUEST_DIAGNOSTIC_TIMER_START:
            return "timer start";
        case RUN_STATE_REQUEST_DIAGNOSTIC_TIMER_STOP:
            return "timer stop";
        default:
            return "unknown";
    }
}

static const char*
CONSOLE_RunStateManager_RequestResultName( RunStateRequestResult_T result )
{
    switch ( result )
    {
        case RUN_STATE_REQUEST_RESULT_NONE:
            return "none";
        case RUN_STATE_REQUEST_RESULT_ACCEPTED:
            return "accepted";
        case RUN_STATE_REQUEST_RESULT_REJECTED_STATE:
            return "rejected (invalid state)";
        case RUN_STATE_REQUEST_RESULT_REJECTED_PENDING:
            return "rejected (transition pending)";
        case RUN_STATE_REQUEST_RESULT_REJECTED_SUBSYSTEM_STATE:
            return "rejected (subsystem not ready)";
        case RUN_STATE_REQUEST_RESULT_FAILED:
            return "failed";
        default:
            return "unknown";
    }
}

/**
 * @brief Prints the Run State Manager command usage.
 */
static void CONSOLE_RunStateManager_PrintUsage( void )
{
    CONSOLE_Printf( "Usage:\r\n" );
    CONSOLE_Printf( "  run_state status\r\n" );
    CONSOLE_Printf( "  run_state <receive|configure|execute|execution_complete>\r\n" );
    CONSOLE_Printf( "  run_state <transfer|transfer_complete|repeat|discard|fault|reset>\r\n" );
    CONSOLE_Printf( "  run_state diagnostic_timer <start|stop>\r\n" );
}

/**
 * @brief Prints whether an asynchronous state request was accepted.
 *
 * @param accepted Whether the request was delivered to the task.
 */
static void CONSOLE_RunStateManager_PrintRequestResult( bool accepted )
{
    if ( accepted )
    {
        CONSOLE_Printf( "Run state request queued.\r\n" );
    }
    else
    {
        CONSOLE_Printf( "Run state request rejected: task is not ready.\r\n" );
    }
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

void CONSOLE_RunStateManager_Command( uint16_t argc, char* argv[] )
{
    if ( argc == 3U && strcmp( argv[1], "diagnostic_timer" ) == 0 )
    {
        if ( strcmp( argv[2], "start" ) == 0 )
        {
            CONSOLE_RunStateManager_PrintRequestResult(
                RUN_STATE_MANAGER_RequestDiagnosticExecutionTimerStart() );
        }
        else if ( strcmp( argv[2], "stop" ) == 0 )
        {
            CONSOLE_RunStateManager_PrintRequestResult(
                RUN_STATE_MANAGER_RequestDiagnosticExecutionTimerStop() );
        }
        else
        {
            CONSOLE_RunStateManager_PrintUsage();
        }
        return;
    }

    if ( argc != 2U )
    {
        CONSOLE_RunStateManager_PrintUsage();
        return;
    }

    if ( strcmp( argv[1], "status" ) == 0 )
    {
        RunStateManagerStatus_T     run_status    = { 0 };
        DutDriverLifecycleStatus_T driver_status = { 0 };
        RUN_STATE_MANAGER_GetStatus( &run_status );
        DUT_DRIVER_LIFECYCLE_GetStatus( &driver_status );

        CONSOLE_Printf( "Run state: %s\r\n",
                        CONSOLE_RunStateManager_StateName( run_status.state ) );
        CONSOLE_Printf( "Transition pending: %s\r\n",
                        run_status.transition_pending ? "yes" : "no" );
        CONSOLE_Printf( "Execution active: %s\r\n",
                        run_status.execution_active ? "yes" : "no" );
        CONSOLE_Printf( "Execution timer: %s\r\n",
                        run_status.execution_timer_running ? "running" : "stopped" );
        CONSOLE_Printf( "Execution frequency: %s\r\n",
                        CONSOLE_RunStateManager_FrequencyName( run_status.execution_frequency ) );
        CONSOLE_Printf( "Fault reason: %s\r\n",
                        CONSOLE_RunStateManager_FaultName( run_status.fault_reason ) );
        CONSOLE_Printf( "Last request: %s\r\n",
                        CONSOLE_RunStateManager_RequestName( run_status.last_request ) );
        CONSOLE_Printf( "Last request result: %s\r\n",
                        CONSOLE_RunStateManager_RequestResultName(
                            run_status.last_request_result ) );
        CONSOLE_Printf(
            "DUT lifecycle: configured=%s, AI=%u/%u, AO=%u/%u, DI=%u/%u, DO=%u/%u\r\n",
            driver_status.configuration_valid ? "yes" : "no",
            ( unsigned int )driver_status.analogue_input_started,
            ( unsigned int )driver_status.analogue_input_enabled,
            ( unsigned int )driver_status.analogue_output_started,
            ( unsigned int )driver_status.analogue_output_enabled,
            ( unsigned int )driver_status.digital_inputs_started,
            ( unsigned int )driver_status.digital_inputs_enabled,
            ( unsigned int )driver_status.digital_outputs_started,
            ( unsigned int )driver_status.digital_outputs_enabled );
        CONSOLE_Printf(
            "DUT channel masks (started/enabled): CAN=%lX/%lX, PWM_IN=%lX/%lX, "
            "PWM_OUT=%lX/%lX, SPI=%lX/%lX, UART=%lX/%lX\r\n",
            ( unsigned long )driver_status.can_started_mask,
            ( unsigned long )driver_status.can_enabled_mask,
            ( unsigned long )driver_status.pwm_capture_started_mask,
            ( unsigned long )driver_status.pwm_capture_enabled_mask,
            ( unsigned long )driver_status.pwm_generation_started_mask,
            ( unsigned long )driver_status.pwm_generation_enabled_mask,
            ( unsigned long )driver_status.spi_started_mask,
            ( unsigned long )driver_status.spi_enabled_mask,
            ( unsigned long )driver_status.uart_started_mask,
            ( unsigned long )driver_status.uart_enabled_mask );
    }
    else if ( strcmp( argv[1], "receive" ) == 0 )
    {
        CONSOLE_RunStateManager_PrintRequestResult(
            RUN_STATE_MANAGER_RequestPackageReceive() );
    }
    else if ( strcmp( argv[1], "configure" ) == 0 )
    {
        CONSOLE_RunStateManager_PrintRequestResult(
            RUN_STATE_MANAGER_RequestConfiguration() );
    }
    else if ( strcmp( argv[1], "execute" ) == 0 )
    {
        CONSOLE_RunStateManager_PrintRequestResult( RUN_STATE_MANAGER_RequestExecution() );
    }
    else if ( strcmp( argv[1], "execution_complete" ) == 0 )
    {
        CONSOLE_RunStateManager_PrintRequestResult(
            RUN_STATE_MANAGER_RequestExecutionComplete() );
    }
    else if ( strcmp( argv[1], "transfer" ) == 0 )
    {
        CONSOLE_RunStateManager_PrintRequestResult(
            RUN_STATE_MANAGER_RequestResultTransfer() );
    }
    else if ( strcmp( argv[1], "transfer_complete" ) == 0 )
    {
        CONSOLE_RunStateManager_PrintRequestResult(
            RUN_STATE_MANAGER_RequestResultTransferComplete() );
    }
    else if ( strcmp( argv[1], "repeat" ) == 0 )
    {
        CONSOLE_RunStateManager_PrintRequestResult( RUN_STATE_MANAGER_RequestRepeat() );
    }
    else if ( strcmp( argv[1], "discard" ) == 0 )
    {
        CONSOLE_RunStateManager_PrintRequestResult(
            RUN_STATE_MANAGER_RequestDiscardResults() );
    }
    else if ( strcmp( argv[1], "fault" ) == 0 )
    {
        CONSOLE_RunStateManager_PrintRequestResult(
            RUN_STATE_MANAGER_RequestFault( RUN_STATE_FAULT_EXTERNAL_REQUEST ) );
    }
    else if ( strcmp( argv[1], "reset" ) == 0 )
    {
        CONSOLE_RunStateManager_PrintRequestResult( RUN_STATE_MANAGER_RequestReset() );
    }
    else
    {
        CONSOLE_RunStateManager_PrintUsage();
    }
}
