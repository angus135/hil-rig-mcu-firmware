/******************************************************************************
 *  File:       run_report_diagnostics.h
 *
 *  Description:
 *      Firmware-owned extension contract for Application RUN_REPORT.
 *
 *  The Application protocol deliberately treats extension_data as opaque. This
 *  file defines the first firmware diagnostic payload carried in that span so
 *  the Python host can decode it without changing the shared protocol. The
 *  payload is a bounded little-endian TLV stream; unknown records are skipped.
 ******************************************************************************/

#ifndef RUN_REPORT_DIAGNOSTICS_H
#define RUN_REPORT_DIAGNOSTICS_H

#include <stdint.h>

#define RUN_REPORT_DIAGNOSTICS_MAX_BYTES     ( 255U )
#define RUN_REPORT_DIAGNOSTICS_MAGIC_0       ( ( uint8_t )'H' )
#define RUN_REPORT_DIAGNOSTICS_MAGIC_1       ( ( uint8_t )'R' )
#define RUN_REPORT_DIAGNOSTICS_VERSION       ( 1U )
#define RUN_REPORT_DIAGNOSTICS_HEADER_BYTES  ( 12U )
#define RUN_REPORT_DIAGNOSTICS_RECORD_HEADER_BYTES ( 2U )

/** Record identifiers in the v1 extension. */
typedef enum
{
    RUN_REPORT_DIAGNOSTICS_RECORD_RUNTIME_LIMITS = 1U,
    RUN_REPORT_DIAGNOSTICS_RECORD_FAILURE_DETAIL,
    RUN_REPORT_DIAGNOSTICS_RECORD_FLASH_DETAIL,
    RUN_REPORT_DIAGNOSTICS_RECORD_UART,
    RUN_REPORT_DIAGNOSTICS_RECORD_SPI,
    RUN_REPORT_DIAGNOSTICS_RECORD_CAN,
} RunReportDiagnosticsRecord_T;

/** Stable wire values for the Execution Manager's terminal first cause. */
typedef enum
{
    RUN_REPORT_DIAGNOSTICS_EXECUTION_FAILURE_NONE = 0U,
    RUN_REPORT_DIAGNOSTICS_EXECUTION_FAILURE_NOT_PREPARED,
    RUN_REPORT_DIAGNOSTICS_EXECUTION_FAILURE_INSTRUCTION_UNDERRUN,
    RUN_REPORT_DIAGNOSTICS_EXECUTION_FAILURE_INSTRUCTION_CORRUPT,
    RUN_REPORT_DIAGNOSTICS_EXECUTION_FAILURE_INSTRUCTION_LATE,
    RUN_REPORT_DIAGNOSTICS_EXECUTION_FAILURE_OPERATION_REJECTED,
    RUN_REPORT_DIAGNOSTICS_EXECUTION_FAILURE_INSTRUCTION_CONSUME,
    RUN_REPORT_DIAGNOSTICS_EXECUTION_FAILURE_MEASUREMENT_REJECTED,
    RUN_REPORT_DIAGNOSTICS_EXECUTION_FAILURE_INSTRUCTION_UNCONSUMED,
    RUN_REPORT_DIAGNOSTICS_EXECUTION_FAILURE_UNKNOWN = UINT8_MAX,
} RunReportDiagnosticsExecutionFailure_T;

/** Stable wire identity for a measurement adapter. */
typedef enum
{
    RUN_REPORT_DIAGNOSTICS_MEASUREMENT_ANALOGUE_INPUT = 0U,
    RUN_REPORT_DIAGNOSTICS_MEASUREMENT_DIGITAL_INPUT,
    RUN_REPORT_DIAGNOSTICS_MEASUREMENT_PWM_CAPTURE,
    RUN_REPORT_DIAGNOSTICS_MEASUREMENT_UART_RECEIVE,
    RUN_REPORT_DIAGNOSTICS_MEASUREMENT_SPI_RECEIVE,
    RUN_REPORT_DIAGNOSTICS_MEASUREMENT_CAN_RECEIVE,
    RUN_REPORT_DIAGNOSTICS_MEASUREMENT_INVALID = UINT8_MAX,
} RunReportDiagnosticsMeasurementType_T;

/** Stable wire values for the most recent result commit failure. */
typedef enum
{
    RUN_REPORT_DIAGNOSTICS_COMMIT_OK = 0U,
    RUN_REPORT_DIAGNOSTICS_COMMIT_INVALID_STATE,
    RUN_REPORT_DIAGNOSTICS_COMMIT_INVALID_LEASE,
    RUN_REPORT_DIAGNOSTICS_COMMIT_OVERFLOW,
    RUN_REPORT_DIAGNOSTICS_COMMIT_SESSION_CAPACITY_EXCEEDED,
    RUN_REPORT_DIAGNOSTICS_COMMIT_INTERNAL_ERROR,
    RUN_REPORT_DIAGNOSTICS_COMMIT_UNKNOWN = UINT8_MAX,
} RunReportDiagnosticsCommitStatus_T;

/** Stable interpretation of the SPI record's TX state byte. */
typedef enum
{
    RUN_REPORT_DIAGNOSTICS_SPI_TX_IDLE = 0U,
    RUN_REPORT_DIAGNOSTICS_SPI_TX_DMA_ACTIVE,
    RUN_REPORT_DIAGNOSTICS_SPI_TX_WAIT_FINAL_DRAIN,
    RUN_REPORT_DIAGNOSTICS_SPI_TX_ERROR,
    RUN_REPORT_DIAGNOSTICS_SPI_TX_UNKNOWN = UINT8_MAX,
} RunReportDiagnosticsSpiTxState_T;

/** Stable wire reasons for a rejected execution operation. */
typedef enum
{
    RUN_REPORT_DIAGNOSTICS_OPERATION_FAILURE_NONE = 0U,
    RUN_REPORT_DIAGNOSTICS_OPERATION_FAILURE_INVALID_ARGUMENT,
    RUN_REPORT_DIAGNOSTICS_OPERATION_FAILURE_QUEUE_FULL,
    RUN_REPORT_DIAGNOSTICS_OPERATION_FAILURE_BUSY,
    RUN_REPORT_DIAGNOSTICS_OPERATION_FAILURE_EMPTY,
    RUN_REPORT_DIAGNOSTICS_OPERATION_FAILURE_NOT_CONFIGURED,
    RUN_REPORT_DIAGNOSTICS_OPERATION_FAILURE_NOT_STARTED,
    RUN_REPORT_DIAGNOSTICS_OPERATION_FAILURE_TIMING_ERROR,
    RUN_REPORT_DIAGNOSTICS_OPERATION_FAILURE_FILTER_ERROR,
    RUN_REPORT_DIAGNOSTICS_OPERATION_FAILURE_DRIVER_FAULT,
    RUN_REPORT_DIAGNOSTICS_OPERATION_FAILURE_DRIVER_REJECTED,
} RunReportDiagnosticsOperationFailure_T;

/** Stable wire reasons for a rejected execution measurement. */
typedef enum
{
    RUN_REPORT_DIAGNOSTICS_MEASUREMENT_FAILURE_NONE = 0U,
    RUN_REPORT_DIAGNOSTICS_MEASUREMENT_FAILURE_RESULT_RESERVE_FAILED,
    RUN_REPORT_DIAGNOSTICS_MEASUREMENT_FAILURE_RESULT_COMMIT_FAILED,
    RUN_REPORT_DIAGNOSTICS_MEASUREMENT_FAILURE_INVALID_SAMPLE,
    RUN_REPORT_DIAGNOSTICS_MEASUREMENT_FAILURE_INVALID_ARGUMENT,
    RUN_REPORT_DIAGNOSTICS_MEASUREMENT_FAILURE_BUSY,
    RUN_REPORT_DIAGNOSTICS_MEASUREMENT_FAILURE_EMPTY,
    RUN_REPORT_DIAGNOSTICS_MEASUREMENT_FAILURE_NOT_CONFIGURED,
    RUN_REPORT_DIAGNOSTICS_MEASUREMENT_FAILURE_NOT_STARTED,
    RUN_REPORT_DIAGNOSTICS_MEASUREMENT_FAILURE_TIMING_ERROR,
    RUN_REPORT_DIAGNOSTICS_MEASUREMENT_FAILURE_FILTER_ERROR,
    RUN_REPORT_DIAGNOSTICS_MEASUREMENT_FAILURE_DRIVER_FAULT,
    RUN_REPORT_DIAGNOSTICS_MEASUREMENT_FAILURE_DRIVER_REJECTED,
} RunReportDiagnosticsMeasurementFailure_T;

/* Fixed v1 payload lengths. The first byte of UART/SPI/CAN records is the
 * logical channel. Runtime limits is shared by all channels; failure and flash
 * detail are run-wide records. */
#define RUN_REPORT_DIAGNOSTICS_RUNTIME_LIMITS_BYTES ( 26U )
#define RUN_REPORT_DIAGNOSTICS_FAILURE_DETAIL_BYTES ( 15U )
#define RUN_REPORT_DIAGNOSTICS_FLASH_DETAIL_BYTES   ( 11U )
#define RUN_REPORT_DIAGNOSTICS_UART_BYTES           ( 22U )
#define RUN_REPORT_DIAGNOSTICS_SPI_BYTES            ( 29U )
#define RUN_REPORT_DIAGNOSTICS_CAN_BYTES            ( 35U )

/* v1 payload offsets, after the two-byte record header:
 *
 * RUNTIME_LIMITS: core clock u32, instruction/result RAM capacities u32/u32,
 * UART TX/RX u16/u16, SPI TX/RX/descriptor u16/u16/u16, CAN TX/RX u16/u16.
 * FAILURE_DETAIL: operation-valid, RunReportDiagnosticsExecutionFailure_T,
 * operation index/opcode/
 * channel, RunReportDiagnosticsOperationFailure_T, execution boundary u32,
 * measurement-valid/index/type/channel, RunReportDiagnosticsMeasurementFailure_T.
 * FLASH_DETAIL: pending bytes u32, failed-reserve payload u16, free bytes at
 * failed reserve u32, RunReportDiagnosticsCommitStatus_T.
 * UART: channel u8, flags u8, TX pending/peak u16/u16, TX rejects u32,
 * DMA errors u32, RX unread/peak u16/u16, latched faults u32.
 * SPI: channel u8, flags u8, TX pending/peak/in-flight bytes u16/u16/u16,
 * TX pending/peak packets u16/u16, rejects/DMA errors/drain timeouts
 * u32/u32/u32, RX unread/peak u16/u16, TX state u8.
 * CAN: channel u8, flags u8, TX pending/peak u16/u16, pending mailbox u32,
 * RX queued/peak u16/u16, RX dropped u32, TEC/REC/last-error u8/u8/u8,
 * TSR/ESR u32/u32, cumulative error-interrupt count u32, max TEC/REC u8/u8.
 */

/** Header flags. */
enum
{
    RUN_REPORT_DIAGNOSTICS_FLAG_RX_OVERRUN_UNDETECTED = 1U << 0U,
    RUN_REPORT_DIAGNOSTICS_FLAG_PARTIAL = 1U << 1U,
};

/** Common high bit in the flags byte of UART/SPI/CAN records. */
enum
{
    RUN_REPORT_DIAGNOSTICS_PERIPHERAL_FLAG_VALID = 1U << 7U,
};

/** UART record flags. */
enum
{
    RUN_REPORT_DIAGNOSTICS_UART_FLAG_TX_DMA_ACTIVE = 1U << 0U,
    RUN_REPORT_DIAGNOSTICS_UART_FLAG_STARTED = 1U << 1U,
    RUN_REPORT_DIAGNOSTICS_UART_FLAG_CONFIGURED = 1U << 2U,
};

/** UART record latched-fault bits. */
enum
{
    RUN_REPORT_DIAGNOSTICS_UART_FAULT_TX_DMA = 1U << 0U,
    RUN_REPORT_DIAGNOSTICS_UART_FAULT_RX_DMA = 1U << 1U,
};

/** SPI record flags. */
enum
{
    RUN_REPORT_DIAGNOSTICS_SPI_FLAG_STARTED = 1U << 0U,
    RUN_REPORT_DIAGNOSTICS_SPI_FLAG_CONFIGURED = 1U << 1U,
    RUN_REPORT_DIAGNOSTICS_SPI_FLAG_MASTER = 1U << 2U,
};

/** CAN record flags. */
enum
{
    RUN_REPORT_DIAGNOSTICS_CAN_FLAG_TX_ACTIVE = 1U << 0U,
    RUN_REPORT_DIAGNOSTICS_CAN_FLAG_TX_ERROR = 1U << 1U,
};

/** bxCAN ESR last-error-code values carried in the CAN record. */
typedef enum
{
    RUN_REPORT_DIAGNOSTICS_CAN_LEC_NONE = 0U,
    RUN_REPORT_DIAGNOSTICS_CAN_LEC_STUFF_ERROR,
    RUN_REPORT_DIAGNOSTICS_CAN_LEC_FORM_ERROR,
    RUN_REPORT_DIAGNOSTICS_CAN_LEC_ACK_ERROR,
    RUN_REPORT_DIAGNOSTICS_CAN_LEC_BIT_RECESSIVE_ERROR,
    RUN_REPORT_DIAGNOSTICS_CAN_LEC_BIT_DOMINANT_ERROR,
    RUN_REPORT_DIAGNOSTICS_CAN_LEC_CRC_ERROR,
    RUN_REPORT_DIAGNOSTICS_CAN_LEC_SOFTWARE_SET,
} RunReportDiagnosticsCanLastError_T;

/**
 * Header layout (all multi-byte fields little-endian):
 * magic[2], version, header_bytes, total_bytes, record_count, flags, reserved, run_sequence[4].
 * Each following record is type, length, payload. A decoder must ignore an
 * unknown type, accept a known record whose length is at least the documented
 * prefix, ignore any trailing bytes in that record, and stop safely at
 * total_bytes.
 */

#endif /* RUN_REPORT_DIAGNOSTICS_H */
