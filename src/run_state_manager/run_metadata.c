/******************************************************************************
 *  File:       run_metadata.c
 *
 *  Description:
 *      Owns reset, first-cause latching, capture, and sealing of run metadata.
 ******************************************************************************/

#include "run_metadata.h"

#include <stddef.h>
#include <string.h>

#define RUN_METADATA_EXECUTION_VALID_FLAGS                                                   \
    ( RUN_METADATA_VALID_LAST_COMPLETED_BOUNDARY | RUN_METADATA_VALID_ISR_TIMING              \
      | RUN_METADATA_VALID_INSTRUCTION_BUFFER | RUN_METADATA_VALID_RESULT_BUFFER              \
      | RUN_METADATA_VALID_FLASH_THROUGHPUT )

static RunMetadataSnapshot_T run_metadata_snapshot = { 0 };
static bool                  execution_statistics_captured = false;
static bool                  run_metadata_sealed            = false;

void RUN_METADATA_Reset( void )
{
    ( void )memset( &run_metadata_snapshot, 0, sizeof( run_metadata_snapshot ) );
    run_metadata_snapshot.structure_version = RUN_METADATA_STRUCTURE_VERSION;
    execution_statistics_captured           = false;
    run_metadata_sealed                      = false;
}

bool RUN_METADATA_LatchTerminal( RunMetadataTerminalStatus_T status,
                                 RunMetadataFailureSource_T source, uint32_t reason )
{
    const bool is_complete = status == RUN_METADATA_TERMINAL_COMPLETE;
    const bool is_failure = status == RUN_METADATA_TERMINAL_FAILED
                            || status == RUN_METADATA_TERMINAL_ABORTED;

    if ( run_metadata_sealed || ( !is_complete && !is_failure )
         || ( is_complete
              && ( source != RUN_METADATA_FAILURE_SOURCE_NONE || reason != 0U ) )
         || ( is_failure
              && ( source == RUN_METADATA_FAILURE_SOURCE_NONE || reason == 0U ) )
         || ( ( run_metadata_snapshot.valid_sections & RUN_METADATA_VALID_TERMINAL ) != 0U ) )
    {
        return false;
    }

    /* Publish status last so it acts as the first-cause latch for the owner task. */
    run_metadata_snapshot.failure_source = source;
    run_metadata_snapshot.failure_reason = reason;
    run_metadata_snapshot.terminal_status = status;
    run_metadata_snapshot.valid_sections |= RUN_METADATA_VALID_TERMINAL;
    return true;
}

bool RUN_METADATA_CaptureExecution( const RunMetadataExecutionCapture_T* capture )
{
    if ( ( capture == NULL ) || run_metadata_sealed || execution_statistics_captured
         || ( ( capture->valid_sections & ~RUN_METADATA_EXECUTION_VALID_FLAGS ) != 0U ) )
    {
        return false;
    }

    if ( ( capture->valid_sections & RUN_METADATA_VALID_LAST_COMPLETED_BOUNDARY ) != 0U )
    {
        run_metadata_snapshot.last_completed_boundary = capture->last_completed_boundary;
    }
    if ( ( capture->valid_sections & RUN_METADATA_VALID_ISR_TIMING ) != 0U )
    {
        run_metadata_snapshot.isr_timing = capture->isr_timing;
    }
    if ( ( capture->valid_sections & RUN_METADATA_VALID_INSTRUCTION_BUFFER ) != 0U )
    {
        run_metadata_snapshot.instruction_buffer = capture->instruction_buffer;
    }
    if ( ( capture->valid_sections & RUN_METADATA_VALID_RESULT_BUFFER ) != 0U )
    {
        run_metadata_snapshot.result_buffer = capture->result_buffer;
    }
    if ( ( capture->valid_sections & RUN_METADATA_VALID_FLASH_THROUGHPUT ) != 0U )
    {
        run_metadata_snapshot.flash_throughput = capture->flash_throughput;
    }

    run_metadata_snapshot.valid_sections |= capture->valid_sections;
    execution_statistics_captured = true;
    return true;
}

bool RUN_METADATA_SetResultStreamStatus( RunMetadataResultStreamStatus_T status )
{
    if ( run_metadata_sealed || ( status == RUN_METADATA_RESULT_STREAM_PENDING )
         || ( run_metadata_snapshot.result_stream_status != RUN_METADATA_RESULT_STREAM_PENDING ) )
    {
        return false;
    }

    switch ( status )
    {
        case RUN_METADATA_RESULT_STREAM_COMPLETE:
        case RUN_METADATA_RESULT_STREAM_PARTIAL:
        case RUN_METADATA_RESULT_STREAM_UNAVAILABLE:
            run_metadata_snapshot.result_stream_status = status;
            return true;

        case RUN_METADATA_RESULT_STREAM_PENDING:
        default:
            return false;
    }
}

bool RUN_METADATA_Seal( void )
{
    if ( run_metadata_sealed )
    {
        return true;
    }

    if ( ( ( run_metadata_snapshot.valid_sections & RUN_METADATA_VALID_TERMINAL ) == 0U )
         || ( run_metadata_snapshot.result_stream_status == RUN_METADATA_RESULT_STREAM_PENDING ) )
    {
        return false;
    }

    run_metadata_sealed = true;
    return true;
}

bool RUN_METADATA_GetSnapshot( RunMetadataSnapshot_T* snapshot )
{
    if ( ( snapshot == NULL ) || !run_metadata_sealed )
    {
        return false;
    }

    *snapshot = run_metadata_snapshot;
    return true;
}
