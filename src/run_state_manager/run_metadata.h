/******************************************************************************
 *  File:       run_metadata.h
 *
 *  Description:
 *      Firmware-side semantic model for one execution's terminal metadata.
 *
 *  Notes:
 *      This is not a wire-format structure. Protocol encoding must serialize
 *      each field explicitly and must not copy this native C representation.
 ******************************************************************************/

#ifndef RUN_METADATA_H
#define RUN_METADATA_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stdint.h>

/** Version of the firmware-side metadata model. */
#define RUN_METADATA_STRUCTURE_VERSION ( 1U )

/** Authoritative validity bits for optional fields and statistic sections. */
#define RUN_METADATA_VALID_TERMINAL ( UINT32_C( 1 ) << 0U )
#define RUN_METADATA_VALID_LAST_COMPLETED_BOUNDARY ( UINT32_C( 1 ) << 1U )
#define RUN_METADATA_VALID_ISR_TIMING ( UINT32_C( 1 ) << 2U )
#define RUN_METADATA_VALID_INSTRUCTION_BUFFER ( UINT32_C( 1 ) << 3U )
#define RUN_METADATA_VALID_RESULT_BUFFER ( UINT32_C( 1 ) << 4U )
#define RUN_METADATA_VALID_FLASH_THROUGHPUT ( UINT32_C( 1 ) << 5U )

/** Terminal outcome of the requested execution. */
typedef enum
{
    /** No terminal outcome has been latched for the current execution. */
    RUN_METADATA_TERMINAL_PENDING = 0,

    /** Every requested execution boundary completed successfully. */
    RUN_METADATA_TERMINAL_COMPLETE,

    /** Execution ended because an internal or runtime failure was detected. */
    RUN_METADATA_TERMINAL_FAILED,

    /** Execution ended because the host or operator explicitly aborted it. */
    RUN_METADATA_TERMINAL_ABORTED
} RunMetadataTerminalStatus_T;

/** Availability of the result stream associated with the terminal outcome. */
typedef enum
{
    /** Result finalisation has not yet established stream availability. */
    RUN_METADATA_RESULT_STREAM_PENDING = 0,

    /** Results cover the complete requested execution and are transferable. */
    RUN_METADATA_RESULT_STREAM_COMPLETE,

    /** Every committed result is transferable, but execution ended early. */
    RUN_METADATA_RESULT_STREAM_PARTIAL,

    /** The result stream cannot be trusted or retrieved. */
    RUN_METADATA_RESULT_STREAM_UNAVAILABLE
} RunMetadataResultStreamStatus_T;

/** Subsystem whose native failure code is stored in failure_reason. */
typedef enum
{
    /** No failure is associated with this execution. */
    RUN_METADATA_FAILURE_SOURCE_NONE = 0,

    /** failure_reason is a RunStateFaultReason_T value. */
    RUN_METADATA_FAILURE_SOURCE_RUN_STATE_MANAGER,

    /** failure_reason is an ExecutionManagerFailure_T value. */
    RUN_METADATA_FAILURE_SOURCE_EXECUTION_MANAGER,

    /** failure_reason is a Flash Manager status or fault code. */
    RUN_METADATA_FAILURE_SOURCE_FLASH_MANAGER,

    /** failure_reason is a Host Interface status or fault code. */
    RUN_METADATA_FAILURE_SOURCE_HOST_INTERFACE
} RunMetadataFailureSource_T;

/** Raw execution-timer ISR timing measured by the CPU cycle counter. */
typedef struct
{
    /** Number of completed ISR timing samples, including boundary zero. */
    uint32_t sample_count;

    /** Sum of all samples; divide by sample_count on the host for the mean. */
    uint64_t total_cycles;

    /** Smallest completed sample. Valid only when sample_count is non-zero. */
    uint32_t minimum_cycles;

    /** Largest completed sample. Valid only when sample_count is non-zero. */
    uint32_t maximum_cycles;

    /** Execution boundary that produced maximum_cycles. */
    uint32_t maximum_boundary;
} RunMetadataIsrTiming_T;

/** Instruction-buffer pressure sampled while unread stream data remains. */
typedef struct
{
    /** Number of occupancy samples considered for the minimum. */
    uint32_t sample_count;

    /** Smallest number of prefetched, unread instruction bytes. */
    uint32_t minimum_unread_bytes;

    /** Execution boundary at which minimum_unread_bytes was observed. */
    uint32_t minimum_boundary;
} RunMetadataInstructionBuffer_T;

/** Result-buffer production and pressure statistics. */
typedef struct
{
    /** Number of successfully committed result records. */
    uint32_t committed_record_count;

    /** Total header and payload bytes in successfully committed records. */
    uint32_t committed_bytes;

    /** Largest committed byte count awaiting a completed NAND drain. */
    uint32_t peak_pending_bytes;

    /** Result timestamp whose commit first established peak_pending_bytes. */
    uint32_t peak_pending_boundary;

    /** Number of result-record reservations rejected during execution. */
    uint32_t reserve_failure_count;

    /** Number of result-record commits rejected during execution. */
    uint32_t commit_failure_count;
} RunMetadataResultBuffer_T;

/** NAND service statistics collected only while execution is active. */
typedef struct
{
    /** Result pages successfully written to NAND during execution. */
    uint32_t result_pages_drained;

    /** Logical result bytes written by those page drains. */
    uint64_t result_bytes_drained;

    /** Sum of CPU cycles spent servicing successful result page drains. */
    uint64_t result_drain_total_cycles;

    /** Largest successful result page-drain service time. */
    uint32_t result_drain_maximum_cycles;

    /** Instruction pages successfully read from NAND during execution. */
    uint32_t instruction_pages_refilled;

    /** Logical instruction bytes read by those page refills. */
    uint64_t instruction_bytes_refilled;

    /** Sum of CPU cycles spent servicing successful instruction page refills. */
    uint64_t instruction_refill_total_cycles;

    /** Largest successful instruction page-refill service time. */
    uint32_t instruction_refill_maximum_cycles;

    /** Number of instruction-page publication timing samples. */
    uint32_t instruction_publish_sample_count;

    /** Sum of CPU cycles spent publishing refilled instruction pages. */
    uint64_t instruction_publish_total_cycles;

    /** Largest instruction-page publication time. */
    uint32_t instruction_publish_maximum_cycles;

    /** Number of gaps measured between consecutive NAND services. */
    uint32_t service_gap_sample_count;

    /** Sum of measured inter-service gaps in CPU cycles. */
    uint64_t service_gap_total_cycles;

    /** Largest measured inter-service gap in CPU cycles. */
    uint32_t service_gap_maximum_cycles;

    /** Times result drain and instruction refill simultaneously required service. */
    uint32_t refill_drain_contention_count;
} RunMetadataFlashThroughput_T;

/**
 * @brief Immutable terminal metadata for one execution after snapshot completion.
 *
 * Zero is a valid measurement. Consumers must use valid_sections rather than
 * infer validity from field values. Cycle counts are deliberately not converted
 * to time using the MCU-reported clock; the host may combine them with the
 * independently measured clock frequency.
 *
 * failure_reason contains the native numeric reason from failure_source. It is
 * an internal representation only; the protocol encoder must translate it to
 * an explicitly versioned wire value.
 */
typedef struct
{
    /** RUN_METADATA_STRUCTURE_VERSION used to construct this snapshot. */
    uint16_t structure_version;

    /** Bitwise OR of RUN_METADATA_VALID_* flags. */
    uint32_t valid_sections;

    RunMetadataTerminalStatus_T     terminal_status;
    RunMetadataResultStreamStatus_T result_stream_status;
    RunMetadataFailureSource_T      failure_source;

    /** First-cause native failure code; zero when failure_source is NONE. */
    uint32_t failure_reason;

    /**
     * Last boundary whose measurements, due instruction, and completion checks
     * all succeeded. Meaningful only when LAST_COMPLETED_BOUNDARY is valid.
     */
    uint32_t last_completed_boundary;

    RunMetadataIsrTiming_T         isr_timing;
    RunMetadataInstructionBuffer_T instruction_buffer;
    RunMetadataResultBuffer_T      result_buffer;
    RunMetadataFlashThroughput_T   flash_throughput;
} RunMetadataSnapshot_T;

/** Task-context inputs captured after the execution timer has stopped. */
typedef struct
{
    /** RUN_METADATA_VALID_* bits for the fields supplied below. */
    uint32_t valid_sections;

    uint32_t last_completed_boundary;
    RunMetadataIsrTiming_T         isr_timing;
    RunMetadataInstructionBuffer_T instruction_buffer;
    RunMetadataResultBuffer_T      result_buffer;
    RunMetadataFlashThroughput_T   flash_throughput;
} RunMetadataExecutionCapture_T;

/**
 * @brief Resets the owner for a newly accepted execution.
 *
 * Call only from task context before execution preparation begins and while
 * the execution timer is stopped. A sealed snapshot remains available until
 * this function deliberately starts the next report lifecycle.
 */
void RUN_METADATA_Reset( void );

/**
 * @brief Latches the first terminal cause without blocking.
 *
 * This function is safe to call from the execution ISR. The lifecycle owner
 * must prevent concurrent task and ISR calls. FAILED and ABORTED require a
 * non-NONE source and non-zero native reason. COMPLETE requires neither.
 *
 * @return true when this call latched the terminal cause; false when the
 *         arguments were invalid, the report was sealed, or a cause was
 *         already latched.
 */
bool RUN_METADATA_LatchTerminal( RunMetadataTerminalStatus_T status,
                                 RunMetadataFailureSource_T source, uint32_t reason );

/**
 * @brief Copies execution-owned statistics after TIM4 and its ISR are quiescent.
 *
 * Each execution section can be captured once. Only validity bits applicable
 * to RunMetadataExecutionCapture_T are accepted.
 */
bool RUN_METADATA_CaptureExecution( const RunMetadataExecutionCapture_T* capture );

/** Establishes result availability exactly once before the report is sealed. */
bool RUN_METADATA_SetResultStreamStatus( RunMetadataResultStreamStatus_T status );

/**
 * @brief Seals a terminal report against all subsequent modification.
 *
 * @return true when the report is sealed or was already sealed; false while
 *         terminal or result-stream status is still pending.
 */
bool RUN_METADATA_Seal( void );

/** Copies the sealed report into caller-owned task-context storage. */
bool RUN_METADATA_GetSnapshot( RunMetadataSnapshot_T* snapshot );

#ifdef __cplusplus
}
#endif

#endif /* RUN_METADATA_H */
